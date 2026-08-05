// nukebench gate runner + gate_report.json (03 §11, M1-T5-b).
//
// Runs a gate's 08 §2 procedure over its normative seed set and produces the append-only
// gate_report the gate table cites. A gate PASSES iff every normative seed passes every
// criterion; a dirty (uncommitted) tree caps the verdict at "conditional" (03 §11).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app/nukebench/gates.h"

namespace ns::nukebench {

struct CriterionResult {
    std::string name;       // e.g. "k_deviation_pcm"
    std::string op;         // "abs_le" | "le"
    double value = 0.0;     // measured value
    double threshold = 0.0;
    bool pass = false;
};

struct Attempt {
    int attempt = 0;                 // 1-based, continues across re-runs (append-only)
    std::int64_t seed = 0;
    std::string verdict;             // "pass" | "fail"
    double k = 0.0;
    double sigma_pcm = 0.0;
    double k_deviation_pcm = 0.0;    // (k - 1) * 1e5  (for a diff report: |k_a - k_b| in pcm)
    double k_b = 0.0;                // G0c diff only: the SECOND backend's k (k = k_a, k_b = k_b);
                                     // 0 for a single-backend report (then not emitted)
    std::vector<CriterionResult> criteria;
    std::string run_dir;             // may be empty (bundle untracked; report stands alone)
};

struct GateReport {
    int schema_version = 1;
    std::string gate;
    std::string verdict;             // "pass" | "fail" | "conditional"
    std::string gates_toml_sha256;
    std::string spec_sha256;
    std::string code_version;
    std::string git;
    bool dirty = false;
    std::string backend;             // "ref" | "gpu"
    std::string device;
    std::string started;
    std::string finished;
    std::vector<Attempt> attempts;
    std::string notes;
};

std::string to_json(const GateReport& r);
GateReport parse_report_json(const std::string& json);

/// Run `gate` (from gates.toml) over its normative seeds against `data/xs/fast4.json` + the
/// gate's scenario under `repo`. `backend` ∈ {"ref","gpu"}. `eigen_batch_override` > 0 runs
/// at a reduced batch instead of the gate's C-900 config (tests / quick looks; a real gate
/// report uses 0). Fills measurements, per-seed verdicts, and the overall verdict; leaves
/// provenance fields (git/dirty/timestamps) to the caller.
GateReport run_gate(const Gate& gate, const std::filesystem::path& repo,
                    const std::string& backend, std::int64_t eigen_batch_override = 0);

/// Run `gate` on TWO backends and evaluate the G0c cross-backend differential (08 §2 criterion a):
/// per seed, |k_a - k_b| ≤ C-932 (100 pcm) AND ≤ 3·√(σ_a²+σ_b²). Reuses `run_gate` for each backend
/// (so `backend_b = "gpu"` needs a CUDA build — else it throws). The report's `backend` is "a|b";
/// per attempt `k = k_a`, `k_b = k_b`, `k_deviation_pcm = |k_a − k_b|·1e5`, `sigma_pcm` = the combined
/// σ. Criteria b (per-shell) / c (population-series) are deferred (the GPU eigen doesn't expose them).
GateReport run_diff(const Gate& gate, const std::filesystem::path& repo,
                    const std::string& backend_a, const std::string& backend_b,
                    std::int64_t eigen_batch_override = 0);

/// Write `report` to `path`, appending its attempts to any existing report there (attempt
/// numbers continue; the overall verdict is recomputed over ALL attempts). Append-only is the
/// anti-seed-shopping guarantee (MAJ-22): you cannot drop a failing seed by re-running.
/// Returns the final merged verdict ("pass" | "fail" | "conditional").
std::string write_report_append(const std::filesystem::path& path, GateReport report);

}  // namespace ns::nukebench

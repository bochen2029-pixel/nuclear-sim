// nukebench CLI handlers (M1-T5-b/-c). The thin CLI11 dispatch lives in main.cpp; the testable
// logic is here: `gate` (M1-T5-b), `diff` (the G0c differential, M1-T5-c-3), `run` (M1-T5-c-4).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ns::nukebench {

struct GateOutcome {
    int exit_code = 0;         // 06 §5: 0 ok, 4 gate-fail
    std::string verdict;       // pass | fail | conditional
    std::filesystem::path report_path;
};

/// artifacts/gate_reports/<gate>/gate_report.json (03 §11 committed location).
std::filesystem::path default_report_path(const std::filesystem::path& repo,
                                          const std::string& gate_id);

/// Run gate `gate_id` (from gates.toml under `repo`) over its normative seeds and write the
/// 03 §11 report to `report_path` (append-only). `backend` ∈ {"ref"}; `batch_override` > 0
/// runs a reduced batch (quick looks / tests); `dirty` caps a passing verdict at conditional.
/// `git` (short commit hash of the code that produced the run) + `device` are recorded as
/// provenance; `started`/`finished` (ISO-8601 UTC) are stamped around the run. Throws
/// ns::nukebench::GatesError on a validation failure (unknown gate, spec_sha256 mismatch) —
/// main maps that to exit 3. exit_code is 0 iff the final verdict is "pass".
GateOutcome cli_gate(const std::string& gate_id, const std::filesystem::path& repo,
                     const std::filesystem::path& report_path, const std::string& backend,
                     std::int64_t batch_override, bool dirty, const std::string& git = "",
                     const std::string& device = "");

/// Run the **G0c** cross-backend differential for gate `gate_id` (ref vs gpu) and write the 03 §11
/// report (append-only) to `report_path`. Needs a CUDA build (the gpu backend; else run_gate throws
/// -> GatesError -> main exit 3). Same provenance + exit-code contract as cli_gate (0 pass / 4 fail).
GateOutcome cli_diff(const std::string& gate_id, const std::filesystem::path& repo,
                     const std::filesystem::path& report_path, std::int64_t batch_override,
                     bool dirty, const std::string& git = "", const std::string& device = "");

struct RunOutcome {
    int exit_code = 0;              // 06 §5: 0 ok, 3 validation
    std::string unit_id;           // 03 §6 dedup key = the artifact directory name
    std::filesystem::path out_dir; // <out_root>/<unit_id>/
    bool detonate = false;         // EMERGENT: prompt-supercritical + self-quenched + finite yield
    bool fizzle = false;           // ran but never reached prompt-criticality
    double yield_kt = 0.0;
};

/// `nukebench run` (06 §1): build a demon-core StudioConfig from `scenario_file`
/// (a JSON object of flat dotted viz keys — the viz/js/scenario.js + studio_bridge
/// shape; the fast4-independent demon-core path) merged with `overrides` (key ->
/// numeric value, applied verbatim onto the cfg), run the emergent burst
/// (ns::api::generate_run), and write the 03 §6 artifact bundle {run.json,
/// tally.json} to `<out_root>/<unit_id>/`. Stamps the environment provenance
/// (code/spec version, git, dirty, device, started/finished) the physics layer
/// leaves for a frontend to fill (the cli_gate pattern). `backend` must be "ref" —
/// the demon-core disassembly burst runs on the CPU reference transport (the gpu
/// eigen is k-only, not a full burst). Returns exit_code 3 for a validation failure
/// (missing/malformed scenario, non-numeric override, unsupported backend); 0 on a
/// completed run — a fizzle is a valid run, not an error. SCOPE: the full 03 §4
/// TOML scenario (nested materials/geometry, real transport) is the fast4-gated
/// path; this is the demon-core studio-cfg path.
RunOutcome cli_run(const std::filesystem::path& scenario_file,
                   const std::vector<std::pair<std::string, std::string>>& overrides,
                   const std::filesystem::path& out_root, const std::string& backend,
                   const std::string& git = "", const std::string& device = "", bool dirty = false);

}  // namespace ns::nukebench

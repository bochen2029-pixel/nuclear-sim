// nukebench CLI handlers (M1-T5-b). The thin CLI11 dispatch lives in main.cpp; the testable
// logic is here. Currently the `gate` subcommand (run/diff are M1-T5-c).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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

}  // namespace ns::nukebench

#include "app/nukebench/cli.h"

#include "app/nukebench/gate_report.h"
#include "app/nukebench/gates.h"
#include "core/hash/sha256.h"

#include <fstream>
#include <sstream>

namespace ns::nukebench {

namespace {
std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string norm_lf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    return out;
}
}  // namespace

std::filesystem::path default_report_path(const std::filesystem::path& repo,
                                          const std::string& gate_id) {
    return repo / "artifacts" / "gate_reports" / gate_id / "gate_report.json";
}

GateOutcome cli_gate(const std::string& gate_id, const std::filesystem::path& repo,
                     const std::filesystem::path& report_path, const std::string& backend,
                     std::int64_t batch_override, bool dirty) {
    const auto gates_toml = repo / "data" / "benchmarks" / "gates.toml";
    const auto spec08 = repo / "spec" / "08-validation.md";

    // load_gates enforces the spec_sha256 + constant-drift guards (throws GatesError -> exit 3);
    // find_gate throws if the id is unknown.
    const GatesConfig cfg = load_gates(gates_toml, spec08);
    const Gate& gate = find_gate(cfg, gate_id);

    GateReport rep = run_gate(gate, repo, backend, batch_override);
    rep.gates_toml_sha256 = ns::hash::sha256_hex(norm_lf(read_file(gates_toml)));
    rep.spec_sha256 = cfg.spec_sha256;
    rep.code_version = "0.1.0";
    rep.dirty = dirty;

    const std::string verdict = write_report_append(report_path, rep);
    GateOutcome out;
    out.verdict = verdict;
    out.report_path = report_path;
    out.exit_code = (verdict == "pass") ? 0 : 4;  // 06 §5: gate met only on a clean pass
    return out;
}

}  // namespace ns::nukebench

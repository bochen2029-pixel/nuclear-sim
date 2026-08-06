#include "app/nukebench/cli.h"

#include "api/run_provenance.h"
#include "api/studio.h"
#include "app/nukebench/gate_report.h"
#include "app/nukebench/gates.h"
#include "core/hash/sha256.h"
#include "physics/tally/tally.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ns::nukebench {

namespace {
std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::filesystem::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f << content;
}

/// UTC wall-clock as ISO-8601 (e.g. "2026-08-05T14:30:00Z"), for the report's started/finished
/// provenance (03 §11). Portable: gmtime_s on MSVC, gmtime_r elsewhere.
std::string iso8601_utc_now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
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
                     std::int64_t batch_override, bool dirty, const std::string& git,
                     const std::string& device) {
    const auto gates_toml = repo / "data" / "benchmarks" / "gates.toml";
    const auto spec08 = repo / "spec" / "08-validation.md";

    // load_gates enforces the spec_sha256 + constant-drift guards (throws GatesError -> exit 3);
    // find_gate throws if the id is unknown.
    const GatesConfig cfg = load_gates(gates_toml, spec08);
    const Gate& gate = find_gate(cfg, gate_id);

    const std::string started = iso8601_utc_now();
    GateReport rep = run_gate(gate, repo, backend, batch_override);
    rep.finished = iso8601_utc_now();
    rep.started = started;
    rep.gates_toml_sha256 = ns::hash::sha256_hex(norm_lf(read_file(gates_toml)));
    rep.spec_sha256 = cfg.spec_sha256;
    rep.code_version = "0.1.0";
    rep.git = git;
    rep.device = device;
    rep.dirty = dirty;

    const std::string verdict = write_report_append(report_path, rep);
    GateOutcome out;
    out.verdict = verdict;
    out.report_path = report_path;
    out.exit_code = (verdict == "pass") ? 0 : 4;  // 06 §5: gate met only on a clean pass
    return out;
}

GateOutcome cli_diff(const std::string& gate_id, const std::filesystem::path& repo,
                     const std::filesystem::path& report_path, std::int64_t batch_override,
                     bool dirty, const std::string& git, const std::string& device) {
    const auto gates_toml = repo / "data" / "benchmarks" / "gates.toml";
    const auto spec08 = repo / "spec" / "08-validation.md";
    const GatesConfig cfg = load_gates(gates_toml, spec08);  // spec_sha256 + drift guards
    const Gate& gate = find_gate(cfg, gate_id);

    const std::string started = iso8601_utc_now();
    GateReport rep = run_diff(gate, repo, "ref", "gpu", batch_override);  // gpu needs a CUDA build
    rep.finished = iso8601_utc_now();
    rep.started = started;
    rep.gates_toml_sha256 = ns::hash::sha256_hex(norm_lf(read_file(gates_toml)));
    rep.spec_sha256 = cfg.spec_sha256;
    rep.code_version = "0.1.0";
    rep.git = git;
    rep.device = device;
    rep.dirty = dirty;

    const std::string verdict = write_report_append(report_path, rep);
    GateOutcome out;
    out.verdict = verdict;
    out.report_path = report_path;
    out.exit_code = (verdict == "pass") ? 0 : 4;
    return out;
}

RunOutcome cli_run(const std::filesystem::path& scenario_file,
                   const std::vector<std::pair<std::string, std::string>>& overrides,
                   const std::filesystem::path& out_root, const std::string& backend,
                   const std::string& git, const std::string& device, bool dirty) {
    RunOutcome out;

    // The demon-core burst is CPU reference transport; the gpu eigen is k-only (no burst).
    if (backend != "ref") {
        std::fprintf(stderr,
                     "nukebench run: --backend %s not supported; the demon-core burst runs on the "
                     "CPU reference transport (--backend ref)\n",
                     backend.c_str());
        out.exit_code = 3;  // 06 §5 validation
        return out;
    }

    std::error_code ec;
    if (!std::filesystem::exists(scenario_file, ec)) {
        std::fprintf(stderr, "nukebench run: scenario file not found: %s\n",
                     scenario_file.string().c_str());
        out.exit_code = 3;
        return out;
    }

    // The scenario is a flat dotted-key JSON object (viz/js/scenario.js + studio_bridge shape).
    nlohmann::json cfg_obj;
    try {
        cfg_obj = nlohmann::json::parse(read_file(scenario_file));
    } catch (const nlohmann::json::exception& e) {
        std::fprintf(stderr, "nukebench run: scenario is not valid JSON: %s\n", e.what());
        out.exit_code = 3;
        return out;
    }
    if (!cfg_obj.is_object()) {
        std::fprintf(stderr, "nukebench run: scenario must be a JSON object of dotted keys\n");
        out.exit_code = 3;
        return out;
    }

    // Merge overrides verbatim; every demon-core knob is numeric, so a non-numeric
    // value is a validation error (StudioConfig::from_json would silently ignore it).
    for (const auto& [key, val] : overrides) {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(val);
        } catch (const nlohmann::json::exception&) {
            parsed = nullptr;
        }
        if (!parsed.is_number()) {
            std::fprintf(stderr, "nukebench run: override value must be numeric: %s=%s\n",
                         key.c_str(), val.c_str());
            out.exit_code = 3;
            return out;
        }
        cfg_obj[key] = parsed;
    }

    // Run the emergent burst. unit_id = studio_unit_id(cfg) — the overrides are folded
    // into the cfg (hence into the canonical hash and unit_id); scenario_overrides stays
    // [] so the recorded unit_id remains recomputable from the recorded fields (03 §6).
    const ns::api::StudioConfig cfg = ns::api::StudioConfig::from_json(cfg_obj.dump());
    const std::string started = iso8601_utc_now();
    ns::api::GenerateRunResult res = ns::api::generate_run(cfg);
    const std::string finished = iso8601_utc_now();

    // Stamp the environment provenance the physics layer leaves for a frontend (cli_gate pattern).
    res.run.code_version = "0.1.0";
    res.run.spec_version = "0.3";
    res.run.git = git;
    res.run.dirty = dirty;
    if (!device.empty()) res.run.device = device;
    res.run.started = started;
    res.run.finished = finished;

    // Emit the 03 §6 bundle: artifacts/<unit_id>/{run.json, tally.json}.
    const std::filesystem::path dir = out_root / res.run.unit_id;
    std::filesystem::create_directories(dir, ec);
    write_file(dir / "run.json", ns::api::to_json(res.run));
    write_file(dir / "tally.json", ns::physics::to_json(res.tally));

    out.unit_id = res.run.unit_id;
    out.out_dir = dir;
    out.detonate = res.detonate;
    out.fizzle = !res.supercritical;
    out.yield_kt = res.yield_kt;
    out.exit_code = 0;  // a completed run (fizzle or detonate) is a success; `run` is not a gate
    return out;
}

}  // namespace ns::nukebench

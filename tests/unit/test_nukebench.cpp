// M1-T5-a: the gates.toml loader (03 §10) + its two integrity guards.

#include <catch2/catch_test_macros.hpp>

#include "api/run_provenance.h"
#include "api/studio.h"
#include "app/nukebench/cli.h"
#include "app/nukebench/gate_report.h"
#include "app/nukebench/gates.h"
#include "physics/tally/tally.h"

#include "spec_examples.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
fs::path repo() { return spec_examples::repo_root(); }
fs::path gates_toml() { return repo() / "data" / "benchmarks" / "gates.toml"; }
fs::path spec08() { return repo() / "spec" / "08-validation.md"; }

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
fs::path write_tmp(const std::string& name, const std::string& content) {
    const auto p = fs::temp_directory_path() / name;
    std::ofstream out(p, std::ios::binary);
    out << content;
    return p;
}
}  // namespace

TEST_CASE("gates.toml loads G0a/G0b at the C-900 config with constant-resolved criteria",
          "[nukebench]") {
    const auto cfg = ns::nukebench::load_gates(gates_toml(), spec08());
    REQUIRE(cfg.schema_version == 1);
    REQUIRE(cfg.generated_from == "spec/08-validation.md");
    REQUIRE(cfg.gates.size() >= 2);

    const auto g0a = ns::nukebench::find_gate(cfg, "G0a");  // copy: dodge gcc -Wdangling-reference
    REQUIRE(g0a.scenario == "data/scenarios/godiva.toml");
    REQUIRE(g0a.seeds == std::vector<std::int64_t>{1, 2, 3, 4, 5});  // normative seed set
    REQUIRE(g0a.eigen.batch == 1000000);  // C-900
    REQUIRE(g0a.eigen.inactive == 50);
    REQUIRE(g0a.eigen.active == 200);

    bool kdev = false, sigma = false;
    for (const auto& c : g0a.criteria) {
        if (c.name == "k_deviation_pcm") {
            kdev = true;
            REQUIRE(c.op == "abs_le");
            REQUIRE(c.value == 500.0);  // == C-930 (the loader's drift guard already enforced this)
            REQUIRE(c.constant_id == "C-930");
        } else if (c.name == "sigma_pcm") {
            sigma = true;
            REQUIRE(c.op == "le");
            REQUIRE(c.value == 25.0);  // == C-931
            REQUIRE(c.constant_id == "C-931");
        }
    }
    REQUIRE(kdev);
    REQUIRE(sigma);
    REQUIRE(ns::nukebench::find_gate(cfg, "G0b").scenario == "data/scenarios/jezebel.toml");
}

TEST_CASE("the spec_sha256 guard rejects a stale gates.toml", "[nukebench]") {
    // A gates.toml whose spec_sha256 != sha256(08-validation.md) is a hard fail (03 §10):
    // it means the gate procedures changed without regenerating gates.toml.
    std::string content = slurp(gates_toml());
    const std::string prefix = "spec_sha256    = \"";  // the assignment, not the header comment
    const auto pos = content.find(prefix);
    REQUIRE(pos != std::string::npos);
    content.replace(pos + prefix.size(), 4, "dead");  // corrupt the first 4 hex of the digest
    const auto stale = write_tmp("nukebench_gates_stale.toml", content);
    REQUIRE_THROWS_AS(ns::nukebench::load_gates(stale, spec08()), ns::nukebench::GatesError);
    fs::remove(stale);
}

TEST_CASE("the drift guard rejects a criterion value that disagrees with its constant",
          "[nukebench]") {
    // A gates.toml whose criterion value drifted from its constant is rejected — thresholds
    // ARE the appendix constants (03 §10), never a hand-edited copy. (spec_sha256 still
    // matches, so this exercises the value/constant drift guard specifically.)
    std::string content = slurp(gates_toml());
    const auto pos = content.find("value = 500");
    REQUIRE(pos != std::string::npos);
    content.replace(pos, std::string("value = 500").size(), "value = 999");
    const auto drifted = write_tmp("nukebench_gates_drift.toml", content);
    REQUIRE_THROWS_AS(ns::nukebench::load_gates(drifted, spec08()), ns::nukebench::GatesError);
    fs::remove(drifted);
}

TEST_CASE("find_gate throws on an unknown gate id", "[nukebench]") {
    const auto cfg = ns::nukebench::load_gates(gates_toml(), spec08());
    REQUIRE_THROWS_AS(ns::nukebench::find_gate(cfg, "G9z"), ns::nukebench::GatesError);
}

TEST_CASE("run_gate produces one attempt per normative seed with evaluated criteria",
          "[nukebench]") {
    // Reduced batch: this tests the runner MECHANICS (an attempt per seed, criteria evaluated,
    // per-seed + overall verdict, JSON round-trip), not the C-900 precision (that is the DoD
    // run). fast4 must exist.
    if (!std::filesystem::exists(repo() / "data" / "xs" / "fast4.json")) {
        WARN("data/xs/fast4.json absent — skipping run_gate");
        return;
    }
    const auto cfg = ns::nukebench::load_gates(gates_toml(), spec08());
    const auto g0a = ns::nukebench::find_gate(cfg, "G0a");  // copy: dodge gcc -Wdangling-reference
    const auto rep = ns::nukebench::run_gate(g0a, repo(), "ref", 2000);

    REQUIRE(rep.gate == "G0a");
    REQUIRE(rep.backend == "ref");
    REQUIRE(rep.attempts.size() == g0a.seeds.size());
    for (std::size_t i = 0; i < rep.attempts.size(); ++i) {
        const auto& a = rep.attempts[i];
        REQUIRE(a.attempt == static_cast<int>(i + 1));
        REQUIRE(a.seed == g0a.seeds[i]);
        REQUIRE(a.k > 0.9);
        REQUIRE(a.k < 1.15);  // a sane bare-fast-metal k
        REQUIRE(a.criteria.size() == g0a.criteria.size());
        REQUIRE((a.verdict == "pass" || a.verdict == "fail"));
    }
    REQUIRE((rep.verdict == "pass" || rep.verdict == "fail"));
    const auto rt = ns::nukebench::parse_report_json(ns::nukebench::to_json(rep));
    REQUIRE(rt.gate == "G0a");
    REQUIRE(rt.attempts.size() == rep.attempts.size());
}

TEST_CASE("gate_report append-only merge continues numbering and cannot drop a failing seed",
          "[nukebench]") {
    const auto tmp = fs::temp_directory_path() / "nukebench_report_append.json";
    fs::remove(tmp);
    ns::nukebench::GateReport r;
    r.gate = "G0a";
    r.backend = "ref";
    ns::nukebench::Attempt a;
    a.attempt = 1;
    a.seed = 7;
    a.verdict = "fail";
    r.attempts.push_back(a);
    REQUIRE(ns::nukebench::write_report_append(tmp, r) == "fail");   // 1 attempt
    REQUIRE(ns::nukebench::write_report_append(tmp, r) == "fail");   // append -> 2
    const auto merged = ns::nukebench::parse_report_json(slurp(tmp));
    REQUIRE(merged.attempts.size() == 2);
    REQUIRE(merged.attempts[0].attempt == 1);
    REQUIRE(merged.attempts[1].attempt == 2);  // numbering continued
    REQUIRE(merged.verdict == "fail");         // a failing attempt survives re-runs (MAJ-22)
    fs::remove(tmp);
}

TEST_CASE("cli_gate writes the report and returns a pass/fail exit code", "[nukebench]") {
    if (!std::filesystem::exists(repo() / "data" / "xs" / "fast4.json")) {
        WARN("data/xs/fast4.json absent — skipping cli_gate");
        return;
    }
    const auto report = fs::temp_directory_path() / "nukebench_cli_g0a.json";
    fs::remove(report);
    const auto out = ns::nukebench::cli_gate("G0a", repo(), report, "ref", 2000, false);
    REQUIRE(fs::exists(report));
    REQUIRE((out.exit_code == 0 || out.exit_code == 4));
    REQUIRE(out.verdict != "");
    const auto rep = ns::nukebench::parse_report_json(slurp(report));
    REQUIRE(rep.gate == "G0a");
    REQUIRE(!rep.gates_toml_sha256.empty());  // provenance filled by the CLI
    REQUIRE(!rep.spec_sha256.empty());
    fs::remove(report);
}

TEST_CASE("cli_gate records git/device/timestamp provenance (03 sec 11)", "[nukebench]") {
    // A committed gate report is void without honest provenance (QC-07): the code hash it was
    // produced from, the device, and when. This is the M1-T5-c-1 completion of the fields the
    // M1-T5-b runner left empty.
    if (!std::filesystem::exists(repo() / "data" / "xs" / "fast4.json")) {
        WARN("data/xs/fast4.json absent — skipping cli_gate provenance");
        return;
    }
    const auto report = fs::temp_directory_path() / "nukebench_cli_g0a_prov.json";
    fs::remove(report);
    ns::nukebench::cli_gate("G0a", repo(), report, "ref", 2000, false, "abc1234", "CPU ref (test)");
    const auto rep = ns::nukebench::parse_report_json(slurp(report));
    REQUIRE(rep.git == "abc1234");             // threaded through from the CLI
    REQUIRE(rep.device == "CPU ref (test)");
    REQUIRE(rep.backend == "ref");
    REQUIRE(rep.code_version == "0.1.0");
    REQUIRE(rep.started.size() == 20);         // ISO-8601 "YYYY-MM-DDTHH:MM:SSZ"
    REQUIRE(rep.finished.size() == 20);
    REQUIRE(rep.started.back() == 'Z');        // UTC
    REQUIRE(rep.finished >= rep.started);       // finished after started (ISO-8601 sorts lexically)
    fs::remove(report);
}

// --- M1-T5-c-4: `nukebench run` (scenario cfg -> the 03 §6 bundle) ---

TEST_CASE("cli_run writes the 03 sec 6 bundle {run.json, tally.json} with stamped provenance",
          "[nukebench]") {
    // A strongly-compressed heavy pit -> a real prompt-supercritical burst (the meaningful `run`
    // case): a finite yield and a clean, round-trippable 03 §5 tally. Small eigen batch for speed;
    // deeply supercritical, so batch noise never flips the deterministic-seed outcome.
    const std::string cfg =
        R"({"pit.mass_kg":9.0,"compression.ratio":2.5,"eigen.batch":800,"seed":20260805})";
    const auto scen = write_tmp("nukebench_run_scen.json", cfg);
    const auto out_root = fs::temp_directory_path() / "nukebench_run_out";
    fs::remove_all(out_root);

    const auto r = ns::nukebench::cli_run(scen, {}, out_root, "ref", "abc1234", "CPU ref (test)", false);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.detonate);                                 // EMERGENT from the burst, not a rule
    REQUIRE(r.yield_kt > 0.0);
    REQUIRE_FALSE(r.unit_id.empty());
    REQUIRE(r.out_dir.filename() == r.unit_id);          // artifacts/<unit_id>/ (03 §6)
    REQUIRE(fs::exists(r.out_dir / "run.json"));
    REQUIRE(fs::exists(r.out_dir / "tally.json"));

    // run.json round-trips (every 03 §6 required field present) + records the stamped provenance.
    const auto run = ns::api::parse_run_json(slurp(r.out_dir / "run.json"));
    REQUIRE(run.unit_id == r.unit_id);
    REQUIRE(run.backend == "ref");
    REQUIRE(run.git == "abc1234");
    REQUIRE(run.device == "CPU ref (test)");
    REQUIRE(run.code_version == "0.1.0");
    REQUIRE(run.spec_version == "0.3");
    REQUIRE(run.started.size() == 20);                   // ISO-8601 "YYYY-MM-DDTHH:MM:SSZ"
    REQUIRE(run.finished.size() == 20);
    REQUIRE(run.scenario_overrides.empty());             // baked into the cfg -> unit_id recomputable
    // the recorded unit_id IS the studio dedup key for the effective cfg (== nukefarm's key).
    REQUIRE(run.unit_id == ns::api::studio_unit_id(ns::api::StudioConfig::from_json(cfg)));

    // tally.json round-trips as a valid 03 §5 TallyResult — the primary `run` output.
    const auto tally = ns::physics::parse_tally_json(slurp(r.out_dir / "tally.json"));
    REQUIRE(std::isfinite(tally.yield_kt));
    REQUIRE(tally.yield_kt > 0.0);
    fs::remove_all(out_root);
}

TEST_CASE("cli_run folds a numeric --override into the effective cfg and the unit_id",
          "[nukebench]") {
    const std::string base =
        R"({"pit.mass_kg":4.0,"compression.ratio":1.0,"eigen.batch":600,"seed":20260805})";
    const auto scen = write_tmp("nukebench_run_ov.json", base);
    const auto out_root = fs::temp_directory_path() / "nukebench_run_ov_out";
    fs::remove_all(out_root);

    const auto a = ns::nukebench::cli_run(scen, {}, out_root, "ref");
    const auto b = ns::nukebench::cli_run(scen, {{"pit.mass_kg", "5.0"}}, out_root, "ref");
    REQUIRE(a.exit_code == 0);
    REQUIRE(b.exit_code == 0);
    REQUIRE(a.unit_id != b.unit_id);                     // the override changed the effective cfg
    REQUIRE(fs::exists(a.out_dir / "run.json"));
    REQUIRE(fs::exists(b.out_dir / "run.json"));
    // b's unit_id is the studio key of the *merged* cfg (override applied verbatim onto the file).
    const std::string merged =
        R"({"pit.mass_kg":5.0,"compression.ratio":1.0,"eigen.batch":600,"seed":20260805})";
    REQUIRE(b.unit_id == ns::api::studio_unit_id(ns::api::StudioConfig::from_json(merged)));
    fs::remove_all(out_root);
}

TEST_CASE("cli_run rejects a bad backend, a missing scenario, and a non-numeric override (exit 3)",
          "[nukebench]") {
    // All three are validation failures caught BEFORE any burst runs (fast + no side effects).
    const auto scen = write_tmp("nukebench_run_bad.json", R"({"pit.mass_kg":4.0})");
    const auto out_root = fs::temp_directory_path() / "nukebench_run_bad_out";

    REQUIRE(ns::nukebench::cli_run(scen, {}, out_root, "gpu").exit_code == 3);         // unsupported backend
    REQUIRE(ns::nukebench::cli_run(out_root / "nope.json", {}, out_root, "ref").exit_code == 3);  // no file
    REQUIRE(ns::nukebench::cli_run(scen, {{"pit.mass_kg", "heavy"}}, out_root, "ref").exit_code == 3);  // NaN override
}

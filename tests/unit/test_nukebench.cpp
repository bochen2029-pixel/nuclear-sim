// M1-T5-a: the gates.toml loader (03 §10) + its two integrity guards.

#include <catch2/catch_test_macros.hpp>

#include "app/nukebench/cli.h"
#include "app/nukebench/gate_report.h"
#include "app/nukebench/gates.h"

#include "spec_examples.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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

    const auto& g0a = ns::nukebench::find_gate(cfg, "G0a");
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
    const auto& g0a = ns::nukebench::find_gate(cfg, "G0a");
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

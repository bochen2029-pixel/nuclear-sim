// M1-T5-a: the gates.toml loader (03 §10) + its two integrity guards.

#include <catch2/catch_test_macros.hpp>

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

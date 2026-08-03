// M3-T3-f: run.json (03 §6) provenance contract.
//
// DoD: RunProvenance (de)serializes to the 03 §6 shape and round-trips
// idempotently against the 03 §6 example (read from the spec markdown so it
// cannot drift — the M3-T3-a pattern); the parser enforces a required-field
// contract; compute_unit_id realizes 03 §6's sha256(canonical_hash ‖ overrides ‖
// seed) deterministically, sensitive to each input and order-independent in the
// overrides (a dedup key).

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "api/run_provenance.h"
#include "spec_examples.h"

#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

using ns::api::compute_unit_id;
using ns::api::parse_run_json;
using ns::api::RunProvenance;
using ns::api::to_json;

TEST_CASE("the 03 section 6 example parses to the documented provenance fields", "[api]") {
    const RunProvenance r = parse_run_json(spec_examples::run_example());
    REQUIRE(r.schema_version == 1);
    REQUIRE(r.scenario_file == "scenarios/trinity_canonical.toml");
    REQUIRE(r.seed == 12345u);
    REQUIRE(r.code_version == "0.1.0");
    REQUIRE(r.spec_version == "0.2");
    REQUIRE(r.git == "a1b2c3d");
    REQUIRE_FALSE(r.dirty);
    REQUIRE(r.backend == "gpu");
    REQUIRE(r.device == "NVIDIA GeForce RTX 4070 Ti SUPER");
    // data_hashes carries the xs hash and the per-material hash object (03 §6).
    REQUIRE_FALSE(r.data_hashes.materials.empty());
    REQUIRE(r.data_hashes.materials.front().first == "pu_ga_delta");
}

TEST_CASE("run.json serialize is idempotent after parse", "[api]") {
    const std::string once = to_json(parse_run_json(spec_examples::run_example()));
    const std::string twice = to_json(parse_run_json(once));
    REQUIRE(once == twice);
}

TEST_CASE("the run.json serializer emits the 03 section 6 field order", "[api]") {
    // Field order is part of the contract (deterministic re-serialization): the
    // top-level keys appear in the 03 §6 example's order.
    const std::string text = to_json(parse_run_json(spec_examples::run_example()));
    const std::vector<std::string> order = {
        "schema_version", "run_id",      "unit_id", "scenario_file", "scenario_sha256",
        "data_hashes",    "seed",        "code_version", "spec_version", "git",
        "dirty",          "backend",     "device",  "started",       "finished"};
    std::size_t cursor = 0;
    for (const auto& key : order) {
        const std::string quoted = "\"" + key + "\"";
        const std::size_t at = text.find(quoted, cursor);
        INFO("expected key in 03 §6 order: " << key);
        REQUIRE(at != std::string::npos);
        cursor = at + quoted.size();
    }
}

TEST_CASE("the parser rejects a run.json missing a required provenance field", "[api]") {
    // A provenance record that cannot reproduce/dedup a run is a hard parse error.
    for (const char* field : {"run_id", "unit_id", "scenario_sha256", "seed", "git", "backend",
                              "started", "finished"}) {
        nlohmann::ordered_json j = nlohmann::ordered_json::parse(spec_examples::run_example());
        j.erase(field);
        INFO("erased required field: " << field);
        REQUIRE_THROWS_AS(parse_run_json(j.dump()), std::runtime_error);
    }
    // data_hashes.xs is required inside the nested object.
    nlohmann::ordered_json nested = nlohmann::ordered_json::parse(spec_examples::run_example());
    nested["data_hashes"].erase("xs");
    REQUIRE_THROWS_AS(parse_run_json(nested.dump()), std::runtime_error);
}

TEST_CASE("unit_id realizes 03 section 6: deterministic, input-sensitive, order-independent", "[api]") {
    const std::string canon = "a1b2c3d4e5f6";  // stand-in Scenario::canonical_hash()
    const std::vector<std::string> overrides = {"materials.pu_ga_delta.Pu240=0.010",
                                                "xs.Pu239.nu=[2.98,2.92,2.89,2.89]"};
    const std::uint64_t seed = 12345;

    const std::string base = compute_unit_id(canon, overrides, seed);

    // A sha256 hex digest: 64 lowercase hex chars.
    REQUIRE(base.size() == 64);
    REQUIRE(std::regex_match(base, std::regex("[0-9a-f]{64}")));

    // Deterministic.
    REQUIRE(compute_unit_id(canon, overrides, seed) == base);

    // Order-independent in the overrides (a set / dedup key).
    const std::vector<std::string> reordered = {overrides[1], overrides[0]};
    REQUIRE(compute_unit_id(canon, reordered, seed) == base);

    // Sensitive to each of the three 03 §6 inputs.
    REQUIRE(compute_unit_id(canon + "0", overrides, seed) != base);   // canonical hash
    REQUIRE(compute_unit_id(canon, {overrides[0]}, seed) != base);    // overrides set
    REQUIRE(compute_unit_id(canon, overrides, seed + 1) != base);     // seed

    // No overrides is a distinct, still-valid key.
    const std::string none = compute_unit_id(canon, {}, seed);
    REQUIRE(none.size() == 64);
    REQUIRE(none != base);
}

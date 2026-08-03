// M3-T3-f: run.json (03 §6) provenance contract.
//
// DoD: RunProvenance (de)serializes to the 03 §6 shape and round-trips
// idempotently against the 03 §6 example (read from the spec markdown so it
// cannot drift — the M3-T3-a pattern); the parser enforces a required-field
// contract; compute_unit_id realizes 03 §6's sha256(canonical_hash ‖ overrides ‖
// seed) deterministically, sensitive to each input and order-independent in the
// overrides (a dedup key).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include "api/run_provenance.h"
#include "api/studio.h"
#include "core/constants/constants.h"
#include "spec_examples.h"

#include <cmath>
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
        "schema_version", "run_id",   "unit_id",      "scenario_file",      "scenario_sha256",
        "data_hashes",    "scenario_overrides",       "seed",               "code_version",
        "spec_version",   "git",      "dirty",        "backend",            "device",
        "started",        "finished"};
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

// --- M3-T3-g: the demon-core assembly + evaluate gauge ---

using ns::api::DemonCoreAssembly;
using ns::api::EvaluateResult;
using ns::api::evaluate;
using ns::api::evaluate_json;
using ns::api::StudioConfig;
using Catch::Matchers::WithinRel;

namespace {
// A canonical demon-core cfg with a small eigen batch + fixed seed: fast, and the
// fixed seed gives common random numbers so a one-variable change moves k
// SYSTEMATICALLY (the MC noise largely cancels) — the M3-T3-d technique.
StudioConfig gauge_cfg() {
    StudioConfig cfg;         // viz CANONICAL defaults (scenario.js)
    cfg.eigen_batch = 2000;
    cfg.seed = 20260803;
    return cfg;
}
}  // namespace

TEST_CASE("the demon-core assembly builds the C-101/C-102-consistent pit", "[api]") {
    const DemonCoreAssembly a(gauge_cfg());     // pit_mass_kg = 6.15 (canonical)
    // 6.15 kg at the derived δ-Pu density → the canonical pit radius OD/2 = 4.585 cm.
    REQUIRE_THAT(a.r0_cm(), WithinRel(ns::consts::od_pu_ga_core / 2.0, 1e-3));
    REQUIRE_THAT(a.density_g_cm3(), WithinRel(15.23, 2e-3));   // pu_ga_delta card (DERIVED)
    REQUIRE_THAT(a.geometry().outermost_radius(), WithinRel(a.r0_cm(), 1e-9));
}

TEST_CASE("evaluate returns a sane criticality gauge on the demon-core config", "[api]") {
    const EvaluateResult r = evaluate(gauge_cfg());
    REQUIRE(std::isfinite(r.k_eff));
    REQUIRE(r.k_eff > 0.0);
    REQUIRE(r.sigma_pcm > 0.0);
    // k_prompt = k_eff·(1 - beta_eff), a small delayed fraction (SIM beta 0.0021).
    REQUIRE(r.k_prompt < r.k_eff);
    REQUIRE(r.k_prompt > 0.99 * r.k_eff);
    // `ready` is exactly the prompt-supercritical predicate.
    REQUIRE(r.ready == (r.k_prompt >= 1.0));
}

TEST_CASE("compression raises the gauge k (the reactivity handle)", "[api]") {
    StudioConfig bare = gauge_cfg();  bare.compression_ratio = 1.0;
    StudioConfig comp = gauge_cfg();  comp.compression_ratio = 2.5;
    const double k_bare = evaluate(bare).k_eff;
    const double k_comp = evaluate(comp).k_eff;
    INFO("k_bare=" << k_bare << " k_compressed=" << k_comp);
    REQUIRE(k_comp > k_bare);         // ρ ∝ ratio via mass-cons → k rises (M3-T3-d)
}

TEST_CASE("more pit mass raises k; more Pu-240 lowers it", "[api]") {
    // Common random numbers (same seed) → each one-variable change is systematic.
    StudioConfig light = gauge_cfg();  light.pit_mass_kg = 4.0;
    StudioConfig heavy = gauge_cfg();  heavy.pit_mass_kg = 9.0;
    const double k_light = evaluate(light).k_eff;
    const double k_heavy = evaluate(heavy).k_eff;
    INFO("k(4kg)=" << k_light << " k(9kg)=" << k_heavy);
    REQUIRE(k_heavy > k_light);        // more mass → bigger sphere → less leakage

    StudioConfig clean = gauge_cfg();  clean.pu240_fraction = 0.005;
    StudioConfig dirty = gauge_cfg();  dirty.pu240_fraction = 0.030;
    const double k_clean = evaluate(clean).k_eff;
    const double k_dirty = evaluate(dirty).k_eff;
    INFO("k(0.5% Pu240)=" << k_clean << " k(3% Pu240)=" << k_dirty);
    REQUIRE(k_dirty < k_clean);        // Pu-240 is a net poison (SIM: a weaker fuel)
}

TEST_CASE("evaluate_json is the JSON cfg seam and from_json reads the dotted keys", "[api]") {
    // from_json reads the viz scenario.js dotted keys; missing keys keep defaults;
    // unknown keys are ignored.
    const std::string cfg_json = R"({
      "pit.mass_kg": 7.0, "materials.pu_ga_delta.Pu240": 0.02,
      "compression.ratio": 2.3, "seed": 12345, "eigen.batch": 1500,
      "some.unknown.key": 42
    })";
    const StudioConfig cfg = StudioConfig::from_json(cfg_json);
    REQUIRE_THAT(cfg.pit_mass_kg, WithinRel(7.0, 1e-12));
    REQUIRE_THAT(cfg.pu240_fraction, WithinRel(0.02, 1e-12));
    REQUIRE_THAT(cfg.compression_ratio, WithinRel(2.3, 1e-12));
    REQUIRE(cfg.seed == 12345u);
    REQUIRE(cfg.eigen_batch == 1500);
    REQUIRE_THAT(cfg.generation_time_s_initial, WithinRel(1.0e-8, 1e-12));  // default (key absent)

    // evaluate_json returns the EvaluateResult shape.
    const std::string out = evaluate_json(
        R"({"pit.mass_kg":6.15,"compression.ratio":2.2,"seed":20260803,"eigen.batch":1500})");
    const auto j = nlohmann::json::parse(out);
    REQUIRE(j.contains("k_eff"));
    REQUIRE(j.contains("k_prompt"));
    REQUIRE(j.contains("sigma_pcm"));
    REQUIRE(j.contains("ready"));
    REQUIRE(j["k_eff"].get<double>() > 0.0);
}

// --- M3-T3-h: the emergent demon-core burst (generate_run) ---

using ns::api::generate_run;
using ns::api::generate_run_json;
using ns::api::GenerateRunResult;

namespace {
// A fast burst cfg: small eigen batch + bounded window.
StudioConfig burst_cfg() {
    StudioConfig cfg;
    cfg.eigen_batch = 1500;
    cfg.seed = 20260803;
    cfg.burst_t_max_s = 1.0e-5;
    cfg.burst_max_generations = 3000;
    return cfg;
}

ns::physics::InvariantConstants burst_invariant_constants(const StudioConfig& cfg) {
    ns::physics::InvariantConstants c;
    c.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    c.n_a = ns::consts::avogadro_constant;
    c.m_pit_g = cfg.pit_mass_kg * 1000.0;
    c.m_pu239 = ns::consts::molar_mass_pu239;
    c.m_pu240 = ns::consts::molar_mass_pu240;
    c.m_pu241 = ns::consts::molar_mass_pu241;
    c.t_max_s = cfg.burst_t_max_s;
    return c;
}
}  // namespace

TEST_CASE("generate_run: a supercritical demon-core config detonates and self-limits", "[api]") {
    StudioConfig cfg = burst_cfg();
    cfg.pit_mass_kg = 9.0;
    cfg.compression_ratio = 2.5;   // a strongly-compressed heavy pit -> prompt-supercritical
    const GenerateRunResult r = generate_run(cfg);

    INFO("detonate=" << r.detonate << " super=" << r.supercritical << " quenched=" << r.quenched
                     << " yield_kt=" << r.yield_kt << " k_peak=" << r.k_eff_peak << " k_quench="
                     << r.tally.k_eff.at_quench << " gens=" << r.tally.timing.generations
                     << " samples=" << r.samples.size());
    REQUIRE(r.supercritical);      // it ignited -- emergent from k, not a rule
    REQUIRE(r.quenched);           // self-terminated by disassembly
    REQUIRE(r.detonate);
    REQUIRE(std::isfinite(r.yield_kt));
    REQUIRE(r.yield_kt > 0.0);
    REQUIRE(r.tally.k_eff.at_quench < r.tally.k_eff.peak);   // disassembly dropped k

    // The tally satisfies all nine 03 section 5 invariants.
    REQUIRE(ns::physics::tally_invariants(r.tally, burst_invariant_constants(cfg)).empty());

    // The sample/site stream (pitscope's main course) is present with real sites.
    REQUIRE_FALSE(r.samples.empty());
    std::size_t with_sites = 0;
    for (const auto& g : r.samples) {
        if (!g.sites.empty()) ++with_sites;
    }
    REQUIRE(with_sites >= 1);

    // A valid run.json provenance that round-trips.
    REQUIRE(r.run.unit_id.size() == 64);
    REQUIRE(r.run.backend == "ref");
    const ns::api::RunProvenance rt = ns::api::parse_run_json(ns::api::to_json(r.run));
    REQUIRE(rt.unit_id == r.run.unit_id);
    REQUIRE(rt.seed == cfg.seed);
}

TEST_CASE("generate_run: a subcritical config fizzles (never prompt-critical)", "[api]") {
    StudioConfig cfg = burst_cfg();
    cfg.pit_mass_kg = 4.0;
    cfg.compression_ratio = 1.0;       // an uncompressed light pit -> subcritical
    cfg.burst_max_generations = 60;    // a decaying burst never quenches; cap for speed
    const GenerateRunResult r = generate_run(cfg);

    INFO("detonate=" << r.detonate << " super=" << r.supercritical << " yield=" << r.yield_kt
                     << " k_peak=" << r.k_eff_peak);
    REQUIRE_FALSE(r.supercritical);    // never reached prompt-criticality
    REQUIRE_FALSE(r.detonate);         // fizzle -- emergent from k, not a hardcoded rule
    REQUIRE_FALSE(r.reasons.empty());
}

TEST_CASE("generate_run_json carries the tally, run, and sample stream", "[api]") {
    // A short window keeps it fast; the JSON SHAPE is independent of the outcome.
    const std::string cfg_json =
        R"({"pit.mass_kg":9.0,"compression.ratio":2.5,"seed":20260803,"eigen.batch":1200,"kinetics.t_max_s":1.0e-6})";
    const auto j = nlohmann::json::parse(generate_run_json(cfg_json));

    // Top-level DATA fields (simstub.js generateRun shape, minus the JS closures).
    for (const char* key : {"detonate", "reasons", "yield_kt", "supercritical", "quenched", "tally",
                            "run", "samples"}) {
        INFO("expected top-level key: " << key);
        REQUIRE(j.contains(key));
    }
    // tally is the 03 §5 object; run is the 03 §6 object (with the reconciled overrides).
    REQUIRE(j["tally"].contains("fissions_by_isotope"));
    REQUIRE(j["tally"].contains("yield_split"));
    REQUIRE(j["run"].contains("unit_id"));
    REQUIRE(j["run"].contains("scenario_overrides"));
    REQUIRE(j["run"]["backend"].get<std::string>() == "ref");

    // The sample/site stream carries the pitscope fields, and at least one sample
    // carries real fission SITES (the volumetric chain-reaction cascade).
    REQUIRE(j["samples"].is_array());
    REQUIRE_FALSE(j["samples"].empty());
    const auto& s0 = j["samples"][0];
    for (const char* key : {"n", "t_s", "lambda_s", "k_eff", "log10_population", "sites"}) {
        INFO("expected sample key: " << key);
        REQUIRE(s0.contains(key));
    }
    bool any_sites = false;
    for (const auto& s : j["samples"]) {
        if (!s["sites"].empty()) {
            const auto& site = s["sites"][0];
            REQUIRE(site["pos"].is_array());
            REQUIRE(site["pos"].size() == 3);
            REQUIRE(site.contains("isotope"));
            REQUIRE(site.contains("layer"));
            any_sites = true;
            break;
        }
    }
    REQUIRE(any_sites);
}

TEST_CASE("run.json round-trips scenario_overrides (the 03 section 4 vs 6 reconciliation)", "[api]") {
    // 03 §4 mandates overrides recorded verbatim in run.json; the §6 example now
    // carries the field. A provenance with overrides survives to_json -> parse.
    ns::api::RunProvenance r = ns::api::parse_run_json(spec_examples::run_example());
    REQUIRE(r.scenario_overrides.empty());   // the canonical example has none

    r.scenario_overrides = {"materials.pu_ga_delta.Pu240=0.020", "xs.Pu239.nu=[2.98,2.92,2.89,2.89]"};
    const ns::api::RunProvenance rt = ns::api::parse_run_json(ns::api::to_json(r));
    REQUIRE(rt.scenario_overrides == r.scenario_overrides);   // verbatim, in order

    // The 03 §6 example itself now includes the field (the reconciliation is in the spec).
    REQUIRE(nlohmann::json::parse(spec_examples::run_example()).contains("scenario_overrides"));
}

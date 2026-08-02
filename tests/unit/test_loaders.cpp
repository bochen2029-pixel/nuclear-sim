// M0-T5: xs / materials / scenario loaders (03 §2-§4, 04 §3/§5/§6).
//
// The positive tests parse the SPEC'S OWN example documents, extracted from
// 03-data-contracts.md at run time. The negative tests cover one rule per
// violation class, because "the loader validates" is a claim and "the loader
// rejects this exact document with this exact diagnostic" is evidence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/diagnostics.h"
#include "core/material/material.h"
#include "core/scenario/scenario.h"
#include "core/xs/xs.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

#include "spec_examples.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

/// A scratch tree that looks like the repository's data layout.
class Fixture {
public:
    Fixture() {
        root_ = fs::temp_directory_path() / fs::path("nukesim_loaders_"
                                                     + std::to_string(counter()++));
        fs::remove_all(root_);
        fs::create_directories(root_ / "data" / "xs");
        fs::create_directories(root_ / "data" / "materials");
        fs::create_directories(root_ / "data" / "scenarios");
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    const fs::path& root() const { return root_; }
    fs::path xs_path() const { return root_ / "data" / "xs" / "fast4.json"; }
    fs::path materials_dir() const { return root_ / "data" / "materials"; }
    fs::path scenario_path() const { return root_ / "data" / "scenarios" / "s.toml"; }

    void write(const fs::path& relative, const std::string& text) const {
        spec_examples::write_file(root_ / relative, text);
    }

    /// The spec's §2 example, with the extra isotopes the §3/§4 examples name.
    /// Only Pu239 is spelled out in the spec; the rest are clones so that
    /// resolution can be tested. They are scaffolding, not physics — M1-T4a
    /// authors the cited dataset.
    json xs_document() const {
        json doc = json::parse(spec_examples::xs_example());
        const json base = doc["isotopes"]["Pu239"];
        for (const char* name : {"Pu240", "Ga69", "Ga71", "U235", "U238", "B10", "B11",
                                 "C12", "H1", "O16", "Al27", "Be9", "Po210"}) {
            json clone = base;
            clone["status"] = "SIM";
            clone["cite"] = "test scaffolding — M1-T4a authors the cited dataset";
            doc["isotopes"][name] = clone;
        }
        return doc;
    }

    void write_full_dataset() const {
        write("data/xs/fast4.json", xs_document().dump(2));
        write("data/materials/pu_ga_delta.json", spec_examples::material_example());
        // Minimal stand-ins for the other materials the §4 scenario names.
        for (const auto& [name, species] : std::vector<std::pair<std::string, std::string>>{
                 {"be_po_urchin", "Be9"}, {"u_natural", "U238"},
                 {"b10_acrylic", "B10"}, {"aluminum", "Al27"}}) {
            json m = {{"schema_version", 1},
                      {"name", name},
                      {"density_g_cm3", 2.7},
                      {"status", "SIM"},
                      {"cite", "test scaffolding — M2-T1 authors the cited materials"},
                      {"isotopes", {{species, 1.0}}}};
            write("data/materials/" + name + ".json", m.dump(2));
        }
    }

private:
    static int& counter() {
        static int value = 0;
        return value;
    }
    fs::path root_;
};

/// Write an xs document with one mutation applied, then expect a rejection.
void expect_xs_rejected(const Fixture& fx, const std::function<void(json&)>& mutate,
                        const std::string& expected_substring) {
    json doc = fx.xs_document();
    mutate(doc);
    fx.write("data/xs/fast4.json", doc.dump(2));
    REQUIRE_THROWS_WITH(ns::xs::FewGroupXS::load(fx.xs_path()),
                        Catch::Matchers::ContainsSubstring(expected_substring));
}

}  // namespace

// ---------------------------------------------------------------------------
// Cross sections (03 §2)
// ---------------------------------------------------------------------------

TEST_CASE("the spec's own xs example parses", "[loaders]") {
    Fixture fx;
    fx.write("data/xs/fast4.json", spec_examples::xs_example());
    const auto set = ns::xs::FewGroupXS::load(fx.xs_path());

    REQUIRE(set.name() == "fast4");
    REQUIRE(set.groups() == 4);
    REQUIRE(set.bounds_MeV().size() == 5);
    REQUIRE(set.has_isotope("Pu239"));

    const auto& pu = set.isotope("Pu239");
    REQUIRE(pu.beta == 0.0020);          // REQUIRED by ADR-013
    REQUIRE(pu.g.size() == 4);
    REQUIRE(pu.status == "PUBLIC");
    REQUIRE_FALSE(pu.transfer_is_null);

    // sigma_t is COMPUTED, never read from file.
    const auto& g0 = pu.g[0];
    REQUIRE_THAT(g0.sigma_t(),
                 Catch::Matchers::WithinRel(g0.sigma_f + g0.sigma_c + g0.sigma_s + g0.sigma_n2n,
                                            1e-15));
    REQUIRE_THAT(g0.sigma_tr(),
                 Catch::Matchers::WithinRel(g0.sigma_t() - g0.mu_bar * g0.sigma_s, 1e-15));
    REQUIRE(g0.sigma_tr() < g0.sigma_t());  // transport correction reduces it
}

TEST_CASE("xs loader rejects every schema-v2 violation class", "[loaders]") {
    Fixture fx;

    SECTION("sigma_a gets a migration diagnostic, not a generic error") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"]["sigma_a"] = json::array({1, 1, 1, 1}); },
                           "sigma_c for radiative capture");
    }
    SECTION("sigma_t present in file") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"]["sigma_t"] = json::array({1, 1, 1, 1}); },
                           "computed by the loader");
    }
    SECTION("missing mu_bar") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"].erase("mu_bar"); },
                           "is REQUIRED");
    }
    SECTION("missing beta") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"].erase("beta"); },
                           "numerically undetectable");
    }
    SECTION("non-descending group bounds") {
        expect_xs_rejected(fx, [](json& d) { d["group_bounds_MeV"] = json::array({20.0, 3.0, 3.0, 0.1, 1e-3}); },
                           "strictly descending");
    }
    SECTION("wrong number of group bounds") {
        expect_xs_rejected(fx, [](json& d) { d["group_bounds_MeV"] = json::array({20.0, 3.0, 1.0}); },
                           "schema-v3 path");
    }
    SECTION("upscatter") {
        expect_xs_rejected(fx, [](json& d) {
            d["isotopes"]["Pu239"]["transfer"] = json::array({
                json::array({0.70, 0.20, 0.08, 0.02}), json::array({0.10, 0.60, 0.20, 0.10}),
                json::array({0.0, 0.0, 0.80, 0.20}), json::array({0.0, 0.0, 0.0, 1.0})});
        }, "upscatter");
    }
    SECTION("transfer row that does not sum to 1") {
        expect_xs_rejected(fx, [](json& d) {
            d["isotopes"]["Pu239"]["transfer"][0] = json::array({0.70, 0.20, 0.08, 0.10});
        }, "must sum to 1.0");
    }
    SECTION("null transfer outside a SIM set") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"]["transfer"] = nullptr; },
                           "only for status = \"SIM\"");
    }
    SECTION("unknown schema version") {
        expect_xs_rejected(fx, [](json& d) { d["schema_version"] = 1; }, "v1 used sigma_a");
    }
    SECTION("missing cite") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"].erase("cite"); },
                           "cite/status");
    }
    SECTION("implausible beta") {
        expect_xs_rejected(fx, [](json& d) { d["isotopes"]["Pu239"]["beta"] = 0.5; },
                           "must lie in [0, 0.05)");
    }
}

TEST_CASE("a null transfer is permitted for a SIM set and flagged", "[loaders]") {
    Fixture fx;
    json doc = fx.xs_document();
    doc["isotopes"]["Pu239"]["status"] = "SIM";
    doc["isotopes"]["Pu239"]["transfer"] = nullptr;
    fx.write("data/xs/fast4.json", doc.dump(2));

    const auto set = ns::xs::FewGroupXS::load(fx.xs_path());
    REQUIRE(set.isotope("Pu239").transfer_is_null);
    // Gate runs reject these; the flag is how nukebench will know (03 §2).
    REQUIRE(set.has_null_transfer());
}

// ---------------------------------------------------------------------------
// Materials (03 §3)
// ---------------------------------------------------------------------------

TEST_CASE("the spec's own material example parses and yields number densities", "[loaders]") {
    Fixture fx;
    fx.write_full_dataset();
    const auto set = ns::xs::FewGroupXS::load(fx.xs_path());

    std::vector<ns::LoadWarning> warnings;
    const auto mat = ns::material::MaterialLib::load_file(
        fx.materials_dir() / "pu_ga_delta.json", set, &warnings);

    REQUIRE(mat.name == "pu_ga_delta");
    REQUIRE(mat.density == 15.23);
    REQUIRE(mat.fracs.size() == 4);
    // 04 §5's sorted-name index order.
    REQUIRE(mat.fracs.front().species == "Ga69");

    // Mean molar mass is fraction-weighted; the pit is overwhelmingly Pu, so it
    // must land near Pu-239 rather than near the Ga stand-ins.
    REQUIRE(mat.mean_molar_mass > 230.0);
    REQUIRE(mat.mean_molar_mass < 240.0);

    // Total number density: rho/Mbar * N_A. A units slip here (g vs kg, barns
    // vs cm^2) moves this by 24 orders of magnitude, so the bound is loose on
    // purpose and still decisive.
    double total = 0.0;
    for (std::size_t i = 0; i < mat.fracs.size(); ++i) {
        total += mat.number_density(i);
    }
    REQUIRE(total > 1e22);
    REQUIRE(total < 1e23);

    REQUIRE(mat.macro.sigma_f.size() == 4);
    REQUIRE(mat.macro.sigma_f[0] > 0.0);
    REQUIRE(mat.macro.nu_sigma_f[0] > mat.macro.sigma_f[0]);  // nu-bar ~ 2.9 > 1
    REQUIRE(mat.macro.sigma_tr[0] < mat.macro.sigma_t[0]);

    // The spec example's composition_check should agree with its own fractions.
    for (const auto& w : warnings) {
        INFO(w.field << ": " << w.message);
    }
    REQUIRE(warnings.empty());
}

TEST_CASE("material loader rejects every 03 section 3 violation class", "[loaders]") {
    Fixture fx;
    fx.write_full_dataset();
    const auto set = ns::xs::FewGroupXS::load(fx.xs_path());
    const auto path = fx.materials_dir() / "probe.json";

    auto write_and_expect = [&](const json& doc, const std::string& expected) {
        spec_examples::write_file(path, doc.dump(2));
        REQUIRE_THROWS_WITH(ns::material::MaterialLib::load_file(path, set),
                            Catch::Matchers::ContainsSubstring(expected));
    };

    json good = json::parse(spec_examples::material_example());

    SECTION("atom fractions that do not sum to 1") {
        json bad = good;
        bad["isotopes"]["Pu239"] = 0.5;
        write_and_expect(bad, "MUST sum to 1.0");
    }
    SECTION("a species with no molar mass is a HARD error") {
        json bad = good;
        bad["isotopes"] = {{"Unobtanium", 1.0}};
        write_and_expect(bad, "completeness rule");
    }
    SECTION("non-positive density") {
        json bad = good;
        bad["density_g_cm3"] = 0.0;
        write_and_expect(bad, "must be positive");
    }
    SECTION("unknown schema version") {
        json bad = good;
        bad["schema_version"] = 7;
        write_and_expect(bad, "must be 1");
    }
    SECTION("missing cite") {
        json bad = good;
        bad.erase("cite");
        write_and_expect(bad, "is REQUIRED");
    }
}

TEST_CASE("composition_check deviation warns rather than failing", "[loaders]") {
    // 03 §3 makes this a WARN: the declared weight percents are derived readouts
    // of a composition the atom fractions already fix, so a mismatch means the
    // note is stale, not that the material is unusable.
    Fixture fx;
    fx.write_full_dataset();
    const auto set = ns::xs::FewGroupXS::load(fx.xs_path());

    json doc = json::parse(spec_examples::material_example());
    doc["composition_check"]["Ga_wt_pct"] = 9.0;  // far from the true ~1.0
    const auto path = fx.materials_dir() / "probe.json";
    spec_examples::write_file(path, doc.dump(2));

    std::vector<ns::LoadWarning> warnings;
    REQUIRE_NOTHROW(ns::material::MaterialLib::load_file(path, set, &warnings));
    REQUIRE(warnings.size() == 1);
    REQUIRE_THAT(warnings.front().field, Catch::Matchers::ContainsSubstring("Ga_wt_pct"));
}

// ---------------------------------------------------------------------------
// Scenario (03 §4)
// ---------------------------------------------------------------------------

TEST_CASE("the spec's own scenario example parses and resolves its datasets", "[loaders]") {
    Fixture fx;
    fx.write_full_dataset();
    fx.write("data/scenarios/s.toml", spec_examples::scenario_example());

    std::vector<ns::LoadWarning> warnings;
    const auto loaded = ns::scenario::load_scenario(fx.scenario_path(), fx.root(), &warnings);
    const auto& s = loaded.scenario;

    REQUIRE(s.name == "trinity_canonical");
    REQUIRE(s.seed == 12345);
    REQUIRE(s.mode == ns::scenario::Mode::Alpha);
    REQUIRE(s.layers.size() == 5);
    REQUIRE(s.layers.front().id == "initiator");
    REQUIRE(s.layers.back().id == "pusher");
    REQUIRE(s.t_zero == "he_initiation");
    REQUIRE(s.compression_tier == 2);
    REQUIRE(s.tallies.size() == 6);
    REQUIRE(s.ui.size() == 2);
    REQUIRE(loaded.xs.name() == "fast4");
    REQUIRE(loaded.materials.contains("pu_ga_delta"));

    // t_c_s is Tier-1 only and the example runs at tier 2, so it must be
    // ignored WITH a warning rather than silently honoured (MAJ-09).
    REQUIRE_FALSE(s.compression_t_c_s.has_value());
    bool warned_t_c = false;
    bool warned_source = false;
    for (const auto& w : warnings) {
        warned_t_c = warned_t_c || w.field == "compression.t_c_s";
        warned_source = warned_source || w.field == "source";
    }
    REQUIRE(warned_t_c);
    // The example also carries a [source] block under mode = "alpha" as a schema
    // illustration. It is ignored WITH a warning, never silently — the same
    // treatment compression.t_c_s gets at tier 2.
    REQUIRE(warned_source);

    // The [ui.*] annotation carries display units without changing the value.
    const auto& ui = s.ui.at("kinetics.generation_time_s_initial");
    REQUIRE(ui.display_unit.has_value());
    REQUIRE(*ui.display_scale == 1.0e9);
    REQUIRE(s.generation_time_s_initial == 1.0e-8);  // stored in schema units
}

TEST_CASE("scenario loader rejects every 03 section 4 violation class", "[loaders]") {
    Fixture fx;
    fx.write_full_dataset();
    const std::string good = spec_examples::scenario_example();

    auto expect = [&](const std::string& text, const std::string& expected) {
        fx.write("data/scenarios/s.toml", text);
        REQUIRE_THROWS_WITH(ns::scenario::load_scenario(fx.scenario_path(), fx.root()),
                            Catch::Matchers::ContainsSubstring(expected));
    };
    auto replace = [](std::string text, const std::string& from, const std::string& to) {
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        return text.replace(at, from.size(), to);
    };

    SECTION("mode = td is a validation error with its reason") {
        expect(replace(good, "mode = \"alpha\"", "mode = \"td\""), "ADR-003");
    }
    SECTION("unknown mode") {
        expect(replace(good, "mode = \"alpha\"", "mode = \"quantum\""), "must be one of alpha");
    }
    SECTION("unknown top-level key") {
        expect(good + "\nquench_epsilion = 1.0e-4\n", "unknown key");
    }
    SECTION("unknown key inside a table") {
        expect(replace(good, "[transport]\nsim_neutrons", "[transport]\nsim_neutronz"),
               "unknown key");
    }
    SECTION("non-increasing layer radii") {
        expect(replace(good, "r_outer_cm = 11.43", "r_outer_cm = 4.585"),
               "strictly increasing");
    }
    SECTION("layers placed under [data] is diagnosed, not silently reparented") {
        // Reproduce the original formatting artifact by moving the array below
        // the [data] header.
        std::string text = good;
        const auto start = text.find("layers = [");
        const auto end = text.find("]\n", start) + 2;
        const std::string block = text.substr(start, end - start);
        text.erase(start, end - start);
        const auto data_at = text.find("materials_dir = \"data/materials\"");
        text.insert(text.find('\n', data_at) + 1, "\n" + block);
        expect(text, "must be a TOP-LEVEL array");
    }
    SECTION("unknown tally name") {
        expect(replace(good, "\"fission_mesh\"]", "\"fission_mesh\", \"vibes\"]"),
               "closed vocabulary");
    }
    SECTION("fixed_source mode without a source block") {
        // The example carries a [source] block, so it must be removed as well as
        // the mode changed — otherwise this would test nothing.
        std::string text = replace(good, "mode = \"alpha\"", "mode = \"fixed_source\"");
        const auto start = text.find("[source]");
        REQUIRE(start != std::string::npos);
        const auto end = text.find("[overrides]", start);
        REQUIRE(end != std::string::npos);
        text.erase(start, end - start);
        expect(text, "is REQUIRED when mode");
    }
    SECTION("t_zero other than he_initiation") {
        expect(replace(good, "t_zero = \"he_initiation\"", "t_zero = \"first_neutron\""),
               "ADR-010");
    }
    SECTION("compression tier 3") {
        expect(replace(good, "tier = 2", "tier = 3"), "stretch interface only");
    }
    SECTION("value outside its declared [ui.*] range") {
        expect(replace(good, "strength_n_per_s   = 1.0e8", "strength_n_per_s   = 9.0e8"),
               "outside the declared range");
    }
    SECTION("missing seed") {
        expect(replace(good, "seed = 12345", "# no seed"), "Determinism");
    }
    SECTION("xs set whose name does not match") {
        json doc = fx.xs_document();
        doc["name"] = "something_else";
        fx.write("data/xs/fast4.json", doc.dump(2));
        expect(good, "must equal data.xs_set");
    }
    SECTION("material that does not resolve") {
        fs::remove(fx.materials_dir() / "aluminum.json");
        expect(good, "does not resolve under data.materials_dir");
    }
}

TEST_CASE("jezebel may not borrow the Trinity pit composition", "[loaders]") {
    // 03 §3: Jezebel is ~4.5 wt% Pu-240. Using pu_ga_delta would move k well
    // past G0b's tolerance while looking entirely plausible.
    Fixture fx;
    fx.write_full_dataset();
    std::string text = spec_examples::scenario_example();
    const auto at = text.find("name = \"trinity_canonical\"");
    text = text.replace(at, std::string("name = \"trinity_canonical\"").size(),
                        "name = \"jezebel\"");
    fx.write("data/scenarios/s.toml", text);

    REQUIRE_THROWS_WITH(ns::scenario::load_scenario(fx.scenario_path(), fx.root()),
                        Catch::Matchers::ContainsSubstring("pu_ga_jezebel"));
}

TEST_CASE("override grammar accepts the canonical forms and rejects the rest", "[loaders]") {
    ns::scenario::Scenario s;

    REQUIRE_NOTHROW(s.apply_overrides({{"layers.pit.r_outer_cm", 4.6}}));
    REQUIRE_NOTHROW(s.apply_overrides({{"layers[1].r_outer_cm", 4.6}}));
    REQUIRE_NOTHROW(s.apply_overrides({{"kinetics.t_max_s", 1.0e-5}}));

    // Data overrides touch PUBLIC/DECLASSIFIED values, so the run stops being
    // canonical and may not be used as gate evidence (03 §4).
    REQUIRE_FALSE(s.non_canonical);
    REQUIRE_NOTHROW(s.apply_overrides({{"materials.pu_ga_delta.Pu240", 0.010}}));
    REQUIRE(s.non_canonical);

    ns::scenario::Scenario other;
    REQUIRE_NOTHROW(other.apply_overrides({{"xs.Pu239.nu", std::vector<double>{2.9, 2.9, 2.9, 2.9}}}));
    REQUIRE(other.non_canonical);

    // Ad-hoc prefixes are rejected, not ignored (04 §6).
    REQUIRE_THROWS_WITH(s.apply_overrides({{"tamper_override.thickness", 1.0}}),
                        Catch::Matchers::ContainsSubstring("unrecognised override prefix"));
    REQUIRE_THROWS_WITH(s.apply_overrides({{"nonsense", 1.0}}),
                        Catch::Matchers::ContainsSubstring("unrecognised override prefix"));
}

TEST_CASE("diagnostics name file, field and constraint", "[loaders]") {
    // 03's preamble requires all three. A loader that says "invalid input"
    // costs the next session an hour.
    Fixture fx;
    json doc = fx.xs_document();
    doc["isotopes"]["Pu239"].erase("beta");
    fx.write("data/xs/fast4.json", doc.dump(2));

    try {
        ns::xs::FewGroupXS::load(fx.xs_path());
        FAIL("expected a LoadError");
    } catch (const ns::LoadError& err) {
        REQUIRE(err.file().filename() == "fast4.json");
        REQUIRE(err.field() == "isotopes.Pu239.beta");
        REQUIRE_THAT(err.constraint(), Catch::Matchers::ContainsSubstring("REQUIRED"));
        REQUIRE_THAT(std::string(err.what()), Catch::Matchers::ContainsSubstring("fast4.json"));
    }
}

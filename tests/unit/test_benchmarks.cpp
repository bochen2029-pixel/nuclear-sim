// M1-T4a-1: the PUBLIC-DERIVED benchmark models (08 §1, ADR-016/ADR-017).
//
// Every number in data/benchmarks/*.md is RECOMPUTED here from the committed
// material files, so the data card cannot drift from the data it documents.
// The published atom densities are the primary datum; density, mass and weight
// percentages are derived, which makes each of them an independent check on the
// figures quoted in the literature rather than a restatement of them.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/constants/constants.h"
#include "core/diagnostics.h"
#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/scenario/scenario.h"
#include "core/xs/xs.h"
#include "physics/eigen/eigen.h"
#include "ref/ref_transport.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

#include "spec_examples.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr double kPi = 3.14159265358979323846;

fs::path repo() { return spec_examples::repo_root(); }

/// A cross-section set carrying every species the benchmarks name.
///
/// Deliberately synthetic: the CITED fast4 dataset is M1-T4a-2 and must not be
/// invented. Cross sections play no part in the checks below — they are all
/// composition arithmetic — but MaterialLib needs a set to resolve against, and
/// a species with no entry is legal (it contributes mass, not Sigma).
class Fixture {
public:
    Fixture() {
        root_ = fs::temp_directory_path() / "nukesim_benchmarks";
        fs::remove_all(root_);
        fs::create_directories(root_);

        const auto four = [](double v) { return json::array({v, v, v, v}); };
        json iso = {{"nu", four(2.9)}, {"chi", json::array({1.0, 0.0, 0.0, 0.0})},
                    {"sigma_f", four(1.0)}, {"sigma_c", four(0.1)}, {"sigma_s", four(4.0)},
                    {"sigma_n2n", four(0.0)}, {"mu_bar", four(0.0)}, {"beta", 0.0020},
                    {"transfer", nullptr},
                    {"cite", "placeholder — the cited dataset is M1-T4a-2"},
                    {"status", "SIM"}};
        json xs = {{"schema_version", 2}, {"name", "probe"},
                   {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3})},
                   {"isotopes", json::object()}};
        for (const char* n : {"U234", "U235", "U238", "Pu239", "Pu240", "Pu241", "Ga69", "Ga71"}) {
            xs["isotopes"][n] = iso;
        }
        spec_examples::write_file(root_ / "probe.json", xs.dump(2));
        set_ = std::make_unique<ns::xs::FewGroupXS>(
            ns::xs::FewGroupXS::load(root_ / "probe.json"));
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ns::material::Material load(const std::string& name,
                                std::vector<ns::LoadWarning>* warnings) const {
        return ns::material::MaterialLib::load_file(
            repo() / "data" / "materials" / (name + ".json"), *set_, warnings);
    }

private:
    fs::path root_;
    std::unique_ptr<ns::xs::FewGroupXS> set_;
};

/// Total number density in nuclei/b-cm — the published primary datum.
double sum_atom_density(const ns::material::Material& mat) {
    double total = 0.0;
    for (std::size_t i = 0; i < mat.fracs.size(); ++i) {
        total += mat.number_density(i);
    }
    return total * 1e-24;
}

double mass_kg(const ns::material::Material& mat, double radius_cm) {
    return mat.density * (4.0 / 3.0) * kPi * radius_cm * radius_cm * radius_cm / 1000.0;
}

double weight_pct(const ns::material::Material& mat, const std::string& species) {
    double num = 0.0;
    for (const auto& c : mat.fracs) {
        if (c.species == species) {
            num += c.atom_fraction * c.molar_mass;
        }
    }
    return 100.0 * num / mat.mean_molar_mass;
}

}  // namespace

TEST_CASE("Godiva reproduces its published atom densities, density and mass", "[benchmarks]") {
    // ADR-016. JEFF Report 16 Annex 3 / CSEWG F5.
    const Fixture fx;
    std::vector<ns::LoadWarning> warnings;
    const auto mat = fx.load("u_godiva", &warnings);

    REQUIRE(mat.name == "u_godiva");
    REQUIRE(mat.status == "PUBLIC-DERIVED");
    REQUIRE(warnings.empty());

    // The published table, recovered from atom fractions x density / Mbar.
    const double total = sum_atom_density(mat);
    REQUIRE_THAT(total, Catch::Matchers::WithinRel(0.047990, 1e-5));

    for (const auto& [species, published] : std::vector<std::pair<std::string, double>>{
             {"U235", 0.045000}, {"U238", 0.002498}, {"U234", 0.000492}}) {
        double n = 0.0;
        for (std::size_t i = 0; i < mat.fracs.size(); ++i) {
            if (mat.fracs[i].species == species) {
                n = mat.number_density(i) * 1e-24;
            }
        }
        INFO(species << ": published " << published << ", recovered " << n);
        REQUIRE_THAT(n, Catch::Matchers::WithinRel(published, 1e-5));
    }

    // Derived quantities, checked against the independently published figures.
    REQUIRE_THAT(mat.density, Catch::Matchers::WithinRel(18.7421, 1e-4));
    REQUIRE_THAT(mass_kg(mat, 8.741), Catch::Matchers::WithinRel(52.431, 1e-4));
    REQUIRE_THAT(weight_pct(mat, "U235"), Catch::Matchers::WithinRel(93.7112, 1e-4));
    REQUIRE_THAT(weight_pct(mat, "U238"), Catch::Matchers::WithinRel(5.2686, 1e-3));
    REQUIRE_THAT(weight_pct(mat, "U234"), Catch::Matchers::WithinRel(1.0202, 1e-3));
}

TEST_CASE("Jezebel reproduces its published atom densities, density and mass", "[benchmarks]") {
    // ADR-017. JEFF Report 16 Annex 3 / CSEWG F1.
    const Fixture fx;
    std::vector<ns::LoadWarning> warnings;
    const auto mat = fx.load("pu_ga_jezebel", &warnings);

    REQUIRE(mat.name == "pu_ga_jezebel");
    REQUIRE(mat.status == "PUBLIC-DERIVED");
    for (const auto& w : warnings) {
        INFO(w.field << ": " << w.message);
    }
    REQUIRE(warnings.empty());

    REQUIRE_THAT(sum_atom_density(mat), Catch::Matchers::WithinRel(0.040293, 1e-5));
    REQUIRE_THAT(mat.density, Catch::Matchers::WithinRel(15.6112, 1e-4));
    REQUIRE_THAT(mass_kg(mat, 6.385), Catch::Matchers::WithinRel(17.022, 1e-4));

    // The natural-Ga split is an explicit author choice (03 §3 bars the loader
    // from making it). It must reproduce the natural-Ga mass contribution, or
    // the derived density would have moved.
    const double ga_wt = weight_pct(mat, "Ga69") + weight_pct(mat, "Ga71");
    REQUIRE_THAT(ga_wt, Catch::Matchers::WithinRel(1.0197, 1e-3));

    double ga_atoms = 0.0;
    for (std::size_t i = 0; i < mat.fracs.size(); ++i) {
        if (mat.fracs[i].species.rfind("Ga", 0) == 0) {
            ga_atoms += mat.number_density(i) * 1e-24;
        }
    }
    REQUIRE_THAT(ga_atoms, Catch::Matchers::WithinRel(0.001375, 1e-5));

    // All three readings of "4.5% Pu-240" (ADR-017).
    double pu_mass = 0.0, pu240_mass = 0.0, pu_atoms = 0.0, pu240_atoms = 0.0;
    for (const auto& c : mat.fracs) {
        if (c.species.rfind("Pu", 0) == 0) {
            pu_mass += c.atom_fraction * c.molar_mass;
            pu_atoms += c.atom_fraction;
            if (c.species == "Pu240") {
                pu240_mass += c.atom_fraction * c.molar_mass;
                pu240_atoms += c.atom_fraction;
            }
        }
    }
    REQUIRE_THAT(weight_pct(mat, "Pu240"), Catch::Matchers::WithinRel(4.4710, 1e-3));
    REQUIRE_THAT(100.0 * pu240_mass / pu_mass, Catch::Matchers::WithinRel(4.5171, 1e-3));
    REQUIRE_THAT(100.0 * pu240_atoms / pu_atoms, Catch::Matchers::WithinRel(4.4990, 1e-3));
}

TEST_CASE("Jezebel is not the Trinity pit material", "[benchmarks]") {
    // 03 §3's loader prohibition exists because these look alike and are not:
    // a 4.5x difference in Pu-240 moves k far beyond G0b's tolerance while
    // reading as unremarkable in a diff.
    const Fixture fx;
    const auto jezebel = fx.load("pu_ga_jezebel", nullptr);
    REQUIRE(weight_pct(jezebel, "Pu240") > 4.0);

    // The Trinity material is M2-T1's to author; assert the separation holds
    // once it exists rather than silently skipping forever.
    const auto trinity = repo() / "data" / "materials" / "pu_ga_delta.json";
    if (fs::exists(trinity)) {
        const auto pit = fx.load("pu_ga_delta", nullptr);
        REQUIRE(weight_pct(pit, "Pu240") < 1.5);
        REQUIRE(weight_pct(jezebel, "Pu240") > 3.0 * weight_pct(pit, "Pu240"));
    }
}

TEST_CASE("benchmark scenarios parse and name the right material", "[benchmarks]") {
    // Dataset resolution needs the cited fast4 set (M1-T4a-2), so these check
    // the scenario documents themselves rather than a full load_scenario().
    for (const auto& [name, radius, material] :
         std::vector<std::tuple<std::string, double, std::string>>{
             {"godiva", 8.741, "u_godiva"}, {"jezebel", 6.385, "pu_ga_jezebel"}}) {
        INFO("scenario " << name);
        const auto scenario = ns::scenario::Scenario::load(
            repo() / "data" / "scenarios" / (name + ".toml"), repo());

        REQUIRE(scenario.name == name);
        REQUIRE(scenario.mode == ns::scenario::Mode::EigenOnly);
        REQUIRE(scenario.layers.size() == 1);
        REQUIRE(scenario.layers[0].material == material);
        REQUIRE_THAT(scenario.layers[0].r_outer_cm, Catch::Matchers::WithinRel(radius, 1e-12));

        // C-900 is the GATE eigen configuration; C-900b is never valid for a
        // gate claim, so a benchmark scenario must carry the former.
        REQUIRE(scenario.eigen_batch == 1000000);
        REQUIRE(scenario.eigen_inactive == 50);
        REQUIRE(scenario.eigen_active == 200);
    }
}

TEST_CASE("the benchmark data cards carry their source and retrieval date", "[benchmarks]") {
    // 08 §1 requires every number to carry an open citation, a URL and a
    // retrieval date, and the model to be tagged PUBLIC-DERIVED. A card that
    // lost its provenance would leave the values unattributable.
    for (const std::string name : {"godiva", "jezebel"}) {
        INFO("card " << name);
        const auto card = spec_examples::read_file(repo() / "data" / "benchmarks"
                                                   / (name + ".md"));
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("PUBLIC-DERIVED"));
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("oecd-nea.org"));
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("retrieved"));
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("2026-08-02"));
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("BNL 19302"));
        // BLK-14: no autonomous session may seek Handbook access, and the card
        // must say the model was not taken from it.
        REQUIRE_THAT(card, Catch::Matchers::ContainsSubstring("BLK-14"));
    }
}

TEST_CASE("fast4 gives a sane fast-metal k on Godiva and Jezebel", "[benchmarks]") {
    // A regression guard on the cited fast4 set + the ADR-021 transport correction. NOT the
    // precision gate (that is gate_probe / nukebench, M1-T5): the honest 4-group set lands
    // Godiva ~1.026 / Jezebel ~1.016 (ADR-022, both ~1.5-2.5% high, outside +/-500 pcm). This
    // asserts only that a bare fast-metal critical assembly comes out NEAR critical -- which a
    // broken transport correction does not: mu_bar=0 gives k~1.12, and the INCONSISTENT split
    // (pre-ADR-021, collide on sigma_t while flying on sigma_tr) gives k~0.84. Both fail this.
    const auto fast4 = repo() / "data" / "xs" / "fast4.json";
    if (!fs::exists(fast4)) {
        WARN("data/xs/fast4.json absent (authored by M1-T4a-2a) — skipping the eigen guard");
        return;
    }
    const auto xs = ns::xs::FewGroupXS::load(fast4);
    std::vector<ns::LoadWarning> warnings;
    const auto lib = ns::material::MaterialLib::load_dir(repo() / "data" / "materials", xs,
                                                         &warnings);

    for (const auto& [name, radius, material] :
         std::vector<std::tuple<std::string, double, std::string>>{
             {"godiva", 8.741, "u_godiva"}, {"jezebel", 6.385, "pu_ga_jezebel"}}) {
        INFO("benchmark " << name);
        const int mid = lib.index_of(material);
        REQUIRE(mid >= 0);
        const ns::geom::LayerStack stack(
            {ns::geom::Layer{"core", radius, mid, "PUBLIC-DERIVED"}});
        ns::ref::RefTransport transport(stack, lib, xs, 20260802ull);

        ns::physics::EigenSpec spec;
        spec.batch = 30000;
        spec.inactive = 25;
        spec.active = 60;
        spec.seed = 20260802ull;
        const auto res = ns::physics::run_eigen(transport, spec);
        INFO("k_eff = " << res.k);
        REQUIRE(res.k > 0.95);
        REQUIRE(res.k < 1.06);
    }
}

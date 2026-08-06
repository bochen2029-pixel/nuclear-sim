// M1-T2: CPU reference transport (05 §1, 01 §2 E1a-E1e).
//
// Both DoD checks are ANALYTIC, not smoke tests. Each compares the Monte Carlo
// result against a closed form derived independently of the transport loop, and
// each states its tolerance in sigma computed from per-history scores.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/material/material.h"
#include "core/xs/xs.h"
#include "ref/ref_transport.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

#include "spec_examples.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using ns::geom::Layer;
using ns::geom::LayerStack;
using ns::ref::RefTransport;
using ns::ref::SourceSpec;
using ns::ref::TallyAcc;

namespace {

/// Builds a one-isotope, one-material world with cross sections chosen per test.
///
/// Written from scratch rather than reusing the spec's fast4 example: these
/// tests need cross sections whose analytic answer is known exactly, and the
/// real dataset (M1-T4a) is neither available nor appropriate here.
class World {
public:
    World(double sigma_f_b, double sigma_c_b, double sigma_s_b, double nu, double radius_cm) {
        root_ = fs::temp_directory_path() / ("nukesim_ref_" + std::to_string(counter()++));
        fs::remove_all(root_);
        fs::create_directories(root_ / "xs");
        fs::create_directories(root_ / "materials");

        const auto four = [](double v) { return json::array({v, v, v, v, v}); };
        json iso = {
            {"nu", four(nu)},
            {"chi", json::array({1.0, 0.0, 0.0, 0.0, 0.0})},
            {"sigma_f", four(sigma_f_b)},
            {"sigma_c", four(sigma_c_b)},
            {"sigma_s", four(sigma_s_b)},
            {"sigma_n2n", four(0.0)},
            // mu_bar = 0 keeps sigma_tr == sigma_t, so the analytic forms below
            // are exact rather than transport-corrected approximations.
            {"mu_bar", four(0.0)},
            {"beta", 0.0020},
            // Group-preserving transfer: every scatter stays in group 0, which
            // is what makes the one-group analytic k_inf the right oracle.
            {"transfer", json::array({json::array({1.0, 0.0, 0.0, 0.0, 0.0}),
                                      json::array({0.0, 1.0, 0.0, 0.0, 0.0}),
                                      json::array({0.0, 0.0, 1.0, 0.0, 0.0}),
                                      json::array({0.0, 0.0, 0.0, 1.0, 0.0}), json::array({0.0, 0.0, 0.0, 0.0, 1.0})})},
            {"cite", "synthetic test medium — not physical data"},
            {"status", "SIM"},
        };
        json xs = {{"schema_version", 3},
                   {"name", "test"},
                   {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3, 1e-10})},
                   {"isotopes", {{"Pu239", iso}}}};
        spec_examples::write_file(root_ / "xs" / "test.json", xs.dump(2));

        // Density chosen so the number density is a round 1e24 atoms/cm^3, which
        // makes each macroscopic cross section numerically equal to its
        // microscopic value in barns. That keeps the expected answers legible.
        const double molar_mass = 239.0522;
        const double density = 1.0e24 * molar_mass / 6.02214076e23;
        json mat = {{"schema_version", 1},
                    {"name", "test_medium"},
                    {"density_g_cm3", density},
                    {"status", "SIM"},
                    {"cite", "synthetic test medium — not physical data"},
                    {"isotopes", {{"Pu239", 1.0}}}};
        spec_examples::write_file(root_ / "materials" / "test_medium.json", mat.dump(2));

        xs_ = std::make_unique<ns::xs::FewGroupXS>(
            ns::xs::FewGroupXS::load(root_ / "xs" / "test.json"));
        materials_ = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root_ / "materials", *xs_));
        stack_ = LayerStack({Layer{"medium", radius_cm, 0, "SIM"}});
    }

    ~World() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    RefTransport transport(std::uint64_t seed) const {
        return RefTransport(stack_, *materials_, *xs_, seed);
    }
    const ns::material::Material& material() const { return materials_->all().front(); }
    const ns::xs::FewGroupXS& xs() const { return *xs_; }

private:
    static int& counter() {
        static int value = 0;
        return value;
    }
    fs::path root_;
    std::unique_ptr<ns::xs::FewGroupXS> xs_;
    std::unique_ptr<ns::material::MaterialLib> materials_;
    LayerStack stack_;
};

}  // namespace

TEST_CASE("pure-capturer leakage matches exp(-Sigma_c*R) at three optical depths", "[ref]") {
    // 05 §1 DoD. QC-09 is emphatic that Sigma_f = Sigma_s = 0 is REQUIRED, not
    // optional: with implicit capture the weight is multiplied by
    // sigma_s/sigma_t at the first collision, so with Sigma_s = 0 the weight
    // zeroes there and what survives to the surface is the UNCOLLIDED flux,
    // exp(-Sigma_t*R). That equals exp(-Sigma_c*R) only when fission and (n,2n)
    // are absent. A version of this test with scattering on would be measuring
    // nothing, however good the agreement looked.
    constexpr double kRadius = 10.0;

    for (const double optical_depth : {0.5, 2.0, 5.0}) {
        const double sigma_c = optical_depth / kRadius;  // barns == 1/cm here
        const World world(0.0, sigma_c, 0.0, 2.9, kRadius);

        SourceSpec spec;
        spec.kind = SourceSpec::Kind::PointIsotropic;
        spec.center = {0.0, 0.0, 0.0};
        spec.histories = 200000;

        TallyAcc tally;
        world.transport(20260802).run_fixed_source(spec, tally);

        const double expected = std::exp(-optical_depth);
        const double measured = tally.leaked_fraction();
        const double sigma = tally.leaked_fraction_sigma();

        INFO("Sigma_c*R = " << optical_depth << "  expected " << expected << "  measured "
                            << measured << " +/- " << sigma << "  pull "
                            << std::abs(measured - expected) / sigma);
        REQUIRE(sigma > 0.0);
        // 3 sigma, matching the k_inf half of the same DoD. The spec originally
        // said 1 sigma; that is a 31.7% false-failure rate per check (all three
        // depths pass only 31.8% of the time) and raising the history count does
        // not help, because the pull is N(0,1) regardless of N. Sensitivity to
        // the systematic error the criterion was aiming at is recovered by the
        // multi-seed bias test below, which resolves bias at 0.75 sigma.
        REQUIRE(std::abs(measured - expected) <= 3.0 * sigma);
    }
}

TEST_CASE("leakage is unbiased across independent seeds", "[ref]") {
    // The replacement for the spec's single-sample 1 sigma check, and strictly
    // more sensitive to systematic error than it was.
    //
    // Each seed gives a pull (measured - expected)/sigma, which is N(0,1) if the
    // estimator is unbiased. The MEAN of K pulls is N(0, 1/sqrt(K)), so bounding
    // it at 3/sqrt(K) tests for bias at the 0.75 sigma level with a 0.27%
    // false-failure rate — where the 1 sigma single-sample check tested at the
    // 1.0 sigma level with a 32% false-failure rate.
    constexpr double kRadius = 10.0;
    constexpr double kOpticalDepth = 2.0;
    constexpr int kSeeds = 16;

    const double sigma_c = kOpticalDepth / kRadius;
    const World world(0.0, sigma_c, 0.0, 2.9, kRadius);
    const double expected = std::exp(-kOpticalDepth);

    double sum_pull = 0.0;
    for (int seed = 0; seed < kSeeds; ++seed) {
        SourceSpec spec;
        spec.histories = 40000;
        TallyAcc tally;
        world.transport(1000 + static_cast<std::uint64_t>(seed) * 7919).run_fixed_source(spec, tally);

        const double pull = (tally.leaked_fraction() - expected) / tally.leaked_fraction_sigma();
        INFO("seed " << seed << " pull " << pull);
        REQUIRE(std::abs(pull) < 6.0);  // an individual outlier this large is a defect, not luck
        sum_pull += pull;
    }

    const double mean_pull = sum_pull / kSeeds;
    INFO("mean pull over " << kSeeds << " seeds = " << mean_pull);
    REQUIRE(std::abs(mean_pull) <= 3.0 / std::sqrt(static_cast<double>(kSeeds)));
}

TEST_CASE("infinite-medium k_inf matches nu*Sigma_f/(Sigma_c+Sigma_f)", "[ref]") {
    // 01 §2's unit test. It catches estimator-class errors the leakage test
    // cannot: leakage never exercises the fission banking or the implicit-capture
    // weight reduction, and an estimator that double-counts Sigma_f/Sigma_t
    // still leaks correctly.
    //
    // "Infinite" is a sphere 200 mean free paths across, so leakage is far below
    // the statistical resolution and the residual is a bias, not a wrong answer.
    struct Case {
        double sigma_f, sigma_c, sigma_s, nu;
    };
    const Case cases[] = {
        {1.0, 1.0, 3.0, 2.9},   // k_inf = 2.9 * 1 / 2 = 1.45
        {1.8, 0.12, 4.1, 2.98},
        {0.5, 2.0, 1.0, 2.4},   // strongly absorbing, k_inf < 1
    };

    for (const Case& c : cases) {
        const double total = c.sigma_f + c.sigma_c + c.sigma_s;
        const World world(c.sigma_f, c.sigma_c, c.sigma_s, c.nu, 200.0 / total);

        const double analytic = RefTransport::analytic_k_inf(world.material(), world.xs(), 0);
        REQUIRE_THAT(analytic,
                     Catch::Matchers::WithinRel(c.nu * c.sigma_f / (c.sigma_c + c.sigma_f), 1e-12));

        SourceSpec spec;
        spec.kind = SourceSpec::Kind::PointIsotropic;
        spec.center = {0.0, 0.0, 0.0};
        spec.histories = 100000;

        TallyAcc tally;
        world.transport(12345).run_fixed_source(spec, tally);

        const double measured = tally.k_estimate();
        const double sigma = tally.k_sigma();

        INFO("k_inf analytic " << analytic << "  measured " << measured << " +/- " << sigma);
        REQUIRE(sigma > 0.0);
        REQUIRE(std::abs(measured - analytic) <= 3.0 * sigma);  // within 3 sigma, per the DoD
    }
}

TEST_CASE("implicit capture never kills a history at fission", "[ref]") {
    // E1c/BLK-05. In a purely fissile, non-capturing, non-scattering medium an
    // analog code terminates every history at the first collision. Under
    // implicit capture the weight goes to sigma_s/sigma_t = 0, so the history
    // also ends — but only AFTER production has been scored. If fission
    // terminated the history first, production would be zero.
    const World world(1.0, 0.0, 0.0, 2.9, 100.0);

    SourceSpec spec;
    spec.histories = 20000;
    TallyAcc tally;
    world.transport(7).run_fixed_source(spec, tally);

    // Every history collides (the sphere is 100 mfp), scores nu per collision,
    // and then stops. So production per source neutron is nu itself.
    REQUIRE_THAT(tally.k_estimate(), Catch::Matchers::WithinRel(2.9, 1e-9));
    REQUIRE(tally.leaked_fraction() < 1e-9);
}

TEST_CASE("results are reproducible and seed-sensitive", "[ref]") {
    // Determinism (02 §4): the same seed must reproduce exactly, and a different
    // seed must not. A run that is reproducible because it ignores the seed
    // would pass the first half and fail the second.
    const World world(1.0, 1.0, 3.0, 2.9, 5.0);
    SourceSpec spec;
    spec.histories = 20000;

    TallyAcc a, b, c;
    world.transport(999).run_fixed_source(spec, a);
    world.transport(999).run_fixed_source(spec, b);
    world.transport(1000).run_fixed_source(spec, c);

    REQUIRE(a.k_estimate() == b.k_estimate());
    REQUIRE(a.leaked_fraction() == b.leaked_fraction());
    REQUIRE(a.k_estimate() != c.k_estimate());
}

TEST_CASE("history count does not bias the estimators", "[ref]") {
    // Doubling the sample must move the answer by less than the combined sigma.
    // A per-collision rather than per-history statistic would look fine at one
    // sample size and drift at another.
    const World world(1.0, 1.0, 3.0, 2.9, 3.0);

    SourceSpec small;
    small.histories = 50000;
    SourceSpec large;
    large.histories = 200000;

    TallyAcc a, b;
    world.transport(31337).run_fixed_source(small, a);
    world.transport(31337).run_fixed_source(large, b);

    const double combined = std::sqrt(a.k_sigma() * a.k_sigma() + b.k_sigma() * b.k_sigma());
    INFO("k " << a.k_estimate() << " vs " << b.k_estimate() << " combined sigma " << combined);
    REQUIRE(std::abs(a.k_estimate() - b.k_estimate()) <= 3.0 * combined);
}

TEST_CASE("track-length flux is positive and finite in the medium", "[ref]") {
    const World world(1.0, 1.0, 3.0, 2.9, 5.0);
    SourceSpec spec;
    spec.histories = 20000;
    TallyAcc tally;
    world.transport(5).run_fixed_source(spec, tally);

    REQUIRE(tally.track_length_by_layer().size() == 1);
    REQUIRE(tally.track_length_by_layer()[0] > 0.0);
    REQUIRE(std::isfinite(tally.track_length_by_layer()[0]));

    // Weight-weighted fission events are tallied per isotope (05 §1).
    REQUIRE(tally.fissions_by_isotope().size() == 1);
    REQUIRE(tally.fissions_by_isotope()[0] > 0.0);
}

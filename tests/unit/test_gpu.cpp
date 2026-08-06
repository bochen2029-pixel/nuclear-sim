// M4-T1: CUDA backend foundation.
//
// The DoD has two halves and this file is both:
//   (1) device KATs MATCH the CPU / frozen values — the device Philox is the
//       same bijection as ref/, proven against the published Random123 vectors,
//       the frozen uniform_f vector, and fork(42,1000,3);
//   (2) same-backend BIT-IDENTITY across thread counts — the fixed-point
//       reduction and the progeny prefix-sum return identical bits for different
//       launch geometries, which is the whole determinism contract (01 §9).
//
// Compiled by the host compiler (Catch2 stays out of nvcc); it calls the device
// through the plain-typed bridge in gpu/gpu_backend.h. Only built when
// NUKESIM_WITH_CUDA is on, so the CPU-only CI never sees it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/rng/rng.h"
#include "core/xs/xs.h"
#include "gpu/device_data.h"
#include "gpu/eigen.h"
#include "gpu/gpu_backend.h"
#include "gpu/transport.h"
#include "physics/eigen/eigen.h"
#include "ref/ref_transport.h"

#include "app/nukebench/gate_report.h"  // M1-T5-c-2: exercise run_gate's gpu backend
#include "app/nukebench/gates.h"

#include "rng_kat.inl"
#include "spec_examples.h"

namespace {

/// A toy world: a 2-isotope cross-section set and two materials, loaded through
/// the real loaders, plus a 2-layer stack. Mirrors test_ref.cpp's World; used to
/// feed the device material builder the same CPU objects the transport will.
struct ToyWorld {
    std::filesystem::path root;
    std::unique_ptr<ns::xs::FewGroupXS> xs;
    std::unique_ptr<ns::material::MaterialLib> materials;
    ns::geom::LayerStack stack;

    ToyWorld() {
        namespace fs = std::filesystem;
        using nlohmann::json;
        static int counter = 0;
        root = fs::temp_directory_path() / ("nukesim_gpu_mat_" + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root / "xs");
        fs::create_directories(root / "materials");

        const auto four = [](double a, double b, double c, double d) {
            return json::array({a, b, c, d, d});
        };
        const json identity = json::array({json::array({1.0, 0.0, 0.0, 0.0, 0.0}),
                                           json::array({0.0, 1.0, 0.0, 0.0, 0.0}),
                                           json::array({0.0, 0.0, 1.0, 0.0, 0.0}),
                                           json::array({0.0, 0.0, 0.0, 1.0, 0.0}), json::array({0.0, 0.0, 0.0, 0.0, 1.0})});
        const json u235 = {{"nu", four(2.6, 2.5, 2.45, 2.44)},
                           {"chi", four(1.0, 0.0, 0.0, 0.0)},
                           {"sigma_f", four(1.3, 1.2, 1.1, 1.05)},
                           {"sigma_c", four(0.5, 0.4, 0.35, 0.6)},
                           {"sigma_s", four(4.0, 5.0, 6.0, 7.0)},
                           {"sigma_n2n", four(0.0, 0.0, 0.0, 0.0)},
                           {"mu_bar", four(0.1, 0.1, 0.1, 0.1)},
                           {"beta", 0.0065},
                           {"transfer", identity},
                           {"cite", "synthetic test medium — not physical data"},
                           {"status", "SIM"}};
        const json u238 = {{"nu", four(2.5, 2.4, 0.0, 0.0)},
                           {"chi", four(1.0, 0.0, 0.0, 0.0)},
                           {"sigma_f", four(0.55, 0.1, 0.0, 0.0)},
                           {"sigma_c", four(0.3, 0.25, 0.4, 0.8)},
                           {"sigma_s", four(5.0, 6.0, 7.0, 8.0)},
                           {"sigma_n2n", four(0.0, 0.0, 0.0, 0.0)},
                           {"mu_bar", four(0.05, 0.05, 0.05, 0.05)},
                           {"beta", 0.0157},
                           {"transfer", identity},
                           {"cite", "synthetic test medium — not physical data"},
                           {"status", "SIM"}};
        const json xs_doc = {{"schema_version", 3},
                             {"name", "toy"},
                             {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3, 1e-10})},
                             {"isotopes", {{"U235", u235}, {"U238", u238}}}};
        spec_examples::write_file(root / "xs" / "toy.json", xs_doc.dump(2));

        // Two materials: mat_a mixes both isotopes, mat_b is pure U235. Sorted by
        // name, so mat_a is material index 0 and mat_b is 1.
        const json mat_a = {{"schema_version", 1},
                            {"name", "mat_a"},
                            {"density_g_cm3", 18.0},
                            {"status", "SIM"},
                            {"cite", "synthetic"},
                            {"isotopes", {{"U235", 0.6}, {"U238", 0.4}}}};
        const json mat_b = {{"schema_version", 1},
                            {"name", "mat_b"},
                            {"density_g_cm3", 12.0},
                            {"status", "SIM"},
                            {"cite", "synthetic"},
                            {"isotopes", {{"U235", 1.0}}}};
        spec_examples::write_file(root / "materials" / "mat_a.json", mat_a.dump(2));
        spec_examples::write_file(root / "materials" / "mat_b.json", mat_b.dump(2));

        xs = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root / "xs" / "toy.json"));
        materials = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root / "materials", *xs));
        stack = ns::geom::LayerStack({ns::geom::Layer{"inner", 2.0, 0, "SIM"},
                                      ns::geom::Layer{"outer", 4.0, 1, "SIM"}});
    }

    ~ToyWorld() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    ToyWorld(const ToyWorld&) = delete;
    ToyWorld& operator=(const ToyWorld&) = delete;
};

/// A single-sphere PURE ABSORBER: one U238-capture isotope (σ_f = σ_s = 0,
/// μ̄ = 0), so Σ_tr = Σ_c and the leaked fraction is exp(−Σ_c·R) — the same DoD
/// oracle test_ref.cpp uses. Density and σ_c give an optical depth ≈ 2.
struct PureCaptureWorld {
    std::filesystem::path root;
    std::unique_ptr<ns::xs::FewGroupXS> xs;
    std::unique_ptr<ns::material::MaterialLib> materials;
    ns::geom::LayerStack stack;
    double radius_cm = 10.0;

    PureCaptureWorld() {
        namespace fs = std::filesystem;
        using nlohmann::json;
        static int counter = 0;
        root = fs::temp_directory_path() / ("nukesim_gpu_cap_" + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root / "xs");
        fs::create_directories(root / "materials");

        const auto four = [](double v) { return json::array({v, v, v, v, v}); };
        const json iso = {{"nu", four(0.0)},
                          {"chi", json::array({1.0, 0.0, 0.0, 0.0, 0.0})},
                          {"sigma_f", four(0.0)},
                          {"sigma_c", four(4.0)},
                          {"sigma_s", four(0.0)},
                          {"sigma_n2n", four(0.0)},
                          {"mu_bar", four(0.0)},
                          {"beta", 0.0065},
                          {"transfer", json::array({json::array({1.0, 0.0, 0.0, 0.0, 0.0}),
                                                    json::array({0.0, 1.0, 0.0, 0.0, 0.0}),
                                                    json::array({0.0, 0.0, 1.0, 0.0, 0.0}),
                                                    json::array({0.0, 0.0, 0.0, 1.0, 0.0}), json::array({0.0, 0.0, 0.0, 0.0, 1.0})})},
                          {"cite", "synthetic pure absorber — not physical data"},
                          {"status", "SIM"}};
        const json xs_doc = {{"schema_version", 3},
                             {"name", "cap"},
                             {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3, 1e-10})},
                             {"isotopes", {{"U238", iso}}}};
        spec_examples::write_file(root / "xs" / "cap.json", xs_doc.dump(2));

        // density so n·1e-24·σ_c ≈ 0.2 /cm ⇒ Σ_c·R ≈ 2 at R = 10 cm.
        const json mat = {{"schema_version", 1},
                          {"name", "absorber"},
                          {"density_g_cm3", 19.76},
                          {"status", "SIM"},
                          {"cite", "synthetic"},
                          {"isotopes", {{"U238", 1.0}}}};
        spec_examples::write_file(root / "materials" / "absorber.json", mat.dump(2));

        xs = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root / "xs" / "cap.json"));
        materials = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root / "materials", *xs));
        stack = ns::geom::LayerStack(
            {ns::geom::Layer{"medium", radius_cm, 0, "SIM"}});
    }
    ~PureCaptureWorld() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    PureCaptureWorld(const PureCaptureWorld&) = delete;
    PureCaptureWorld& operator=(const PureCaptureWorld&) = delete;
};

/// A single fissioning + scattering isotope (one-group, group-preserving) in a
/// LARGE sphere so leakage is negligible and production per source ≈ k_inf =
/// νΣ_f/(Σ_c+Σ_f). Exercises the multi-superstep event loop (scatter, roulette).
struct FissionWorld {
    std::filesystem::path root;
    std::unique_ptr<ns::xs::FewGroupXS> xs;
    std::unique_ptr<ns::material::MaterialLib> materials;
    ns::geom::LayerStack stack;
    double radius_cm = 200.0;

    FissionWorld() {
        namespace fs = std::filesystem;
        using nlohmann::json;
        static int counter = 0;
        root = fs::temp_directory_path() / ("nukesim_gpu_fis_" + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root / "xs");
        fs::create_directories(root / "materials");

        const auto four = [](double v) { return json::array({v, v, v, v, v}); };
        const json iso = {{"nu", four(2.9)},
                          {"chi", json::array({1.0, 0.0, 0.0, 0.0, 0.0})},
                          {"sigma_f", four(1.0)},
                          {"sigma_c", four(0.5)},
                          {"sigma_s", four(3.0)},
                          {"sigma_n2n", four(0.0)},
                          {"mu_bar", four(0.0)},  // Σ_tr = Σ_t, so k_inf is exact one-group
                          {"beta", 0.0020},
                          // group-preserving: every scatter stays in group 0.
                          {"transfer", json::array({json::array({1.0, 0.0, 0.0, 0.0, 0.0}),
                                                    json::array({0.0, 1.0, 0.0, 0.0, 0.0}),
                                                    json::array({0.0, 0.0, 1.0, 0.0, 0.0}),
                                                    json::array({0.0, 0.0, 0.0, 1.0, 0.0}), json::array({0.0, 0.0, 0.0, 0.0, 1.0})})},
                          {"cite", "synthetic fissioning medium — not physical data"},
                          {"status", "SIM"}};
        const json xs_doc = {{"schema_version", 3},
                             {"name", "fis"},
                             {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3, 1e-10})},
                             {"isotopes", {{"Pu239", iso}}}};
        spec_examples::write_file(root / "xs" / "fis.json", xs_doc.dump(2));

        const json mat = {{"schema_version", 1},
                          {"name", "fuel"},
                          {"density_g_cm3", 15.0},
                          {"status", "SIM"},
                          {"cite", "synthetic"},
                          {"isotopes", {{"Pu239", 1.0}}}};
        spec_examples::write_file(root / "materials" / "fuel.json", mat.dump(2));

        xs = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root / "xs" / "fis.json"));
        materials = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root / "materials", *xs));
        stack = ns::geom::LayerStack({ns::geom::Layer{"medium", radius_cm, 0, "SIM"}});
    }
    ~FissionWorld() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    FissionWorld(const FissionWorld&) = delete;
    FissionWorld& operator=(const FissionWorld&) = delete;
};

}  // namespace

TEST_CASE("a CUDA device is present", "[gpu]") {
    // Every other test in this file needs a device; fail loudly here rather than
    // as a confusing CUDA error deeper down. The file is CUDA-guarded, so this
    // only runs where a GPU is expected (the dev machine, 12 §1).
    REQUIRE(ns::gpu::device_count() >= 1);
}

TEST_CASE("device Philox reproduces the published Random123 vectors", "[gpu]") {
    // Same external ground truth as the CPU test (04 §2a). Agreement here is what
    // makes "device matches CPU" mean "device is correct", not merely "device
    // agrees with our own convention".
    std::array<std::array<std::uint32_t, 4>, 3> got{};
    REQUIRE(ns::gpu::device_philox_published(got));

    REQUIRE(got[0] == std::array<std::uint32_t, 4>{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});
    REQUIRE(got[1] == std::array<std::uint32_t, 4>{0x408F276Du, 0x41C83B0Eu, 0xA20BC7C6u, 0x6D5451FDu});
    REQUIRE(got[2] == std::array<std::uint32_t, 4>{0xD16CFE09u, 0x94FDCCEBu, 0x5001E420u, 0x24126EA1u});
}

TEST_CASE("device uniform_f matches the frozen CPU vector bit-for-bit", "[gpu]") {
    // 04 §2b. The frozen vector was emitted by an independent Python Philox; the
    // device must reproduce it exactly, and it must also equal a CPU Stream run
    // here in the same process (belt and braces on the wrapper layout).
    std::array<float, 16> got{};
    REQUIRE(ns::gpu::device_uniform_f_first16(got));

    ns::rng::Stream cpu(0, 0);
    for (std::size_t i = 0; i < got.size(); ++i) {
        INFO("draw " << i);
        REQUIRE(got[i] == kRngKatUniformF[i]);  // device == frozen
        REQUIRE(got[i] == cpu.uniform_f());      // device == CPU, this run
    }
}

TEST_CASE("device fork matches the frozen value", "[gpu]") {
    // 04 §2c — the one value the spec singles out for M4-T1 to reproduce on the
    // device. Parent-identity forking is what keeps GPU progeny streams
    // independent of buffer position (05 §6 item 3 / BLK-11).
    std::uint64_t got = 0;
    REQUIRE(ns::gpu::device_fork_42_1000_3(got));
    REQUIRE(got == kRngKatFork42_1000_3);
    REQUIRE(got == ns::rng::fork(42, 1000, 3));  // device == CPU
}

TEST_CASE("fixed-point reduction is bit-identical across launch configs", "[gpu]") {
    // The determinism contract (01 §9): same seed ⇒ identical bits regardless of
    // thread count / block size / launch order. The fixed-point int64 accumulator
    // is exactly associative, so every geometry must return the SAME `fixed`.
    constexpr std::int64_t n = 200000;
    constexpr std::uint64_t seed = 20260802;

    const std::array<std::pair<int, int>, 4> configs{{{64, 128}, {256, 256}, {13, 97}, {1024, 64}}};

    ns::gpu::WeightSumResult first{};
    REQUIRE(ns::gpu::deterministic_weight_sum(n, seed, configs[0].first, configs[0].second, first));

    for (std::size_t i = 1; i < configs.size(); ++i) {
        ns::gpu::WeightSumResult r{};
        INFO("config " << configs[i].first << "x" << configs[i].second);
        REQUIRE(ns::gpu::deterministic_weight_sum(n, seed, configs[i].first, configs[i].second, r));
        REQUIRE(r.fixed == first.fixed);  // bit-identical across geometries
    }

    // And it computes the RIGHT sum: replicate the per-particle weights on the CPU
    // (device uniform_f == CPU uniform_f is proven above) and compare the double.
    double cpu_sum = 0.0;
    for (std::int64_t p = 0; p < n; ++p) {
        const std::uint64_t child = ns::rng::fork(seed, 0, static_cast<std::uint32_t>(p));
        ns::rng::Stream s(seed, child);
        cpu_sum += static_cast<double>(s.uniform_f());
    }
    REQUIRE_THAT(first.value, Catch::Matchers::WithinAbs(cpu_sum, 1e-3));
    // Sanity: mean of U(0,1) draws is ~0.5, so the sum sits near n/2.
    REQUIRE(first.value > 0.4 * static_cast<double>(n));
    REQUIRE(first.value < 0.6 * static_cast<double>(n));
}

TEST_CASE("progeny slot prefix-sum is correct and tiling-independent", "[gpu]") {
    // 05 §6 item 3: bank offsets from an exclusive prefix sum, no atomic cursor.
    // The result is defined by index, so any tile size must yield identical
    // offsets — and they must match a plain host scan.
    std::vector<std::int32_t> counts(10000);
    for (std::size_t i = 0; i < counts.size(); ++i) {
        counts[i] = static_cast<std::int32_t>((i * 7 + 3) % 5);  // 0..4, varied
    }

    std::vector<std::int64_t> cpu(counts.size() + 1, 0);
    for (std::size_t i = 0; i < counts.size(); ++i) {
        cpu[i + 1] = cpu[i] + counts[i];
    }

    std::vector<std::int64_t> a, b, c;
    REQUIRE(ns::gpu::progeny_offsets(counts, 128, a));
    REQUIRE(ns::gpu::progeny_offsets(counts, 256, b));
    REQUIRE(ns::gpu::progeny_offsets(counts, 100, c));  // non-power-of-two tile

    REQUIRE(a == cpu);
    REQUIRE(b == cpu);   // tiling-independent
    REQUIRE(c == cpu);
    REQUIRE(a.back() == cpu.back());  // total progeny count

    // Empty input is the boundary case: no slots, offsets == {0}.
    std::vector<std::int64_t> empty_out;
    REQUIRE(ns::gpu::progeny_offsets(std::vector<std::int32_t>{}, 128, empty_out));
    REQUIRE(empty_out == std::vector<std::int64_t>{0});
}

TEST_CASE("device analytic tracker matches the CPU tracker (float vs double parity)", "[gpu]") {
    // M4-T2-a: the device float tracker is a re-implementation of core/geometry's
    // double one (04 §4). They cannot be bit-identical (float vs double) and are
    // not required to be — G0c is statistical/parity across backends (01 §9). On
    // clean rays (intersections well away from any boundary), locate/nudge agree
    // exactly and the distance agrees to float precision.
    const std::vector<float> r_outer{1.0f, 2.0f, 3.0f};
    const ns::geom::LayerStack cpu_stack(
        {{"a", 1.0, 0, ""}, {"b", 2.0, 0, ""}, {"c", 3.0, 0, ""}});
    const ns::geom::AnalyticSphereTracker cpu(cpu_stack);

    struct Ray {
        std::array<float, 3> p;
        std::array<float, 3> dir;
        int layer;
    };
    const std::vector<Ray> rays{
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0},        // axial from the centre, layer 0
        {{0.0f, 0.0f, 1.5f}, {0.0f, 0.0f, 1.0f}, 1},        // outward in layer 1
        {{0.0f, 0.0f, 2.5f}, {0.0f, 0.0f, -1.0f}, 2},       // inward in layer 2 (inner sphere wins)
        {{0.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0},        // tangential-ish, layer 0
        {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, ns::geom::kOutside},  // from outside, inbound
        {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 1.0f}, ns::geom::kOutside},   // from outside, miss
    };

    std::vector<ns::gpu::TrackerQuery> q;
    for (const auto& r : rays) {
        q.push_back({r.p, r.dir, r.layer});
    }
    std::vector<ns::gpu::TrackerResult> got;
    REQUIRE(ns::gpu::device_tracker_batch(r_outer, q, got));
    REQUIRE(got.size() == rays.size());

    for (std::size_t i = 0; i < rays.size(); ++i) {
        INFO("ray " << i);
        const ns::geom::Vec3 p{rays[i].p[0], rays[i].p[1], rays[i].p[2]};
        const ns::geom::Vec3 d{rays[i].dir[0], rays[i].dir[1], rays[i].dir[2]};

        REQUIRE(got[i].located == cpu.locate(p));
        REQUIRE(got[i].nudged == cpu.nudge_and_locate(p, d));

        const double cpu_dist = cpu.distance_to_boundary(p, d, rays[i].layer);
        if (std::isinf(cpu_dist)) {
            REQUIRE(std::isinf(got[i].distance));
        } else {
            REQUIRE(std::isfinite(got[i].distance));
            REQUIRE_THAT(static_cast<double>(got[i].distance),
                         Catch::Matchers::WithinRel(cpu_dist, 1e-4));
        }
    }
}

TEST_CASE("device macro cross sections match the CPU mix() (float vs double parity)", "[gpu]") {
    // M4-T2-a: the device holds the same per-layer macro Σ the CPU mix()/LayerData
    // computes (04 §3, 05 §1). Two checks: the uploaded macro round-trips (storage
    // the flight sampling reads), and a device-side recompute from the per-isotope
    // slots agrees with mix() to float precision (the collision data is sound).
    const ToyWorld w;
    ns::gpu::MaterialParity p;
    REQUIRE(ns::gpu::device_materials_parity(w.stack, *w.materials, p));
    REQUIRE(p.num_layers == w.stack.size());

    for (int layer = 0; layer < w.stack.size(); ++layer) {
        const auto& mat = w.materials->all()[static_cast<std::size_t>(
            w.stack.layer(layer).material_id)];
        for (int g = 0; g < 5; ++g) {
            const auto i = static_cast<std::size_t>(layer * 5 + g);
            const auto gi = static_cast<std::size_t>(g);
            INFO("layer " << layer << " group " << g);

            // Uploaded macro round-trip: within a float cast of the double value.
            REQUIRE_THAT(static_cast<double>(p.stored_sigma_t[i]),
                         Catch::Matchers::WithinRel(mat.macro.sigma_t[gi], 1e-5));
            REQUIRE_THAT(static_cast<double>(p.stored_sigma_tr[i]),
                         Catch::Matchers::WithinRel(mat.macro.sigma_tr[gi], 1e-5));

            // Device-recomputed from the per-isotope slots (float arithmetic).
            REQUIRE_THAT(static_cast<double>(p.recomputed_sigma_t[i]),
                         Catch::Matchers::WithinRel(mat.macro.sigma_t[gi], 1e-4));
            REQUIRE_THAT(static_cast<double>(p.recomputed_sigma_tr[i]),
                         Catch::Matchers::WithinRel(mat.macro.sigma_tr[gi], 1e-4));
            REQUIRE_THAT(static_cast<double>(p.nu_sigma_f[i]),
                         Catch::Matchers::WithinRel(mat.macro.nu_sigma_f[gi], 1e-4));
        }
    }
}

TEST_CASE("gpu fixed-source pure-capturer leakage matches ref (T-diff, G0c)", "[gpu]") {
    // M4-T2-b: the event-based GPU transport vs the CPU oracle. Both estimate the
    // pure-absorber leaked fraction exp(−Σ_c·R); G0c asks they agree within
    // 3·√(σ_ref² + σ_gpu²) — statistical, because the backends draw different
    // (double vs float) uniform sequences. This is the first differential check.
    const PureCaptureWorld w;
    const std::int64_t histories = 500000;
    const std::uint64_t seed = 20260802;

    // ref/ (double).
    ns::ref::RefTransport ref(w.stack, *w.materials, *w.xs, seed);
    ns::ref::TallyAcc tally;
    ns::ref::SourceSpec spec;
    spec.kind = ns::ref::SourceSpec::Kind::PointIsotropic;
    spec.histories = histories;
    ref.run_fixed_source(spec, tally);
    const double leak_ref = tally.leaked_fraction();
    const double sig_ref = tally.leaked_fraction_sigma();

    // gpu (float).
    ns::gpu::FixedSourceResult g;
    REQUIRE(ns::gpu::gpu_fixed_source(w.stack, *w.materials, seed, histories,
                                      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 256, 128, g));

    const double optical_depth = w.materials->all().front().macro.sigma_c[0] * w.radius_cm;
    const double analytic = std::exp(-optical_depth);
    INFO("gpu=" << g.leaked_fraction << " ref=" << leak_ref << " analytic=" << analytic
                << " Σc·R=" << optical_depth << " supersteps=" << g.supersteps);

    // The GPU result sits on the analytic value...
    REQUIRE_THAT(g.leaked_fraction, Catch::Matchers::WithinAbs(analytic, 0.005));
    // ...and agrees with ref within G0c's statistical bound.
    const double bound = 3.0 * std::sqrt(sig_ref * sig_ref + g.leaked_sigma * g.leaked_sigma);
    REQUIRE(std::abs(g.leaked_fraction - leak_ref) <= bound);

    // No fission ⇒ zero production; a pure capturer leaks or dies in ≤ 2 events
    // (a leaker crosses the surface then leaks; an absorber collides and dies).
    REQUIRE(g.k_estimate == 0.0);
    REQUIRE(g.fission_bank_size == 0);  // no fission ⇒ nothing to bank
    REQUIRE(g.supersteps >= 1);
    REQUIRE(g.supersteps <= 2);
}

TEST_CASE("gpu fixed-source with scattering + fission matches ref (T-diff, G0c)", "[gpu]") {
    // M4-T2-b: the full event loop — scatter, roulette, weight-weighted fission
    // production — on a fissioning medium in a large sphere. production/source ≈
    // k_inf = νΣ_f/(Σ_c+Σ_f). The GPU (float) and ref (double) must agree
    // statistically (G0c) on both production/source and leakage.
    const FissionWorld w;
    const std::int64_t histories = 200000;
    const std::uint64_t seed = 20260802;

    ns::ref::RefTransport ref(w.stack, *w.materials, *w.xs, seed);
    ns::ref::TallyAcc tally;
    ns::ref::SourceSpec spec;
    spec.kind = ns::ref::SourceSpec::Kind::PointIsotropic;
    spec.histories = histories;
    ref.run_fixed_source(spec, tally);
    const double k_ref = tally.k_estimate();
    const double k_sig_ref = tally.k_sigma();
    const double leak_ref = tally.leaked_fraction();
    const double leak_sig_ref = tally.leaked_fraction_sigma();

    ns::gpu::FixedSourceResult g;
    REQUIRE(ns::gpu::gpu_fixed_source(w.stack, *w.materials, seed, histories,
                                      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 256, 128, g));

    const auto& macro = w.materials->all().front().macro;
    const double k_inf = macro.nu_sigma_f[0] / (macro.sigma_c[0] + macro.sigma_f[0]);
    INFO("gpu k=" << g.k_estimate << " ref k=" << k_ref << " k_inf=" << k_inf
                  << " gpu leak=" << g.leaked_fraction << " ref leak=" << leak_ref
                  << " supersteps=" << g.supersteps);

    // production/source ≈ k_inf (a few % headroom for the tiny finite-sphere leak).
    REQUIRE_THAT(g.k_estimate, Catch::Matchers::WithinRel(k_inf, 0.03));
    // G0c: GPU vs ref, production and leakage, both statistical.
    const double k_bound = 3.0 * std::sqrt(k_sig_ref * k_sig_ref + g.k_sigma * g.k_sigma);
    REQUIRE(std::abs(g.k_estimate - k_ref) <= k_bound);
    const double leak_bound =
        3.0 * std::sqrt(leak_sig_ref * leak_sig_ref + g.leaked_sigma * g.leaked_sigma);
    REQUIRE(std::abs(g.leaked_fraction - leak_ref) <= leak_bound);
    // Genuinely multi-superstep (many scatters before roulette death).
    REQUIRE(g.supersteps > 5);

    // Deterministic fission bank: E[⌊production + ξ⌋] = production, so the bank
    // size ≈ total production = k·N.
    REQUIRE(g.fission_bank_size > 0);
    const double expected_bank = g.k_estimate * static_cast<double>(histories);
    REQUIRE_THAT(static_cast<double>(g.fission_bank_size),
                 Catch::Matchers::WithinRel(expected_bank, 0.02));
}

TEST_CASE("gpu eigen power iteration is deterministic and gives a sane k", "[gpu]") {
    // M4-T3: the fission-source iteration on the GPU. A finite Pu sphere (leakage
    // matters, so k_eff < k_inf). The result must be bit-identical across launch
    // configs (streams by fork, progeny at prefix-sum slots), and k in a sane
    // range. The differential vs ref/'s eigen is the next check.
    const FissionWorld w;  // reuse the fissioning material (index 0)
    const ns::geom::LayerStack sphere({ns::geom::Layer{"core", 12.0, 0, "SIM"}});
    const std::uint64_t seed = 20260802;

    ns::gpu::EigenResultGpu a;
    ns::gpu::EigenResultGpu b;
    REQUIRE(ns::gpu::gpu_eigen(sphere, *w.materials, seed, 3000, 20, 40, 64, 128, a));
    REQUIRE(ns::gpu::gpu_eigen(sphere, *w.materials, seed, 3000, 20, 40, 256, 256, b));
    INFO("k=" << a.k << " +/- " << a.k_sigma << " H=" << a.entropy_final);

    // Deterministic across launch geometries.
    REQUIRE(a.k == b.k);
    REQUIRE(a.source_checksum == b.source_checksum);
    REQUIRE(a.entropy_final == b.entropy_final);
    // The G0c (b)/(c) outputs are host-side, index-ordered -> also bit-identical (M1-T5-c-5).
    REQUIRE(a.k_history == b.k_history);            // per-active-gen k sequence (c)
    REQUIRE(a.per_shell_source == b.per_shell_source);  // radial fission histogram (b)
    REQUIRE(a.k_history.size() == 40u);            // == active generations
    REQUIRE(a.per_shell_source.size() == 8u);      // kShells radial shells

    // Sane eigenvalue and a converged, non-degenerate source.
    REQUIRE(a.k > 0.5);
    REQUIRE(a.k < 3.0);
    REQUIRE(a.entropy_final > 0.0);
}

TEST_CASE("gpu eigen k matches ref eigen (differential, G0c-style)", "[gpu]") {
    // M4-T3: GPU power iteration k vs ref/'s eigen solver on the same finite Pu
    // sphere. Both estimate k_eff; the GPU (float, reservoir fission sites) and
    // ref (double, full source) agree within a toy-batch tolerance. The FORMAL
    // G0c gate (≤100 pcm at C-900 batch via `nukebench diff`) awaits M1-T5.
    const FissionWorld w;
    const ns::geom::LayerStack sphere({ns::geom::Layer{"core", 12.0, 0, "SIM"}});
    const std::uint64_t seed = 20260802;
    const std::int64_t batch = 4000;
    const int inactive = 30;
    const int active = 60;

    ns::ref::RefTransport reft(sphere, *w.materials, *w.xs, seed);
    ns::physics::EigenSpec spec;
    spec.batch = batch;
    spec.inactive = inactive;
    spec.active = active;
    spec.seed = seed;
    spec.h_tol = 1.0;  // loose: run the full active set, don't gate on entropy here
    const ns::physics::EigenResult er = ns::physics::run_eigen(reft, spec);

    ns::gpu::EigenResultGpu g;
    REQUIRE(ns::gpu::gpu_eigen(sphere, *w.materials, seed, batch, inactive, active, 256, 128, g));

    INFO("gpu k=" << g.k << " +/- " << g.k_sigma << "   ref k=" << er.k << " +/- "
                  << er.sigma_pcm * 1e-5);
    REQUIRE(er.k > 0.5);
    // Statistical equivalence, with a toy-batch tolerance (successive-cycle
    // correlation inflates the true σ beyond the naive estimate).
    REQUIRE_THAT(g.k, Catch::Matchers::WithinRel(er.k, 0.03));
}

TEST_CASE("gpu fixed-source is bit-identical across launch configs", "[gpu]") {
    // Same-backend determinism (01 §9 / BLK-11): tallies AND the fission bank
    // depend only on the per-particle index-keyed streams and the prefix-sum
    // slotting, never the launch geometry. Uses the fissioning medium so the bank
    // is non-trivial and the multi-superstep loop is exercised.
    const FissionWorld w;
    const std::int64_t histories = 50000;
    const std::uint64_t seed = 7;

    ns::gpu::FixedSourceResult a;
    ns::gpu::FixedSourceResult b;
    REQUIRE(ns::gpu::gpu_fixed_source(w.stack, *w.materials, seed, histories,
                                      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 64, 128, a));
    REQUIRE(ns::gpu::gpu_fixed_source(w.stack, *w.materials, seed, histories,
                                      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 512, 256, b));
    REQUIRE(a.leaked_fraction == b.leaked_fraction);  // bit-identical tallies
    REQUIRE(a.leaked_sigma == b.leaked_sigma);
    REQUIRE(a.k_estimate == b.k_estimate);
    REQUIRE(a.fission_bank_size == b.fission_bank_size);          // deterministic slots
    REQUIRE(a.fission_bank_checksum == b.fission_bank_checksum);  // + fork streams
    REQUIRE(a.fission_bank_size > 0);
}

TEST_CASE("GPU eigen matches the CPU oracle on the fast4 Godiva assembly (ADR-021 parity)",
          "[gpu]") {
    // G0c on REAL data: with the ADR-021 transport correction on BOTH backends, the device
    // eigen reproduces the CPU oracle on a mu_bar != 0 set. This is the check the SIM-xs
    // (mu_bar=0) differential tests could never make. Without the device fix the GPU would
    // still fly on sigma_tr but collide on sigma_t -> the mu_bar=0 answer (~1.12) vs the
    // CPU's ~1.026, ~9000 pcm apart.
    const auto repo = spec_examples::repo_root();
    const auto fast4 = repo / "data" / "xs" / "fast4.json";
    if (!std::filesystem::exists(fast4)) {
        WARN("data/xs/fast4.json absent (M1-T4a-2a) — skipping the GPU/CPU fast4 parity check");
        return;
    }
    const auto xs = ns::xs::FewGroupXS::load(fast4);
    const auto lib = ns::material::MaterialLib::load_dir(repo / "data" / "materials", xs);
    const int mid = lib.index_of("u_godiva");
    REQUIRE(mid >= 0);
    const ns::geom::LayerStack stack({ns::geom::Layer{"core", 8.741, mid, "PUBLIC-DERIVED"}});

    ns::ref::RefTransport transport(stack, lib, xs, 20260802ull);
    ns::physics::EigenSpec spec;
    spec.batch = 30000;
    spec.inactive = 25;
    spec.active = 60;
    spec.seed = 20260802ull;
    const auto cpu = ns::physics::run_eigen(transport, spec);

    ns::gpu::EigenResultGpu gpu;
    REQUIRE(ns::gpu::gpu_eigen(stack, lib, 20260802ull, 30000, 25, 60, 64, 128, gpu));

    INFO("CPU k = " << cpu.k << ", GPU k = " << gpu.k);
    REQUIRE(gpu.k > 0.95);  // near critical — a broken device transport correction gives ~1.12
    REQUIRE(gpu.k < 1.06);
    REQUIRE(std::abs(gpu.k - cpu.k) < 0.006);  // < 600 pcm: float-vs-double + independent sampling
}

TEST_CASE("nukebench run_gate gpu backend produces sane measurements matching the ref oracle",
          "[gpu]") {
    // M1-T5-c-2: run_gate --backend gpu dispatches to gpu_eigen and reports k / sigma_pcm
    // (= k_sigma x 1e5). It must agree with the ref oracle (ADR-021 parity) on the same gate.
    // Reduced batch -> the runner MECHANICS (per-seed attempts, k_sigma->pcm, the backend field),
    // not C-900 precision. fast4 must exist.
    const auto repo = spec_examples::repo_root();
    if (!std::filesystem::exists(repo / "data" / "xs" / "fast4.json")) {
        WARN("data/xs/fast4.json absent — skipping the gpu run_gate check");
        return;
    }
    const auto cfg = ns::nukebench::load_gates(repo / "data" / "benchmarks" / "gates.toml",
                                               repo / "spec" / "08-validation.md");
    const auto g0a = ns::nukebench::find_gate(cfg, "G0a");  // copy: dodge gcc -Wdangling-reference

    const auto gpu = ns::nukebench::run_gate(g0a, repo, "gpu", 3000);
    REQUIRE(gpu.backend == "gpu");
    REQUIRE(gpu.attempts.size() == g0a.seeds.size());
    double gpu_k_sum = 0.0;
    for (const auto& a : gpu.attempts) {
        REQUIRE(a.k > 0.95);   // near-critical bare fast metal; a broken device transport gives ~1.12
        REQUIRE(a.k < 1.10);
        REQUIRE(a.sigma_pcm > 0.0);  // k_sigma x 1e5 populated
        REQUIRE(std::abs(a.k_deviation_pcm - (a.k - 1.0) * 1e5) < 1e-6);  // pcm = (k-1)*1e5
        gpu_k_sum += a.k;
    }
    const double gpu_k = gpu_k_sum / static_cast<double>(gpu.attempts.size());

    // Parity: the SAME gate on the ref oracle (same reduced batch) must agree within ~600 pcm.
    const auto ref = ns::nukebench::run_gate(g0a, repo, "ref", 3000);
    double ref_k_sum = 0.0;
    for (const auto& a : ref.attempts) ref_k_sum += a.k;
    const double ref_k = ref_k_sum / static_cast<double>(ref.attempts.size());
    INFO("gpu mean k = " << gpu_k << ", ref mean k = " << ref_k);
    REQUIRE(std::abs(gpu_k - ref_k) < 0.006);  // < 600 pcm (float-vs-double + independent sampling)
}

TEST_CASE("nukebench run_diff computes the G0c ref-vs-gpu k-equivalence differential", "[gpu]") {
    // M1-T5-c-3: run_diff runs G0c on ref + gpu and reports the per-seed |k_ref - k_gpu| differential
    // vs C-932 (100 pcm) AND <= 3 sigma (08 sec 2 G0c criterion a). Reduced batch -> the diff
    // MECHANICS (both backends, k_b populated, delta = |k - k_b|, backend "ref|gpu"), not C-900
    // precision. fast4 must exist.
    const auto repo = spec_examples::repo_root();
    if (!std::filesystem::exists(repo / "data" / "xs" / "fast4.json")) {
        WARN("data/xs/fast4.json absent — skipping run_diff");
        return;
    }
    const auto cfg = ns::nukebench::load_gates(repo / "data" / "benchmarks" / "gates.toml",
                                               repo / "spec" / "08-validation.md");
    const auto g0c = ns::nukebench::find_gate(cfg, "G0c");  // copy: dodge gcc -Wdangling-reference
    REQUIRE(g0c.seeds.size() == 3);  // the fixed 3-seed differential set (08 sec 2)

    const auto rep = ns::nukebench::run_diff(g0c, repo, "ref", "gpu", 3000);
    REQUIRE(rep.gate == "G0c");
    REQUIRE(rep.backend == "ref|gpu");
    REQUIRE(rep.attempts.size() == 3);
    for (const auto& a : rep.attempts) {
        REQUIRE(a.k > 0.95);    // k_ref, near-critical bare fast metal
        REQUIRE(a.k_b > 0.95);  // k_gpu populated
        REQUIRE(a.k < 1.10);
        REQUIRE(a.k_b < 1.10);
        // the reported deviation IS the |k_ref - k_gpu| differential (pcm)
        REQUIRE(std::abs(a.k_deviation_pcm - std::abs(a.k - a.k_b) * 1e5) < 1e-6);
        // M1-T5-c-5: 4 criteria now — (a) <=C-932, (a) <=3sigma, (b) per-shell, (c) population.
        REQUIRE(a.criteria.size() == 4);
        bool has_shell = false, has_pop = false;
        for (const auto& c : a.criteria) {
            if (c.name == "per_shell_equivalence_ratio") {
                has_shell = true;
                REQUIRE(c.threshold == 1.0);       // ratio form: pass iff worst |delta|/bound <= 1
            }
            if (c.name == "population_series_equivalence_ratio") {
                has_pop = true;
                REQUIRE(c.threshold == 1.0);
            }
        }
        REQUIRE(has_shell);
        REQUIRE(has_pop);
        REQUIRE((a.verdict == "pass" || a.verdict == "fail"));
    }
    REQUIRE((rep.verdict == "pass" || rep.verdict == "fail"));
    // JSON round-trip preserves k_b (the diff-only measurement)
    const auto rt = ns::nukebench::parse_report_json(ns::nukebench::to_json(rep));
    REQUIRE(rt.attempts.size() == 3);
    REQUIRE(std::abs(rt.attempts[0].k_b - rep.attempts[0].k_b) < 1e-9);
}

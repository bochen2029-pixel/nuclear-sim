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
#include <cstdint>
#include <vector>

#include "core/rng/rng.h"
#include "gpu/gpu_backend.h"

#include "rng_kat.inl"

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

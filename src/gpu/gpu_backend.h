// Host-callable entry points for the CUDA backend foundation (M4-T1).
//
// Deliberately free of CUDA types so the Catch2 test translation unit (compiled
// by the host compiler, with Catch2 headers) can call straight in — the same
// bridge pattern M0-T2 used for the toolchain smoke test. The kernels live in
// gpu_backend.cu; everything here returns plain data.
//
// Scope is exactly M4-T1: the device Philox (which MUST match ref/ and the
// frozen KATs), SoA buffers, and the three deterministic primitives of 05 §6 —
// slots (prefix-sum), streams (fork), reductions (fixed-point int64). The
// event-transport kernels and the RefTransport-shaped GpuTransport interface are
// M4-T2 / M4-T3 and are deliberately absent here (SYNC-M1: do not declare an
// unimplemented method).

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ns::gpu {

/// Visible CUDA device count, or -1 if the runtime could not be queried.
int device_count();

// --- Device Philox known-answer tests (DoD: device matches CPU) -------------

/// The three published Random123 Philox4x32-10 single-block vectors (04 §2a),
/// computed on the device from the all-zero, all-ones and pi-pattern inputs.
/// Order matches tests/unit/test_rng.cpp. False on any CUDA error.
bool device_philox_published(std::array<std::array<std::uint32_t, 4>, 3>& out);

/// First 16 uniform_f() of Stream(seed=0, stream=0) computed on the device.
/// Must equal the frozen rng_kat.inl vector, i.e. the CPU, bit-for-bit (04 §2b).
bool device_uniform_f_first16(std::array<float, 16>& out);

/// fork(42, 1000, 3) computed on the device (04 §2c) — must equal the frozen
/// kRngKatFork42_1000_3.
bool device_fork_42_1000_3(std::uint64_t& out);

// --- Deterministic reduction (05 §6 item 4) ---------------------------------

struct WeightSumResult {
    /// Raw fixed-point int64 accumulator. Identical across launch configs by
    /// construction (integer addition is exactly associative and commutative),
    /// which is the whole point of the fixed-point domain.
    std::int64_t fixed = 0;
    /// fixed / fixed_point_scale() — the physical sum, promoted to double once.
    double value = 0.0;
};

/// The documented fixed-point scale (a power of two). Exposed so a host
/// cross-check can convert. A numerical detail, not a physical constant.
double fixed_point_scale();

/// Fill an `n`-particle SoA batch (05 §6 item 1), giving particle p the stream
/// id fork(seed, 0, p) — parent identity, never buffer position (05 §6 item 3) —
/// and one uniform_f() draw as its weight, then sum the weights with the
/// deterministic fixed-point int64 reduction on a `blocks` x `threads` launch.
/// The DoD's bit-identity is asserted by calling this at two launch configs and
/// comparing `fixed`. False on any CUDA error.
bool deterministic_weight_sum(std::int64_t n, std::uint64_t seed, int blocks,
                              int threads, WeightSumResult& out);

// --- Deterministic progeny slots (05 §6 item 3) -----------------------------

/// Exclusive prefix sum of per-particle progeny `counts` → `offsets`, the
/// bank_offset[i] reservation with NO atomic cursor. offsets.size() ==
/// counts.size() + 1 and offsets.back() == total. The result is index-defined,
/// hence identical for any `threads` tiling; the test asserts it and checks it
/// against a host scan. False on any CUDA error.
bool progeny_offsets(const std::vector<std::int32_t>& counts, int threads,
                     std::vector<std::int64_t>& offsets);

}  // namespace ns::gpu

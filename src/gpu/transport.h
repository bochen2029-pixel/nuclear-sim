// Event-based fixed-source GPU transport (M4-T2-b), host-callable.
//
// CUDA-free signature so the Catch2 T-diff test calls straight in. Runs E1a–E1e
// (implicit capture) on the GPU with float per-event arithmetic (01 §9) and
// per-particle streams derived by rng::fork from the source index — deterministic
// within the backend (bit-identical across thread counts), compared to ref/
// STATISTICALLY (G0c, cross-backend).

#pragma once

#include <array>
#include <cstdint>

#include "core/geometry/geometry.h"
#include "core/material/material.h"

namespace ns::gpu {

struct FixedSourceResult {
    /// Leaked weight per source neutron, and the standard error of that mean
    /// (per-history estimator, identical to ref/'s TallyAcc).
    double leaked_fraction = 0.0;
    double leaked_sigma = 0.0;
    /// Fission neutrons produced per source neutron (= k_inf with no leakage),
    /// and its standard error.
    double k_estimate = 0.0;
    double k_sigma = 0.0;
    std::int64_t histories = 0;
    /// Supersteps the event loop ran (diagnostic).
    int supersteps = 0;
};

/// Point-isotropic source at the origin with birth spectrum `group_weights`.
/// Runs the event loop on a `blocks` x `threads` launch (the result is identical
/// for any geometry — the DoD's same-backend bit-identity). False on any CUDA
/// error or an out-of-range material_id.
bool gpu_fixed_source(const ns::geom::LayerStack& stack,
                      const ns::material::MaterialLib& materials, std::uint64_t seed,
                      std::int64_t histories, const std::array<float, 4>& group_weights,
                      int blocks, int threads, FixedSourceResult& out);

}  // namespace ns::gpu

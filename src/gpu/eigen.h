// GPU k-eigenvalue power iteration (M4-T3), host-callable.
//
// The fission-source iteration on the GPU: transport a generation from the
// current fission bank, bank ⌊production/k + ξ⌋ progeny at a reservoir-sampled
// fission site (position + χ birth group), the bank becomes the next generation's
// source; k iterates (E2a) and the fixed-8³ source entropy is reported (E2b).
// Float per-event arithmetic (01 §9); per-particle streams from rng::fork, so the
// result is bit-identical across launch geometries and compared to ref/'s eigen
// solver STATISTICALLY (G0c). A history-per-thread transport keeps it compact —
// the event-based superstep transport (M4-T2-b) is the M4-T4 perf substitution.

#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry/geometry.h"
#include "core/material/material.h"

namespace ns::gpu {

struct EigenResultGpu {
    double k = 0.0;            // mean k over active generations
    double k_sigma = 0.0;      // standard error of that mean
    double entropy_final = 0.0;  // Shannon entropy of the last fission source (E2b)
    int generations = 0;
    /// Position-weighted checksum of the final fission bank — bit-identical across
    /// launch configs (determinism).
    unsigned long long source_checksum = 0;
    /// Per-active-generation k, in generation order (G0c criterion c — the
    /// population series log10 N(n) = cumsum log10 k). Bit-identical across launch
    /// configs (collected host-side in gen order). Length == active generations.
    std::vector<double> k_history;
    /// Final fission source binned into equal-width radial shells over [0, radius]
    /// (G0c criterion b — per-shell fission-source equivalence vs the CPU oracle).
    /// Counts; bit-identical across launch configs (host-side integer binning).
    std::vector<double> per_shell_source;
    /// Peak device memory attributed to this run (M4-T4), via cudaMemGetInfo.
    /// Observational only — launches nothing, cannot affect k or determinism.
    std::int64_t peak_vram_bytes = 0;
};

/// Bare/layered-sphere k-eigenvalue by GPU power iteration. `batch` source
/// neutrons per generation, `inactive` settling generations then `active` tallied
/// ones. Launched blocks x threads (result identical for any geometry). False on
/// any CUDA error, out-of-range material_id, or bank overflow.
bool gpu_eigen(const ns::geom::LayerStack& stack, const ns::material::MaterialLib& materials,
               std::uint64_t seed, std::int64_t batch, int inactive, int active, int blocks,
               int threads, EigenResultGpu& out);

}  // namespace ns::gpu

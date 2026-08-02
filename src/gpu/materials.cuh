// Device-resident material / cross-section data (M4-T2-a), the float mirror of
// what ref/'s RefTransport::LayerData precomputes (05 §1) and mix() builds
// (04 §3). This is the static data the GPU transport (M4-T2-b) reads: per-layer
// macro Σ_t/Σ_tr for flight sampling, and per-isotope slots for collision
// sampling (pick isotope ∝ nᵢ·Σ_t,ᵢ, then use its nu/chi/transfer).
//
// Flattened arrays, no per-layer std::vector — this is what a kernel indexes.
// float per 01 §9; the CPU keeps double and the parity test bounds the gap.

#pragma once

#include "core/hd.h"

namespace ns::gpu {

/// One isotope's per-group cross sections, float (mirrors xs::GroupData).
struct DGroup {
    float nu, chi, sigma_f, sigma_c, sigma_s, sigma_n2n, mu_bar;
};

/// Total macroscopic-per-atom cross section = f + c + s + n2n (xs.h sigma_t()).
NUKESIM_HD inline float d_group_sigma_t(const DGroup& g) noexcept {
    return g.sigma_f + g.sigma_c + g.sigma_s + g.sigma_n2n;
}

/// Transport-corrected: sigma_t − mu_bar·sigma_s (xs.h sigma_tr()).
NUKESIM_HD inline float d_group_sigma_tr(const DGroup& g) noexcept {
    return d_group_sigma_t(g) - g.mu_bar * g.sigma_s;
}

/// Device material/xs, flattened. Non-owning pointers into device memory; passed
/// to kernels by value. Group index is 0..3 (kGroups), group-major throughout.
struct DeviceMaterials {
    int num_layers;
    const float* sigma_t;     // [num_layers*4] macro, 1/cm — precomputed, for flight
    const float* sigma_tr;    // [num_layers*4]
    const int* slot_begin;    // [num_layers] index of a layer's first isotope slot
    const int* slot_count;    // [num_layers] isotope slots in the layer
    const float* nd;          // [total_slots] number density, atoms/barn-cm (nᵢ)
    const int* global_index;  // [total_slots] stable isotope id across layers
    const DGroup* g;          // [total_slots*4] per slot, per group
    const float* transfer;    // [total_slots*16] [from*4 + to], rows sum to 1
};

}  // namespace ns::gpu

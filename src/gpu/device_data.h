// Host-callable bridge for the M4-T2-a device-data parity checks: run the device
// analytic tracker (and, below, the device macro cross sections) on host-provided
// inputs and hand the results back for comparison against the CPU. CUDA-free
// signatures so the Catch2 test TU calls straight in (same pattern as
// gpu_backend.h). Kernels live in device_data.cu.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/geometry/geometry.h"
#include "core/material/material.h"

namespace ns::gpu {

// --- Device analytic tracker (parity vs core/geometry) ----------------------

struct TrackerQuery {
    std::array<float, 3> p;
    std::array<float, 3> dir;  // unit
    int layer;                 // the layer the particle is in, or kOutside (-1)
};

struct TrackerResult {
    float distance;  // distance_to_boundary(p, dir, layer); +inf on a miss
    int located;     // locate(p)
    int nudged;      // nudge_and_locate(p, dir)
};

/// Uploads a float layer stack (r_outer, innermost first) and evaluates the
/// DEVICE tracker for every query, one thread each. False on any CUDA error.
bool device_tracker_batch(const std::vector<float>& r_outer,
                          const std::vector<TrackerQuery>& queries,
                          std::vector<TrackerResult>& results);

// --- Device material / cross sections (parity vs mix() / LayerData) ----------

/// What the device holds and computes for the per-layer macro cross sections,
/// all group-major [layer*4 + group]. `stored_*` is the uploaded macro (round-
/// trip check); `recomputed_*` and `nu_sigma_f` are summed on the DEVICE from the
/// per-isotope slots (the float-arithmetic parity check).
struct MaterialParity {
    int num_layers = 0;
    std::vector<float> stored_sigma_t;
    std::vector<float> stored_sigma_tr;
    std::vector<float> recomputed_sigma_t;
    std::vector<float> recomputed_sigma_tr;
    std::vector<float> nu_sigma_f;
};

/// Builds the device material/xs representation from the CPU objects (mirroring
/// mix() / RefTransport::LayerData), then reads back both the uploaded macro Σ
/// and a device-recomputed Σ (and νΣ_f) from the per-isotope slots, for the
/// parity test to compare against `Material::macro`. False on a CUDA error or an
/// out-of-range material_id.
bool device_materials_parity(const ns::geom::LayerStack& stack,
                             const ns::material::MaterialLib& materials, MaterialParity& out);

}  // namespace ns::gpu

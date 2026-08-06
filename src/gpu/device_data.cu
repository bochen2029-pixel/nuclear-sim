// Device-data parity kernels + host wrappers (M4-T2-a): run the device analytic
// tracker (and the device macro cross sections) so the tests can compare them
// against the CPU. This is the static data the GPU transport (M4-T2-b) reads.

#include "gpu/device_data.h"

#include <map>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "gpu/geometry.cuh"
#include "gpu/materials.cuh"

namespace ns::gpu {
namespace {

// Kernel-side packed query (POD, one per particle).
struct DQuery {
    float px, py, pz;
    float dx, dy, dz;
    int layer;
};

__global__ void k_tracker(DeviceLayerStack stack, const DQuery* q, int n, float* dist, int* loc,
                          int* nud) {
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= n) {
        return;
    }
    const DFloat3 p{q[i].px, q[i].py, q[i].pz};
    const DFloat3 d{q[i].dx, q[i].dy, q[i].dz};
    dist[i] = d_distance_to_boundary(stack, p, d, q[i].layer);
    loc[i] = d_locate(stack, p);
    nud[i] = d_nudge_and_locate(stack, p, d);
}

bool fail() {
    cudaGetLastError();
    return false;
}

}  // namespace

bool device_tracker_batch(const std::vector<float>& r_outer,
                          const std::vector<TrackerQuery>& queries,
                          std::vector<TrackerResult>& results) {
    const int num = static_cast<int>(r_outer.size());
    const int n = static_cast<int>(queries.size());
    results.assign(static_cast<std::size_t>(n), TrackerResult{});
    if (n == 0) {
        return true;
    }

    std::vector<DQuery> packed(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& q = queries[static_cast<std::size_t>(i)];
        packed[static_cast<std::size_t>(i)] =
            DQuery{q.p[0], q.p[1], q.p[2], q.dir[0], q.dir[1], q.dir[2], q.layer};
    }

    float* d_r = nullptr;
    DQuery* d_q = nullptr;
    float* d_dist = nullptr;
    int* d_loc = nullptr;
    int* d_nud = nullptr;
    bool ok = cudaMalloc(&d_r, static_cast<std::size_t>(num) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_q, static_cast<std::size_t>(n) * sizeof(DQuery)) == cudaSuccess
           && cudaMalloc(&d_dist, static_cast<std::size_t>(n) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_loc, static_cast<std::size_t>(n) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_nud, static_cast<std::size_t>(n) * sizeof(int)) == cudaSuccess;

    ok = ok && cudaMemcpy(d_r, r_outer.data(), static_cast<std::size_t>(num) * sizeof(float),
                          cudaMemcpyHostToDevice) == cudaSuccess;
    ok = ok && cudaMemcpy(d_q, packed.data(), static_cast<std::size_t>(n) * sizeof(DQuery),
                          cudaMemcpyHostToDevice) == cudaSuccess;

    if (ok) {
        const DeviceLayerStack stack{d_r, num};
        const int threads = 128;
        const int blocks = (n + threads - 1) / threads;
        k_tracker<<<blocks, threads>>>(stack, d_q, n, d_dist, d_loc, d_nud);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    std::vector<float> h_dist(static_cast<std::size_t>(n));
    std::vector<int> h_loc(static_cast<std::size_t>(n));
    std::vector<int> h_nud(static_cast<std::size_t>(n));
    ok = ok && cudaMemcpy(h_dist.data(), d_dist, static_cast<std::size_t>(n) * sizeof(float),
                          cudaMemcpyDeviceToHost) == cudaSuccess;
    ok = ok && cudaMemcpy(h_loc.data(), d_loc, static_cast<std::size_t>(n) * sizeof(int),
                          cudaMemcpyDeviceToHost) == cudaSuccess;
    ok = ok && cudaMemcpy(h_nud.data(), d_nud, static_cast<std::size_t>(n) * sizeof(int),
                          cudaMemcpyDeviceToHost) == cudaSuccess;

    cudaFree(d_r);
    cudaFree(d_q);
    cudaFree(d_dist);
    cudaFree(d_loc);
    cudaFree(d_nud);
    if (!ok) {
        return fail();
    }

    for (int i = 0; i < n; ++i) {
        results[static_cast<std::size_t>(i)] =
            TrackerResult{h_dist[static_cast<std::size_t>(i)], h_loc[static_cast<std::size_t>(i)],
                          h_nud[static_cast<std::size_t>(i)]};
    }
    return true;
}

// --- Device materials -------------------------------------------------------

namespace {

/// One thread per (layer, group). Recomputes the macro Σ_t/Σ_tr and νΣ_f from the
/// per-isotope slots in float — the device-arithmetic side of the parity check.
__global__ void k_recompute_macro(DeviceMaterials m, float* out_st, float* out_str,
                                  float* out_nsf) {
    const int lg = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (lg >= m.num_layers * 5) {
        return;
    }
    const int layer = lg / 5;
    const int group = lg % 5;
    const int begin = m.slot_begin[layer];
    const int count = m.slot_count[layer];

    float st = 0.0f;
    float str = 0.0f;
    float nsf = 0.0f;
    for (int s = 0; s < count; ++s) {
        const int slot = begin + s;
        const float n = m.nd[slot];
        const DGroup& gd = m.g[slot * 5 + group];
        st += n * d_group_sigma_t(gd);
        str += n * d_group_sigma_tr(gd);
        nsf += n * gd.nu * gd.sigma_f;
    }
    out_st[lg] = st;
    out_str[lg] = str;
    out_nsf[lg] = nsf;
}

}  // namespace

bool device_materials_parity(const ns::geom::LayerStack& stack,
                             const ns::material::MaterialLib& materials, MaterialParity& out) {
    const int num_layers = stack.size();
    const auto& mats = materials.all();

    // Flatten the CPU objects the way mix()/RefTransport::LayerData does: number
    // density nᵢ = atom_fraction·ρ/M̄·N_A · 1e-24 (atoms/barn-cm), per-layer macro
    // Σ from the material's already-computed macro. global_index and transfer are
    // collision-only and are built with the collision kernel in M4-T2-b.
    std::vector<float> h_st(static_cast<std::size_t>(num_layers) * 5, 0.0f);
    std::vector<float> h_str(static_cast<std::size_t>(num_layers) * 5, 0.0f);
    std::vector<int> h_begin(static_cast<std::size_t>(num_layers), 0);
    std::vector<int> h_count(static_cast<std::size_t>(num_layers), 0);
    std::vector<float> h_nd;
    std::vector<DGroup> h_g;

    for (int L = 0; L < num_layers; ++L) {
        const auto& layer = stack.layer(L);
        if (layer.material_id < 0
            || static_cast<std::size_t>(layer.material_id) >= mats.size()) {
            return false;
        }
        const auto& mat = mats[static_cast<std::size_t>(layer.material_id)];
        h_begin[static_cast<std::size_t>(L)] = static_cast<int>(h_nd.size());
        int cnt = 0;
        for (std::size_t c = 0; c < mat.fracs.size(); ++c) {
            const auto& con = mat.fracs[c];
            if (con.iso == nullptr) {
                continue;  // mass only, no cross sections in this set
            }
            h_nd.push_back(static_cast<float>(mat.number_density(c) * 1e-24));
            for (int group = 0; group < 5; ++group) {
                const auto& gd = con.iso->g[static_cast<std::size_t>(group)];
                h_g.push_back(DGroup{
                    static_cast<float>(gd.nu), static_cast<float>(gd.chi),
                    static_cast<float>(gd.sigma_f), static_cast<float>(gd.sigma_c),
                    static_cast<float>(gd.sigma_s), static_cast<float>(gd.sigma_n2n),
                    static_cast<float>(gd.mu_bar)});
            }
            ++cnt;
        }
        h_count[static_cast<std::size_t>(L)] = cnt;
        for (int group = 0; group < 5; ++group) {
            const auto g = static_cast<std::size_t>(group);
            h_st[static_cast<std::size_t>(L * 5 + group)] = static_cast<float>(mat.macro.sigma_t[g]);
            h_str[static_cast<std::size_t>(L * 5 + group)] =
                static_cast<float>(mat.macro.sigma_tr[g]);
        }
    }

    const int total_slots = static_cast<int>(h_nd.size());
    const auto nlg = static_cast<std::size_t>(num_layers) * 5;

    float* d_st = nullptr;
    float* d_str = nullptr;
    int* d_begin = nullptr;
    int* d_count = nullptr;
    float* d_nd = nullptr;
    DGroup* d_g = nullptr;
    float* d_out_st = nullptr;
    float* d_out_str = nullptr;
    float* d_out_nsf = nullptr;

    const std::size_t slots = static_cast<std::size_t>(total_slots);
    bool ok = cudaMalloc(&d_st, nlg * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_str, nlg * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_begin, static_cast<std::size_t>(num_layers) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_count, static_cast<std::size_t>(num_layers) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_nd, slots * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_g, slots * 5 * sizeof(DGroup)) == cudaSuccess
           && cudaMalloc(&d_out_st, nlg * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_out_str, nlg * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_out_nsf, nlg * sizeof(float)) == cudaSuccess;

    ok = ok
      && cudaMemcpy(d_st, h_st.data(), nlg * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess
      && cudaMemcpy(d_str, h_str.data(), nlg * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess
      && cudaMemcpy(d_begin, h_begin.data(), static_cast<std::size_t>(num_layers) * sizeof(int),
                    cudaMemcpyHostToDevice) == cudaSuccess
      && cudaMemcpy(d_count, h_count.data(), static_cast<std::size_t>(num_layers) * sizeof(int),
                    cudaMemcpyHostToDevice) == cudaSuccess;
    if (ok && total_slots > 0) {
        ok = cudaMemcpy(d_nd, h_nd.data(), slots * sizeof(float), cudaMemcpyHostToDevice)
                 == cudaSuccess
          && cudaMemcpy(d_g, h_g.data(), slots * 5 * sizeof(DGroup), cudaMemcpyHostToDevice)
                 == cudaSuccess;
    }

    if (ok) {
        const DeviceMaterials m{num_layers, d_st,  d_str, d_begin, d_count,
                                d_nd,       nullptr, d_g,   nullptr};
        const int threads = 128;
        const int blocks = (static_cast<int>(nlg) + threads - 1) / threads;
        k_recompute_macro<<<blocks, threads>>>(m, d_out_st, d_out_str, d_out_nsf);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    out.num_layers = num_layers;
    out.stored_sigma_t.assign(nlg, 0.0f);
    out.stored_sigma_tr.assign(nlg, 0.0f);
    out.recomputed_sigma_t.assign(nlg, 0.0f);
    out.recomputed_sigma_tr.assign(nlg, 0.0f);
    out.nu_sigma_f.assign(nlg, 0.0f);
    ok = ok
      && cudaMemcpy(out.stored_sigma_t.data(), d_st, nlg * sizeof(float), cudaMemcpyDeviceToHost)
             == cudaSuccess
      && cudaMemcpy(out.stored_sigma_tr.data(), d_str, nlg * sizeof(float), cudaMemcpyDeviceToHost)
             == cudaSuccess
      && cudaMemcpy(out.recomputed_sigma_t.data(), d_out_st, nlg * sizeof(float),
                    cudaMemcpyDeviceToHost) == cudaSuccess
      && cudaMemcpy(out.recomputed_sigma_tr.data(), d_out_str, nlg * sizeof(float),
                    cudaMemcpyDeviceToHost) == cudaSuccess
      && cudaMemcpy(out.nu_sigma_f.data(), d_out_nsf, nlg * sizeof(float), cudaMemcpyDeviceToHost)
             == cudaSuccess;

    cudaFree(d_st);
    cudaFree(d_str);
    cudaFree(d_begin);
    cudaFree(d_count);
    cudaFree(d_nd);
    cudaFree(d_g);
    cudaFree(d_out_st);
    cudaFree(d_out_str);
    cudaFree(d_out_nsf);
    return ok ? true : fail();
}

}  // namespace ns::gpu

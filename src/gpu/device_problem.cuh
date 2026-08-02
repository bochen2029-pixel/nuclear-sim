// Device-resident problem data owner (M4-T2-b): builds the DeviceLayerStack
// (float radii) and the full DeviceMaterials (per-layer macro Σ + per-isotope
// slots WITH the transfer matrix this time) from the CPU objects, mirroring
// mix()/RefTransport::LayerData in float, and frees on destruction.
//
// M4-T2-a's device_data.cu built a parity-only subset (no transfer); the event
// transport needs transfer for scatter, so this is the complete build. Included
// only by .cu translation units.

#pragma once

#include <cstddef>
#include <vector>

#include <cuda_runtime.h>

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "gpu/geometry.cuh"
#include "gpu/materials.cuh"

namespace ns::gpu {

/// RAII owner of the device problem data. `ok()` reports whether the build (host
/// flatten + upload) succeeded; the views are valid only then.
class DeviceProblem {
public:
    DeviceProblem(const ns::geom::LayerStack& stack,
                  const ns::material::MaterialLib& materials) {
        ok_ = build(stack, materials);
    }
    ~DeviceProblem() { free_all(); }
    DeviceProblem(const DeviceProblem&) = delete;
    DeviceProblem& operator=(const DeviceProblem&) = delete;

    bool ok() const noexcept { return ok_; }
    DeviceLayerStack geometry() const noexcept { return {d_r_outer_, num_layers_}; }
    DeviceMaterials materials() const noexcept {
        return {num_layers_, d_sigma_t_, d_sigma_tr_, d_slot_begin_, d_slot_count_,
                d_nd_,       nullptr,    d_group_,    d_transfer_};
    }

private:
    template <typename T>
    static bool up(T** dst, const std::vector<T>& src) {
        if (src.empty()) {
            return true;  // nothing to allocate/copy (e.g. a layer with no isotopes)
        }
        return cudaMalloc(dst, src.size() * sizeof(T)) == cudaSuccess
            && cudaMemcpy(*dst, src.data(), src.size() * sizeof(T), cudaMemcpyHostToDevice)
                   == cudaSuccess;
    }

    bool build(const ns::geom::LayerStack& stack, const ns::material::MaterialLib& materials) {
        num_layers_ = stack.size();
        const auto& mats = materials.all();

        std::vector<float> r_outer(static_cast<std::size_t>(num_layers_));
        std::vector<float> sigma_t(static_cast<std::size_t>(num_layers_) * 4, 0.0f);
        std::vector<float> sigma_tr(static_cast<std::size_t>(num_layers_) * 4, 0.0f);
        std::vector<int> slot_begin(static_cast<std::size_t>(num_layers_), 0);
        std::vector<int> slot_count(static_cast<std::size_t>(num_layers_), 0);
        std::vector<float> nd;
        std::vector<DGroup> g;
        std::vector<float> transfer;

        for (int L = 0; L < num_layers_; ++L) {
            const auto& layer = stack.layer(L);
            if (layer.material_id < 0
                || static_cast<std::size_t>(layer.material_id) >= mats.size()) {
                return false;
            }
            const auto& mat = mats[static_cast<std::size_t>(layer.material_id)];
            r_outer[static_cast<std::size_t>(L)] = static_cast<float>(layer.r_outer);
            slot_begin[static_cast<std::size_t>(L)] = static_cast<int>(nd.size());
            int cnt = 0;
            for (std::size_t c = 0; c < mat.fracs.size(); ++c) {
                const auto& con = mat.fracs[c];
                if (con.iso == nullptr) {
                    continue;
                }
                nd.push_back(static_cast<float>(mat.number_density(c) * 1e-24));
                for (int group = 0; group < 4; ++group) {
                    const auto& gd = con.iso->g[static_cast<std::size_t>(group)];
                    g.push_back(DGroup{
                        static_cast<float>(gd.nu), static_cast<float>(gd.chi),
                        static_cast<float>(gd.sigma_f), static_cast<float>(gd.sigma_c),
                        static_cast<float>(gd.sigma_s), static_cast<float>(gd.sigma_n2n),
                        static_cast<float>(gd.mu_bar)});
                }
                for (int from = 0; from < 4; ++from) {
                    for (int to = 0; to < 4; ++to) {
                        // A null transfer (SIM sets only) is group-preserving:
                        // identity, so a scatter keeps the group.
                        const double t = con.iso->transfer_is_null
                                             ? (from == to ? 1.0 : 0.0)
                                             : con.iso->transfer[static_cast<std::size_t>(from)]
                                                                [static_cast<std::size_t>(to)];
                        transfer.push_back(static_cast<float>(t));
                    }
                }
                ++cnt;
            }
            slot_count[static_cast<std::size_t>(L)] = cnt;
            for (int group = 0; group < 4; ++group) {
                const auto gg = static_cast<std::size_t>(group);
                sigma_t[static_cast<std::size_t>(L * 4 + group)] =
                    static_cast<float>(mat.macro.sigma_t[gg]);
                sigma_tr[static_cast<std::size_t>(L * 4 + group)] =
                    static_cast<float>(mat.macro.sigma_tr[gg]);
            }
        }

        return up(&d_r_outer_, r_outer) && up(&d_sigma_t_, sigma_t) && up(&d_sigma_tr_, sigma_tr)
            && up(&d_slot_begin_, slot_begin) && up(&d_slot_count_, slot_count) && up(&d_nd_, nd)
            && up(&d_group_, g) && up(&d_transfer_, transfer);
    }

    void free_all() noexcept {
        cudaFree(d_r_outer_);
        cudaFree(d_sigma_t_);
        cudaFree(d_sigma_tr_);
        cudaFree(d_slot_begin_);
        cudaFree(d_slot_count_);
        cudaFree(d_nd_);
        cudaFree(d_group_);
        cudaFree(d_transfer_);
        cudaGetLastError();
    }

    bool ok_ = false;
    int num_layers_ = 0;
    float* d_r_outer_ = nullptr;
    float* d_sigma_t_ = nullptr;
    float* d_sigma_tr_ = nullptr;
    int* d_slot_begin_ = nullptr;
    int* d_slot_count_ = nullptr;
    float* d_nd_ = nullptr;
    DGroup* d_group_ = nullptr;
    float* d_transfer_ = nullptr;
};

}  // namespace ns::gpu

// Event-based fixed-source GPU transport (M4-T2-b): E1a–E1e with implicit
// capture (01 §2), one flight resolved per particle per superstep, looping until
// every particle has terminated. Float per-event arithmetic (01 §9).
//
// Determinism (01 §9 / BLK-11): each particle's RNG stream is rng::fork of its
// SOURCE INDEX — never its buffer position — and its (ctr, sub) cursor rides in
// the SoA and is resumed each superstep, so the sequence a particle sees depends
// only on its identity. Per-history scores land in score_* indexed by that same
// index. The result is therefore identical for any launch geometry; the ref
// comparison is statistical only (G0c), because the GPU draws float uniforms and
// ref draws double ones (different sequences, same physics).
//
// This step kernel resolves each event with in-thread branches; splitting into
// per-event branchless kernels behind a prefix-sum event partition is a
// warp-divergence optimisation for the M4-T4 perf pass (02 §4: profile first).
// The prefix-sum COMPACTION of live particles is added next on this same loop.

#include "gpu/transport.h"

#include <cmath>
#include <vector>

#include <cuda_runtime.h>

#include "core/rng/rng.h"
#include "gpu/device_problem.cuh"
#include "gpu/geometry.cuh"
#include "gpu/materials.cuh"

namespace ns::gpu {
namespace {

// E1e weight cutoff / survival (C-902), and 2π for isotropic sampling.
inline constexpr float kWeightMin = 1e-4f;
inline constexpr float kWeightSurv = 1e-2f;
inline constexpr float kTwoPi = 6.28318530717958648f;

// Base for source-particle stream ids: particle p uses fork(kSourceBase, 0, p).
// Cross-backend is statistical (G0c), so this need not match ref's C-907 source
// id — it is an RNG namespace choice, like the Philox multipliers, not a
// physical constant.
inline constexpr unsigned long long kSourceBase = 1ull;

/// Device SoA view: raw pointers, passed to kernels by value.
struct SoA {
    float* px;
    float* py;
    float* pz;
    float* dx;
    float* dy;
    float* dz;
    int* group;
    float* weight;
    int* layer;
    int* alive;
    unsigned long long* ctr;
    unsigned char* sub;
    float* score_leak;
    float* score_prod;
};

__device__ inline DFloat3 d_sample_isotropic(ns::rng::Stream& s) {
    const float mu = 2.0f * s.uniform_f() - 1.0f;
    const float phi = kTwoPi * s.uniform_f();
    const float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
    return {sin_theta * cosf(phi), sin_theta * sinf(phi), mu};
}

__global__ void k_init(SoA soa, DeviceLayerStack geom, unsigned long long seed, int n, float w0,
                       float w1, float w2, float w3) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        ns::rng::Stream s(seed, ns::rng::fork(kSourceBase, 0, static_cast<unsigned int>(p)));

        soa.px[p] = 0.0f;  // point-isotropic source at the origin
        soa.py[p] = 0.0f;
        soa.pz[p] = 0.0f;
        const DFloat3 d = d_sample_isotropic(s);
        soa.dx[p] = d.x;
        soa.dy[p] = d.y;
        soa.dz[p] = d.z;

        const float total = w0 + w1 + w2 + w3;
        const float xi = s.uniform_f() * total;
        const float cum[4] = {w0, w0 + w1, w0 + w1 + w2, w0 + w1 + w2 + w3};
        int grp = 3;
        for (int gi = 0; gi < 4; ++gi) {
            if (xi <= cum[gi]) {
                grp = gi;
                break;
            }
        }
        soa.group[p] = grp;
        soa.weight[p] = 1.0f;
        soa.layer[p] = d_locate(geom, {0.0f, 0.0f, 0.0f});
        soa.alive[p] = 1;
        soa.score_leak[p] = 0.0f;
        soa.score_prod[p] = 0.0f;

        const auto st = s.state();
        soa.ctr[p] = st.first;
        soa.sub[p] = st.second;
    }
}

__global__ void k_step(SoA soa, DeviceLayerStack geom, DeviceMaterials mat, unsigned long long seed,
                       int n, int* alive_next) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        if (soa.alive[p] == 0) {
            continue;
        }
        ns::rng::Stream s(seed, ns::rng::fork(kSourceBase, 0, static_cast<unsigned int>(p)),
                          soa.ctr[p], soa.sub[p]);
        DFloat3 pos{soa.px[p], soa.py[p], soa.pz[p]};
        DFloat3 dir{soa.dx[p], soa.dy[p], soa.dz[p]};
        int L = soa.layer[p];
        int grp = soa.group[p];
        float w = soa.weight[p];

        bool terminal = false;
        bool leaked = false;

        if (L == kOutsideDev) {
            leaked = true;
            terminal = true;
        } else {
            const float sigma_tr = mat.sigma_tr[L * 4 + grp];
            const float sigma_t = mat.sigma_t[L * 4 + grp];
            const float to_boundary = d_distance_to_boundary(geom, pos, dir, L);

            if (sigma_tr <= 0.0f) {
                // Void: stream straight to the boundary (no infinite flight).
                if (to_boundary >= kInfF) {
                    leaked = true;
                    terminal = true;
                } else {
                    pos = d_advance(pos, dir, to_boundary);
                    L = d_nudge_and_locate(geom, pos, dir);
                }
            } else {
                const float flight = -logf(s.uniform_f()) / sigma_tr;  // E1a, on Σ_tr
                if (flight > to_boundary) {
                    // E1b — advance to the surface and re-locate.
                    if (to_boundary >= kInfF) {
                        leaked = true;
                        terminal = true;
                    } else {
                        pos = d_advance(pos, dir, to_boundary);
                        L = d_nudge_and_locate(geom, pos, dir);
                    }
                } else {
                    pos = d_advance(pos, dir, flight);
                    // E1c — collision. Sample the isotope ∝ nᵢ·Σ_t,ᵢ.
                    const int begin = mat.slot_begin[L];
                    const int count = mat.slot_count[L];
                    float pick = s.uniform_f() * sigma_t;
                    int chosen = begin;
                    for (int si = 0; si < count; ++si) {
                        const int slot = begin + si;
                        pick -= mat.nd[slot] * d_group_sigma_t(mat.g[slot * 4 + grp]);
                        if (pick <= 0.0f) {
                            chosen = slot;
                            break;
                        }
                    }
                    const DGroup gd = mat.g[chosen * 4 + grp];
                    const float sigma_t_i = d_group_sigma_t(gd);
                    if (sigma_t_i <= 0.0f) {
                        terminal = true;  // transparent isotope
                    } else {
                        const float fission_share = gd.sigma_f / sigma_t_i;
                        soa.score_prod[p] += w * gd.nu * fission_share;  // weight-weighted, expected

                        w *= gd.sigma_s / sigma_t_i;  // implicit capture (never kill at fission)
                        if (w <= 0.0f) {
                            terminal = true;
                        } else if (w < kWeightMin
                                   && s.uniform_f() > w / kWeightSurv) {
                            terminal = true;  // E1e — rouletted out
                        } else {
                            if (w < kWeightMin) {
                                w = kWeightSurv;  // E1e — rouletted in
                            }
                            dir = d_sample_isotropic(s);  // isotropic-in-lab scatter
                            float xi = s.uniform_f();
                            int to = grp;
                            for (int t = 0; t < 4; ++t) {
                                xi -= mat.transfer[chosen * 16 + grp * 4 + t];
                                if (xi <= 0.0f) {
                                    to = t;
                                    break;
                                }
                            }
                            grp = to;
                        }
                    }
                }
            }
        }

        if (leaked) {
            soa.score_leak[p] += w;  // E1d — the leaked weight (unchanged on this path)
        }

        soa.px[p] = pos.x;
        soa.py[p] = pos.y;
        soa.pz[p] = pos.z;
        soa.dx[p] = dir.x;
        soa.dy[p] = dir.y;
        soa.dz[p] = dir.z;
        soa.group[p] = grp;
        soa.weight[p] = w;
        soa.layer[p] = L;
        const auto st = s.state();
        soa.ctr[p] = st.first;
        soa.sub[p] = st.second;

        if (terminal) {
            soa.alive[p] = 0;
        } else {
            atomicAdd(alive_next, 1);  // integer count — order-independent
        }
    }
}

bool fail() {
    cudaGetLastError();
    return false;
}

// Per-history mean and its standard error, exactly ref/'s TallyAcc estimator.
double mean_of(const std::vector<float>& v) {
    double sum = 0.0;
    for (const float x : v) {
        sum += static_cast<double>(x);
    }
    return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
}

double stderr_of(const std::vector<float>& v) {
    const auto n = static_cast<double>(v.size());
    if (v.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    double sum_sq = 0.0;
    for (const float x : v) {
        const double d = static_cast<double>(x);
        sum += d;
        sum_sq += d * d;
    }
    const double mean = sum / n;
    const double variance = fmax(0.0, (sum_sq / n) - mean * mean) * n / (n - 1.0);
    return std::sqrt(variance / n);
}

}  // namespace

bool gpu_fixed_source(const ns::geom::LayerStack& stack,
                      const ns::material::MaterialLib& materials, std::uint64_t seed,
                      std::int64_t histories, const std::array<float, 4>& group_weights,
                      int blocks, int threads, FixedSourceResult& out) {
    if (histories <= 0 || blocks <= 0 || threads <= 0) {
        return false;
    }

    DeviceProblem problem(stack, materials);
    if (!problem.ok()) {
        return fail();
    }

    const int n = static_cast<int>(histories);
    const std::size_t nf = static_cast<std::size_t>(n) * sizeof(float);
    const std::size_t ni = static_cast<std::size_t>(n) * sizeof(int);

    SoA soa{};
    int* d_alive_next = nullptr;
    bool ok = cudaMalloc(&soa.px, nf) == cudaSuccess && cudaMalloc(&soa.py, nf) == cudaSuccess
           && cudaMalloc(&soa.pz, nf) == cudaSuccess && cudaMalloc(&soa.dx, nf) == cudaSuccess
           && cudaMalloc(&soa.dy, nf) == cudaSuccess && cudaMalloc(&soa.dz, nf) == cudaSuccess
           && cudaMalloc(&soa.group, ni) == cudaSuccess && cudaMalloc(&soa.weight, nf) == cudaSuccess
           && cudaMalloc(&soa.layer, ni) == cudaSuccess && cudaMalloc(&soa.alive, ni) == cudaSuccess
           && cudaMalloc(&soa.ctr, static_cast<std::size_t>(n) * sizeof(unsigned long long))
                  == cudaSuccess
           && cudaMalloc(&soa.sub, static_cast<std::size_t>(n) * sizeof(unsigned char)) == cudaSuccess
           && cudaMalloc(&soa.score_leak, nf) == cudaSuccess
           && cudaMalloc(&soa.score_prod, nf) == cudaSuccess
           && cudaMalloc(&d_alive_next, sizeof(int)) == cudaSuccess;

    if (ok) {
        k_init<<<blocks, threads>>>(soa, problem.geometry(), seed, n, group_weights[0],
                                    group_weights[1], group_weights[2], group_weights[3]);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    int supersteps = 0;
    constexpr int kMaxSupersteps = 100000;  // safety net against a non-terminating history
    while (ok && supersteps < kMaxSupersteps) {
        ok = cudaMemset(d_alive_next, 0, sizeof(int)) == cudaSuccess;
        if (!ok) {
            break;
        }
        k_step<<<blocks, threads>>>(soa, problem.geometry(), problem.materials(), seed, n,
                                    d_alive_next);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        int alive_host = 0;
        ok = ok
          && cudaMemcpy(&alive_host, d_alive_next, sizeof(int), cudaMemcpyDeviceToHost)
                 == cudaSuccess;
        ++supersteps;
        if (!ok || alive_host == 0) {
            break;
        }
    }

    std::vector<float> leak(static_cast<std::size_t>(n));
    std::vector<float> prod(static_cast<std::size_t>(n));
    ok = ok && cudaMemcpy(leak.data(), soa.score_leak, nf, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(prod.data(), soa.score_prod, nf, cudaMemcpyDeviceToHost) == cudaSuccess;

    cudaFree(soa.px);
    cudaFree(soa.py);
    cudaFree(soa.pz);
    cudaFree(soa.dx);
    cudaFree(soa.dy);
    cudaFree(soa.dz);
    cudaFree(soa.group);
    cudaFree(soa.weight);
    cudaFree(soa.layer);
    cudaFree(soa.alive);
    cudaFree(soa.ctr);
    cudaFree(soa.sub);
    cudaFree(soa.score_leak);
    cudaFree(soa.score_prod);
    cudaFree(d_alive_next);
    if (!ok) {
        return fail();
    }

    out.leaked_fraction = mean_of(leak);
    out.leaked_sigma = stderr_of(leak);
    out.k_estimate = mean_of(prod);
    out.k_sigma = stderr_of(prod);
    out.histories = histories;
    out.supersteps = supersteps;
    return true;
}

}  // namespace ns::gpu

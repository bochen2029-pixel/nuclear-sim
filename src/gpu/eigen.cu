// GPU k-eigenvalue power iteration (M4-T3 physics; M4-T4-b transport). See eigen.h.
//
// The fission-source iteration on the GPU. M4-T4-a's Nsight profile showed the
// original history-per-thread transport ran at 29% thread efficiency (deep
// neutrons stall a warp), while the event-based superstep transport (transport.cu
// k_step) runs at ~100% BECAUSE it COMPACTS live particles between supersteps —
// so a warp always processes live work. M4-T4-b therefore gives the eigen the
// same event model: one flight per particle per superstep, live particles packed
// to the front by an exclusive prefix sum (no atomic cursor), looping until all
// terminate.
//
// This is a re-scheduling, NOT a physics change: each source neutron draws the
// SAME transport-stream (s) and fission-stream (fs) sequence, in the same order,
// as the old k_generation — the streams' (ctr,sub) cursors ride in the SoA and
// resume each superstep, keyed by the source's identity, never its buffer
// position. So k / source-checksum / entropy are bit-identical to the old
// history-per-thread eigen (asserted by test) AND bit-identical across launch
// configs (determinism, 01 §9). The reservoir fission-site sampling (one site per
// source neutron, ∝ per-collision production) is carried per particle in the SoA
// (rx,ry,rz,rg + the fs cursor) and keeps k unbiased (M4-T3-a rationale).
//
// The compaction machinery (k_scan_tiles / k_scan_tilesums / k_scatter /
// k_add_base) mirrors transport.cu's; a future refactor may share one copy
// behind a backend-agnostic Transport interface. Float per-event arithmetic
// (01 §9); compared to ref/'s eigen STATISTICALLY (G0c).

#include "gpu/eigen.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <cuda_runtime.h>

#include "core/rng/rng.h"
#include "gpu/device_problem.cuh"
#include "gpu/geometry.cuh"
#include "gpu/gpu_backend.h"
#include "gpu/materials.cuh"

namespace ns::gpu {
namespace {

inline constexpr float kWeightMin = 1e-4f;
inline constexpr float kWeightSurv = 1e-2f;
inline constexpr float kTwoPi = 6.28318530717958648f;
inline constexpr unsigned long long kSourceBase = 1ull;
inline constexpr unsigned long long kFissionBase = 4ull;

// Scan tile width; a two-level scan (per-tile then a single-block scan of the
// tile sums) handles up to kTile*kTile ≈ 1.05e6 live particles per superstep,
// which bounds the source count nsrc (≤ cap = batch*8).
inline constexpr int kTile = 1024;

__device__ inline DFloat3 d_iso(ns::rng::Stream& s) {
    const float mu = 2.0f * s.uniform_f() - 1.0f;
    const float phi = kTwoPi * s.uniform_f();
    const float st = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
    return {st * cosf(phi), st * sinf(phi), mu};
}

/// Fission bank = next generation's source: positions, birth groups, stream ids.
struct Bank {
    float* x;
    float* y;
    float* z;
    int* group;
    unsigned long long* stream;
};

/// Device SoA for the in-flight generation, indexed by SOURCE id (never
/// compacted; the active-index list is compacted). Carries the transport state,
/// the transport/fission RNG cursors, the accumulated production, and the
/// reservoir fission site.
struct EigenSoA {
    float* px;
    float* py;
    float* pz;
    float* dx;
    float* dy;
    float* dz;
    int* group;
    float* weight;
    int* layer;
    unsigned long long* ctr;   // transport stream (s) cursor
    unsigned char* sub;
    unsigned long long* fctr;  // fission stream (fs) cursor
    unsigned char* fsub;
    float* prod;               // accumulated fission production (per source)
    float* rx;                 // reservoir fission site
    float* ry;
    float* rz;
    int* rg;                   // reservoir birth group
    unsigned long long* sid;   // source stream id (== bank.stream); parent for progeny
};

__global__ void k_init_source(Bank b, int n) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        b.x[p] = 0.0f;  // gen-0 source: point at the origin, group 0
        b.y[p] = 0.0f;
        b.z[p] = 0.0f;
        b.group[p] = 0;
        b.stream[p] = ns::rng::fork(kSourceBase, 0, static_cast<unsigned int>(p));
    }
}

/// Initialise the SoA from the current source bank: initial isotropic direction
/// (2 s-draws, exactly as the old k_generation drew before its loop), located
/// layer, unit weight, reservoir seeded at the source site, fs cursor at 0.
__global__ void k_init_eigen(Bank in, int nsrc, DeviceLayerStack geom, unsigned long long seed,
                             EigenSoA soa) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < nsrc; p += stride) {
        const unsigned long long sid = in.stream[p];
        ns::rng::Stream s(seed, sid);
        const DFloat3 pos{in.x[p], in.y[p], in.z[p]};
        const int grp = in.group[p];
        const DFloat3 dir = d_iso(s);  // initial direction — same 2 draws as before

        soa.px[p] = pos.x;
        soa.py[p] = pos.y;
        soa.pz[p] = pos.z;
        soa.dx[p] = dir.x;
        soa.dy[p] = dir.y;
        soa.dz[p] = dir.z;
        soa.group[p] = grp;
        soa.weight[p] = 1.0f;
        soa.layer[p] = d_locate(geom, pos);
        soa.prod[p] = 0.0f;
        soa.rx[p] = pos.x;  // reservoir seeded at the source site (matches k_generation)
        soa.ry[p] = pos.y;
        soa.rz[p] = pos.z;
        soa.rg[p] = grp;
        soa.sid[p] = sid;
        const auto st = s.state();
        soa.ctr[p] = st.first;
        soa.sub[p] = st.second;
        soa.fctr[p] = 0;  // fs = Stream(seed, fork(kFissionBase, sid, 0)) starts at (0,0)
        soa.fsub[p] = 0;
    }
}

__global__ void k_iota(int* active, int n) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < n; i += stride) {
        active[i] = i;
    }
}

/// Resolve one flight for each live particle (active[0..num_active)); keep[i]=1 if
/// it survives to the next superstep. The s-draw sequence is identical to
/// transport.cu's k_step and to the old k_generation; the reservoir fs-draws are
/// inserted at the collision exactly where k_generation had them, so the whole
/// iteration is bit-identical to the old history-per-thread transport.
__global__ void k_step_eigen(EigenSoA soa, DeviceLayerStack geom, DeviceMaterials mat,
                             unsigned long long seed, const int* active, int num_active,
                             int* keep) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < num_active;
         i += stride) {
        const int p = active[i];
        const unsigned long long sid = soa.sid[p];
        ns::rng::Stream s(seed, sid, soa.ctr[p], soa.sub[p]);
        ns::rng::Stream fs(seed, ns::rng::fork(kFissionBase, sid, 0), soa.fctr[p], soa.fsub[p]);
        DFloat3 pos{soa.px[p], soa.py[p], soa.pz[p]};
        DFloat3 dir{soa.dx[p], soa.dy[p], soa.dz[p]};
        int L = soa.layer[p];
        int grp = soa.group[p];
        float w = soa.weight[p];

        bool terminal = false;

        if (L == kOutsideDev) {
            terminal = true;  // leaked
        } else {
            const float sigma_tr = mat.sigma_tr[L * 4 + grp];
            const float sigma_t = mat.sigma_t[L * 4 + grp];
            const float to_boundary = d_distance_to_boundary(geom, pos, dir, L);

            if (sigma_tr <= 0.0f) {
                // Void: stream to the boundary (no draw), or leak.
                if (to_boundary >= kInfF) {
                    terminal = true;
                } else {
                    pos = d_advance(pos, dir, to_boundary);
                    L = d_nudge_and_locate(geom, pos, dir);
                }
            } else {
                const float flight = -logf(s.uniform_f()) / sigma_tr;  // E1a
                if (flight > to_boundary) {
                    if (to_boundary >= kInfF) {
                        terminal = true;
                    } else {
                        pos = d_advance(pos, dir, to_boundary);
                        L = d_nudge_and_locate(geom, pos, dir);
                    }
                } else {
                    pos = d_advance(pos, dir, flight);
                    // E1c — collision. Isotope ∝ nᵢ·Σ_t,ᵢ.
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
                        terminal = true;
                    } else {
                        const float pcoll = w * gd.nu * (gd.sigma_f / sigma_t_i);
                        const float prod = soa.prod[p] + pcoll;  // cumulative
                        soa.prod[p] = prod;
                        // Weighted reservoir (1 sample): keep this site with
                        // probability pcoll/prod. fs drawn ONLY when pcoll>0
                        // (short-circuit &&), exactly as k_generation did.
                        if (pcoll > 0.0f && fs.uniform_f() * prod <= pcoll) {
                            soa.rx[p] = pos.x;
                            soa.ry[p] = pos.y;
                            soa.rz[p] = pos.z;
                            const float xic = fs.uniform_f();  // birth group from χ
                            float cum = 0.0f;
                            int bg = 3;
                            for (int gg = 0; gg < 4; ++gg) {
                                cum += mat.g[chosen * 4 + gg].chi;
                                if (xic <= cum) {
                                    bg = gg;
                                    break;
                                }
                            }
                            soa.rg[p] = bg;
                        }
                        w *= gd.sigma_s / sigma_t_i;  // implicit capture
                        if (w <= 0.0f) {
                            terminal = true;
                        } else if (w < kWeightMin && s.uniform_f() > w / kWeightSurv) {
                            terminal = true;  // E1e — rouletted out
                        } else {
                            if (w < kWeightMin) {
                                w = kWeightSurv;  // E1e — rouletted in
                            }
                            dir = d_iso(s);  // isotropic-in-lab scatter
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

        soa.px[p] = pos.x;
        soa.py[p] = pos.y;
        soa.pz[p] = pos.z;
        soa.dx[p] = dir.x;
        soa.dy[p] = dir.y;
        soa.dz[p] = dir.z;
        soa.group[p] = grp;
        soa.weight[p] = w;
        soa.layer[p] = L;
        {
            const auto st = s.state();
            soa.ctr[p] = st.first;
            soa.sub[p] = st.second;
        }
        {
            const auto fst = fs.state();
            soa.fctr[p] = fst.first;
            soa.fsub[p] = fst.second;
        }
        keep[i] = terminal ? 0 : 1;
    }
}

// --- Compaction scan (mirrors transport.cu; kept eigen-local for now) --------

__global__ void k_scan_tiles(const int* keep, int n, int* local, int* tilesums) {
    __shared__ int sdata[kTile];
    const int tid = static_cast<int>(threadIdx.x);
    const int gid = static_cast<int>(blockIdx.x * blockDim.x) + tid;
    const int v = (gid < n) ? keep[gid] : 0;
    sdata[tid] = v;
    __syncthreads();
    for (int off = 1; off < static_cast<int>(blockDim.x); off <<= 1) {
        const int add = (tid >= off) ? sdata[tid - off] : 0;
        __syncthreads();
        sdata[tid] += add;
        __syncthreads();
    }
    if (gid < n) {
        local[gid] = sdata[tid] - v;  // inclusive − own = exclusive
    }
    if (tid == static_cast<int>(blockDim.x) - 1) {
        tilesums[blockIdx.x] = sdata[tid];
    }
}

__global__ void k_scan_tilesums(const int* tilesums, int nblocks, int* tilebase) {
    __shared__ int sdata[kTile];
    const int tid = static_cast<int>(threadIdx.x);
    const int v = (tid < nblocks) ? tilesums[tid] : 0;
    sdata[tid] = v;
    __syncthreads();
    for (int off = 1; off < static_cast<int>(blockDim.x); off <<= 1) {
        const int add = (tid >= off) ? sdata[tid - off] : 0;
        __syncthreads();
        sdata[tid] += add;
        __syncthreads();
    }
    if (tid < nblocks) {
        tilebase[tid] = sdata[tid] - v;
    }
}

__global__ void k_scatter(const int* active, const int* keep, const int* local, const int* tilebase,
                          int n, int* next_active) {
    const int gid = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (gid < n && keep[gid] != 0) {
        next_active[tilebase[blockIdx.x] + local[gid]] = active[gid];
    }
}

/// Progeny count per source: m[p] = ⌊prod/k + ξ⌋, ξ the FINAL fs draw (resumed
/// from the SoA cursor left by transport) — the same last draw k_generation made.
__global__ void k_progeny_eigen(EigenSoA soa, unsigned long long seed, float k_est, int nsrc,
                                int* m) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < nsrc; p += stride) {
        ns::rng::Stream fs(seed, ns::rng::fork(kFissionBase, soa.sid[p], 0), soa.fctr[p],
                           soa.fsub[p]);
        m[p] = static_cast<int>(floorf(soa.prod[p] / k_est + fs.uniform_f()));
    }
}

/// Write each source's progeny into the next bank at its reserved slots: the
/// reservoir fission site + birth group, stream = fork(parent sid, 0, ordinal).
__global__ void k_bank_eigen(EigenSoA soa, const int* m, const int* offset, int nsrc, int cap,
                             Bank out, int* overflow) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < nsrc; p += stride) {
        const unsigned long long parent = soa.sid[p];
        const int base = offset[p];
        const int cnt = m[p];
        for (int o = 0; o < cnt; ++o) {
            const int slot = base + o;
            if (slot >= cap) {
                atomicExch(overflow, 1);
                break;
            }
            out.x[slot] = soa.rx[p];
            out.y[slot] = soa.ry[p];
            out.z[slot] = soa.rz[p];
            out.group[slot] = soa.rg[p];
            out.stream[slot] = ns::rng::fork(parent, 0, static_cast<unsigned int>(o));
        }
    }
}

bool fail() {
    cudaGetLastError();
    return false;
}

bool alloc_bank(Bank& b, int cap) {
    return cudaMalloc(&b.x, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
        && cudaMalloc(&b.y, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
        && cudaMalloc(&b.z, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
        && cudaMalloc(&b.group, static_cast<std::size_t>(cap) * sizeof(int)) == cudaSuccess
        && cudaMalloc(&b.stream, static_cast<std::size_t>(cap) * sizeof(unsigned long long))
               == cudaSuccess;
}

void free_bank(Bank& b) {
    cudaFree(b.x);
    cudaFree(b.y);
    cudaFree(b.z);
    cudaFree(b.group);
    cudaFree(b.stream);
}

bool alloc_soa(EigenSoA& s, int cap) {
    const std::size_t f = static_cast<std::size_t>(cap) * sizeof(float);
    const std::size_t i = static_cast<std::size_t>(cap) * sizeof(int);
    const std::size_t u = static_cast<std::size_t>(cap) * sizeof(unsigned long long);
    const std::size_t b = static_cast<std::size_t>(cap) * sizeof(unsigned char);
    return cudaMalloc(&s.px, f) == cudaSuccess && cudaMalloc(&s.py, f) == cudaSuccess
        && cudaMalloc(&s.pz, f) == cudaSuccess && cudaMalloc(&s.dx, f) == cudaSuccess
        && cudaMalloc(&s.dy, f) == cudaSuccess && cudaMalloc(&s.dz, f) == cudaSuccess
        && cudaMalloc(&s.group, i) == cudaSuccess && cudaMalloc(&s.weight, f) == cudaSuccess
        && cudaMalloc(&s.layer, i) == cudaSuccess && cudaMalloc(&s.ctr, u) == cudaSuccess
        && cudaMalloc(&s.sub, b) == cudaSuccess && cudaMalloc(&s.fctr, u) == cudaSuccess
        && cudaMalloc(&s.fsub, b) == cudaSuccess && cudaMalloc(&s.prod, f) == cudaSuccess
        && cudaMalloc(&s.rx, f) == cudaSuccess && cudaMalloc(&s.ry, f) == cudaSuccess
        && cudaMalloc(&s.rz, f) == cudaSuccess && cudaMalloc(&s.rg, i) == cudaSuccess
        && cudaMalloc(&s.sid, u) == cudaSuccess;
}

void free_soa(EigenSoA& s) {
    cudaFree(s.px);
    cudaFree(s.py);
    cudaFree(s.pz);
    cudaFree(s.dx);
    cudaFree(s.dy);
    cudaFree(s.dz);
    cudaFree(s.group);
    cudaFree(s.weight);
    cudaFree(s.layer);
    cudaFree(s.ctr);
    cudaFree(s.sub);
    cudaFree(s.fctr);
    cudaFree(s.fsub);
    cudaFree(s.prod);
    cudaFree(s.rx);
    cudaFree(s.ry);
    cudaFree(s.rz);
    cudaFree(s.rg);
    cudaFree(s.sid);
}

/// Shannon entropy of a fission source on a fixed 8³ mesh over [-R, R]³ (E2b).
double entropy_8cubed(const std::vector<float>& x, const std::vector<float>& y,
                      const std::vector<float>& z, int n, double radius) {
    std::vector<long long> bins(512, 0);
    for (int i = 0; i < n; ++i) {
        auto axis = [radius](float v) {
            int c = static_cast<int>((static_cast<double>(v) + radius) / (2.0 * radius) * 8.0);
            return c < 0 ? 0 : (c > 7 ? 7 : c);
        };
        const int ix = axis(x[static_cast<std::size_t>(i)]);
        const int iy = axis(y[static_cast<std::size_t>(i)]);
        const int iz = axis(z[static_cast<std::size_t>(i)]);
        ++bins[static_cast<std::size_t>((ix * 8 + iy) * 8 + iz)];
    }
    double h = 0.0;
    for (const long long c : bins) {
        if (c > 0) {
            const double pj = static_cast<double>(c) / n;
            h -= pj * std::log(pj);
        }
    }
    return h;
}

}  // namespace

bool gpu_eigen(const ns::geom::LayerStack& stack, const ns::material::MaterialLib& materials,
               std::uint64_t seed, std::int64_t batch, int inactive, int active, int blocks,
               int threads, EigenResultGpu& out) {
    if (batch <= 0 || inactive < 0 || active <= 0 || blocks <= 0 || threads <= 0) {
        return false;
    }

    DeviceProblem problem(stack, materials);
    if (!problem.ok()) {
        return fail();
    }
    const double radius = stack.outermost_radius();
    const int cap = static_cast<int>(batch) * 8;  // bank headroom for k>1 systems

    // Peak-VRAM probe (M4-T4): free memory before/after the up-front allocation.
    std::size_t free_before = 0;
    std::size_t total_mem = 0;
    std::size_t min_free = 0;
    if (cudaMemGetInfo(&free_before, &total_mem) == cudaSuccess) {
        min_free = free_before;
    }

    Bank ba{};
    Bank bb{};
    EigenSoA soa{};
    int* m = nullptr;
    int* d_offset = nullptr;
    int* active_idx = nullptr;
    int* next_active = nullptr;
    int* keep = nullptr;
    int* local = nullptr;
    int* tilesums = nullptr;
    int* tilebase = nullptr;
    int* d_overflow = nullptr;
    const std::size_t icap = static_cast<std::size_t>(cap) * sizeof(int);
    bool ok = alloc_bank(ba, cap) && alloc_bank(bb, cap) && alloc_soa(soa, cap)
           && cudaMalloc(&m, icap) == cudaSuccess && cudaMalloc(&d_offset, icap) == cudaSuccess
           && cudaMalloc(&active_idx, icap) == cudaSuccess
           && cudaMalloc(&next_active, icap) == cudaSuccess && cudaMalloc(&keep, icap) == cudaSuccess
           && cudaMalloc(&local, icap) == cudaSuccess
           && cudaMalloc(&tilesums, static_cast<std::size_t>(kTile) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&tilebase, static_cast<std::size_t>(kTile) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_overflow, sizeof(int)) == cudaSuccess;

    if (ok) {
        std::size_t f = 0;
        std::size_t t = 0;
        if (cudaMemGetInfo(&f, &t) == cudaSuccess && f < min_free) {
            min_free = f;  // peak: banks + SoA + scan scratch all resident
        }
    }

    if (ok) {
        k_init_source<<<blocks, threads>>>(ba, static_cast<int>(batch));
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    Bank cur = ba;
    Bank nxt = bb;
    int nsrc = static_cast<int>(batch);
    float k_est = 1.0f;
    double k_sum = 0.0;
    double k_sumsq = 0.0;
    int k_count = 0;
    double last_entropy = 0.0;

    const int total_gens = inactive + active;
    for (int gen = 0; ok && gen < total_gens; ++gen) {
        // --- Event-based transport of this generation (compacted supersteps) ---
        k_init_eigen<<<blocks, threads>>>(cur, nsrc, problem.geometry(), seed, soa);
        k_iota<<<blocks, threads>>>(active_idx, nsrc);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        if (!ok) {
            break;
        }

        int num_active = nsrc;
        int supersteps = 0;
        constexpr int kMaxSupersteps = 100000;
        while (ok && num_active > 0 && supersteps < kMaxSupersteps) {
            const int step_blocks = (num_active + threads - 1) / threads;
            k_step_eigen<<<step_blocks, threads>>>(soa, problem.geometry(), problem.materials(),
                                                   seed, active_idx, num_active, keep);
            const int nblocks = (num_active + kTile - 1) / kTile;
            k_scan_tiles<<<nblocks, kTile>>>(keep, num_active, local, tilesums);
            k_scan_tilesums<<<1, kTile>>>(tilesums, nblocks, tilebase);
            k_scatter<<<nblocks, kTile>>>(active_idx, keep, local, tilebase, num_active,
                                          next_active);
            ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
            int last_base = 0;
            int last_sum = 0;
            ok = ok
              && cudaMemcpy(&last_base, tilebase + (nblocks - 1), sizeof(int),
                            cudaMemcpyDeviceToHost) == cudaSuccess
              && cudaMemcpy(&last_sum, tilesums + (nblocks - 1), sizeof(int),
                            cudaMemcpyDeviceToHost) == cudaSuccess;
            num_active = last_base + last_sum;
            std::swap(active_idx, next_active);
            ++supersteps;
        }
        if (!ok) {
            break;
        }

        // --- Progeny counts (final fs draw), then bank at prefix-sum slots. ----
        k_progeny_eigen<<<blocks, threads>>>(soa, seed, k_est, nsrc, m);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        if (!ok) {
            break;
        }

        std::vector<std::int32_t> counts(static_cast<std::size_t>(nsrc));
        ok = cudaMemcpy(counts.data(), m, static_cast<std::size_t>(nsrc) * sizeof(int),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
        std::vector<std::int64_t> offs;
        if (ok) {
            ok = progeny_offsets(counts, 256, offs);
        }
        const long long total = ok ? offs.back() : 0;
        if (!ok || total <= 0 || total > cap) {
            ok = false;
            break;
        }
        std::vector<int> offs32(static_cast<std::size_t>(nsrc));
        for (int i = 0; i < nsrc; ++i) {
            offs32[static_cast<std::size_t>(i)] = static_cast<int>(offs[static_cast<std::size_t>(i)]);
        }
        ok = cudaMemcpy(d_offset, offs32.data(), static_cast<std::size_t>(nsrc) * sizeof(int),
                        cudaMemcpyHostToDevice) == cudaSuccess
          && cudaMemset(d_overflow, 0, sizeof(int)) == cudaSuccess;
        if (!ok) {
            break;
        }

        k_bank_eigen<<<blocks, threads>>>(soa, m, d_offset, nsrc, cap, nxt, d_overflow);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        int overflow = 0;
        ok = ok
          && cudaMemcpy(&overflow, d_overflow, sizeof(int), cudaMemcpyDeviceToHost) == cudaSuccess;
        if (!ok || overflow != 0) {
            ok = false;
            break;
        }

        // k for this generation = fission neutrons produced per source neutron.
        std::vector<float> prod_host(static_cast<std::size_t>(nsrc));
        ok = cudaMemcpy(prod_host.data(), soa.prod, static_cast<std::size_t>(nsrc) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
        if (!ok) {
            break;
        }
        double production = 0.0;
        for (const float pr : prod_host) {
            production += static_cast<double>(pr);
        }
        const double k_this = production / static_cast<double>(nsrc);
        k_est = static_cast<float>(k_this > 0.0 ? k_this : 1.0);

        // Entropy of the NEW source (bank nxt).
        std::vector<float> hx(static_cast<std::size_t>(total));
        std::vector<float> hy(static_cast<std::size_t>(total));
        std::vector<float> hz(static_cast<std::size_t>(total));
        ok = cudaMemcpy(hx.data(), nxt.x, static_cast<std::size_t>(total) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess
          && cudaMemcpy(hy.data(), nxt.y, static_cast<std::size_t>(total) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess
          && cudaMemcpy(hz.data(), nxt.z, static_cast<std::size_t>(total) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
        if (!ok) {
            break;
        }
        last_entropy = entropy_8cubed(hx, hy, hz, static_cast<int>(total), radius);

        if (gen >= inactive) {
            k_sum += k_this;
            k_sumsq += k_this * k_this;
            ++k_count;
        }

        Bank tmp = cur;
        cur = nxt;
        nxt = tmp;
        nsrc = static_cast<int>(total);
    }

    // Determinism checksum of the final source (position-weighted stream ids).
    unsigned long long checksum = 0;
    if (ok) {
        std::vector<unsigned long long> s(static_cast<std::size_t>(nsrc));
        ok = cudaMemcpy(s.data(), cur.stream,
                        static_cast<std::size_t>(nsrc) * sizeof(unsigned long long),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
        for (std::size_t i = 0; ok && i < s.size(); ++i) {
            checksum += s[i] * (static_cast<unsigned long long>(i) + 1ull);
        }
    }

    free_bank(ba);
    free_bank(bb);
    free_soa(soa);
    cudaFree(m);
    cudaFree(d_offset);
    cudaFree(active_idx);
    cudaFree(next_active);
    cudaFree(keep);
    cudaFree(local);
    cudaFree(tilesums);
    cudaFree(tilebase);
    cudaFree(d_overflow);
    if (!ok) {
        return fail();
    }

    const double n = static_cast<double>(k_count);
    out.k = k_count > 0 ? k_sum / n : 0.0;
    out.k_sigma = k_count > 1 ? std::sqrt(std::fmax(0.0, (k_sumsq / n) - out.k * out.k) / (n - 1.0))
                              : 0.0;
    out.entropy_final = last_entropy;
    out.generations = total_gens;
    out.source_checksum = checksum;
    out.peak_vram_bytes =
        (free_before > min_free) ? static_cast<std::int64_t>(free_before - min_free) : 0;
    return true;
}

}  // namespace ns::gpu

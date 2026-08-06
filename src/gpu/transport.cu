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
// Live particles are compacted between supersteps by an EXCLUSIVE PREFIX SUM over
// a keep-flag (05 §6 item 2): dead particles fall out and the survivors pack to
// the front of the active-index list, so each superstep launches only over the
// still-live count. The compacted order is immaterial (identity/scores/streams
// are index-keyed) but it is produced by prefix-sum, not an atomic cursor.
// Splitting the single step kernel into per-event branchless kernels is the
// M4-T4 warp-divergence pass (02 §4: profile first).

#include "gpu/transport.h"

#include <cmath>
#include <utility>
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

// Base for the per-history FISSION stream (progeny-count sampling), distinct from
// the source stream so it does not perturb the transport sequence.
inline constexpr unsigned long long kFissionBase = 4ull;

// Scan tile width. A two-level scan (per-tile then a single-block scan of the
// tile sums) handles up to kTile*kTile ≈ 1.05e6 live particles per superstep,
// which bounds `histories`.
inline constexpr int kTile = 1024;
inline constexpr long long kMaxHistories = static_cast<long long>(kTile) * kTile;

/// Device SoA view: raw pointers, passed to kernels by value. Indexed by the
/// particle's source id (never compacted); the active-index list is compacted.
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

__global__ void k_iota(int* active, int n) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < n; i += stride) {
        active[i] = i;
    }
}

__global__ void k_init(SoA soa, DeviceLayerStack geom, unsigned long long seed, int n, float w0,
                       float w1, float w2, float w3, float w4) {
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

        const float total = w0 + w1 + w2 + w3 + w4;
        const float xi = s.uniform_f() * total;
        const float cum[5] = {w0, w0 + w1, w0 + w1 + w2, w0 + w1 + w2 + w3, w0 + w1 + w2 + w3 + w4};
        int grp = 4;
        for (int gi = 0; gi < 5; ++gi) {
            if (xi <= cum[gi]) {
                grp = gi;
                break;
            }
        }
        soa.group[p] = grp;
        soa.weight[p] = 1.0f;
        soa.layer[p] = d_locate(geom, {0.0f, 0.0f, 0.0f});
        soa.score_leak[p] = 0.0f;
        soa.score_prod[p] = 0.0f;

        const auto st = s.state();
        soa.ctr[p] = st.first;
        soa.sub[p] = st.second;
    }
}

/// Resolve one flight for each live particle (active[0..num_active)); write
/// keep[i] = 1 if it survives to the next superstep, else 0.
__global__ void k_step(SoA soa, DeviceLayerStack geom, DeviceMaterials mat, unsigned long long seed,
                       const int* active, int num_active, int* keep) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < num_active;
         i += stride) {
        const int p = active[i];
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
            const float sigma_tr = mat.sigma_tr[L * 5 + grp];
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
                    // Collision on the transport-corrected medium (ADR-021), matching the
                    // sigma_tr flight: select prop. to n_i*sigma_tr,i, split on sigma_tr,i,
                    // reduced scatter share. mu_bar=0 => sigma_tr=sigma_t (analog, unchanged).
                    float pick = s.uniform_f() * sigma_tr;
                    int chosen = begin;
                    for (int si = 0; si < count; ++si) {
                        const int slot = begin + si;
                        pick -= mat.nd[slot] * d_group_sigma_tr(mat.g[slot * 5 + grp]);
                        if (pick <= 0.0f) {
                            chosen = slot;
                            break;
                        }
                    }
                    const DGroup gd = mat.g[chosen * 5 + grp];
                    const float sigma_tr_i = d_group_sigma_tr(gd);
                    if (sigma_tr_i <= 0.0f) {
                        terminal = true;  // transparent isotope
                    } else {
                        const float fission_share = gd.sigma_f / sigma_tr_i;
                        soa.score_prod[p] += w * gd.nu * fission_share;  // weight-weighted, expected

                        w *= (gd.sigma_s - (d_group_sigma_t(gd) - sigma_tr_i)) / sigma_tr_i;  // implicit capture
                        if (w <= 0.0f) {
                            terminal = true;
                        } else if (w < kWeightMin && s.uniform_f() > w / kWeightSurv) {
                            terminal = true;  // E1e — rouletted out
                        } else {
                            if (w < kWeightMin) {
                                w = kWeightSurv;  // E1e — rouletted in
                            }
                            dir = d_sample_isotropic(s);  // isotropic-in-lab scatter
                            float xi = s.uniform_f();
                            int to = grp;
                            for (int t = 0; t < 5; ++t) {
                                xi -= mat.transfer[chosen * 25 + grp * 5 + t];
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

        keep[i] = terminal ? 0 : 1;
    }
}

/// Per-tile exclusive prefix sum of keep[] (Hillis–Steele), writing the local
/// exclusive scan to `local` and each tile's total to `tilesums`.
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

/// Single-block exclusive scan of the tile sums (≤ kTile of them) → tilebase.
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

/// Pack survivors to the front of `next_active` at their prefix-sum positions.
__global__ void k_scatter(const int* active, const int* keep, const int* local,
                          const int* tilebase, int n, int* next_active) {
    const int gid = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (gid < n && keep[gid] != 0) {
        next_active[tilebase[blockIdx.x] + local[gid]] = active[gid];
    }
}

// --- Deterministic fission bank (05 §6 item 3) ------------------------------

/// Integer progeny per history via stochastic rounding: ⌊production + ξ⌋, ξ from
/// a per-history fission stream. E[m] = production (fixed source, k = 1).
__global__ void k_progeny_count(const float* score_prod, unsigned long long seed, int n, int* m) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        ns::rng::Stream s(seed, ns::rng::fork(kFissionBase, 0, static_cast<unsigned int>(p)));
        m[p] = static_cast<int>(floorf(score_prod[p] + s.uniform_f()));
    }
}

/// offset[gid] = tilebase[tile] + local[gid] — the full exclusive prefix sum.
__global__ void k_add_base(const int* local, const int* tilebase, int n, int* offset) {
    const int gid = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (gid < n) {
        offset[gid] = tilebase[blockIdx.x] + local[gid];
    }
}

/// Write each history's progeny at its reserved slots; a progeny's stream id is
/// fork(parent stream, parent final ctr, ordinal) — parent identity, never slot.
__global__ void k_bank(const int* m, const int* offset, const unsigned long long* ctr, int n,
                       unsigned long long* bank) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        const unsigned long long parent = ns::rng::fork(kSourceBase, 0, static_cast<unsigned int>(p));
        const int base = offset[p];
        const int count = m[p];
        for (int o = 0; o < count; ++o) {
            bank[base + o] = ns::rng::fork(parent, ctr[p], static_cast<unsigned int>(o));
        }
    }
}

/// Position-weighted checksum, so a change in slot ORDER (not just contents)
/// shows up. atomicAdd is on a 64-bit integer — exact and order-independent.
__global__ void k_bank_checksum(const unsigned long long* bank, int total,
                                unsigned long long* out) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < total; i += stride) {
        atomicAdd(out, bank[i] * (static_cast<unsigned long long>(i) + 1ull));
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
                      std::int64_t histories, const std::array<float, 5>& group_weights,
                      int blocks, int threads, FixedSourceResult& out) {
    if (histories <= 0 || histories > kMaxHistories || blocks <= 0 || threads <= 0) {
        return false;
    }

    DeviceProblem problem(stack, materials);
    if (!problem.ok()) {
        return fail();
    }

    // Peak-VRAM probe (M4-T4): free memory now, then the minimum free seen at the
    // allocation milestones below. Observational; never gates control flow.
    std::size_t free_before = 0;
    std::size_t total_mem = 0;
    std::size_t min_free = 0;
    if (cudaMemGetInfo(&free_before, &total_mem) == cudaSuccess) {
        min_free = free_before;
    }
    const auto probe_free = [&min_free]() {
        std::size_t f = 0;
        std::size_t t = 0;
        if (cudaMemGetInfo(&f, &t) == cudaSuccess && f < min_free) {
            min_free = f;
        }
    };

    const int n = static_cast<int>(histories);
    const std::size_t nf = static_cast<std::size_t>(n) * sizeof(float);
    const std::size_t ni = static_cast<std::size_t>(n) * sizeof(int);

    SoA soa{};
    int* active = nullptr;
    int* next_active = nullptr;
    int* keep = nullptr;
    int* local = nullptr;
    int* tilesums = nullptr;
    int* tilebase = nullptr;
    bool ok = cudaMalloc(&soa.px, nf) == cudaSuccess && cudaMalloc(&soa.py, nf) == cudaSuccess
           && cudaMalloc(&soa.pz, nf) == cudaSuccess && cudaMalloc(&soa.dx, nf) == cudaSuccess
           && cudaMalloc(&soa.dy, nf) == cudaSuccess && cudaMalloc(&soa.dz, nf) == cudaSuccess
           && cudaMalloc(&soa.group, ni) == cudaSuccess && cudaMalloc(&soa.weight, nf) == cudaSuccess
           && cudaMalloc(&soa.layer, ni) == cudaSuccess
           && cudaMalloc(&soa.ctr, static_cast<std::size_t>(n) * sizeof(unsigned long long))
                  == cudaSuccess
           && cudaMalloc(&soa.sub, static_cast<std::size_t>(n) * sizeof(unsigned char)) == cudaSuccess
           && cudaMalloc(&soa.score_leak, nf) == cudaSuccess
           && cudaMalloc(&soa.score_prod, nf) == cudaSuccess && cudaMalloc(&active, ni) == cudaSuccess
           && cudaMalloc(&next_active, ni) == cudaSuccess && cudaMalloc(&keep, ni) == cudaSuccess
           && cudaMalloc(&local, ni) == cudaSuccess
           && cudaMalloc(&tilesums, static_cast<std::size_t>(kTile) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&tilebase, static_cast<std::size_t>(kTile) * sizeof(int)) == cudaSuccess;

    if (ok) {
        probe_free();  // SoA + scan scratch now resident
        k_init<<<blocks, threads>>>(soa, problem.geometry(), seed, n, group_weights[0],
                                    group_weights[1], group_weights[2], group_weights[3],
                                    group_weights[4]);
        k_iota<<<blocks, threads>>>(active, n);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    int num_active = n;
    int supersteps = 0;
    constexpr int kMaxSupersteps = 100000;  // safety net against a non-terminating history
    while (ok && num_active > 0 && supersteps < kMaxSupersteps) {
        const int step_blocks = (num_active + threads - 1) / threads;
        k_step<<<step_blocks, threads>>>(soa, problem.geometry(), problem.materials(), seed, active,
                                         num_active, keep);

        const int nblocks = (num_active + kTile - 1) / kTile;
        k_scan_tiles<<<nblocks, kTile>>>(keep, num_active, local, tilesums);
        k_scan_tilesums<<<1, kTile>>>(tilesums, nblocks, tilebase);
        k_scatter<<<nblocks, kTile>>>(active, keep, local, tilebase, num_active, next_active);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;

        // New live count = base of the last tile + that tile's own sum.
        int last_base = 0;
        int last_sum = 0;
        ok = ok
          && cudaMemcpy(&last_base, tilebase + (nblocks - 1), sizeof(int), cudaMemcpyDeviceToHost)
                 == cudaSuccess
          && cudaMemcpy(&last_sum, tilesums + (nblocks - 1), sizeof(int), cudaMemcpyDeviceToHost)
                 == cudaSuccess;
        num_active = last_base + last_sum;
        std::swap(active, next_active);
        ++supersteps;
    }

    // --- Deterministic fission bank (05 §6 item 3): built from the per-history
    // production and each history's final RNG cursor. Reuses the scan scratch. ---
    int* m = nullptr;
    int* offset = nullptr;
    unsigned long long* d_checksum = nullptr;
    long long bank_total = 0;
    unsigned long long bank_checksum = 0;
    if (ok) {
        ok = cudaMalloc(&m, ni) == cudaSuccess && cudaMalloc(&offset, ni) == cudaSuccess
          && cudaMalloc(&d_checksum, sizeof(unsigned long long)) == cudaSuccess;
    }
    if (ok) {
        k_progeny_count<<<blocks, threads>>>(soa.score_prod, seed, n, m);
        const int nblocks = (n + kTile - 1) / kTile;
        k_scan_tiles<<<nblocks, kTile>>>(m, n, local, tilesums);
        k_scan_tilesums<<<1, kTile>>>(tilesums, nblocks, tilebase);
        k_add_base<<<nblocks, kTile>>>(local, tilebase, n, offset);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        int last_off = 0;
        int last_m = 0;
        ok = ok
          && cudaMemcpy(&last_off, offset + (n - 1), sizeof(int), cudaMemcpyDeviceToHost)
                 == cudaSuccess
          && cudaMemcpy(&last_m, m + (n - 1), sizeof(int), cudaMemcpyDeviceToHost) == cudaSuccess;
        bank_total = static_cast<long long>(last_off) + last_m;
    }
    unsigned long long* bank = nullptr;
    if (ok && bank_total > 0) {
        ok = cudaMalloc(&bank, static_cast<std::size_t>(bank_total) * sizeof(unsigned long long))
                 == cudaSuccess
          && cudaMemset(d_checksum, 0, sizeof(unsigned long long)) == cudaSuccess;
        if (ok) {
            k_bank<<<blocks, threads>>>(m, offset, soa.ctr, n, bank);
            k_bank_checksum<<<blocks, threads>>>(bank, static_cast<int>(bank_total), d_checksum);
            ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
            ok = ok
              && cudaMemcpy(&bank_checksum, d_checksum, sizeof(unsigned long long),
                            cudaMemcpyDeviceToHost) == cudaSuccess;
        }
    }
    probe_free();  // peak: SoA + scan scratch + bank scratch + fission bank all resident
    cudaFree(m);
    cudaFree(offset);
    cudaFree(bank);
    cudaFree(d_checksum);

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
    cudaFree(soa.ctr);
    cudaFree(soa.sub);
    cudaFree(soa.score_leak);
    cudaFree(soa.score_prod);
    cudaFree(active);
    cudaFree(next_active);
    cudaFree(keep);
    cudaFree(local);
    cudaFree(tilesums);
    cudaFree(tilebase);
    if (!ok) {
        return fail();
    }

    out.leaked_fraction = mean_of(leak);
    out.leaked_sigma = stderr_of(leak);
    out.k_estimate = mean_of(prod);
    out.k_sigma = stderr_of(prod);
    out.histories = histories;
    out.supersteps = supersteps;
    out.fission_bank_size = bank_total;
    out.fission_bank_checksum = bank_checksum;
    out.peak_vram_bytes =
        (free_before > min_free) ? static_cast<std::int64_t>(free_before - min_free) : 0;
    return true;
}

}  // namespace ns::gpu

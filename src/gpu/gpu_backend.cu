// CUDA backend foundation (M4-T1): device Philox KATs + the three deterministic
// primitives of 05 §6 — slots (prefix-sum), streams (fork), reductions
// (fixed-point int64). No transport physics yet (that is M4-T2).
//
// The determinism guarantee (01 §9 / BLK-11): same seed ⇒ bit-identical result
// regardless of thread count, block size or launch order. Two design choices
// deliver it here:
//   * the RNG stream of each particle is fork(parent identity), never a function
//     of buffer position, so which thread touches a particle is irrelevant;
//   * the reduction accumulates in fixed-point int64, and integer addition is
//     exactly associative and commutative, so any partition of the work sums to
//     the identical accumulator. Only ONE promotion to double happens, at the
//     very end — never per-block, which would reintroduce float non-associativity
//     across differing block counts. Floating-point atomicAdd is never used.

#include "gpu/gpu_backend.h"

#include <array>
#include <cstdint>
#include <vector>

#include <cuda_runtime.h>

#include "core/rng/rng.h"
#include "gpu/buffers.cuh"

namespace ns::gpu {
namespace {

// Documented fixed-point scale, 2^40. Resolution 2^-40 ≈ 9.1e-13 (resolves the
// C-902 weight floor 1e-4 with ~1e8 levels to spare); a per-reduction total must
// stay below int64_max/2^40 = 2^23 ≈ 8.4e6, which the M4-T1 workloads honour. It
// is an algorithm parameter, not a physical constant (cf. the Philox multipliers
// in philox.h, deliberately not in constants.data.toml). M4-T2's per-generation
// tallies promote to double once per block per generation to bound magnitude.
inline constexpr double kFixedScale = 1099511627776.0;  // 2^40

__device__ inline unsigned long long to_fixed(float w) {
    // Round-to-nearest, explicit, so the mapping is independent of the ambient
    // FP rounding mode. Non-negative here (weights are), so the reinterpret to
    // unsigned for atomicAdd is a straight bit copy.
    return static_cast<unsigned long long>(__double2ll_rn(static_cast<double>(w) * kFixedScale));
}

// --- Philox known-answer kernels (04 §2) ------------------------------------

__global__ void k_philox_published(unsigned int* out) {
    const int i = static_cast<int>(threadIdx.x);  // 0..2
    ns::rng::Counter ctr{};
    ns::rng::Key key{};
    if (i == 0) {
        ctr = ns::rng::Counter{0u, 0u, 0u, 0u};
        key = ns::rng::Key{0u, 0u};
    } else if (i == 1) {
        ctr = ns::rng::Counter{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        key = ns::rng::Key{0xFFFFFFFFu, 0xFFFFFFFFu};
    } else {
        ctr = ns::rng::Counter{0x243F6A88u, 0x85A308D3u, 0x13198A2Eu, 0x03707344u};
        key = ns::rng::Key{0xA4093822u, 0x299F31D0u};
    }
    const ns::rng::Counter r = ns::rng::philox4x32_10(ctr, key);
    for (int j = 0; j < 4; ++j) {
        out[i * 4 + j] = r[static_cast<std::size_t>(j)];
    }
}

__global__ void k_uniform_f_first16(float* out) {
    ns::rng::Stream s(0, 0);
    for (int i = 0; i < 16; ++i) {
        out[i] = s.uniform_f();
    }
}

__global__ void k_fork(unsigned long long* out) {
    *out = ns::rng::fork(42, 1000, 3);
}

// --- Reduction: fill the SoA batch, then sum weights deterministically -------

__global__ void k_fill_batch(ParticleSoA soa, unsigned long long seed) {
    const long long n = soa.n;
    const long long stride = static_cast<long long>(gridDim.x) * blockDim.x;
    for (long long p = static_cast<long long>(blockIdx.x) * blockDim.x + threadIdx.x; p < n;
         p += stride) {
        // Stream id from PARENT IDENTITY (05 §6 item 3): fork of the source
        // stream `seed`, counter 0, progeny ordinal = particle index. It does not
        // depend on which thread or block reaches this p.
        const unsigned long long child = ns::rng::fork(seed, 0, static_cast<unsigned int>(p));
        ns::rng::Stream s(seed, child);
        const float w = s.uniform_f();
        const auto st = s.state();  // (ctr, sub) after the draw

        soa.weight[p] = w;
        soa.stream_id[p] = child;
        soa.ctr[p] = st.first;
        soa.sub[p] = st.second;
        soa.group[p] = static_cast<int>(w * 4.0f);  // 0..3, exercises the field
        soa.pos_x[p] = 0.0f;
        soa.pos_y[p] = 0.0f;
        soa.pos_z[p] = 0.0f;
        soa.dir_theta[p] = 0.0f;
        soa.dir_phi[p] = 0.0f;
    }
}

__global__ void k_reduce_weight(const float* weight, long long n, unsigned long long* g_fixed) {
    unsigned long long local = 0;
    const long long stride = static_cast<long long>(gridDim.x) * blockDim.x;
    for (long long p = static_cast<long long>(blockIdx.x) * blockDim.x + threadIdx.x; p < n;
         p += stride) {
        local += to_fixed(weight[p]);
    }
    // Integer atomicAdd: exact and order-independent, so the accumulator is
    // identical for any launch geometry. (Floating-point atomicAdd is the thing
    // 05 §6 forbids; this is not that.)
    atomicAdd(g_fixed, local);
}

// --- Exclusive prefix sum for progeny slots (05 §6 item 3) ------------------

__global__ void k_scan_tiles(const int* counts, long long n, long long* local,
                             long long* tile_sums) {
    extern __shared__ long long sbuf[];
    const int tid = static_cast<int>(threadIdx.x);
    const long long gid = static_cast<long long>(blockIdx.x) * blockDim.x + tid;

    const long long v = (gid < n) ? static_cast<long long>(counts[gid]) : 0;
    sbuf[tid] = v;
    __syncthreads();

    // Hillis–Steele inclusive scan within the tile; a fixed sequence of steps, so
    // the result does not depend on warp scheduling.
    for (int offset = 1; offset < static_cast<int>(blockDim.x); offset <<= 1) {
        const long long add = (tid >= offset) ? sbuf[tid - offset] : 0;
        __syncthreads();
        sbuf[tid] += add;
        __syncthreads();
    }

    if (gid < n) {
        local[gid] = sbuf[tid] - v;  // inclusive − own value = exclusive within tile
    }
    if (tid == static_cast<int>(blockDim.x) - 1) {
        tile_sums[blockIdx.x] = sbuf[tid];  // tile total (padding contributes 0)
    }
}

__global__ void k_add_base(long long* local, long long n, const long long* tile_base) {
    const long long gid = static_cast<long long>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (gid < n) {
        local[gid] += tile_base[blockIdx.x];
    }
}

// Clears any sticky error and returns false — a uniform bail-out for the host
// wrappers so a CUDA failure never poisons a later call in the same process.
bool fail() {
    cudaGetLastError();
    return false;
}

}  // namespace

double fixed_point_scale() { return kFixedScale; }

int device_count() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        cudaGetLastError();
        return -1;
    }
    return count;
}

bool device_philox_published(std::array<std::array<std::uint32_t, 4>, 3>& out) {
    unsigned int* d = nullptr;
    if (cudaMalloc(&d, 12 * sizeof(unsigned int)) != cudaSuccess) return fail();

    k_philox_published<<<1, 3>>>(d);
    bool ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;

    unsigned int host[12] = {};
    ok = ok && cudaMemcpy(host, d, sizeof(host), cudaMemcpyDeviceToHost) == cudaSuccess;
    cudaFree(d);
    if (!ok) return fail();

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                host[i * 4 + j];
        }
    }
    return true;
}

bool device_uniform_f_first16(std::array<float, 16>& out) {
    float* d = nullptr;
    if (cudaMalloc(&d, 16 * sizeof(float)) != cudaSuccess) return fail();

    k_uniform_f_first16<<<1, 1>>>(d);
    bool ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    ok = ok && cudaMemcpy(out.data(), d, 16 * sizeof(float), cudaMemcpyDeviceToHost) == cudaSuccess;
    cudaFree(d);
    return ok ? true : fail();
}

bool device_fork_42_1000_3(std::uint64_t& out) {
    unsigned long long* d = nullptr;
    if (cudaMalloc(&d, sizeof(unsigned long long)) != cudaSuccess) return fail();

    k_fork<<<1, 1>>>(d);
    bool ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    unsigned long long host = 0;
    ok = ok && cudaMemcpy(&host, d, sizeof(host), cudaMemcpyDeviceToHost) == cudaSuccess;
    cudaFree(d);
    if (!ok) return fail();
    out = host;
    return true;
}

bool deterministic_weight_sum(std::int64_t n, std::uint64_t seed, int blocks, int threads,
                              WeightSumResult& out) {
    if (n < 0 || blocks <= 0 || threads <= 0) return false;

    ParticleBuffers pb(n);
    if (!pb.ok()) return fail();

    unsigned long long* g = nullptr;
    if (cudaMalloc(&g, sizeof(unsigned long long)) != cudaSuccess) return fail();
    bool ok = cudaMemset(g, 0, sizeof(unsigned long long)) == cudaSuccess;

    if (ok && n > 0) {
        k_fill_batch<<<blocks, threads>>>(pb.view(), seed);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }
    if (ok && n > 0) {
        k_reduce_weight<<<blocks, threads>>>(pb.view().weight, n, g);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    unsigned long long host = 0;
    ok = ok && cudaMemcpy(&host, g, sizeof(host), cudaMemcpyDeviceToHost) == cudaSuccess;
    cudaFree(g);
    if (!ok) return fail();

    out.fixed = static_cast<std::int64_t>(host);
    out.value = static_cast<double>(out.fixed) / kFixedScale;
    return true;
}

bool progeny_offsets(const std::vector<std::int32_t>& counts, int threads,
                     std::vector<std::int64_t>& offsets) {
    if (threads <= 0) return false;

    const long long n = static_cast<long long>(counts.size());
    offsets.assign(static_cast<std::size_t>(n) + 1, 0);
    if (n == 0) return true;  // offsets == {0}

    const long long nblocks = (n + threads - 1) / threads;

    int* d_counts = nullptr;
    long long* d_local = nullptr;
    long long* d_tilesums = nullptr;
    long long* d_tilebase = nullptr;
    bool ok = cudaMalloc(&d_counts, static_cast<std::size_t>(n) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_local, static_cast<std::size_t>(n) * sizeof(long long)) == cudaSuccess
           && cudaMalloc(&d_tilesums, static_cast<std::size_t>(nblocks) * sizeof(long long)) == cudaSuccess
           && cudaMalloc(&d_tilebase, static_cast<std::size_t>(nblocks) * sizeof(long long)) == cudaSuccess;

    ok = ok && cudaMemcpy(d_counts, counts.data(), static_cast<std::size_t>(n) * sizeof(int),
                          cudaMemcpyHostToDevice) == cudaSuccess;

    if (ok) {
        const std::size_t shmem = static_cast<std::size_t>(threads) * sizeof(long long);
        k_scan_tiles<<<static_cast<unsigned>(nblocks), static_cast<unsigned>(threads), shmem>>>(
            d_counts, n, d_local, d_tilesums);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    // Stage two: exclusive scan of the per-tile sums on the host — a handful of
    // int64 adds, deterministic, then broadcast back as each tile's base.
    std::vector<long long> tilesums(static_cast<std::size_t>(nblocks));
    ok = ok && cudaMemcpy(tilesums.data(), d_tilesums,
                          static_cast<std::size_t>(nblocks) * sizeof(long long),
                          cudaMemcpyDeviceToHost) == cudaSuccess;

    long long total = 0;
    if (ok) {
        std::vector<long long> tilebase(static_cast<std::size_t>(nblocks));
        for (long long b = 0; b < nblocks; ++b) {
            tilebase[static_cast<std::size_t>(b)] = total;
            total += tilesums[static_cast<std::size_t>(b)];
        }
        ok = cudaMemcpy(d_tilebase, tilebase.data(),
                        static_cast<std::size_t>(nblocks) * sizeof(long long),
                        cudaMemcpyHostToDevice) == cudaSuccess;
    }

    if (ok) {
        k_add_base<<<static_cast<unsigned>(nblocks), static_cast<unsigned>(threads)>>>(
            d_local, n, d_tilebase);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
    }

    ok = ok && cudaMemcpy(offsets.data(), d_local, static_cast<std::size_t>(n) * sizeof(long long),
                          cudaMemcpyDeviceToHost) == cudaSuccess;

    cudaFree(d_counts);
    cudaFree(d_local);
    cudaFree(d_tilesums);
    cudaFree(d_tilebase);
    if (!ok) return fail();

    offsets[static_cast<std::size_t>(n)] = total;
    return true;
}

}  // namespace ns::gpu

// GPU k-eigenvalue power iteration (M4-T3). See eigen.h.
//
// Each generation, one thread transports one source neutron to completion
// (history-per-thread — compact; the event-based superstep transport is the
// M4-T4 perf substitution), reservoir-samples ONE fission site weighted by
// per-collision production (from a separate fission stream, so the transport RNG
// is unperturbed), and emits ⌊production/k + ξ⌋ progeny there. The progeny are
// banked at EXCLUSIVE-PREFIX-SUM slots (reusing gpu_backend's scan) with
// fork-derived streams — deterministic, so the whole iteration is bit-identical
// across launch configs. k iterates as production/source; entropy is the E2b
// signal.

#include "gpu/eigen.h"

#include <cmath>
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

__device__ inline DFloat3 d_iso(ns::rng::Stream& s) {
    const float mu = 2.0f * s.uniform_f() - 1.0f;
    const float phi = kTwoPi * s.uniform_f();
    const float st = sqrtf(fmaxf(0.0f, 1.0f - mu * mu));
    return {st * cosf(phi), st * sinf(phi), mu};
}

/// Fission bank: source neutron positions, birth groups, stream ids.
struct Bank {
    float* x;
    float* y;
    float* z;
    int* group;
    unsigned long long* stream;
};

__global__ void k_init(Bank b, int n) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < n; p += stride) {
        b.x[p] = 0.0f;  // gen-0 source: point at the origin, group 0
        b.y[p] = 0.0f;
        b.z[p] = 0.0f;
        b.group[p] = 0;
        b.stream[p] = ns::rng::fork(kSourceBase, 0, static_cast<unsigned int>(p));
    }
}

/// Transport a generation; emit per-source progeny count m[] + the reservoir
/// fission site (fx/fy/fz/fg) + production prod_out[].
__global__ void k_generation(Bank in, int nsrc, DeviceLayerStack geom, DeviceMaterials mat,
                             unsigned long long seed, float k_est, int* m, float* fx, float* fy,
                             float* fz, int* fg, float* prod_out) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < nsrc; p += stride) {
        const unsigned long long sid = in.stream[p];
        ns::rng::Stream s(seed, sid);
        ns::rng::Stream fs(seed, ns::rng::fork(kFissionBase, sid, 0));
        DFloat3 pos{in.x[p], in.y[p], in.z[p]};
        int grp = in.group[p];
        DFloat3 dir = d_iso(s);
        int L = d_locate(geom, pos);
        float w = 1.0f;
        float prod = 0.0f;
        float rx = pos.x;
        float ry = pos.y;
        float rz = pos.z;
        int rg = grp;

        for (int step = 0; step < 100000; ++step) {
            if (L == kOutsideDev) {
                break;  // leaked
            }
            const float str = mat.sigma_tr[L * 5 + grp];
            const float d = d_distance_to_boundary(geom, pos, dir, L);
            if (str <= 0.0f) {
                if (d >= kInfF) {
                    break;
                }
                pos = d_advance(pos, dir, d);
                L = d_nudge_and_locate(geom, pos, dir);
                continue;
            }
            const float flight = -logf(s.uniform_f()) / str;
            if (flight > d) {
                if (d >= kInfF) {
                    break;
                }
                pos = d_advance(pos, dir, d);
                L = d_nudge_and_locate(geom, pos, dir);
                continue;
            }
            pos = d_advance(pos, dir, flight);
            const int begin = mat.slot_begin[L];
            const int count = mat.slot_count[L];
            // Collision on the transport-corrected medium (ADR-021), matching the sigma_tr
            // flight: select prop. to n_i*sigma_tr,i and split on sigma_tr,i. mu_bar=0 =>
            // sigma_tr=sigma_t, so this is byte-identical to the analog split (the CPU
            // oracle does the same; G0c parity on a mu!=0 set holds only with this).
            float pick = s.uniform_f() * str;
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
            const float sti = d_group_sigma_tr(gd);  // collision denominator = sigma_tr,i
            if (sti <= 0.0f) {
                break;
            }
            const float pcoll = w * gd.nu * (gd.sigma_f / sti);
            prod += pcoll;
            // Weighted reservoir (1 sample): keep this site with prob pcoll/prod.
            if (pcoll > 0.0f && fs.uniform_f() * prod <= pcoll) {
                rx = pos.x;
                ry = pos.y;
                rz = pos.z;
                const float xic = fs.uniform_f();  // birth group from χ of the fissioner
                float cum = 0.0f;
                int bg = 3;
                for (int gg = 0; gg < 5; ++gg) {
                    cum += mat.g[chosen * 5 + gg].chi;
                    if (xic <= cum) {
                        bg = gg;
                        break;
                    }
                }
                rg = bg;
            }
            // implicit capture: survive as the transport-reduced scatter share
            // sigma_s,tr = sigma_s - (sigma_t - sigma_tr) = sigma_s - mu*sigma_s.
            w *= (gd.sigma_s - (d_group_sigma_t(gd) - sti)) / sti;
            if (w <= 0.0f) {
                break;
            }
            if (w < kWeightMin) {
                if (s.uniform_f() > w / kWeightSurv) {
                    break;
                }
                w = kWeightSurv;
            }
            dir = d_iso(s);
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

        prod_out[p] = prod;
        m[p] = static_cast<int>(floorf(prod / k_est + fs.uniform_f()));
        fx[p] = rx;
        fy[p] = ry;
        fz[p] = rz;
        fg[p] = rg;
    }
}

/// Write each source's progeny at its reserved slots (fork streams, no atomic).
__global__ void k_bank(Bank in, const int* m, const int* offset, const float* fx, const float* fy,
                       const float* fz, const int* fg, int nsrc, int cap, Bank out, int* overflow) {
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int p = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); p < nsrc; p += stride) {
        const unsigned long long parent = in.stream[p];
        const int base = offset[p];
        const int cnt = m[p];
        for (int o = 0; o < cnt; ++o) {
            const int slot = base + o;
            if (slot >= cap) {
                atomicExch(overflow, 1);
                break;
            }
            out.x[slot] = fx[p];
            out.y[slot] = fy[p];
            out.z[slot] = fz[p];
            out.group[slot] = fg[p];
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

    // Peak-VRAM probe (M4-T4): the eigen allocates its whole footprint up front
    // (two ping-pong banks + per-source scratch, all cap-sized), so the free-memory
    // drop right after allocation is the peak. Observational; never gates flow.
    std::size_t free_before = 0;
    std::size_t total_mem = 0;
    std::size_t min_free = 0;
    if (cudaMemGetInfo(&free_before, &total_mem) == cudaSuccess) {
        min_free = free_before;
    }

    Bank ba{};
    Bank bb{};
    int* m = nullptr;
    int* d_offset = nullptr;
    float* fx = nullptr;
    float* fy = nullptr;
    float* fz = nullptr;
    int* fg = nullptr;
    float* prod = nullptr;
    int* d_overflow = nullptr;
    bool ok = alloc_bank(ba, cap) && alloc_bank(bb, cap)
           && cudaMalloc(&m, static_cast<std::size_t>(cap) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&d_offset, static_cast<std::size_t>(cap) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&fx, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&fy, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&fz, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&fg, static_cast<std::size_t>(cap) * sizeof(int)) == cudaSuccess
           && cudaMalloc(&prod, static_cast<std::size_t>(cap) * sizeof(float)) == cudaSuccess
           && cudaMalloc(&d_overflow, sizeof(int)) == cudaSuccess;

    if (ok) {
        std::size_t f = 0;
        std::size_t t = 0;
        if (cudaMemGetInfo(&f, &t) == cudaSuccess && f < min_free) {
            min_free = f;  // peak: both banks + all per-source scratch resident
        }
    }

    if (ok) {
        k_init<<<blocks, threads>>>(ba, static_cast<int>(batch));
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
        k_generation<<<blocks, threads>>>(cur, nsrc, problem.geometry(), problem.materials(), seed,
                                           k_est, m, fx, fy, fz, fg, prod);
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess;
        if (!ok) {
            break;
        }

        // Exclusive prefix sum of the progeny counts on the host (reuses the
        // tested scan); a few thousand ints per generation.
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

        k_bank<<<blocks, threads>>>(cur, m, d_offset, fx, fy, fz, fg, nsrc, cap, nxt, d_overflow);
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
        ok = cudaMemcpy(prod_host.data(), prod, static_cast<std::size_t>(nsrc) * sizeof(float),
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
            out.k_history.push_back(k_this);  // G0c (c): active-gen k, in order (bit-identical)
        }

        // Swap banks; the new source is `total` neutrons.
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

    // G0c (b): bin the final fission bank into equal-width radial shells over [0, radius].
    // Host-side integer counts (like entropy_8cubed) -> bit-identical across launch configs, no
    // device atomics. run_diff bins the ref sites into the same shell count to compare (08 §2 b).
    if (ok) {
        constexpr int kShells = 8;
        std::vector<float> px(static_cast<std::size_t>(nsrc));
        std::vector<float> py(static_cast<std::size_t>(nsrc));
        std::vector<float> pz(static_cast<std::size_t>(nsrc));
        ok = cudaMemcpy(px.data(), cur.x, static_cast<std::size_t>(nsrc) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess
          && cudaMemcpy(py.data(), cur.y, static_cast<std::size_t>(nsrc) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess
          && cudaMemcpy(pz.data(), cur.z, static_cast<std::size_t>(nsrc) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
        if (ok && radius > 0.0) {
            std::vector<long long> shell(static_cast<std::size_t>(kShells), 0);
            for (int i = 0; i < nsrc; ++i) {
                const double r = std::sqrt(static_cast<double>(px[i]) * px[i]
                                           + static_cast<double>(py[i]) * py[i]
                                           + static_cast<double>(pz[i]) * pz[i]);
                int b = static_cast<int>(r / radius * kShells);
                if (b < 0) b = 0;
                if (b >= kShells) b = kShells - 1;  // clamp r >= radius into the outer shell
                ++shell[static_cast<std::size_t>(b)];
            }
            out.per_shell_source.resize(shell.size());
            for (std::size_t i = 0; i < shell.size(); ++i) {
                out.per_shell_source[i] = static_cast<double>(shell[i]);  // explicit i64 -> f64
            }
        }
    }

    free_bank(ba);
    free_bank(bb);
    cudaFree(m);
    cudaFree(d_offset);
    cudaFree(fx);
    cudaFree(fy);
    cudaFree(fz);
    cudaFree(fg);
    cudaFree(prod);
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

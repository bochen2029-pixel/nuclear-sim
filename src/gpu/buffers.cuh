// SoA persistent particle buffers for the CUDA backend (M4-T1, 05 §6 item 1).
//
// One device array per field: pos, dir as (theta, phi), group, weight, and the
// full resumable RNG stream state (stream id + the (ctr, sub) cursor, 03 §8 §2 /
// 04 §2). Positions and directions are float, per the precision policy (01 §9);
// the CPU oracle keeps doubles, the GPU backend does not.
//
// Structure-of-arrays rather than array-of-structs so coalesced access and the
// event-partition compaction of M4-T2 stay cheap. This header is device-only
// (included by .cu translation units); the host talks to it through the RAII
// owner below and the plain-typed API in gpu_backend.h.

#pragma once

#include <cstdint>

#include <cuda_runtime.h>

namespace ns::gpu {

/// Device-side view: raw pointers, trivially copyable so a kernel takes it by
/// value. Never owns memory — ParticleBuffers does.
struct ParticleSoA {
    float* pos_x = nullptr;
    float* pos_y = nullptr;
    float* pos_z = nullptr;
    float* dir_theta = nullptr;  // direction stored as angles (05 §6 item 1)
    float* dir_phi = nullptr;
    int* group = nullptr;
    float* weight = nullptr;
    unsigned long long* stream_id = nullptr;  // per-particle stream (04 §2)
    unsigned long long* ctr = nullptr;        // resumable cursor, block index
    unsigned char* sub = nullptr;             // resumable cursor, sub-word 0..3
    long long n = 0;
};

/// Host-owning RAII manager for the device SoA. Non-copyable; frees on scope
/// exit even on a partially-failed allocation (cudaFree(nullptr) is a no-op).
class ParticleBuffers {
public:
    explicit ParticleBuffers(long long n) : n_(n) { ok_ = allocate(); }
    ~ParticleBuffers() { free_all(); }

    ParticleBuffers(const ParticleBuffers&) = delete;
    ParticleBuffers& operator=(const ParticleBuffers&) = delete;

    bool ok() const noexcept { return ok_; }
    long long size() const noexcept { return n_; }
    ParticleSoA view() const noexcept { return soa_; }

private:
    template <typename T>
    bool alloc(T** p) {
        return cudaMalloc(p, static_cast<std::size_t>(n_) * sizeof(T)) == cudaSuccess;
    }

    bool allocate() {
        soa_.n = n_;
        // Short-circuits on the first failure; free_all() then releases whatever
        // did allocate. A false here is a genuine out-of-memory, surfaced up.
        return alloc(&soa_.pos_x) && alloc(&soa_.pos_y) && alloc(&soa_.pos_z)
            && alloc(&soa_.dir_theta) && alloc(&soa_.dir_phi) && alloc(&soa_.group)
            && alloc(&soa_.weight) && alloc(&soa_.stream_id) && alloc(&soa_.ctr)
            && alloc(&soa_.sub);
    }

    void free_all() noexcept {
        cudaFree(soa_.pos_x);
        cudaFree(soa_.pos_y);
        cudaFree(soa_.pos_z);
        cudaFree(soa_.dir_theta);
        cudaFree(soa_.dir_phi);
        cudaFree(soa_.group);
        cudaFree(soa_.weight);
        cudaFree(soa_.stream_id);
        cudaFree(soa_.ctr);
        cudaFree(soa_.sub);
        cudaGetLastError();  // clear any sticky free error for the next call
    }

    long long n_ = 0;
    bool ok_ = false;
    ParticleSoA soa_{};
};

}  // namespace ns::gpu

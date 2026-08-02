// CUDA half of the M0-T2 toolchain smoke test.
//
// This file exists to make nvcc 13.1 + MSVC 14.44 + C++20 + sm_89 fail at M0 if
// that combination does not actually work, rather than at M4-T1 when there is
// real transport code to debug at the same time. It contains no physics.

#include "test_toolchain_cuda.h"

#include <cuda_runtime.h>

namespace {

__global__ void add_one(int* value) {
    *value += 1;
}

// C++20 template lambda. Not needed for the arithmetic; it is here so that
// compiling this file at all is proof nvcc accepted -std=c++20 for a .cu
// translation unit, which is what 02 §4's C++20 pin requires of src/gpu.
constexpr auto identity = []<typename T>(T v) { return v; };

}  // namespace

int nukesim_cuda_device_count() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        // Clear the sticky error so a later call in the same process is not
        // poisoned by this one.
        cudaGetLastError();
        return -1;
    }
    return count;
}

int nukesim_cuda_roundtrip(int start) {
    int* device_value = nullptr;
    if (cudaMalloc(&device_value, sizeof(int)) != cudaSuccess) {
        cudaGetLastError();
        return -1;
    }

    int result = -1;
    const int seed = identity(start);

    if (cudaMemcpy(device_value, &seed, sizeof(int), cudaMemcpyHostToDevice) == cudaSuccess) {
        add_one<<<1, 1>>>(device_value);
        if (cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess) {
            if (cudaMemcpy(&result, device_value, sizeof(int), cudaMemcpyDeviceToHost) != cudaSuccess) {
                result = -1;
            }
        }
    }

    cudaFree(device_value);
    cudaGetLastError();
    return result;
}

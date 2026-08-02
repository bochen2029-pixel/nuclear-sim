// Bridge between the Catch2 test translation unit and the CUDA one (M0-T2).
// Catch2 headers are deliberately kept out of nvcc's reach: nothing third-party
// should be compiled under -Werror=all-warnings in the device pass.
#pragma once

// Number of visible CUDA devices, or -1 if the runtime could not be queried.
int nukesim_cuda_device_count();

// Launches a trivial kernel that increments `start` on the device and copies the
// result back. Returns the incremented value, or -1 on any CUDA error.
int nukesim_cuda_roundtrip(int start);

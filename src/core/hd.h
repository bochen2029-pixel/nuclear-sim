// Host/device execution-space qualifier (M4-T1).
//
// Expands to `__host__ __device__` when compiled by nvcc, and to nothing on a
// host-only compiler. A single constexpr definition then serves both backends,
// which is exactly what the device Philox needs: 04 §2 / 05 §6 require the GPU
// RNG to be the SAME bijection as ref/, not a second implementation that could
// drift from the frozen KATs. Marking the functions explicitly (rather than
// leaning on nvcc's implicit "constexpr is host-device" rule) keeps device
// readiness self-documenting and independent of --expt-relaxed-constexpr.
//
// It is behaviour-preserving on the host: under MSVC/GCC/Clang the macro is
// empty, so every function it annotates compiles exactly as before. The frozen
// RNG known-answer values are unaffected (tests/unit/test_rng.cpp re-verifies
// them on the host each build).

#pragma once

#if defined(__CUDACC__)
#define NUKESIM_HD __host__ __device__
#else
#define NUKESIM_HD
#endif

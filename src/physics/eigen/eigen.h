// k-eigenvalue solver: power iteration + Shannon-entropy convergence (05 §2, 01 §3).
//
// Backend-agnostic by construction — it drives a transport object through
// generations and never touches geometry or cross sections itself, so the GPU
// backend (M4-T3) substitutes for the transport without the solver changing.

#pragma once

#include "ref/ref_transport.h"

#include <cstdint>
#include <vector>

namespace ns::physics {

struct EigenSpec {
    std::int64_t batch = 100000;
    int inactive = 10;
    int active = 30;
    double k0 = 1.0;
    /// Entropy-window tolerance, C-901.
    double h_tol = 1e-3;
    /// Minimum inactive generations before convergence may be declared (I_min).
    int min_inactive = 5;
    /// Concentrated initial bank — 05 §2's deliberately bad source.
    bool bad_initial_source = false;
    std::uint64_t seed = 20260802;
};

struct EigenResult {
    /// k_eff on TOTAL nu-bar — the benchmark-comparable quantity (ADR-013).
    ///
    /// This is NEVER beta-corrected. k_prompt = k*(1 - beta_eff) is derived
    /// exactly once, downstream, by the kinetics; a solver that returned
    /// k_prompt would be off by ~650 pcm on a U-235 system, which exceeds
    /// G0a's entire tolerance while looking entirely plausible.
    double k = 0.0;
    /// max(active-cycle standard error, batched-means standard error), in pcm.
    /// The larger of the two, because inter-cycle correlation makes the naive
    /// active-cycle estimate optimistic (MAJ-32).
    double sigma_pcm = 0.0;
    /// Source-weighted delayed fraction, Sum_i beta_i S_i / Sum_i S_i (ADR-013).
    double beta_eff = 0.0;
    double lambda_s = 0.0;    // prompt generation time, Lambda = l/k
    double lifetime_s = 0.0;  // mean prompt lifetime, l
    double alpha_per_s = 0.0; // (k-1)/Lambda  (E3c)

    std::vector<double> k_history;
    std::vector<double> h_history;
    ref::FissionSource source;

    /// Generations actually spent before the entropy window converged, or
    /// `inactive` when it never did.
    int inactive_used = 0;
    bool converged = false;

    /// The prompt multiplication. Derived HERE and only here, so the (1-beta)
    /// factor can never be applied twice (ADR-013).
    double k_prompt() const noexcept { return k * (1.0 - beta_eff); }
};

/// Shannon entropy of a fission-site distribution on the fixed 8^3 mesh (E2b).
double shannon_entropy(const std::vector<double>& mesh);

EigenResult run_eigen(ref::RefTransport& transport, const EigenSpec& spec);

}  // namespace ns::physics

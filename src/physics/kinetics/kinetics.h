// α-mode burst kinetics (E3, 01 §4) — the M3-T1 core.
//
// Quasi-static point kinetics driven by the MC eigenvalue (D3). This is the
// generation-discrete E3a update plus the quantities it needs: ν̄_eff from the
// eigen's per-isotope fission source, the Rossi-α readout (E3c), and the refresh
// validity diagnostic q (R-13). The population and its cumulants are held in a
// log10-safe form so k>1 growth over a full burst (~k^n) never overflows and the
// log-renormalization of 05 §3 is invisible to the cumulants BY CONSTRUCTION.
//
// SCOPE (SYNC-M3): the testable core only. `k` in E3a is **k_prompt**, which is
// derived exactly once by `EigenResult::k_prompt()` (ADR-013) — this module
// CONSUMES it and never re-derives it. The full `AlphaKinetics::run(HydroModel&,
// TallySink&)` of 05 §3 (refresh cadence, initiator schedule C-051, hydro Λ
// rescale, E6 termination) is M3-T3, when those dependencies exist — declaring it
// now would repeat the SYNC-M1 mistake of an unimplemented method.

#pragma once

#include <cstdint>
#include <vector>

#include "ref/ref_transport.h"

namespace ns::physics {

/// ν̄_eff = Σ_i ν̄_i·S_i / Σ_i S_i (E3a, 01 §4), formed exactly as
/// Σ production / Σ fissions from the eigen's per-isotope source — group-correct,
/// no per-isotope ν̄ lookup. Returns 0 if the source produced no fissions.
double nu_eff(const ref::FissionSource& source);

/// Per-isotope share of F_n: ∝ each isotope's ν̄_iΣ_f,i (production) share of the
/// source (01 §4). Sums to 1 (empty source → all-zero). This is what a per-isotope
/// F_n split (Pu vs U-238, 00 §3.5) multiplies F_n by; it must reconcile with
/// `fissions_by_isotope` (tally invariant 6) downstream.
std::vector<double> isotope_fission_shares(const ref::FissionSource& source);

/// Rossi α (E3c) = (k − 1)/Λ. 0 if Λ ≤ 0. `k` here is whichever multiplication
/// the caller means (k_eff for the criticality readout, k_prompt for kinetics).
double rossi_alpha(double k, double lambda_s);

/// Refresh validity diagnostic q (R-13) = |Δk| / (k · generations_since_refresh).
/// The caller auto-halves the refresh interval when q > 0.02 and records max_q;
/// G2 is CONDITIONAL if max_q > 0.05. 0 for a non-positive denominator.
double refresh_q(double delta_k, double k, int generations_since_refresh);

/// α-mode burst accumulator: the E3a generation-discrete update. The population N
/// is held as a linear mantissa × 10^offset and the cumulative fissions/energy in
/// the log10 domain (log-sum-exp), so nothing overflows and renormalization
/// cannot perturb a cumulant. Consumes k_prompt; never re-derives it.
class BurstAccumulator {
public:
    explicit BurstAccumulator(double n0 = 1.0);

    /// One generation n → n+1 (01 §4 E3a), all quantities exact:
    ///   F_n       = k_prompt · N_n / ν̄_eff        (fissions this generation)
    ///   E_n       = F_n · E_f                       (energy this generation)
    ///   N_{n+1}   = k_prompt · N_n + S_next         (S_next = initiator source, often 0)
    /// F_cum, E_cum accumulate in the log domain. Auto-renormalizes the mantissa
    /// if it grows past a safe bound (invisible to the cumulants).
    void step(double k_prompt, double nu_eff, double e_f, double s_next = 0.0);

    /// Rescale the population mantissa toward [1,10) without changing the true
    /// population or any cumulant — the explicit 05 §3 log-renormalization.
    void renormalize();

    int generations() const noexcept { return n_; }
    double log10_N() const noexcept;  // log10 of the current population N_n
    double log10_fissions_last() const noexcept { return log_flast_; }        // log10 F_{n-1}
    double log10_fissions_cumulative() const noexcept { return log_fcum_; }   // log10 F_cum
    double log10_energy_cumulative() const noexcept { return log_ecum_; }     // log10 E_cum
    /// log10 of the yield in kt = log10(F_cum / Φ_kt) (01 §6). Exponentiate for kt.
    double log10_yield_kt(double phi_kt) const noexcept;
    const std::vector<double>& log10_N_history() const noexcept { return log10_n_hist_; }

private:
    double mant_;       // population mantissa (linear); true N = mant_ · 10^off_
    double off_;        // population log10 offset
    double log_fcum_;   // log10 Σ F_n  (−inf ⇒ 0)
    double log_ecum_;   // log10 Σ E_n  (−inf ⇒ 0)
    double log_flast_;  // log10 F_{n-1}
    int n_ = 0;
    std::vector<double> log10_n_hist_;
};

}  // namespace ns::physics

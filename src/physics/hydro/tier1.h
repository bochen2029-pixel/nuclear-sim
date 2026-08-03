// Tier-1 parametric compression (E4 / 01 §5) — the M2-T2 core.
//
// A FIXED compression schedule (BLK-01), not a hydro solution: the implosion is
// prescribed by a smootherstep ramp to a target density ratio. Every layer radius
// scales UNIFORMLY, so the density ratio is radius_scale^(-3) and layer masses are
// conserved by construction — the property the M2-T2 DoD pins to 1e-12.
//
// SCOPE (SYNC-M1): the compression MATH only. The `05 §4` `HydroModel::step /
// apply / serialize` interface + `make_hydro` couple to the burst loop and need
// EnergyField / CheckpointBlob (absent) — that is M3-T2 (Tier-2, energy-conserving
// E4) and M3-T3 (coupling). Declaring them now would repeat SYNC-M1's mistake.

#pragma once

#include "core/geometry/geometry.h"

namespace ns::physics {

/// smootherstep 6u^5 − 15u^4 + 10u^3, clamped to [0,1] (u ≤ 0 → 0, u ≥ 1 → 1).
/// Zero first and second derivative at both ends (the reason it, not smoothstep,
/// is used: a jerk-free radius ramp).
double smootherstep(double u);

/// Tier-1 parametric implosion. `ratio` is the FINAL density ratio ρ_final/ρ_0
/// (C-060); the compression runs over [t0_s, t0_s + t_c_s].
struct Tier1Compression {
    double ratio;   // C-060 final ρ/ρ_0 (> 0)
    double t0_s;    // compression start time
    double t_c_s;   // compression duration (> 0)

    /// (t − t0)/t_c clamped to [0,1]; 0 for t ≤ t0, 1 for t ≥ t0 + t_c.
    double u_of(double t_s) const;
    /// smootherstep(u_of(t)) — the compression parameter s(t) ∈ [0,1].
    double s_of(double t_s) const;
    /// r(s)/r_0 = 1 + (ratio^(−1/3) − 1)·s : exactly 1 at s=0, ratio^(−1/3) at s=1.
    double radius_scale(double s) const;
    /// ρ(s)/ρ_0 = radius_scale(s)^(−3) (mass-conserving uniform compression):
    /// exactly 1 at s=0, `ratio` at s=1.
    double density_ratio(double s) const;
};

/// A copy of `stack` with every layer radius multiplied by `scale`
/// (= `radius_scale(s)`); names / materials / status preserved. Uniform scaling
/// keeps the layers strictly nested, so mass per layer is conserved.
ns::geom::LayerStack compress(const ns::geom::LayerStack& stack, double scale);

}  // namespace ns::physics

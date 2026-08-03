// Tier-2 hydro (E4 / 01 §5, 05 §4) — the M3-T2 core.
//
// Two pieces: (1) the Guderley self-similar converging-shock TIMING (drive
// phase), which sets only the timing profile with a DERIVED t_c (MAJ-09); and
// (2) the energy-conserving snowplow shell ODE (E4), integrated by RK4.
//
// SCOPE (SYNC-M1): the timing + ODE math as testable units. The `05 §4`
// `HydroModel::step / apply / serialize` + `make_hydro`, the E5 energy
// deposition, and v_in derived from HE energy (C-061/C-062) are the M3-T3
// coupling — declaring them now would repeat SYNC-M1's mistake.

#pragma once

namespace ns::physics {

/// Guderley self-similar converging-shock timing (01 §5 Tier-2). R_s(t) =
/// A·(t_c − t)^α_G with t_c DERIVED (MAJ-09), not input; the shock converges
/// (R_s → 0 as t → t_c) so Ṙ_s < 0 (inward). Sets ONLY the timing profile — it is
/// not a solution of the layered-metal problem; validity ends at shell contact.
struct GuderleyTiming {
    double alpha_g;  // C-070 = 0.688377 (spherical, γ = 5/3)
    double t0_s;
    double t_c_s;    // DERIVED = t0 + α_G·R_s0/v_in
    double A;        // = R_s0 / (t_c − t0)^α_G

    /// Launch from initial shock radius `r_s0` (= r_lens_inner) and inward speed
    /// `v_in` (> 0). t_c is chosen so R_s(t0)=r_s0 and Ṙ_s(t0)=−v_in exactly.
    static GuderleyTiming from_launch(double r_s0, double v_in, double t0_s, double alpha_g);

    double R_s(double t_s) const;     // A·(t_c − t)^α_G
    double Rdot_s(double t_s) const;  // −A·α_G·(t_c − t)^(α_G − 1)  (inward, < 0)
};

/// Shell + interior-gas state for the energy-conserving snowplow ODE (E4).
struct ShellState {
    double R;      // shell radius
    double Rdot;   // radial velocity Ṙ
    double E_int;  // internal energy of the gas inside R
};

/// Energy-conserving snowplow shell (E4, 05 §4), integrated by RK4.
///
/// SIGN CORRECTION (M3-T2, implementation contact — README §10): the interior
/// gas pressure drives the shell OUTWARD, so
///     M·R̈ = 4πR²·(P_int − P_drive),   dE_int/dt = Ė_dep − P_int·dV/dt,
/// which gives d/dt(E_int + ½MṘ²) = −P_drive·dV/dt — hence EXACT conservation of
/// E_int + ½MṘ² when Ė_dep = 0 and P_drive = 0 (disassembly). The pre-M3-T2 spec
/// wrote (P_drive − P_int), which does NOT conserve; corrected in 01 §5 / 05 §4
/// (ADR-019).
struct SnowplowShell {
    double mass;      // M (> 0)
    double gamma;     // EOS index (5/3 default, C-070-consistent)
    double p_drive;   // external drive pressure (0 during disassembly)
    double edot_dep;  // energy deposition rate Ė_dep (0 for the conservation test)

    double pressure(const ShellState& s) const;     // P_int = (γ−1)·E_int/V
    double total_energy(const ShellState& s) const;  // E_int + ½·M·Ṙ²
    ShellState deriv(const ShellState& s) const;      // d(state)/dt
    ShellState rk4_step(const ShellState& s, double dt) const;
};

}  // namespace ns::physics

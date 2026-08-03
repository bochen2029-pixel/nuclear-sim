// Tier-2 hydro (E4 / 01 §5, 05 §4). See tier2.h.

#include "physics/hydro/tier2.h"

#include <cmath>

namespace ns::physics {
namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

GuderleyTiming GuderleyTiming::from_launch(double r_s0, double v_in, double t0_s, double alpha_g) {
    GuderleyTiming g;
    g.alpha_g = alpha_g;
    g.t0_s = t0_s;
    // MAJ-09: t_c is DERIVED so that R_s(t0) = r_s0 and |Ṙ_s(t0)| = v_in.
    // Ṙ_s(t0) = −α_G·R_s0/(t_c−t0), so t_c−t0 = α_G·R_s0/v_in.
    const double dt = alpha_g * r_s0 / v_in;  // t_c − t0
    g.t_c_s = t0_s + dt;
    g.A = r_s0 / std::pow(dt, alpha_g);
    return g;
}

double GuderleyTiming::R_s(double t_s) const {
    return A * std::pow(t_c_s - t_s, alpha_g);
}

double GuderleyTiming::Rdot_s(double t_s) const {
    return -A * alpha_g * std::pow(t_c_s - t_s, alpha_g - 1.0);
}

double SnowplowShell::pressure(const ShellState& s) const {
    const double vol = 4.0 / 3.0 * kPi * s.R * s.R * s.R;
    return (gamma - 1.0) * s.E_int / vol;
}

double SnowplowShell::total_energy(const ShellState& s) const {
    return s.E_int + 0.5 * mass * s.Rdot * s.Rdot;
}

ShellState SnowplowShell::deriv(const ShellState& s) const {
    const double area = 4.0 * kPi * s.R * s.R;
    const double p_int = pressure(s);
    // dV/dt = area·Ṙ. Force: M·R̈ = area·(P_int − P_drive) (gas drives outward).
    return ShellState{
        s.Rdot,
        area * (p_int - p_drive) / mass,
        edot_dep - p_int * area * s.Rdot,
    };
}

ShellState SnowplowShell::rk4_step(const ShellState& s, double dt) const {
    const auto axpy = [](const ShellState& a, const ShellState& b, double h) {
        return ShellState{a.R + h * b.R, a.Rdot + h * b.Rdot, a.E_int + h * b.E_int};
    };
    const ShellState k1 = deriv(s);
    const ShellState k2 = deriv(axpy(s, k1, dt / 2.0));
    const ShellState k3 = deriv(axpy(s, k2, dt / 2.0));
    const ShellState k4 = deriv(axpy(s, k3, dt));
    return ShellState{
        s.R + dt / 6.0 * (k1.R + 2.0 * k2.R + 2.0 * k3.R + k4.R),
        s.Rdot + dt / 6.0 * (k1.Rdot + 2.0 * k2.Rdot + 2.0 * k3.Rdot + k4.Rdot),
        s.E_int + dt / 6.0 * (k1.E_int + 2.0 * k2.E_int + 2.0 * k3.E_int + k4.E_int),
    };
}

}  // namespace ns::physics

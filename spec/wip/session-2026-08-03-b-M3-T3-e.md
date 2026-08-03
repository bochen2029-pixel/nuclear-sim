# WIP — session-2026-08-03-b — M3-T3-e (step 3 part 2: emergent self-limiting burst)

Wire SnowplowShell E4 (M3-T2) + E5 deposition into run_burst so a supercritical
demon-core excursion self-terminates: deposited E_n → E_int → R expands →
(via ref_eigen_fn_masscons, M3-T3-d) ρ drops → k drops → E6 quench, finite yield.

## Design (append findings BEFORE acting)
- Disassembly model = SnowplowShell{mass=core_mass_kg, gamma, p_drive=0, edot_dep=0};
  state{R=r0_m, Rdot=0, E_int}. Per gen: E_int += E_n_J (impulse E5), rk4_step over Λ,
  rscale = R/r0_m, geom = compress(geom0, rscale). Energy-conserving (ADR-019).
- Units SI: R m, mass kg, E_int J, P Pa; geom radii cm; rscale dimensionless.
  E_n_J = F_n · e_f_joules (cfg field = C-040·C-917). F_n = 10^log10_fissions_last.
- run_burst refactor: density_ratio derived from rscale (= (r_ref/outer)^3),
  mode-agnostic (replaces comp.density_ratio); geom evolution branches tier-1 vs
  disassembly. Caller passes ref_eigen_fn_masscons so ρ drops as R grows.
- Test: real bare-Pu-sphere supercritical burst → quenched=true, R grew, k dropped,
  finite yield, post_peak>0. May need to tune core_mass/e_f/r0 so it quenches
  within max_generations (the physical energy scale sets the timescale).
- ALSO: tier-1 compression path should use ref_eigen_fn_masscons too (else real
  compression reads backwards) — note for the caller, not necessarily changed here.

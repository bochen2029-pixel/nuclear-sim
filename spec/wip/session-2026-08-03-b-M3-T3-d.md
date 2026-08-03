# WIP — session-2026-08-03-b — M3-T3-d (strategy step 3: emergent self-limiting burst)

Wire the built SnowplowShell E4 (M3-T2) + E5 deposition into run_burst so a
supercritical demon-core excursion self-terminates EMERGENTLY (energy →
expansion → k drops → E6 quench), driven by the real-transport ref_eigen_fn.

## Plan / findings (append BEFORE acting)
- Demon core = a SnowplowShell: E_int fed by deposited E_n each generation
  (E5), R expands per E4 (M·R̈ = 4πR²·P_int, P_drive=0 during disassembly),
  R → geometry radius → real-transport eigen sees a diluted core → k drops.
- SIMPLER than the full 05 §4 HydroModel/EnergyField (SYNC-M1 deferred): couple
  E_n → shell E_int DIRECTLY (like the tier-1 coupling), a CoupleConfig flag to
  enable tier-2 disassembly. No separate EnergyField object yet.
- UNIT CONSISTENCY is the crux: E_n = e_f(MeV)·F_n, mass in kg, R in cm, P in
  Pa/consistent — the SnowplowShell ODE needs one consistent system. The
  physical SCALE (real energy → real expansion timescale) is where the real
  energetics enter; a toy-consistent scale suffices to show self-limiting NOW.
- DoD (core): a supercritical bare-Pu-sphere burst via ref_eigen_fn self-limits
  — grows, peaks, expands (R↑), k_p drops < 1, E6 quenches — with a finite yield
  + healthy post-peak fraction. Emergent, not scripted.
- Shared tree: -e now has C:/nuclear-viz worktree; still commit explicit paths.

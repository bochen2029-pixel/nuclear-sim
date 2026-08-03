# WIP — session-2026-08-03-b — M3-T3-b (02 §3 coupling loop)

Task: the α-mode burst coupling loop, `src/physics/couple/`. Wire kinetics
(M3-T1) + eigen (injectable `EigenFn`) + hydro (M2-T2/M3-T2) + `TallySink` →
`TallyResult` (M3-T3-a), with E6 termination. Toy-testable, no fast4.

## Scope decision (append BEFORE acting)
- CI on M3-T3-a fully green (ubuntu gcc + windows MSVC + gate archive). Building
  on a verified foundation.
- **SYNC-M1 deferral:** 05 §4 `HydroModel` has serialize/restore → `CheckpointBlob`
  (an M5 type, absent) + `at_peak_compression` (M3-T4 initiator). So this core
  does NOT implement the full `HydroModel` virtual hierarchy / `make_hydro` /
  `AlphaKinetics::run(HydroModel&,TallySink&)` — it wires the loop via a minimal
  spec-conformant path and defers those (same pattern M3-T1 used deferring `run`).
- **FissionSource is POSITIONAL** (`by_isotope`, `by_layer`, `mesh[512]`), no
  isotope NAMES. The loop/TallySink maps positional source + isotope-name registry
  + layer radii → named `fissions_by_isotope` + `fission_mesh` (shell_edges from
  radii). Toy test supplies names + radii.
- **Quench (E6):** needs k to fall post-peak. Real disassembly (tier-2 E4, energy
  driven) is heavy; the toy `EigenFn` encodes the k(t) trajectory directly (rise
  then fall) so the loop quenches on `F_n < ε_quench·F_peak` — tests the LOOP +
  TALLY without a full disassembly model. Tier-1 compression (M2-T2) exercises the
  geometry/Λ-rescale side.
- **Split fallback:** if the hydro coupling balloons, land M3-T3-b as the driver +
  TallySink core (9 invariants green) and split the full hydro coupling to a -b2.
- **Constants:** e_f=C-040, Φ_kt=C-041, ε_quench=C-909, Λ0=C-030; all `ns::consts`.

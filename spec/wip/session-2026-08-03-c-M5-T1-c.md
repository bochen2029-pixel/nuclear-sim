# WIP — session-2026-08-03-c — M5-T1-c (run_burst resume + T-resume gate)

## Design (bit-identity via the refresh-boundary trick)
- **Checkpoint at a REFRESH boundary** (`n % refresh_gens == 0`) so the cached eigen (k_p/ν/Λ/
  shares) is RE-DERIVED by re-running the deterministic eigen on the resumed run's first
  iteration (which refreshes because n%refresh_gens==0) — NO EigenResult serialized.
- **Geometry NOT serialized** — reconstruct on resume from geom0 + (disassembly ? shell.R :
  t). geom = compress(geom0, R/r0_m) (disassembly) or geometry_at(geom0, comp, t) (tier-1).
- **BurstCheckpoint** (loop state) = {t, n, supercritical, log_f_peak, gens_since_refresh,
  refresh_gens, marked_radius, last_k, eigen_calls, max_q} + BurstAccumulator::State + ShellState.
- **BurstTally sink** also has state → `State` + `state()` + `load_state()` (keeps ctx_ from a
  prior on_begin; overwrites the numeric accumulators). Resume flow: caller does
  `sink.on_begin(ctx); sink.load_state(state)`, then run_burst(restore) SKIPS on_begin + the
  initial eigen.
- **run_burst gains a `BurstResume*`**: {checkpoint_at (capture at this refresh-boundary gen +
  break BEFORE processing it), captured (out), restore (const BurstCheckpoint*)}. Capture is at
  the TOP of the iteration (before the refresh) so acc has n steps + eigen_calls is pre-refresh.
- **eigen_calls trap:** capture eigen_calls BEFORE the refresh at n; resume restores it; the
  first resumed refresh increments it → matches the original (which also refreshed at n).

## T-resume test
Full run vs interrupted(checkpoint_at=G)+serialize-through-container+restore+resume → assert
the two TallyResults are bit-identical (all fields ==). Use a SIM demon-core disassembly burst
(fast, quenches). G = a refresh boundary near 50%.

## Notes
- LE byte helpers: local in couple.cpp (like kinetics.cpp) to avoid churn.
- If bit-identity debugging threatens the budget for the owner-requested handoff prompt, scope
  down to a clean partial (sink+loop serializers + note) + write the handoff. Owner wants BOTH.

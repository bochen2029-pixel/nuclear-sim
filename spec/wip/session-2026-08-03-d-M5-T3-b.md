# WIP — session-2026-08-03-d — M5-T3-b (store-backed sweep run-loop engine)

Task: the `SweepRunner`/`run_sweep` engine — drive M5-T3-a `make_sampler` points through
an evaluator into the M5-T2 `SweepStore`, keyed by unit_id, with resume + idempotency.
DoD portion: 100-run local sweep completes + resume skips done. `src/app/nukefarm/`.

- Built on the M5-T3-a merge (1145cd9). Merge of M5-T3-b GATED on M5-T3-a CI green (run
  30878157574) — do not merge onto an unverified main. (Local dev on the branch is fine.)
- **unit_id up-front:** `generate_run` computes `compute_unit_id(cfg_canonical_hash(cfg), {},
  cfg.seed)` (studio.cpp:298). To skip done units WITHOUT running, EXPOSE
  `std::string ns::api::studio_unit_id(const StudioConfig&)` in studio.h/.cpp (reuse the
  file-local cfg_canonical_hash) and REFACTOR generate_run to call it (dedup, no drift).
  Re-verify `api.` tests unchanged.
- **ParamSet -> cfg:** `StudioConfig::from_json` reads FLAT dotted keys ("compression.ratio",
  "pit.mass_kg", "materials.pu_ga_delta.Pu240"->pu240_fraction, "initiator.strength_n_per_s",
  "kinetics.generation_time_s_initial", "tamper.scale", "lenses.jitter_ns", "seed"). So the
  runner builds a flat-key JSON from the ParamSet and from_json's it (defaults elsewhere; fixed
  seed => distinct params => distinct unit_id). Demon-core sweep = default base + axes; a
  scenario-file base is the fast4-gated full-device path (not M5-T3-b) — document.
- **Injectable evaluator (M3-T3-b pattern):** `Evaluator = std::function<RunOutcome(const
  StudioConfig&)>`; RunOutcome = {TallyResult tally; std::string run_json; double wall_s}. Tests
  inject a synthetic-tally eval (fast, deterministic, in-memory store) for the 100-run/resume/
  no-double-count cases; ONE real `generate_run` smoke (tiny eigen_batch) proves the wiring.
- **Loop:** per point -> cfg -> uid=studio_unit_id -> store.is_done? skip(resume) : eval ->
  store.record_run({uid, params_json, tally_json, wall_s, done}) [INSERT-OR-IGNORE = no double
  count] -> optional bundle artifacts/<uid>/{tally.json,run.json} -> cursor every
  checkpoint_every_runs. Resume is is_done-driven; the cursor is a progress record.
- **Split:** M5-T3-c = the FS work-queue (pending/claimed/done + leases) + stale-lease requeue +
  the distributed worker + submit/worker/status/resume CLI + the reclaim DoD clause.
- Traps: ASCII test names; explicit git paths; watch CI; gcc finds what MSVC misses.

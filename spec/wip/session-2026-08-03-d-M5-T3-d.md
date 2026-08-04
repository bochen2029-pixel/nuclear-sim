# WIP — session-2026-08-03-d — M5-T3-d (distributed-sweep core: worker + policy + ADR-020)

Task: the `06 §2` distributed sweep — `submit` (enqueue points) + `run_worker` (drain) +
the stale-threshold POLICY + ADR-020 (the t_max_s-fallback amendment). `src/app/nukefarm/`
+ a `SweepStore` query. Built on the CI-verified-pending M5-T3-c (570bc6c).

- **ADR-020 (amendment, do carefully):** `06 §2`'s stale-lease fallback "3× scenario
  `t_max_s`" conflates SIM time (~5e-6 s) with wall-clock → 3× ≈ 15 µs marks every lease
  instantly stale. Replace with a WALL-CLOCK fallback: a fixed default (propose 600 s = 10 min,
  an operational SIM parameter, not a physical constant / not a gate), overridable by the
  worker's `stale_lease_fallback_s`. Protocol: ADR in DECISIONS + edit 06 §2 line 27 + CHANGELOG
  + grep the spec for "t_max_s" in the lease context (only 06 §2 has it there; `03 §4`/tally
  `t_max_s` are the physics field, untouched — MAJ-40).
- **Policy:** `double stale_threshold_s(const SweepStore&, double fallback_s)` = the recent-runtime
  rule. Needs `SweepStore::recent_wall_s(limit)` (new, additive to src/core/store): last N done
  `wall_s`, `ORDER BY rowid DESC LIMIT n`. If >= 10 → 2× median; else → fallback_s.
- **submit:** for each `make_sampler` point → `apply_point` → `studio_unit_id` → `WorkQueue::
  enqueue(uid, payload)` where payload = the flat override JSON (what from_json reads). Add a
  `param_json(ParamSet)` helper (refactor apply_point = from_json(param_json(p)) — DRY) so submit
  + the worker share the payload shape. Skip units already `store.is_done` (resume).
- **run_worker(queue, store, eval, clock, threshold_s):** loop — `reclaim_stale(clock(), threshold)`
  → `claim(clock())` → none? break : `from_json(payload)` → cfg; `is_done`? complete+skip :
  eval → `record_run({uid, payload, tally_json, wall_s})` → `complete`. Returns {processed,
  reclaimed}. Clock injected (deterministic test; production = a std::chrono wall-clock lambda).
- **Tests (nukefarm.):** submit enqueues the sampler points by unit_id (count); run_worker drains
  the queue (stub eval + injected clock → all done, store rows match); the threshold policy
  (>=10 done → 2× median; <10 → fallback); a reclaim-in-worker (a pre-staled claimed unit gets
  reclaimed + run). Keep the anchored sum true.
- **DEFER to M5-T3-e:** the `nukefarm` CLI exe (CLI11 main.cpp). -d is the testable functions.
- Traps: ASCII test names; explicit git paths; watch CI green; gcc `std::filesystem`/`<numeric>`.

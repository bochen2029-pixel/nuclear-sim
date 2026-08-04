# WIP — session-2026-08-03-d — M5-T3-c (FS work-queue library)

Task: the `06 §2` filesystem work-queue LIBRARY — `queue/pending→claimed→done` + lease
files, keyed by unit_id, with an induced-reclaim test proving no-double-count.
`src/app/nukefarm/queue.{h,cpp}`. Built on the verified M5-T3-b main (b571b1f).

- SCOPE (deliberately): the queue LIB only. The distributed `worker` loop + the CLI
  (submit/worker/status/resume) + the stale-threshold POLICY are M5-T3-d. Keeping the
  threshold a PARAMETER (`reclaim_stale(threshold_s)`) makes the lib policy-free → NO ADR
  here (the t_max_s-fallback amendment rides with the policy in M5-T3-d).
- Layout: `<queue>/pending/<unit_id>.json` (params) → `<queue>/claimed/<unit_id>.json` +
  `<unit_id>.lease` (a claim timestamp) → `<queue>/done/<unit_id>.json`. unit_id is the key.
- API: `WorkQueue(dir)` (creates the 3 subdirs); `enqueue(unit_id, payload_json)`;
  `claim(now_s) -> optional<WorkItem>` (move pending→claimed + write lease@now); `complete(
  unit_id)` (claimed→done, remove lease); `reclaim_stale(now_s, threshold_s)` (claimed whose
  now-lease > threshold → back to pending, remove lease) -> count reclaimed; counts per dir.
- TIME is a PARAMETER (now_s passed in), NOT wall-clock read inside — deterministic + testable
  (mirrors the no-Date.now discipline; std::chrono is allowed in C++ but injecting now_s keeps
  the reclaim test deterministic). The lease file stores the claim's now_s.
- Idempotency backstop: two "workers" both finish the SAME unit_id (a reclaim race) → the
  store's record_run INSERT-OR-IGNORE (M5-T2) makes the 2nd a no-op. The queue's job is not to
  LOSE a unit; the store's job is no-double-COUNT. The test drives both.
- Reclaim test (the DoD's "no double-count under induced reclaim", in-process, deterministic):
  enqueue a unit; worker A claims@t0; induce staleness (reclaim@t0+2·threshold) → back to
  pending; worker B claims + records + completes; worker A ALSO records (late finisher) →
  store has 1 row (idempotent), queue done has 1. No double count.
- Filesystem: std::filesystem. Test in a temp dir (temp_directory_path()/unique), remove_all
  before+after (like the store persistence test). Untracked; queue/ is object-storage (02 §2).
- Traps: ASCII test names; explicit git paths; watch CI green; gcc finds what MSVC misses.
  Prefix `nukefarm.` (same frontend module; adds to test_nukefarm) — keep the anchored sum true.

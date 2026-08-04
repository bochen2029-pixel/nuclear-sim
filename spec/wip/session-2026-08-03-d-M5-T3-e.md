# WIP — session-2026-08-03-d — M5-T3-e (the nukefarm CLI exe)

Task: the `nukefarm` CLI — the FIRST `src/app/` executable — CLI11 submit/worker/status/
resume wrapping the done functions. Closes M5-T3. Built on the CI-pending M5-T3-d (5ee728a).

- Structure: `src/app/nukefarm/cli.{h,cpp}` = the subcommand HANDLERS (testable, return data,
  take an injectable Evaluator + a paths); `src/app/nukefarm/main.cpp` = the thin CLI11 dispatch
  (default_evaluator + a real std::chrono clock; formats/prints; exit codes). cli.cpp -> the lib
  (tested); main.cpp -> a new `nukefarm` exe.
- Handlers:
  - `std::int64_t cli_submit(sweep_toml, queue_dir, db)` -> SweepManifest::load + open Queue/Store
    + submit(); returns enqueued.
  - `WorkerResult cli_worker(queue_dir, db, stale_lease_s, eval=default_evaluator)` -> open +
    run_worker(q, store, eval, real-clock, stale_threshold_s(store, stale_lease_s)).
  - `StatusReport cli_status(db, queue_dir)` -> {store done count; queue pending/claimed/done}.
  - resume = submit (skips done) + worker; submit --local = submit + worker.
- Clock: `wall_clock_seconds()` = duration<double>(system_clock::now().time_since_epoch()).count()
  — the ONE std::chrono wall-clock read (run_worker took the clock as a param exactly so this is
  the only place it lives). std::chrono is allowed in C++ (the Date.now ban is Workflow-scripts).
- 06 §2 CLI shape: `submit --sweep <toml> --queue <dir> [--local]`, `worker --queue <dir>
  [--backend gpu]`, `status --sweep <name>`, `resume --sweep <name>`. Adapt to --db <sweep.db>
  for the store path (03 §7 sweep.db). --backend is recorded but the demon-core path is ref/CPU
  (fast4-gated GPU later). Exit codes `06 §5`: 0 ok / 1 general / 2 usage / 3 validation.
- Tests (nukefarm.): cli_submit+cli_worker+cli_status round-trip (stub eval + a temp sweep.toml +
  temp queue + temp FILE db so the two connections share state); cli_status on an empty db -> 0.
  The exe building+linking is itself most of the CLI-dispatch coverage. Keep the anchored sum true.
- CMake: cli.cpp into nukesim_nukefarm; `add_executable(nukefarm src/app/nukefarm/main.cpp)` +
  link nukesim_nukefarm + nukesim_strict. It is a non-test exe (like gpu_perf) — does NOT change
  the ctest count; but cli.cpp's tests do (+N nukefarm.).
- Traps: ASCII test names; explicit git paths; watch CI; gcc <chrono>/<filesystem>.

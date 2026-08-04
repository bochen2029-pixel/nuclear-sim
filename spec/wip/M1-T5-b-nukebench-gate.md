# WIP — M1-T5-b (nukebench gate + gate_report.json)

Session `session-2026-08-04-a`. Split from M1-T5. M1-T5-b = the `nukebench gate` subcommand
+ `gate_report.json` (03 §11). `run`/`diff`(G0c) → M1-T5-c.

## Scope (the M1-T5 DoD)
- `gate_report.{h,cpp}` in `nukesim_nukebench` — the 03 §11 struct (schema_version, gate,
  verdict pass|fail|conditional, gates_toml_sha256, spec_sha256, code/git/dirty, backend,
  device, started/finished, `attempts[]` {attempt, seed, verdict, measurements {k, sigma_pcm,
  k_deviation_pcm}, criteria[] {name, value, op, threshold, pass}, run_dir}) + `to_json` +
  **append-only** merge (load an existing report, append new attempts — MAJ-22 anti-seed-shop).
- The gate RUNNER: `run_gate(gate, repo, backend, seeds?)` — load `data/xs/fast4.json` +
  the gate's scenario + build the single-layer benchmark assembly + `run_eigen` (ref) per
  normative seed at the gate's `[gate.eigen]` config → measurements → evaluate each criterion
  (abs_le/le) → per-seed + overall verdict. (Reuses the M1-T4a-2a gate_probe assembly logic.)
- `src/app/nukebench/{cli.{h,cpp}, main.cpp}` — CLI11 `gate --gate <id> [--report <path>]
  [--backend ref|gpu]`; **`--seed` with `--gate` = exit 2** (usage, 08 §2/06 §1); writes
  `artifacts/gate_reports/<gate>/gate_report.json`; exit 0 (pass) / 4 (gate-fail) / 3
  (validation, e.g. spec_sha256 mismatch) / 2 (usage). A non-test exe (like nukefarm).
- Tests: the runner on a REDUCED eigen (a fast config via a test gate or a batch override)
  → a valid report + correct verdict; the append-only merge; the `--seed`+`--gate` exit-2
  path; a live `nukebench gate --gate G0a` smoke → the honest FAIL report.

## Discipline
- The DoD run records the honest **G0a +2562 / G0b +1581 pcm as a FAIL** (ADR-022) — NEVER a
  fudged pass. `dirty` (uncommitted tree) caps verdict at `conditional` (03 §11). The report
  is committed to `artifacts/gate_reports/` (QC-07 un-ignore).

## Findings
- (start) Base: main @ 371938a (M1-T5-a). Parallel session fixing data/constants.toml
  (task_d36c78af) — independent, no overlap with nukebench.

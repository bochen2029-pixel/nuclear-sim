# WIP — M1-T5-a (gen_gates → gates.toml + the gates loader)

Session `session-2026-08-04-a` (2026-08-04). Split from M1-T5 (oversized: gen_gates +
gates.toml + a whole nukebench CLI + gate_report). M1-T5-a = the DATA + GENERATOR + LOADER
foundation; M1-T5-b = the nukebench runner.

## Scope (bounded)
- `tools/gen_gates/gen_gates.py` — emits `data/benchmarks/gates.toml` (03 §10) for **G0a +
  G0b** (the fully-defined, runnable-now bare-sphere benchmark gates) + `spec_sha256` =
  sha256(spec/08-validation.md) + a `--check` staleness mode (like gen_constants).
- `data/benchmarks/gates.toml` — the generated output (committed).
- `nukesim_nukebench` static lib `src/app/nukebench/gates.{h,cpp}` — the gates.toml loader:
  Gate/Criterion/GateEigen/GatesConfig structs, `load_gates(gates_toml, spec_08)`:
  parse + validate, **resolve each criterion `constant_id` → `ns::consts::get(id)` and check
  it equals the stored `value`** (drift guard; 03 §10 "every value MUST resolve to a
  constant"), + the **spec_sha256 guard** (recompute sha256(08-validation.md) via the in-tree
  core/hash/sha256, fail on mismatch). `find_gate(cfg, id)`.
- Tests: `nukebench.` C++ (load the generated gates.toml; G0a has the C-930/C-931 criteria
  resolving to constants; spec_sha256 mismatch throws; unknown gate throws) + a
  `gen_gates.check` ctest (Python --check, staleness).

## From the spec (scoped this task)
- 03 §10 gates.toml: `[[gate]]` id/title/scenario/seeds + `[gate.eigen]` batch/inactive/active
  + `[[gate.criterion]]` name/op/value/constant_id. spec_sha256 mismatch ⇒ nukebench fails.
- 08 §2 G0a/G0b: eigen C-900 (batch≥1e6, inactive≥50, active≥200); pass iff EVERY normative
  seed [1..5]: |k−1.0000| ≤ 500 pcm + bench_unc (C-930) AND σ ≤ 25 pcm (C-931). Bench unc → 0
  for PUBLIC-DERIVED (report says so).
- Constants exist: C-930 (g0_k_deviation_tolerance=500), C-931 (sigma_pcm=25), C-932 (g0c=100).

## Deferred (M1-T5-b or later)
- G0c (differential `nukebench diff`: a/b/c comparison criteria — belongs with the diff runner).
- G1-G5 (need M2-M7; some have SIM-derived bands recorded in gates.toml notes). gen_gates is
  structured (a gate-definition list) so adding them later is a data edit.

## Discipline
- gates.toml is GENERATED, never hand-edited (03 §10). Criterion values come from the
  constants (C-93x), not invented. spec_sha256 links gates.toml to 08-validation.md so a spec
  change forces regeneration + human review (README §6).

## Findings (append as I go)
- (start)

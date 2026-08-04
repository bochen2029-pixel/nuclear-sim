# WIP — session-2026-08-03-d — M5-T3-a (sweep manifest + loader + axis enforcement + samplers)

Task: the `03 §7` sweep.toml loader + the axis_class/ScoreKind/objective enforcement
(MAJ-35) + the `06 §2` Sampler interface + grid/lhs/random. `src/app/nukefarm/`. The
store-independent planning front of M5-T3. Append findings BEFORE acting.

- SYNC-M5c recorded in PROGRESS. First `src/app/` frontend → `nukesim_nukefarm` static lib
  (CLI in M5-T3-b) + tests link it. D7: no physics logic in src/app; samplers CONSUME
  TallyResult, they don't compute.
- Sampler interface (06 §2) verbatim: next() -> optional<ParamSet>; report(ParamSet, Tally);
  score_kind() -> Coverage|BandCenter (OptimizeExtremum REJECTED at load);
  supports_categorical(). Tally = ns::physics::TallyResult (03 §5).
- Enforcement (03 §7/MAJ-35), all at LOAD:
  - numerical  = SIM knob, unrestricted.
  - uncertainty= constant_id REQUIRED; range ⊆ [ns::consts::get_lo(id), get_hi(id)] (throws on
    unknown/PENDING id → rejected).
  - pedagogical= ONLY {sampler==grid, budget_runs<=100, objective.kind==sensitivity}; any
    deviation rejected. (Subsumes "optimizing samplers + calibrate rejected when pedagogical".)
  - calibrate  = every axis numerical/uncertainty (no pedagogical); scores toward band CENTER.
  - sampler ScoreKind must be Coverage|BandCenter; OptimizeExtremum rejected.
- Constant lookup: ns::consts::get_lo/get_hi(id) (04 §1). get(id) throws for band-only/PENDING;
  use get_lo/get_hi for the range check (they exist for all banded constants).
- Grid discretization (unspecified by spec → documented impl choice, no ADR): points_per_axis =
  floor(budget_runs^(1/n_continuous)), >=1; evenly spaced over [lo,hi] inclusive (k>=2:
  lo + i*(hi-lo)/(k-1); k==1: lo). Cartesian product, total <= budget_runs.
- lhs/random seed: deterministic FNV-1a 64 of the manifest name (stable, reproducible per
  sweep; NOT std::hash which isn't stable). random = budget_runs uniform points; lhs =
  budget_runs strata per axis, one sample/stratum, per-axis permutation (seeded).
- TOML idiom: mirror src/core/scenario/scenario.cpp (toml::parse_file, hard error on unknown
  key — "no silent defaults", 02 §4). Loader does FULL validation.
- Traps: ASCII Catch2 names, NO commas/em-dashes. Prefix `nukefarm.` (one prefix for the whole
  frontend; M5-T3-b adds queue/engine tests to the same exe). Stage explicit paths (viz/+.zcode
  are the -e session's). Keep the anchored-probe sum true in PROGRESS. gcc/CI finds what MSVC
  misses on a new module.

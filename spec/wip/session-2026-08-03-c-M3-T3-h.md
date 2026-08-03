# WIP — session-2026-08-03-c — M3-T3-h (generate_run + emergent outcome + samples)

Append-only, one line per non-obvious finding BEFORE acting (README §5.4b).

## Task
Step 4 final: `generate_run(cfg)` on the M3-T3-g `DemonCoreAssembly` → the emergent
demon-core burst as DATA (tally/run/samples + detonate/reasons/yield). + the §4/§6
scenario_overrides reconciliation.

## Key design (nailed down this session)

- **Compressed-start burst (the crux):** the bare pit (4.585 cm) is SUBCRITICAL; compression
  drives it super. So the burst's geom0 = `assembly.compressed_geometry()` (radius
  r_comp = r0·ratio^(-1/3)), and the eigen is `ref_eigen_fn_masscons(r_ref = r0 UNCOMPRESSED)`.
  Then at the start (geom.outermost = r_comp): density_scale = (r0/r_comp)³ = ratio → the
  compressed pit is super. Disassembly grows the radius past r0 → density < base → quench.
  `CoupleConfig.core_radius0_cm = r_comp` (the shell's initial radius = geom0.outermost).
  NOTE run_burst's internal r_ref (= geom0.outermost = r_comp) only feeds the E3b Λ *ratio*
  (r_ref cancels), so the two r_refs differing is correct, not a bug.

- **Emergence from k (replaces simstub's `faults≤1 && symmetry>0.55`):** detonate =
  report.supercritical_reached && report.quenched && yield_kt > 0. Fizzle = never prompt-critical
  (insufficient compression/mass) → no burst. For the bare core, fizzle is COMPRESSION-driven
  (a low-compression cfg), not detonator-driven (detonator asymmetry is step 5 + fast4).
  detonators/dirs are NOT modelled here (bare core) — generate_run takes cfg only.

- **samples = pitscope's "main course" tap:** serialize each collected GenerationSample —
  n, t_s, lambda_s, k_eff, k_prompt, log10_population, log10_fissions, isotope_shares,
  shell_shares, refreshed, q, sites[{pos:[x,y,z] cm, group, isotope, layer}] (ns::ref::FissionSite
  = {Vec3 pos, int group, int isotope, int layer}). Collect all; cap/stride the emitted set.

- **to_json(GenerateRunResult):** {tally (03 §5, embed via json::parse(physics::to_json)), run
  (03 §6, embed via api::to_json), samples, detonate, reasons, yield_kt, k_eff_peak,
  non_canonical}. The JS binding adds the presentation closures (flux/compression) — C++ is
  DATA only (see the viz-seam memory).

- **RunProvenance fill (physics-determinable only):** run_id (deterministic from cfg), unit_id
  (compute_unit_id(cfg-hash, overrides, seed)), scenario_sha256 (cfg-hash), data_hashes (real
  sha256 of the generated xs+material JSON), seed, backend "ref", device "". Environment fields
  (code_version/spec_version/git/dirty/started/finished) left empty — the frontend/binding fills
  them (they are deployment, not physics). Documented.

- **§4/§6 reconciliation:** add `std::vector<std::string> scenario_overrides` to RunProvenance
  (before seed, per the unit_id "overrides ‖ seed" adjacency) + to_json/parse (optional, default
  empty) + the `03 §6` example (`"scenario_overrides": []`) + CHANGELOG (clarification, no ADR) +
  MAJ-40 grep the spec for run.json. generate_run passes overrides (empty for the bare-core
  canonical run; the binding supplies nonCanonicalFlags).

## Empirical RISK (calibrate, do NOT tune-to-pass — trap #3)

- **Burst gen-count / test speed:** the SnowplowShell mass = cfg.pit_mass_kg (REAL, honest — a
  heavier pit disassembles slower). A strongly-super cfg grows fast (~100-300 gens to peak) and
  disassembles in ~few gens once energy deposits — likely fast enough. Use batch ~1500 + refresh
  ~8 for the burst eigen to keep it ~10-20 s. MEASURE the gen count; set max_generations / t_max
  from the measurement, not a guess.
- **Straddle criticality with margin:** detonate test = a clearly-super cfg (mass 9, comp 2.5);
  fizzle test = a clearly-sub cfg (mass 4, comp 1.0, cap max_generations ~50 for speed since a
  subcritical burst never quenches). Pick cfg points that BRACKET criticality — never adjust the
  SIM xs to make a specific k assertion pass.
- If calibration balloons: split M3-T3-h (structure + §4/§6 first, the quench test second) or
  hand off with the measured findings. Quality over a rushed/flaky burst test.

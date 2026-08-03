# WIP — session-2026-08-03-c — M3-T3-g (cfg→demon-core assembly builder + evaluate)

Append-only, one line per non-obvious finding BEFORE acting (README §5.4b). Folded into
SESSIONS at END, then deleted.

## Task
Step 4 part 2, split again: M3-T3-g = the cfg→assembly builder + `evaluate(cfg)` gauge (low
risk, cleanly testable); M3-T3-h = `generate_run` + emergent outcome + samples/sites (higher
risk — needs the assembly to straddle criticality). Both in `src/api/studio.{h,cpp}`.

## Findings / decisions

- **M3-T3-f CI GREEN** (windows-2022 + ubuntu, run 30849583207) before starting M3-T3-g — the
  session's watch-CI-green-before-layering discipline (src/api/ is a new module; gcc/Linux
  catches what MSVC misses).

- **Why split evaluate from generate_run:** `evaluate` is low-risk (returns k, testable by
  monotonicity — no criticality-crossing assertion). `generate_run`'s emergent detonate/fizzle
  DEPENDS on the synthetic xs making the assembly straddle criticality (bare subcritical,
  compressed supercritical) — riskier, may need xs calibration. Landing evaluate green first
  shrinks the blast radius. (Handoff: "a clean half beats a rushed whole.")

- **Demon-core = BARE Pu sphere (single layer), NOT the full 5-layer device.** The viz CANONICAL
  cfg (scenario.js) describes the full Trinity device (initiator/pit/tamper/boron/pusher), but
  the full-device transport needs fast4 (blocked) AND per-layer density scaling (ref_eigen_fn_
  masscons is UNIFORM — bare-core only, §7.1). So the fast4-independent path is the bare pit.
  tamper.scale + lenses.jitter_ns are full-device knobs → RECORDED in StudioConfig but NOT
  modelled in the bare-core path (documented). compression.ratio + pit.mass_kg + Pu240 drive it.

- **Assembly geometry (honest, C-102-consistent):** density 15.23 g/cm³ (pu_ga_delta card,
  DERIVED from mass 6.15 kg + OD 9.17 cm = r 4.585 cm; the commonly-quoted 15.6 is 2.4% off,
  recorded not resolved — MAJ-28). r0 = (3·m/(4πρ))^(1/3): 6.15 kg → 4.585 cm (the canonical pit
  radius, verified). So evaluate on canonical cfg builds the real-sized pit.

- **Emergence source:** a bare 4.585 cm Pu sphere is SUBCRITICAL (the historical demon core was
  bare-subcritical; RealSphere(6cm)≈0.74). compression.ratio 2.2 (ρ×2.2 via ref_eigen_fn_masscons,
  radius ×2.2^(-1/3)) drives k up — the detonate-vs-fizzle emerges from compression+mass+Pu240,
  NOT a hardcoded rule. Whether canonical cfg crosses k_prompt=1 depends on the SIM xs magnitude;
  the emergence TEST checks the ORDERING (compression raises k, Pu240 lowers it), not a fabricated
  k target — never tune xs to pass a specific assertion (trap #3).

- **Construction is FILE-BASED** (RealSphere pattern): FewGroupXS/MaterialLib build only from
  files (no in-memory ctor). `DemonCoreAssembly` = an RAII temp-dir holder (create in ctor, remove
  in dtor). Per-call I/O is negligible vs the MC eigen. In-memory core ctors are a possible future
  refinement (out of scope — would touch core modules).

- **evaluate applies compression via the mass-cons adapter:** geometry = scale_radii(sphere,
  ratio^(-1/3)); eigen = ref_eigen_fn_masscons(materials, xs, spec, seed, r_ref=r0). The adapter
  scales ρ by (r0/r)³ = ratio. Reuses M3-T3-d/e exactly.

- **`ready` = prompt-supercritical (k_prompt ≥ 1)** — the physically-honest burst-ignition gauge
  (simstub used k_eff≥1, the fake version). Expose both k_eff and k_prompt so the UI can reinterpret.

- **SIM xs (permitted, tagged — NOT fabricated cited data, §7.4):** Pu239 fissile (RealSphere-like:
  nu 2.9, σ_f 1.4, σ_c 0.15, σ_s 4.0, β 0.0021, identity transfer, status SIM); Pu240 a net poison
  (lower σ_f, higher σ_c) so higher Pu240 → lower k. cite "synthetic test medium — not physical
  data", status SIM — cited-real when fast4 lands. Ga (3.35 at%) omitted in the bare-core SIM
  stand-in (documented); the real 4-isotope card comes with fast4.

- **eigen spec for the gauge:** batch ~4000, inactive 8, active 15, h_tol 0.05 (M3-T3-d test
  scale) — a usable gauge in ~1-2 s; sigma_pcm reports the noise. The UI debounces (real physics
  costs a solve, unlike simstub's instant formula).

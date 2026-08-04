# DECISIONS — Append-Only ADR Log

> Decisions here are final unless amended via `README.md` §6 (amend = NEW entry referencing the old one; never edit history). Check this log before proposing any design change.

## Format

```
## ADR-nnn — <title> — YYYY-MM-DD
- **Status:** accepted | superseded by ADR-xxx
- **Context:** why
- **Decision:** what
- **Consequences:** what it constrains
```

## Logged decisions (spec v0.1 — authored 2026-08-02)

## ADR-001 — Two-tier ref/gpu implementation with differential validation — 2026-08-02
- **Status:** accepted
- **Context:** GPU event-based MC is hard to debug; correctness must be establishable independently of performance work.
- **Decision:** D1 — CPU history-based oracle built first; GPU backend must match it statistically (G0c).
- **Consequences:** All physics features land in ref/ first. No gate may be claimed on gpu-only code.

## ADR-002 — Analytic sphere tracker first; OptiX deferred to Stage 3 — 2026-08-02
- **Status:** accepted
- **Context:** Stages 0–2 are concentric spheres (closed-form intersections); cloud GPUs (H100/H200) lack RT cores.
- **Decision:** D2 — `AnalyticSphereTracker` is the permanent oracle and cloud path; `OptiXCSGTracker` only for M6 lens geometry.
- **Consequences:** OptiX is a build-time optional component; cloud builds exclude it.

## ADR-003 — α-mode (quasi-static eigen refresh) as default kinetics — 2026-08-02
- **Status:** accepted
- **Context:** True time-dependent MC (TD-mode) is expensive; quasi-static approximation is valid while geometry evolves slower than a shake.
- **Decision:** D3 — E3a generation-discrete update with eigen refresh every M gens + on hydro updates; TD-mode is interface-only stretch.
- **Consequences:** G2 gate runs on α-mode. TD-mode cannot substitute for gate evidence.

## ADR-004 — Few-group (4-group) cross sections, no ENDF parsing in v1 — 2026-08-02
- **Status:** accepted
- **Context:** CE data pipeline is weeks of work before any physics; benchmarks are fast-spectrum so few-group can meet ±500 pcm.
- **Decision:** D4 — curated cited 4-group JSON; schema permits future extension to more groups.
- **Consequences:** R-1 tracks the accuracy risk; group-count changes require ADR.

## ADR-005 — Hydro capped at Tier-2 (thin-shell + Guderley timing); asymmetry phenomenological — 2026-08-02
- **Status:** accepted
- **Context:** Full 3D hydro/RT modeling is out of scope and would exceed public-data fidelity (`00 §3.2`).
- **Decision:** D5 + `05 §4` — Tier-1/2 in scope, Tier-3 1D Lagrangian stretch, jitter→asymmetry linear model only.
- **Consequences:** No CFD code may be introduced; c_a is a calibrated SIM constant.

## ADR-006 — One library, four frontends; frontends contain no physics — 2026-08-02
- **Status:** accepted
- **Context:** User requirement: interactive studio AND offline cinema AND headless bench/cloud farm from one codebase.
- **Decision:** D7 — `nscore` is GUI-free, deterministic, serializable; parity gate G0c-ext binds studio to CLI.
- **Consequences:** Any physics logic found in `src/app/` is a defect.
- *Note (ADR-012): the parity gate was promoted to first-class **G5**; references to "G0c-ext" read as G5.*

## ADR-007 — Sweep objectives restricted to calibration/sensitivity — 2026-08-02
- **Status:** accepted
- **Context:** Boundary discipline (`00 §3.4`): batch studies target public benchmark bands, not design optimization.
- **Decision:** D8 — `objective.kind ∈ {sensitivity, calibrate}` enforced by schema; MCTS/Bayes plugins must consume only these objective types.
- **Consequences:** Extending objective kinds requires ADR + explicit boundary review.

## ADR-008 — Checkpoint/resume is mandatory (D9) — 2026-08-02
- **Status:** accepted
- **Context:** Spot cloud instances get preempted; Philox counter-based RNG chosen specifically to make resume exact.
- **Decision:** D9 — full-state checkpoints; T-resume gate (kill→resume→bit-identical) required for M5-T1.
- **Consequences:** No cursor-based RNG anywhere; checkpoint schema versioned per section.

## ADR-009 — GPU-first development on confirmed dev hardware — 2026-08-02
- **Status:** accepted
- **Context:** Dev machine confirmed (user direction + direct verification): RTX 4070 Ti SUPER (sm_89, 16,376 MiB, RT cores, driver 610.47), CUDA 13.1, MSVC 14.44, CMake 4.3.3, Python 3.13.2, OptiX SDK 9.1.0, vcpkg at C:\vcpkg, Windows 11. Sibling CUDA projects on this machine prove the toolchain. Original spec assumed CUDA 12.4 / OptiX 8.0 / unspecified GPU and sequenced GPU work as a late port (M4 after M1–M3).
- **Decision:** (1) Toolchain pins updated to the confirmed versions (CUDA 13.1, OptiX 9.1.0, vcpkg C:\vcpkg, sm_89 primary + 80/90 cloud). (2) **GPU-first:** every capability ships on the CUDA backend in the same milestone it ships on CPU; `ref/` remains the correctness oracle (D1) but is not the product; default `--backend gpu`. (3) M4 retimed: M4-T1/T2 run in parallel with M2/M3 (start after M1-T1). (4) G4 perf targets re-based to the 4070 Ti SUPER. (5) D2 unchanged in structure — analytic tracker stays oracle + cloud path (H200 lacks RT cores); OptiX 9.1 fully usable locally at M6.
- **Consequences:** Milestone parallelism increases (PROGRESS.md claims must respect the M4 note); VRAM budgets recorded per run (`--vram-report`); no CPU-only milestone deliverables except `ref/` internals. *Amended by ADR-012 (G4 numbers re-derived in gate redesign; CUDA/OptiX version strings now live ONLY in 12-deployment §1).*

## ADR-010 — Staged simulation clock (retro-documents D6) — 2026-08-02
- **Status:** accepted
- **Context:** Review triage (MAJ-02) found D6 was stated in `02 §1` without an ADR; every Dn MUST have exactly one ADR.
- **Decision:** D6 — three phases (BURST / HYDRO / FIREBALL), a global `SimClock` mapping wall/render time to sim time per phase, no fixed-dt integration across phases; t=0 = outermost-HE initiation (03 §4).
- **Consequences:** checkpoints carry phase (03 §8 §1); `11 §4` requires checkpoint round-trips at every phase boundary; UI timebase switch (10 §Panel 6) binds to these phases. `tools/verify/decision_index` now asserts the D↔ADR mapping in ctest.

## ADR-011 — Repository hosting and CI target — 2026-08-02
- **Status:** accepted — **RESOLVED BY OWNER 2026-08-02**
- **Context:** M0-T6 requires CI; no remote was specified. Hosting visibility for an educational nuclear-device simulator built from public sources is an owner decision (MAJ-38).
- **Decision (owner):** **public GitHub repository + MIT license.** CI via GitHub Actions (free for public repos). Owner creates the repo (M0-T6-b); sessions must not create/push public repos autonomously.
- **Consequences:** M0-T6 in scope as (a) local CI + committed workflows, (b) owner-gated repo creation + push + green Actions. `12 §5` public-repo rules apply (no credentials, no access-restricted data files, ever). Artifact governance per R-21.

## ADR-012 — Omnibus triage of the three external spec reviews — 2026-08-02
- **Status:** accepted
- **Context:** Three independent adversarial reviews (`spec/reviews/REVIEW_2026-08-02.md` [BLK/MAJ set], `spec/reviews/bak/REVIEW_2026-08-02.md` [A/B/C/D set], `spec/reviews/bak/REVIEW_2026-08-02_other-agent_0129.md` [lettered set]) — all verdicts AMEND-THEN-SHIP. Convergent findings treated as ground truth; conflicts adjudicated below.
- **Decision:** ALL findings accepted with the following adjudications where reviews conflicted:
  1. **E1c estimator (B-09 vs BLK-05):** full **implicit capture** adopted (BLK-05's fix) — matches the spec header and modern k-eigenvalue practice; the analog-with-implicit-absorption variant rejected.
  2. **G1a repair (B-01 vs MAJ-14 vs F2):** gate the published MASS fraction directly via radius bisection + derived SIM k band (MAJ-14), PLUS a report-only self-consistency window vs the sim's own committed value (B-01). Review 3's harder [0.88,0.97] floor rejected as over-aggressive given one-group-model uncertainty.
  3. **Anisotropy magnitude (C-24 vs MAJ-31 vs F1):** claimed pcm magnitudes (hundreds vs thousands) are UNCERTAIN — NEITHER number adopted. Adopted the shared fix: transport-corrected P0 (mu_bar REQUIRED, xs schema v2) + measure-first + CONDITIONAL-gate fallback (MAJ-31). No tolerance relaxation without measurement + ADR.
  4. **GPU determinism (BLK-11 vs E1):** deterministic design adopted (prefix-sum slots, parent-derived streams via `rng::fork`, fixed-point/fixed-tree reductions) — preserves D9 bit-identical same-backend resume. The "downgrade to statistical" alternative rejected. Cross-backend remains statistical-only (MAJ-48/17).
  5. **Scope boundary (G1 vs MAJ-36):** interactive single-point counterfactual exploration IN scope (non_canonical-marked, never gate evidence); automated search over physical axes toward objectives OUT — enforced mechanically by `axis_class` + `ScoreKind` (MAJ-35), not by objective labels (B-13 subsumed).
  6. **Claim races (B-15 vs MAJ-40):** branch + claim-commit with timestamp/lexicographic tiebreak (no human), plus depends_on column + ready-queue + WIP journals + mandatory impact analysis.
  7. **M1-T4 (BLK-14/C4):** split into M1-T4a (open-literature PUBLIC-DERIVED models, one-time provenance-tracked multigroup-generation carve-out to D4) + M1-T4b (owner-gated ICSBEP). Autonomous sessions MUST NOT request handbook access.
  8. **Ready-queue vs single NEXT ACTION (D1-review3 vs preserve-E):** BOTH kept — one recommended NEXT ACTION + a ready-queue of runnable tasks.
  9. Gate thresholds: every threshold now carries a constant ID + derivation (appendix §5, C-930–C-946); G2 yield band = [16.6, 26.8] kt (envelope of published estimates incl. Selby upper edge, MAJ-13).
- **Consequences:** spec v0.2. Every file amended; see CHANGELOG 2026-08-02 omnibus line for the full file list. Reviewer "preserve" lists (all three) honored — D1/D2/D7/D9 structure, router+living-state design, verify-first, green-state rule, single NEXT ACTION, status tags, bands-not-points, Bethe–Feynman quarantine, E3a bookkeeping core, layer geometry table, amendment protocol all unchanged in substance. Closed risks recorded in `13-risks.md` (R-14/15/16/18). Findings not explicitly discussed here were accepted as written and applied to the sections they name.

## ADR-013 — ν̄ is TOTAL; k_prompt is derived exactly once — 2026-08-03
- **Status:** accepted
- **Context:** v0.2 `01 §1` and C-020/C-021 labelled ν̄ as PROMPT, while `01 §4` stated "the MC eigen returns total-ν̄ k_eff and the loader applies the β correction". Both cannot hold. If the xs `nu` is prompt, the eigenvalue already IS k_prompt and multiplying by (1−β) double-counts: ~200 pcm for a Pu core, **~650 pcm for Godiva** (β(U-235) = 0.0065) — larger than G0a's entire ±500 pcm tolerance. Compounding it, C-022 existed only for Pu-239, so the correction was not even computable for Godiva. The ICSBEP/open-literature benchmark value k_eff = 1.0000 is a total-ν̄ quantity, which settles which convention the gates need. Found in QC review of the ADR-012 triage pass (QC-01).
- **Decision:** (1) All ν̄ constants and all `nu` arrays in xs sets are **TOTAL** ν̄ (prompt + delayed). (2) The MC eigenvalue is `k_eff` on total ν̄ — the benchmark-comparable quantity, and the only one G0a/G0b/G1a/G1b compare. (3) `beta` becomes a REQUIRED per-isotope scalar in xs schema v2; C-022b–C-022e add U-235, U-238, Pu-240, Pu-241. (4) `k_prompt = k_eff·(1 − β_eff)` with `β_eff = Σ_i β_i S_i / Σ_i S_i` is derived **once**, downstream of the eigen solve, and consumed **only** by E3a kinetics. `EigenResult` gains `beta_eff`. (5) `tally.json` reports both `k_eff` and `k_prompt`. (6) v1 approximates β_eff by the source-weighted β, omitting the delayed-neutron importance factor (a few percent in fast metal systems) — recorded as a known approximation.
- **Consequences:** the eigen solver MUST NOT return a β-corrected k; a k-comparing gate consuming `k_prompt` is a defect; prompt-critical is `k_eff = 1/(1−β_eff)`, not `k_eff = 1`, and `02 §3`'s `supercritical_reached` flag tests `k_prompt ≥ 1`. A prompt-only dataset is numerically undetectable, so the convention is enforced by requiring `beta` and by M1-T4a's data card naming the source evaluation (R-22).

## ADR-016 — Godiva benchmark model adopted from JEFF Report 16 / CSEWG F5 — 2026-08-02
- **Status:** accepted
- **Context:** `08 §1` required M1-T4a to finalize the Godiva model from OPEN literature and log one ADR per benchmark (MAJ-46). BLK-14 forbids an autonomous session from applying for the ICSBEP Handbook (access-restricted via the NEA Data Bank, named users, intended-use statement), so the model must come from openly published descriptions.
- **Decision:** adopt the specification in **JEFF Report 16, Annex 3** (OECD/NEA, openly published, <https://www.oecd-nea.org/science/docs/pubs/jeff_16.pdf>, retrieved 2026-08-02), which reproduces the **CSEWG Benchmark Book, BNL 19302, ENDF 202, Rev. 11-81** specification for fast benchmark **F5**. The primary datum is atom densities in nuclei/b-cm — U-235 0.045000, U-238 0.002498, U-234 0.000492 — at radius **8.741 cm**, with measured eigenvalue **1.000 ± 0.001**. Everything else (ρ = 18.7421 g/cm³, mass = 52.431 kg, wt% 93.7112/5.2686/1.0202) is DERIVED from those and is therefore an independent check on the approximate figures the spec previously carried, all of which it reproduces. Model tagged `status = "PUBLIC-DERIVED"`.
- **Consequences:** `data/materials/u_godiva.json` and `data/scenarios/godiva.toml` are normative for G0a. Atom densities are primary: a future edit that changes the derived density or mass without changing the atom densities is a defect, and `tests/unit/test_benchmarks.cpp` recomputes all of them from the committed material so the data card cannot drift. The ±0.001 is an EXPERIMENTAL uncertainty, not a verified benchmark-model uncertainty, so `08 §1`'s operational rule takes it as **0 pcm** for gate purposes and the gate report must state so. M1-T4b may replace this model with Handbook sheet values (owner-gated); doing so is a gate change under §6.

## ADR-017 — Jezebel benchmark model adopted from JEFF Report 16 / CSEWG F1; Ga split explicitly — 2026-08-02
- **Status:** accepted
- **Context:** as ADR-016, for Jezebel. Two additional questions had to be settled. (1) The published model gives natural Ga as a single entry, but `03 §3` forbids the LOADER expanding natural abundance — a rule that exists to force an explicit, recorded choice rather than a silent default. (2) Open sources disagree on whether Jezebel's "4.5% Pu-240" is atom percent or weight percent, while `08 §1` says weight percent.
- **Decision:** adopt **CSEWG F1** via JEFF Report 16 Annex 3 (same source and retrieval date): atom densities Pu-239 0.037050, Pu-240 0.001751, Pu-241 0.000117, Ga(nat) 0.001375 at radius **6.385 cm**, measured eigenvalue **1.000 ± 0.002**. (1) Natural gallium is split **Ga-69 60.108% / Ga-71 39.892%** (IUPAC standard isotopic composition) as an explicit author choice recorded in `data/benchmarks/jezebel.md`; the split reproduces the natural-Ga mass contribution to seven digits, so the derived density is unchanged by it. C-915 (natural Ga) stays `use = "crosscheck"` and is not used. (2) The ambiguity is **moot**: computed from the atom densities, Pu-240 is 4.4710 wt% of the material, 4.5171 wt% of the plutonium, and 4.4990 at% of the plutonium — all three round to 4.5%. `composition_check.Pu240_wt_pct_of_Pu` carries the second, because that is what the loader recomputes.
- **Consequences:** `data/materials/pu_ga_jezebel.json` and `data/scenarios/jezebel.toml` are normative for G0b. The `03 §3` prohibition on a `jezebel` scenario referencing `pu_ga_delta` is now backed by real data on both sides: 4.47 wt% Pu-240 versus 1.0 wt%, a 4.5× difference that moves k far beyond G0b's tolerance while looking unremarkable in a diff. Same PUBLIC-DERIVED uncertainty caveat as ADR-016.

## ADR-015 — `constants.data.toml` gains `[[band]]`; gate bands carry no nominal — 2026-08-02
- **Status:** accepted
- **Context:** `03 §1`'s schema is a scalar `value` with an optional `lo`/`hi` band around it, plus `[[registry]]` for multi-entry tables. Authoring the strict file at M0-T3-a showed it cannot represent two whole appendix sections. Appendix §4 is largely **tuples** — C-900 "1e6 / 50 / 200", C-901 "1e-3 / 1e-4", C-902, C-908 "8×8×8" — and appendix §5 is largely **pure bands with no nominal**: C-933 "[0.68, 0.88]", C-935, C-936, C-937, C-940 "[16.6, 26.8] kt", C-941, C-942. Forcing a gate band into `value`+`lo`+`hi` requires inventing a midpoint. That midpoint is not a physical quantity — the centre of G2's yield band is not a predicted yield — and once emitted as `g2_yield_band` it is one autocomplete away from being compared against, which is exactly the class of error `11 §5` says only arithmetic catches. Tuples already have a home (`[[registry]]`); bands do not. Found by implementation contact, which `README §10` names as the source of post-v0.3 spec change.
- **Decision:** the strict file has **three** array types. (1) `[[constant]]` — a scalar `value`, optionally with a meaningful `lo`/`hi` around it; the generator emits `_lo`/`_hi` companions and a `static_assert(lo <= value <= hi)`. (2) `[[band]]` — `lo`/`hi` and **no** `value`; supplying one is a hard generator error. Emits `_lo`/`_hi` and `static_assert(lo < hi)`, and nothing a consumer can mistake for a nominal. (3) `[[registry]]` — a named table of numbers or `[lo, hi]` pairs, for tuples and stream tables. Every entry of every type carries `id`, `name`, `status`, `cite` and `appendix_text`; missing `cite` or `status` is fatal, per M0-T3's DoD.
- **Consequences:** `03 §1` documents all three (amended). `M1-T5`'s `gen_gates` reads gate thresholds from `[[band]]` entries and must not synthesise a nominal from one. A threshold that genuinely has a nominal plus a tolerance stays a `[[constant]]` with `lo`/`hi` — C-930 (500 pcm) is a tolerance, not a band, and is correctly a constant. `[[band]]` entries emit no bare identifier, so `g2_yield_band` does not exist as a compilable name; only `g2_yield_band_lo`/`_hi` do.

## ADR-014 — Repository created under owner direction; who may publish — 2026-08-03
- **Status:** accepted (amends ADR-011; ADR-011 itself is unchanged — this log is append-only)
- **Context:** ADR-011 recorded "Owner creates the repo (M0-T6-b); sessions must not create/push public repos autonomously." On 2026-08-03 the owner directed a session, in chat and unambiguously, to create the public repository on their behalf and push, restating the MIT/public decision and delegating the `.gitignore` contents. The repository now exists at <https://github.com/bochen2029-pixel/nuclear-sim>. ADR-011's constraint should not be read as violated, but it also should not be left ambiguous for the next session that wants to publish something.
- **Decision:** the rule is about **authority, not mechanism.** A session MAY create or push to a public repository **only** when the owner has directed it for that specific action, in that conversation. Standing permission does not exist and is not inferable from the fact that this repository is already public. Specifically: pushing commits to `origin/main` on THIS repository is ordinary work from M0-T2 onward and needs no further permission; creating a NEW public repository, changing this repository's visibility, publishing releases, or pushing to any other remote each require a fresh owner instruction. Before any first push to a public remote, a session MUST run a credential/secret scan and record the result in `SESSIONS.md` (done for the initial commit: zero credential hits; all matches were historical prose or tiktoken counters).
- **Consequences:** M0-T6 loses its owner gate and becomes a single task (`07`). `PROGRESS.md`'s M0-T6-b blocker is resolved. The `12 §5` public-repo hygiene rules now apply to every commit, not prospectively.

## ADR-018 — Sandbox-mode interaction extensions (detonator selection, clip plane, neutron trails, time-warp markers) — 2026-08-02
- **Status:** accepted
- **Context:** owner direction (2026-08-02, brainstorm with session-2026-08-02-e): the end-state studio should let a user see the whole device in 3D with cross-sections, choose which detonators fire and when, adjust composition, and have **physics — not scripting — decide fizzle vs burst**, with the explosion viewed from a free virtual camera. Most of that is already specified (`10` panels, `09 §4` staged clock, `06` nukestudio). Four pieces were not: per-detonator selection (`10 §1` has only the scalar `lenses.jitter_ns` axis), a free clip plane (`10 §2` has cutaway + exploded view but no arbitrary clipping), neutron-trail visualization (`09 §4` BURST phase has points + heat map, no trajectories), and timeline event markers (`10 §6` has timebase/scrub, no markers). Recorded as an ADR because `03 §4`/`09`/`10` are schema + behavior.
- **Decision:** all four land as interaction/visualization-layer extensions; no physics, no constants, no gate changes.
  1. **Per-detonator selection:** `03 §4` `[lenses]` gains optional `[[lenses.detonators]]` entries (`enable`, `delay_s`); absent = all fire on time (the canonical form). Desync maps onto the EXISTING jitter→c_a asymmetry pipeline (`01 §5`, C-071/C-072 rule, ADR-005) — no new physics model. `enable = false` extrapolates c_a beyond the calibrated jitter envelope and raises the existing "extrapolation beyond public data" badge (`10 §1`; precedent: compression > 2.5).
  2. **Clip plane:** a world-space plane applied to shell rasterization AND the volume raymarch (`09 §2`/`§4`). A pure view transform — it reveals computed fields and adds nothing, so the `09 §4` physical-honesty rule is unchanged. View state, not scenario data: it never enters `canonical_hash()`.
  3. **Neutron trails:** the BURST phase MAY render a deterministic, seed-selected sample of transported histories as fading trajectory trails (`09 §4`). Trails ARE computed transport data (exact history segments), which `09 §4` permits — the rule forbids aesthetic noise, not computed data. Selection is seeded from the run seed, so trails are bit-identical across replays.
  4. **Time-warp markers:** the Timeline panel gains markers at computed state transitions — HE initiation (t=0, `03 §4 [time]`), shock convergence, prompt-critical crossing (`k_prompt ≥ 1`, ADR-013), peak power, quench (E6). Markers derive from kinetics/hydro state, never hand-placed. Scrub remains paused-runs-only (`10 §6` rule unchanged).
- **Consequences:** the safeguards are unchanged and restated: `axis_class`/`ScoreKind` (MAJ-35, ADR-012 adj. 5) still forbid automated search over these physical inputs — detonator selection is interactive single-point exploration (`00 §2`), and every non-default detonator configuration is `non_canonical` and never gate evidence (`00 §2`, `10 §Rules`). The canonical scenario is byte-unchanged. Enrichment is NOT a new axis: the Pu-240 fraction row already exists (`10 §1`); a U-235 enrichment row is deferred until `fast4` isotope coverage exists (M1-T4a-2 blocker). New task **M7-T5** (`07`) implements all four, depending on M7-T3 + M6-T3; the `10 §1` widgets table gains a detonator row resolved by a new `[ui."lenses.detonators"]` annotation (range = the `delay_s` envelope in schema units, matching the 0–1000 ns jitter axis).

## ADR-019 — E4 shell-motion sign correction: interior gas pressure drives the shell outward — 2026-08-03
- **Status:** accepted
- **Context:** M3-T2 (Tier-2 hydro) implementation contact (README §10). `01 §5` E4 wrote the shell EOM as `M·d²R/dt² = 4πR²·(P_drive − P_int)` and claimed `E_int + ½MṘ²` conserved at Ė_dep = 0. That is inconsistent: with that force, `d/dt(E_int + ½MṘ²) = MṘR̈ − P_int·dV/dt = 4πR²Ṙ(P_drive − P_int) − P_int·4πR²Ṙ = 4πR²Ṙ(P_drive − 2P_int)`, which vanishes only if P_drive = 2P_int — not generally, and not in the disassembly phase (P_drive = 0) the claim and the M3-T2 DoD target. The (P_drive − P_int) sign also makes the interior gas DECELERATE its own expansion, which is unphysical.
- **Decision:** the shell equation of motion is `M·d²R/dt² = 4πR²·(P_int − P_drive)` — the interior gas pressure P_int drives the shell OUTWARD; an external drive pressure P_drive opposes it. Then `d/dt(E_int + ½MṘ²) = −P_drive·dV/dt`, so with Ė_dep = 0 and P_drive = 0 (disassembly) `E_int + ½MṘ²` is EXACTLY conserved (to integrator tolerance), and during the drive the change equals the work done by/against the drive. The `dE_int/dt = Ė_dep − P_int·dV/dt` state equation and `P_int = (γ−1)E_int/V` are UNCHANGED.
- **Evidence:** the derivation above; `physics/hydro/tier2` (SnowplowShell, RK4) holds `E_int + ½MṘ²` to < 1e-6 relative over 1e4 steps for an expanding shell with Ė_dep = P_drive = 0 (the `tier2.` conservation test), and the P_drive > 0 case correctly DECREASES the invariant (−P_drive·dV/dt < 0 while expanding). The wrong sign fails the conservation test — it is what surfaced the defect.
- **Consequences:** `01 §5` E4 amended (force sign only). `05 §4` is unaffected — it restates only the unchanged `dE_int/dt` state equation, and "energy-conserving E4" is now literally true. A physics-formula correction, not a model change: γ, the EOS, the Guderley timing (MAJ-09) and Tier-1 are untouched. The review archives (`spec/reviews/`) preserve the original text as the historical record and are not edited.

## ADR-020 — Stale-lease fallback uses a wall-clock default, not `3× t_max_s` — 2026-08-04
- **Status:** accepted
- **Context:** M5-T3-c/-d (`nukefarm` work-queue) implementation contact. `06 §2` set the stale-lease requeue threshold as "2× median runtime of the last 10 completed units (fallback: 3× scenario `t_max_s` when fewer than 10)". The primary rule is a WALL-CLOCK runtime (seconds). But `t_max_s` is the burst SIMULATION time (`03 §4 [kinetics]`, ~5e-6 s), not a wall-clock duration; `3× t_max_s` ≈ 15 µs, so before 10 units complete every lease is instantly older than the threshold and every claim is reclaimed on the next pass — the queue thrashes and can never make progress. A wall-clock fallback (seconds–minutes) is needed to bootstrap the median.
- **Decision:** the `< 10`-completed fallback is a fixed WALL-CLOCK default, `stale_lease_fallback_s = 600` s (10 minutes), overridable by the worker (`--stale-lease-s`, M5-T3-e). It is an operational SIM parameter (like the fixed-point reduction scale), NOT a physical constant and NOT a gate threshold — so it lives as a documented code default, not in `constants.data.toml` / `gates.toml`. The PRIMARY rule (2× median of the last 10 completed wall-clock `wall_s`) is UNCHANGED; once ≥ 10 units complete it adapts to the real per-unit runtime.
- **Evidence:** the arithmetic above (`t_max_s` ≈ 5e-6 s ⇒ 3× ≈ 15 µs ≪ any real unit runtime, which is seconds); `nukefarm` `stale_threshold_s(store, 600)` returns 600 when < 10 units are done and 2× the median otherwise (a `nukefarm.` test). 10 minutes comfortably exceeds a single demon-core `generate_run` (seconds) while still reclaiming a genuinely crashed worker within a bounded time.
- **Consequences:** `06 §2` amended (the fallback wording only; the 2×-median primary rule and the whole queue design are unchanged). `t_max_s` reverts to meaning ONLY the `03 §4 [kinetics]` / tally simulation-time field (its correct meaning); the lease threshold no longer references it. 600 s is a default, not a tuned constant — sweeps override it per their unit runtime. A parameter correction from implementation contact, not a model change.

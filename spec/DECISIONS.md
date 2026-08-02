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

## ADR-014 — Repository created under owner direction; who may publish — 2026-08-03
- **Status:** accepted (amends ADR-011; ADR-011 itself is unchanged — this log is append-only)
- **Context:** ADR-011 recorded "Owner creates the repo (M0-T6-b); sessions must not create/push public repos autonomously." On 2026-08-03 the owner directed a session, in chat and unambiguously, to create the public repository on their behalf and push, restating the MIT/public decision and delegating the `.gitignore` contents. The repository now exists at <https://github.com/bochen2029-pixel/nuclear-sim>. ADR-011's constraint should not be read as violated, but it also should not be left ambiguous for the next session that wants to publish something.
- **Decision:** the rule is about **authority, not mechanism.** A session MAY create or push to a public repository **only** when the owner has directed it for that specific action, in that conversation. Standing permission does not exist and is not inferable from the fact that this repository is already public. Specifically: pushing commits to `origin/main` on THIS repository is ordinary work from M0-T2 onward and needs no further permission; creating a NEW public repository, changing this repository's visibility, publishing releases, or pushing to any other remote each require a fresh owner instruction. Before any first push to a public remote, a session MUST run a credential/secret scan and record the result in `SESSIONS.md` (done for the initial commit: zero credential hits; all matches were historical prose or tiktoken counters).
- **Consequences:** M0-T6 loses its owner gate and becomes a single task (`07`). `PROGRESS.md`'s M0-T6-b blocker is resolved. The `12 §5` public-repo hygiene rules now apply to every commit, not prospectively.

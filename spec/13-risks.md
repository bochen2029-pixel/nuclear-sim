# 13 — Risk Register

Columns: L/I = raw likelihood/impact before mitigation; **Residual** = risk remaining after the normative mitigation (added per review triage — mitigated-certainty risks must be representable honestly).

| ID | Risk | L | I | Residual | Mitigation (normative) | Trigger/owner |
|---|---|---|---|---|---|---|
| R-1 | Few-group XS too coarse for ±500 pcm | **H** | **H** | M | Transport-corrected P0 (mu_bar, required schema v2) + documented collapse method (M1-T4a); if residual > 500 pcm: ONE group-structure refinement (ADR), else CONDITIONAL gate path (`08 §2`). Merged with R-4's cause — adding groups does NOT fix anisotropy | M1-T4a |
| R-2 | Public prose claims inconsistent with curated XS | M | M | L | Gates use bands + ratio criteria, not points; deviations documented in gate reports; NEVER tune XS to force a prose claim (fabricated fidelity) — the anti-tuning statement is recorded in M1-T4a's data card | M2 |
| R-3 | Population overflow in burst (e^80 growth) | **H (raw)** | H | L | Log-renormalization past 1e30 is normative (`01 §4`); M3-T1 renorm-invariance unit test | M3 |
| R-4 | Isotropic-in-lab scattering bias (few-group, leakage-dominated benchmarks) | **H** | **H** | M | Transport-corrected P0 (mu_bar) — schema v2 REQUIRED; honest-error magnitude is UNCERTAIN (hundreds–thousands pcm claims both exist); measure, don't assert; if Godiva pcm > 300 on first pass, apply correction refinement before any group-count ADR | M1 |
| R-5 | Delta-tracking majorant cost on boron layers (WARP's caveat) | L | M | L | Default is surface tracking on analytic tracker (D2); delta only where it wins | M6 |
| R-6 | OptiX/CUDA/CMake toolchain friction (all very new) | M | M | L | OptiX isolated behind Tracker + compile guard; cloud path never needs it; `CMAKE_POLICY_VERSION_MINIMUM` + vcpkg baseline pinned (12 §1); one env var name (OPTIX_SDK_ROOT) | M0-T2, M6 |
| R-7 | Checkpoint format rot across milestones | M | **H** | L | Identity fields + per-section CRC + native-precision bank (03 §8 v2); old checkpoints never required to load, mismatches never load silently | M5 |
| R-8 | Multi-session drift: spec contradicted by code | M | H | M | Session protocol (README): verify-first, gates over prose, amendment protocol, append-only DECISIONS; **milestone-boundary SYNC audit** (07 §SYNC): APIs vs spec signatures, paths vs schema, mismatches to SESSIONS.md | milestone boundaries |
| R-9 | Cloud cost overrun on sweeps | M | **M** | L | budget_runs + budget_wallclock enforced; spot-first; expected-cost line (12 §6); perf_history informs estimates | M5 |
| R-10 | fp32 GPU transport noise vs double ref | M | M | L | Mixed precision policy (`01 §9`); G0c statistical criteria absorb it; systematic bias ⇒ ADR for selective double | M4 |
| R-11 | Scope creep toward non-public fidelity | L | H | L | Hard boundaries `00 §2/§3`; mechanical enforcement via `axis_class` + `ScoreKind` + non_canonical marking (type-level, not label-level); self-consistency windows (G1a-tight) measure the CODE, not the device — allowed; any task touching a status boundary requires ADR citing the public source | continuous |
| R-12 | **ICSBEP Handbook access-restricted**; benchmark data not freely downloadable (BLK-14) | H | H | L | M1-T4a derives models from OPEN literature (PUBLIC-DERIVED, citations + retrieval dates); M1-T4b owner-gated; gate reports record which source was used | M1, project owner |
| R-13 | α-mode quasi-static approximation invalid during rapid disassembly | M | H | M | q validity diagnostic every refresh (`01 §4`); auto-halving at q>0.02; G2 CONDITIONAL at max_q>0.05; cross-check run at halved dr_frac (08 §4); TD-mode is the escalation path | M3 |
| R-17 | Gate bands widened / thresholds relaxed / defaults tuned to make a failing model pass | M | H | L | Every threshold has a constant ID + derivation (appendix §5); gates.toml embeds spec_sha256; gate reports append-only; normative seed sets; canonical scenario = cited nominals (anti-default-shopping); ADR + cited evidence for any change | continuous |
| R-19 | Parallel-session claim races duplicate work | M | M | L | Claim = branch + claim-commit with timestamp tiebreak (README §7); depends_on column + ready-queue (PROGRESS.md); no human needed | continuous |
| R-20 | Crashed/compacted session loses in-flight understanding | M | M | L | WIP journals `spec/wip/` (README §5.4b): append-as-you-go, orphan detection at session start | continuous |
| R-21 | Artifact/sweep-output governance (5000-run parameter DB on a public repo) | L | M | L | Public-repo rule (12 §5); sweep artifacts are tallies of a public-parameter historical model; no access-restricted data committed; retention: sweep.db + reports kept, raw field dumps off-repo (object storage) | M5, owner |
| R-22 | **ν̄ convention silently violated** — an xs set built from prompt-only ν̄ is numerically indistinguishable from a total-ν̄ set, and the (1−β) correction then double-counts (~650 pcm on Godiva, > the whole G0a tolerance) | M | **H** | L | ADR-013: `nu` is TOTAL by definition, `beta` REQUIRED per isotope, correction applied exactly once downstream (`01 §4`); M1-T4a's data card MUST state which evaluation `nu` came from; `k_eff` and `k_prompt` both reported in `tally.json` so the ratio is inspectable; a k-gate consuming `k_prompt` is a defect | M1-T4a |
| R-23 | Gate evidence lost across sessions (reports untracked / CI archives expired) | M | H | L | QC-07: `artifacts/gate_reports/**` and `perf_history.jsonl` are committed (`02 §2` .gitignore); a gate claim whose report is not in the repository is void (`08 §3`); M0-T1 DoD asserts `git check-ignore` reports NOT ignored | M0-T1 |

## Closed risks (retained for provenance — resolved by amendment, not by judgement)

These were live risks that a spec amendment eliminated structurally. They stay recorded so a future session can see *why* the mitigation is normative and does not re-open it as an optimisation.

| ID | Risk | Closed by | Residual |
|---|---|---|---|
| R-14 | Generation time mis-tracked (Λ must scale ∝1/ρ) | CLOSED by BLK-04 amendment: Λ from eigen (E3b), density-rescaled between refreshes; M1-T3 DoD checks Λ(2ρ)/Λ(ρ) ∈ [0.4,0.6] | L |
| R-15 | Burn terminated at peak power (~2× yield error, G2-concealable) | CLOSED by BLK-03 amendment: ε_quench·F_peak termination (E6) + yield_split reporting + post-peak fraction cross-check | L |
| R-16 | GPU nondeterminism (atomic ordering, buffer-position streams, float reductions) breaks T-resume/G0c | CLOSED by BLK-11 amendment: deterministic slots/streams/reductions normative (05 §6, 01 §9); same-backend bit-identity test | L |
| R-18 | Spec forking (two authoritative copies) | CLOSED by BLK-12 amendment: C:\NUCLEAR IS the repo root; exactly one spec/ tree; distribution copies forbidden (README §6) | L |

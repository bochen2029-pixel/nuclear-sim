# PROGRESS — Living Project State

> Maintained by every session per `README.md` §5. Claim = branch + PROGRESS edit + pushed `claim:` commit.
> Status ∈ `todo | in_progress | done | blocked`. A task is **runnable** iff `todo` AND all `depends_on` are `done`.

## Current

- **Milestone:** M0 — Foundation
- **VERIFY:** `test ! -d .git && echo "pre-M0-T1 state confirmed"` (run from repo root `C:\NUCLEAR`; exits 1 once the repo exists — then replace with the M0-T2 build probe)
- **NEXT ACTION:** Execute M0-T1 — `git init` in place at `C:\NUCLEAR`, `git remote add origin <public repo>`, create the `spec/02-architecture.md` §2 skeleton with `.gitkeep`, MIT `LICENSE` + `NOTICE.md`, `.gitignore` **verbatim from `02 §2`** (the `artifacts/gate_reports/**` un-ignores are load-bearing — QC-07), `git mv` the research doc to `research/`; DoD in `spec/07-milestones.md` M0-T1. M0-T1 is exempt from the push-a-claim-commit protocol (`README §5.3`) because it is the task that creates the repository.
- **Owner pre-step (recommended before M0-T1):** create the empty public GitHub repo (ADR-011). Without a remote the §7 claim-race tiebreak is unusable, so run tasks single-session until it exists.

## Ready-queue (runnable now)

1. **M0-T1** (recommended — NEXT ACTION)
2. *(nothing else — all other tasks depend on M0-T1/M0-T2)*

## Gates

| Gate | Status | Evidence (gate_report.json path) | Date |
|---|---|---|---|
| G0a | not_run | — | — |
| G0b | not_run | — | — |
| G0c | not_run | — | — |
| G1a | not_run | — | — |
| G1a-tight (report-only) | not_run | — | — |
| G1b | not_run | — | — |
| G2  | not_run | — | — |
| G3  | not_run | — | — |
| G4  | not_run | — | — |
| G5  | not_run | — | — |

## Tasks

| ID | Status | claimed_by | claimed_at | depends_on | Notes |
|---|---|---|---|---|---|
| M0-T1 | todo | — | — | — | repo init IN PLACE at C:\NUCLEAR (BLK-12); MIT LICENSE (ADR-011) |
| M0-T2 | todo | — | — | M0-T1 | toolchain per `12 §1` (only source of truth); clean-cache build |
| M0-T3 | todo | — | — | M0-T2 | author `spec/appendix/constants.data.toml` + gen_constants + roundtrip |
| M0-T4 | todo | — | — | M0-T2 | Philox per `04 §2` (layout, fork, (ctr,sub), KATs) |
| M0-T5 | todo | — | — | M0-T2, M0-T3 | loaders incl. xs v2 semantics + negative tests |
| M0-T6 | todo | — | — | M0-T5 | (a) local CI + workflows; (b) **owner creates public repo** → push → Actions green |
| M1-T1 | todo | — | — | M0-T5 | geometry + analytic tracker |
| M1-T2 | todo | — | — | M1-T1 | ref transport (implicit capture); k_inf + leakage tests |
| M1-T3 | todo | — | — | M1-T2 | eigen (8³ mesh entropy, dual σ, Λ estimator) |
| M1-T4a | todo | — | — | M0-T5 | OPEN-literature benchmark models; one ADR per benchmark; xs dataset |
| M1-T4b | blocked | owner | — | owner ICSBEP access | OPTIONAL, owner-gated; autonomous sessions MUST NOT claim |
| M1-T5 | todo | — | — | M1-T3, M1-T4a | nukebench + gen_gates → gates.toml |
| M2-T1 | todo | — | — | M1-T4a | materials + trinity_canonical.toml |
| M2-T2 | todo | — | — | M2-T1 | Tier-1 compression (fixed formula) |
| M2-T3 | todo | — | — | M2-T2 | k-vs-compression scan |
| M3-T1 | todo | — | — | M1-T3 | α-kinetics E3a–E3c |
| M3-T2 | todo | — | — | M2-T2 | Tier-2 hydro (derived t_c, conserving E4) |
| M3-T3 | todo | — | — | M3-T1, M3-T2 | coupling + tallies + tally_invariants |
| M3-T4 | todo | — | — | M3-T3 | initiator timing |
| M4-T1 | todo | — | — | M1-T1 | gpu buffers + device Philox + deterministic slots/streams/reductions (start early, ADR-009) |
| M4-T2 | todo | — | — | M4-T1 | event kernels (parallel with M2/M3, ADR-009) |
| M4-T3 | todo | — | — | M1-T3, M4-T2 | gpu eigen + G0c harness |
| M4-T4 | todo | — | — | M4-T3 | perf pass + G4 |
| M5-T1 | todo | — | — | M3-T3, M4-T3 | checkpoint/resume v2 + T-resume |
| M5-T2 | todo | — | — | M5-T1 | artifact store + SQLite + unit_id idempotency |
| M5-T3 | todo | — | — | M5-T2 | sweep engine + samplers |
| M5-T4 | todo | — | — | M5-T3 | MCTS sampler (ScoreKind/axis_class enforced) |
| M5-T5 | todo | — | — | M5-T3 | container (verify base tag+digest) + cloud dry-run |
| M6-T1 | todo | — | — | M4-T3 | OptiX tracker + parity |
| M6-T2 | todo | — | — | M6-T1 | 32-lens geometry generator |
| M6-T3 | todo | — | — | M6-T2, G2 | c_a derived per C-071/C-072; G3 EXECUTED + recorded (never "G3 passes" as DoD) |
| M7-T1 | todo | — | — | M3-T3 | field grids + dumps + budget guardrails |
| M7-T2 | todo | — | — | M7-T1 | raymarcher + color + tonemap + temp calibration |
| M7-T3 | todo | — | — | M7-T2, M4-T4 | nukestudio + G5 parity |
| M7-T4 | todo | — | — | M7-T2 | nukecinema + make_film |

## Milestone-boundary SYNC checklist

Before claiming the first task of a new milestone: run the SYNC audit (`07-milestones.md` §SYNC) — APIs vs `04`/`05`, paths vs `03 §4`, constants vs appendix; log results in SESSIONS.md.

## Environment notes

- Dev machine (confirmed 2026-08-02): **Windows 11, Git Bash; RTX 4070 Ti SUPER (sm_89, 16,376 MiB, driver 610.47, RT cores); CUDA 13.1 (V13.1.80); MSVC 14.44; CMake 4.3.3; Python 3.13.2; OptiX SDK 9.1.0; vcpkg `C:\vcpkg`**. Version strings live ONLY in `spec/12-deployment.md` §1. Default backend: `--backend gpu` (ADR-009).
- Sibling CUDA projects (proven toolchain + process patterns): `C:\Buddhabrot_CUDA`, `C:\backrooms` (vcpkg manifest presets), `C:\Booster_Lander_Simulator` (DECISIONS/HANDOFF pattern), `C:\blackhole`.
- Cloud: RunPod H200 target (M5, sm_90, no RT cores → analytic tracker + plain-CUDA raymarch, D2). Hosting: public GitHub + MIT (ADR-011); repo not yet created — owner step (M0-T6-b).

## Blockers

- **M0-T6-b:** owner creates the public GitHub repo (ADR-011). Until then M0-T6-a delivers local CI + committed workflows.
- **M1-T4b (optional):** owner ICSBEP access decision. Not required — M1-T4a (open literature) is the default path.

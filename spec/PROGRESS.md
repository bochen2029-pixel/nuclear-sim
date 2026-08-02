# PROGRESS — Living Project State

> Maintained by every session per `README.md` §5. Claim = branch + PROGRESS edit + pushed `claim:` commit.
> Status ∈ `todo | in_progress | done | blocked`. A task is **runnable** iff `todo` AND all `depends_on` are `done`.

## Current

- **Milestone:** M0 — Foundation
- **VERIFY:** `cmake --preset win-x64 && cmake --build --preset win-x64-rel && ctest --preset win-x64-rel` — the `12 §2` canonical loop; falsifiable and real (configure + compile + link + 6 passing tests, incl. a CUDA kernel round-trip on the dev GPU). Takes ~1 min warm. Fast per-task probe: `ctest --preset win-x64-rel -R toolchain`.
- **NEXT ACTION:** Execute M0-T3 — author `spec/appendix/constants.data.toml` (strict machine-readable sibling of the human appendix, `03 §1`: values + lo/hi, `PENDING` where unresolved) plus `tools/gen_constants` emitting `data/constants.toml`, the C++ headers and a **generated** `docs/VERIFICATION.md`; the generator must hard-error on a missing citation/status and on any material species lacking a molar mass (appendix §3 completeness rule), and `constants_roundtrip` must prove the bijection under ctest; port `C:\Astrophage\scripts\canon.py` + its `docs/VERIFICATION.md` pattern per `spec/11-testing.md` §5 rather than hand-writing the oracle; see `spec/03-data-contracts.md` §1 and `spec/appendix/constants.md`; DoD in `07-milestones.md` M0-T3.

## Ready-queue (runnable now)

1. **M0-T3** (recommended — NEXT ACTION) — constants + generated verification oracle; unblocks M0-T5
2. **M0-T4** — Philox RNG per `04 §2`; independent of M0-T3, safe to run in parallel in another session
3. *(M0-T5 needs M0-T3; M0-T6 needs M0-T5)*

**Repository:** <https://github.com/bochen2029-pixel/nuclear-sim> — public, MIT (ADR-011). Remote exists, so the full claim protocol (`README §5.3`: branch + PROGRESS edit + pushed `claim:` commit) applies from M0-T2 onward and parallel sessions are now safe.

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
| M0-T1 | **done** | session-2026-08-03-a | 2026-08-03 | — | repo init in place (BLK-12); MIT LICENSE + NOTICE; `.gitignore` gate-evidence un-ignores verified both ways (QC-07); pushed to github.com/bochen2029-pixel/nuclear-sim |
| M0-T2 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T1 | canonical loop green from a clean build dir + cold vcpkg cache; 6/6 ctest incl. a real CUDA kernel round-trip. Generator `Visual Studio 17 2022` (win-x64), triplet `x64-windows-static`, baseline `d59284957…` — all recorded in `12 §1` |
| M0-T3 | todo | — | — | M0-T2 | author `spec/appendix/constants.data.toml` + gen_constants + roundtrip |
| M0-T4 | todo | — | — | M0-T2 | Philox per `04 §2` (layout, fork, (ctr,sub), KATs) |
| M0-T5 | todo | — | — | M0-T2, M0-T3 | loaders incl. xs v2 semantics + negative tests |
| M0-T6 | todo | — | — | M0-T5 | local CI + Actions workflow → Actions green on main (owner repo step resolved 2026-08-03) |
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
- Cloud: RunPod H200 target (M5, sm_90, no RT cores → analytic tracker + plain-CUDA raymarch, D2). Hosting: public GitHub + MIT (ADR-011) — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> since 2026-08-03.

## Blockers

- ~~**M0-T6-b:** owner creates the public GitHub repo (ADR-011).~~ **RESOLVED 2026-08-03** — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> (public, MIT). M0-T6 collapses to a single task: local CI + Actions workflow, then Actions green on main.
- **M1-T4b (optional):** owner ICSBEP access decision. Not required — M1-T4a (open literature) is the default path.

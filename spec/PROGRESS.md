# PROGRESS — Living Project State

> Maintained by every session per `README.md` §5. Claim = branch + PROGRESS edit + pushed `claim:` commit.
> Status ∈ `todo | in_progress | done | blocked`. A task is **runnable** iff `todo` AND all `depends_on` are `done`.

## Current

- **Milestone:** M0 — Foundation
- **VERIFY:** `git rev-parse --git-dir >/dev/null 2>&1 && git ls-files --error-unmatch LICENSE NOTICE.md spec/README.md >/dev/null && ! git check-ignore -q artifacts/gate_reports/x.json && echo "M0-T1 verified"` — falsifiable: fails if the repo is gone, if the licence/notice/spec are untracked, or if the gate-evidence un-ignore regressed (QC-07).
- **NEXT ACTION:** Execute M0-T2 — author `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` + `vcpkg-configuration.json` (with a pinned `builtin-baseline` SHA) per `spec/12-deployment.md` §1, which is the single source of truth for preset naming, `CMAKE_POLICY_VERSION_MINIMUM=3.5`, `CMAKE_CUDA_ARCHITECTURES=89-real;80-virtual;90-virtual` and first-party-only warning flags (`02 §4`); **port the CMake/preset idioms from `C:\Astrophage`, which already builds C++20 + CUDA 13.1 + sm_89 + MSVC on this machine, rather than re-deriving the CMake-4/vcpkg policy friction**; DoD in `spec/07-milestones.md` M0-T2.

## Ready-queue (runnable now)

1. **M0-T2** (recommended — NEXT ACTION) — toolchain; unblocks M0-T3/T4/T5 and therefore everything
2. *(nothing else — M0-T3/T4/T5 all depend on M0-T2)*

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
| M0-T2 | todo | — | — | M0-T1 | toolchain per `12 §1` (only source of truth); clean-cache build |
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

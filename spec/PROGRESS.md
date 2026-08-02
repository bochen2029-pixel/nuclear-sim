# PROGRESS — Living Project State

> Maintained by every session per `README.md` §5. Claim = branch + PROGRESS edit + pushed `claim:` commit.
> Status ∈ `todo | in_progress | done | blocked`. A task is **runnable** iff `todo` AND all `depends_on` are `done`.

## Current

- **Milestone:** M1 — CPU reference transport + bare-sphere benchmarks (M0 complete; SYNC-M1 run; M1-T1 done)
- **VERIFY:** `cmake --preset win-x64 && cmake --build --preset win-x64-rel && ctest --preset win-x64-rel` — the `12 §2` canonical loop; falsifiable and real (configure + compile + link + **61 passing tests**). Takes ~1 min warm. Per-task probes are **anchored** (`11 §1`): `ctest --preset win-x64-rel -R "^<module>\."` for module ∈ {toolchain, constants, oracle, rng, loaders, geometry, ref} — all seven resolve and are disjoint.
- **NEXT ACTION:** Execute M1-T3 — implement `physics/eigen` power iteration per `spec/05-module-transport.md` §2: the fission-bank iteration driving `ns::ref::RefTransport` (which already tallies production but deliberately does NOT propagate progeny — that propagation is this task), the fixed 8³ Shannon-entropy mesh (C-908, over the outermost-layer bbox), the dual σ estimate, and the Λ estimator returned in `EigenResult` (including `beta_eff`, ADR-013 — the solver MUST NOT return a β-corrected k); DoD per `05 §2`: bad-source convergence test, Λ(2ρ)/Λ(ρ) ∈ [0.4, 0.6], and 2× batch ⇒ k within 3σ; register as `catch_discover_tests(test_eigen TEST_PREFIX "eigen.")` and add its row to `11 §1`; DoD in `07-milestones.md` M1-T3.

## Ready-queue (runnable now)

1. **M1-T3** (recommended — NEXT ACTION) — `physics/eigen` power iteration + entropy + Λ
2. **M1-T4a** — OPEN-literature benchmark models + the cited `fast4` xs dataset; independent, safe to run in parallel
3. **M4-T1** — GPU buffers + device Philox; **ADR-009 says it SHOULD start now**, in parallel with M1/M2/M3
4. *(M1-T5 needs M1-T3 + M1-T4a; M2-T1 needs M1-T4a)*

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
| M0-T3-a | **done** | session-2026-08-02-b | 2026-08-02 | M0-T2 | 96 strict entries ↔ 96 appendix rows, bijective; `[[band]]` added (ADR-015); Φ_kt recomputed = 1.4508041e23; 26 validator guards; 15/15 ctest |
| M0-T3-b | **done** | session-2026-08-02-b | 2026-08-02 | M0-T3-a | all 6 `11 §5` sections; 9/9 tally invariants pass on the canonical example (I3 7.5e-8 independently reproduces the QC session's figure); Tier-1 endpoints exact, mass conservation 1.5e-16; §2 layer masses correctly PENDING(M2-T1); found the C-042/C-043 blanket-"~5%" wording defect |
| M0-T4 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T2 | 3 published Random123 vectors reproduce (also at compile time); project-local vector emitted by an INDEPENDENT Python Philox, not self-recorded; fork + (ctr,sub) resume KATs; 10 tests |
| M0-T5 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T2, M0-T3-a | xs v2 / materials / scenario loaders; positive tests parse the SPEC'S OWN examples extracted from `03` at run time; one negative test per violation class; 11 tests |
| M0-T6 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T5 | `tools/ci/local_ci.{ps1,sh}` + `.github/workflows/ci.yml`; Actions green on `main` (windows-2022 + ubuntu-latest + gate-evidence archive); `.env.example` committed |
| M1-T1 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T5 | LayerStack + AnalyticSphereTracker + in-tree SHA-256 + `canonical_hash()` (carried per SYNC-M1); 16 tests incl. the nudge-direction pair and the `04 §6` stability matrix |
| M1-T2 | **done** | session-2026-08-02-b | 2026-08-02 | M1-T1 | E1a–E1e implicit capture; leakage = exp(−Σ_c·R) at 3 depths + a 16-seed bias check; k_inf within 3σ on 3 media; 7 tests |
| M1-T3 | todo | — | — | M1-T2 | eigen (8³ mesh entropy, dual σ, Λ estimator) |
| M1-T4a | todo (RUNNABLE) | — | — | M0-T5 | OPEN-literature benchmark models; one ADR per benchmark; xs dataset |
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

**SYNC-M1 — run 2026-08-02 by session-2026-08-02-b, before claiming M1-T1.** Compared every implemented module against `04`; `05` has no implementation yet. Three findings: (1) `04 §3` named `MatXS mix(const Material&, const FewGroupXS&)` and it did not exist — the logic was inline in `MaterialLib::load_file`; **fixed**, extracted and declared in `core/material` (the dependency runs opposite to `04 §3`'s placement). (2) `04 §5`'s `Material::fracs` was `vector<pair<const IsotopeXS*, double>>`, which cannot carry a species that has a molar mass but no cross sections in the set — appendix §3 requires the mass per species regardless; **spec amended** to the implemented `Constituent` form. (3) `canonical_hash()` (`04 §6`) remains unimplemented — its test is grouped with geometry in `04 §7`, so **M1-T1 carries it**. Scenario paths vs `03 §4` and constants vs the appendix were already covered by `ctest -R loaders` and `constants.roundtrip_bijection` respectively; both green.


Before claiming the first task of a new milestone: run the SYNC audit (`07-milestones.md` §SYNC) — APIs vs `04`/`05`, paths vs `03 §4`, constants vs appendix; log results in SESSIONS.md.

## Environment notes

- Dev machine (confirmed 2026-08-02): **Windows 11, Git Bash; RTX 4070 Ti SUPER (sm_89, 16,376 MiB, driver 610.47, RT cores); CUDA 13.1 (V13.1.80); MSVC 14.44; CMake 4.3.3; Python 3.13.2; OptiX SDK 9.1.0; vcpkg `C:\vcpkg`**. Version strings live ONLY in `spec/12-deployment.md` §1. Default backend: `--backend gpu` (ADR-009).
- Sibling CUDA projects (proven toolchain + process patterns): `C:\Buddhabrot_CUDA`, `C:\backrooms` (vcpkg manifest presets), `C:\Booster_Lander_Simulator` (DECISIONS/HANDOFF pattern), `C:\blackhole`.
- Cloud: RunPod H200 target (M5, sm_90, no RT cores → analytic tracker + plain-CUDA raymarch, D2). Hosting: public GitHub + MIT (ADR-011) — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> since 2026-08-03.

## Blockers

- ~~**M0-T6-b:** owner creates the public GitHub repo (ADR-011).~~ **RESOLVED 2026-08-03** — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> (public, MIT). M0-T6 collapses to a single task: local CI + Actions workflow, then Actions green on main.
- **M1-T4b (optional):** owner ICSBEP access decision. Not required — M1-T4a (open literature) is the default path.

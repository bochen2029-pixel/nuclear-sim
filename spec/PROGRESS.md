# PROGRESS — Living Project State

> Maintained by every session per `README.md` §5. Claim = branch + PROGRESS edit + pushed `claim:` commit.
> Status ∈ `todo | in_progress | done | blocked`. A task is **runnable** iff `todo` AND all `depends_on` are `done`.

## Current

- **Milestone:** M1 — CPU reference transport + bare-sphere benchmarks (M0 complete; M1-T1..T3 + T4a-1 done). **M4 in progress in parallel (ADR-009); SYNC-M4 run; M4-T1 + M4-T2-a + M4-T2-b + M4-T3-a + M4-T4-a done — the GPU event-based transport AND k-eigenvalue power iteration are live and differentially validated against the CPU oracle, and the GPU backend now has a profile-first perf baseline (Nsight FP64 fraction 0.00%; k_step divergence-free; eigen k_generation the hotspot; VRAM ≤92 MiB @1e6).**
- **VERIFY:** `cmake --preset win-x64 && cmake --build --preset win-x64-rel && ctest --preset win-x64-rel` — the `12 §2` canonical loop; falsifiable and real (configure + compile + link + tests). **On the dev machine (CUDA auto-on): 88 passing = 75 CPU + 13 `gpu.` (on the RTX 4070 Ti SUPER). On a CPU-only build / the CI runners (`NUKESIM_WITH_CUDA` OFF): 75.** Takes ~1 min warm. Per-task probes are **anchored** (`11 §1`): `ctest --preset win-x64-rel -R "^<module>\."` for module ∈ {toolchain, constants, oracle, rng, loaders, geometry, ref, eigen, benchmarks, gpu} — all resolve and are disjoint. `^gpu\.` selects 13 and only registers when `NUKESIM_WITH_CUDA` is ON. Disjoint **and** exhaustive still holds: the anchored counts sum to the total — 6+11+1+10+11+15+7+9+5+13 = 88 (or 75 on a CPU-only build). The dev build also produces the **non-test** `gpu_perf` harness (M4-T4-a, `NUKESIM_WITH_CUDA`-guarded) and the committed baseline `artifacts/perf_history.jsonl`; neither is a ctest, so the 88 count is unchanged.
- **NEXT ACTION:** Execute **M4-T4-b** — swap the eigen's history-per-thread transport (`src/gpu/eigen.cu` `k_generation`) for the event-based superstep transport (`src/gpu/transport.cu` `k_step`) and move the 5 per-generation host round-trips (progeny counts, k reduction, entropy) onto the device (device prefix-sum + fixed-point reduction + device 8³ entropy). Re-measure before/after into `artifacts/perf_history.jsonl` and keep 88/88 bit-identity green. **DO NOT do the per-event branchless-kernel split** — M4-T4-a's Nsight profile measured `k_step` at **100% thread efficiency (31.99/32) and 0.00% FP64**, so the split is not warranted (profile-first, `02 §4`). See `spec/05-module-transport.md §6`; DoD in `07-milestones.md`. **Critical-path blocker (unchanged):** **M1-T4a-2 (owner cited xs data)** still gates M1-T5 (`nukebench`/`gates.toml`) → the FORMAL gate reports (G0a/G0b, M4-T3-b's G0c), and all of M2 — and it also blocks the REMAINING G4 criteria (Godiva gate eigen needs `fast4`; canonical-burst gen/s + render fps need M2/M3/M7), so **C-945 stays PENDING** (see Blockers). Providing a citable multigroup library or an ENDF+collapse pipeline unblocks the whole downstream chain.

## Ready-queue (runnable now)

1. **M4-T4-b** (recommended — NEXT ACTION) — event-based transport inside the eigen + device-side per-generation reductions (NOT the branchless split; M4-T4-a measured k_step divergence-free). Unblocked (M4-T4-a done). Perf improvement; not gated by M1-T4a-2.
2. **M3-T1** (runnable, was omitted from the prior queue) — α-kinetics E3a–E3c (`physics/kinetics`); `depends_on = M1-T3` which is **done**, so it is runnable now and needs no `fast4` xs (it is unit-testable on the eigen's toy outputs like M1-T3 was). It advances the M2/M3→G2 physics path on the CPU oracle while the xs blocker holds, though its downstream (M3-T2 hydro, M3-T3 coupling, G2) stays blocked. A session preferring physics over perf may pick this.
3. *(**Blocked on M1-T4a-2** (owner cited-data): M1-T5 nukebench/gates → the formal gate reports incl. **M4-T3-b** (G0c) and G0a/G0b; and all of M2. M1-T4a-2 is the critical path for non-perf work.)*
3. *(M1-T4a-2 is `blocked` pending owner-provided cited xs data — see Blockers. **M2-T1's `depends_on = M1-T4a` is ambiguous after the -1/-2 split:** authoring the canonical materials + `trinity_canonical.toml` and the mass/geometry checks may proceed on M1-T4a-1 (done) since those need densities + molar masses, not Σ — but every downstream G1/G2 gate needs the blocked fast4 xs. A session picking M2-T1 should resolve that dependency explicitly first.)*

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
| M1-T3 | **done** | session-2026-08-02-b | 2026-08-02 | M1-T2 | power iteration + 8³ entropy + dual σ + Λ; **measured that C-901's h_tol=1e-3 is correlation-limited, not batch-limited** — M1-T5 must re-measure at C-900 before G0a; 9 tests |
| M1-T4a-1 | **done** | session-2026-08-02-b | 2026-08-02 | M0-T5 | Godiva + Jezebel from JEFF Report 16 / CSEWG F5+F1 (open); atom densities primary, all derived values cross-check the spec's old placeholders; ADR-016/017; `08 §1` placeholders replaced; 5 tests |
| M1-T4a-2 | **blocked** | owner | 2026-08-02 | M1-T4a-1 | **BLOCKED (session-2026-08-02-c): needs owner-provided cited data.** Isotope-complete 4-group `fast4` dataset — ~450 physical values needing a published multigroup library or ENDF via a documented collapse (D4 carve-out); no NJOY/ENDF tooling or data present locally, BLK-14 forbids ICSBEP. Unblock = owner supplies a citable multigroup library **or** authorises/installs an ENDF+collapse pipeline. MUST NOT be invented. |
| M1-T4b | blocked | owner | — | owner ICSBEP access | OPTIONAL, owner-gated; autonomous sessions MUST NOT claim |
| M1-T5 | todo | — | — | M1-T3, M1-T4a | nukebench + gen_gates → gates.toml |
| M2-T1 | todo | — | — | M1-T4a | materials + trinity_canonical.toml |
| M2-T2 | todo | — | — | M2-T1 | Tier-1 compression (fixed formula) |
| M2-T3 | todo | — | — | M2-T2 | k-vs-compression scan |
| M3-T1 | todo | — | — | M1-T3 | α-kinetics E3a–E3c |
| M3-T2 | todo | — | — | M2-T2 | Tier-2 hydro (derived t_c, conserving E4) |
| M3-T3 | todo | — | — | M3-T1, M3-T2 | coupling + tallies + tally_invariants |
| M3-T4 | todo | — | — | M3-T3 | initiator timing |
| M4-T1 | **done** | session-2026-08-02-c | 2026-08-02 | M1-T1 | SoA buffers + device Philox (**reuses `core/rng`** via `NUKESIM_HD` + `--expt-relaxed-constexpr`, one source, no copy) + deterministic slots (exclusive prefix-sum, no atomic cursor) / streams (`fork`) / reductions (fixed-point int64, no FP atomicAdd). Device KATs match CPU incl. `fork(42,1000,3)`; reduction bit-identical across 4 launch configs, scan across 3 tile sizes. 6 `gpu.` tests; CUDA-guarded so CI stays 75 |
| M4-T2-a | **done** | session-2026-08-02-c | 2026-08-02 | M4-T1 | device geometry (float `AnalyticSphereTracker` mirror, `geometry.cuh`) + device material/xs (`materials.cuh`: per-layer macro Σ_t/Σ_tr + per-isotope slots). Float↔double parity vs CPU tracker (axial/tangential/miss/inside-out rays) and `mix()` (Σ_t/Σ_tr/νΣ_f); 2 new `gpu.` tests → 83 total dev. Per-isotope nu/chi/transfer + `global_index` deferred to M4-T2-b (built with the collision kernel that uses them) |
| M4-T2-b | **done** | session-2026-08-02-c | 2026-08-02 | M4-T2-a | Event-based fixed-source GPU transport (`transport.cu`): SoA + superstep loop (E1a–E1e implicit capture), **prefix-sum compaction** of live particles, **deterministic fission bank** at exclusive-prefix-sum slots + `fork` streams. T-diff vs `ref/` (G0c): pure-capturer leakage + scattering/fission k_inf, both within 3σ; bit-identity (tallies + bank) across launch configs. 3 new `gpu.` tests → 86 dev. **Deferred:** per-event branchless-kernel split → M4-T4 (perf); bank PROPAGATION → M4-T3 (eigen) |
| M4-T3-a | **done** | session-2026-08-02-c | 2026-08-02 | M1-T3, M4-T2-b | SPLIT from M4-T3: GPU k-eigenvalue power iteration (`eigen.cu`) — fission-source iteration (reservoir fission sites → bank at prefix-sum slots → next-gen source, `fork` streams) + fixed-8³ entropy (E2b); **differential vs `ref/` eigen** (GPU k matches within toy-batch tolerance) + bit-identity (k / checksum / entropy) across launch configs. 2 `gpu.` tests → 88 dev |
| M4-T3-b | blocked | owner | 2026-08-02 | M4-T3-a, M1-T5 | SPLIT from M4-T3: the FORMAL G0c gate — `nukebench diff` on the fixed 3-seed set → committed `gate_report.json` (`08 §2`, ≤100 pcm at C-900 batch). Needs `nukebench`+`gates.toml` (M1-T5, behind blocked M1-T4a-2). Statistical equivalence already validated by M4-T3-a's differential test |
| M4-T4-a | **done** | session-2026-08-02-d | 2026-08-02 | M4-T3-a | SPLIT from M4-T4 (§8): GPU perf **profile-first** baseline. `tools/perf/gpu_perf` harness + observational VRAM instrumentation (`peak_vram_bytes`, `device_info()`); gen/s + particles/s-vs-batch + VRAM → `artifacts/perf_history.jsonl` (committed). **Nsight FP64 fraction = 0.00%** on `k_step`+`k_generation` (MAJ-33 ≤1% MET). **Profile finding:** `k_step` already 100% thread-efficient (31.99/32) → the deferred **branchless split is NOT warranted**; `k_generation` (eigen) 29% → the real divergence hotspot. VRAM ≤92 MiB @1e6 (≪12 GB). 88/88 dev, CI 75 |
| M4-T4-b | todo | — | — | M4-T4-a | SPLIT from M4-T4 (§8): profile-motivated opt. **Swap** the eigen's history-per-thread transport (`eigen.cu` `k_generation`) for the event-based superstep transport (`transport.cu` `k_step`, measured divergence-free) + move the 5 per-generation host round-trips onto the device (device prefix-sum / fixed-point k reduction / device entropy). Re-measure before/after → `perf_history.jsonl`. **DO NOT do the branchless-kernel split** — M4-T4-a measured `k_step` at 100% thread-eff, 0.00% FP64; it is not warranted |
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

**SYNC-M4 — run 2026-08-02 by session-2026-08-02-c, before claiming M4-T1.** M4 enters `src/gpu/` (empty but for `optix/.gitkeep`). Audited vs `04`/`05`/`03`: (1) **`04 §2` device RNG** — `core/rng/{philox,rng}.h` are `constexpr` and the philox header already anticipates the device port ("the same source compiles for device code unchanged … the GPU port (M4-T1) substitutes `__umulhi`"). Making them device-compilable is M4-T1's job. Resolution: a behaviour-preserving `NUKESIM_HD` macro (`__host__ __device__` under `__CUDACC__`, empty on a host compiler) via new `src/core/hd.h`; frozen KAT values and the host build are unchanged (test_rng re-verifies). **Not a spec amendment** — it is the implementation of the port `04 §2`/`05 §6` already specify. (2) **`05 §6` GPU design** — M4-T1 declares **no** `GpuTransport` interface: declaring an unimplemented method is exactly what SYNC-M1 caught for `mix()`. M4-T1 ships only the foundation primitives (SoA buffers, device Philox, slots/streams/reductions); the `RefTransport`-shaped `GpuTransport` arrives at M4-T3. (3) **Scenario paths / constants** — N/A for a buffers task; the C-907 stream registry is already generated, and the fixed-point reduction scale is a documented numerical detail (like the Philox multipliers, deliberately not a physical constant — philox.h). No divergence found; no amendment needed.


Before claiming the first task of a new milestone: run the SYNC audit (`07-milestones.md` §SYNC) — APIs vs `04`/`05`, paths vs `03 §4`, constants vs appendix; log results in SESSIONS.md.

## Environment notes

- Dev machine (confirmed 2026-08-02): **Windows 11, Git Bash; RTX 4070 Ti SUPER (sm_89, 16,376 MiB, driver 610.47, RT cores); CUDA 13.1 (V13.1.80); MSVC 14.44; CMake 4.3.3; Python 3.13.2; OptiX SDK 9.1.0; vcpkg `C:\vcpkg`**. Version strings live ONLY in `spec/12-deployment.md` §1. Default backend: `--backend gpu` (ADR-009).
- Sibling CUDA projects (proven toolchain + process patterns): `C:\Buddhabrot_CUDA`, `C:\backrooms` (vcpkg manifest presets), `C:\Booster_Lander_Simulator` (DECISIONS/HANDOFF pattern), `C:\blackhole`.
- Cloud: RunPod H200 target (M5, sm_90, no RT cores → analytic tracker + plain-CUDA raymarch, D2). Hosting: public GitHub + MIT (ADR-011) — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> since 2026-08-03.

## Blockers

- ~~**M0-T6-b:** owner creates the public GitHub repo (ADR-011).~~ **RESOLVED 2026-08-03** — repo live at <https://github.com/bochen2029-pixel/nuclear-sim> (public, MIT). M0-T6 collapses to a single task: local CI + Actions workflow, then Actions green on main.
- **M1-T4a-2 (blocks M1-T5/G0a + all of M2) — NEW 2026-08-02:** needs owner-provided cited cross-section data — a published 4-group / multigroup fast library, or an ENDF + documented-collapse pipeline (NJOY-class), neither present locally. BLK-14 forbids an autonomous session seeking ICSBEP access. **Owner action to unblock:** supply a citable multigroup library, or authorise/install an ENDF processing pipeline. Assessed by session-2026-08-02-c: no local NJOY/ENDF/ACE tooling; the benchmark scenarios already reference `xs_set = "fast4"`, which does not yet exist, so G0a/G0b cannot run until this lands.
- **M1-T4b (optional):** owner ICSBEP access decision. Not required — M1-T4a (open literature) is the default path.
- **C-945 (`g4_perf_budgets`) stays PENDING — NEW 2026-08-02 (M4-T4-a):** M4-T4-a resolved the one G4 criterion that is a pure GPU-kernel property — the **FP64 instruction fraction = 0.00% ≤ 1%** (MAJ-33) — and recorded dev-GPU throughput/VRAM baselines (`artifacts/perf_history.jsonl`). The REST of the 08 §2 G4 budget list needs workloads that do not exist yet: the **Godiva gate eigen** (2.5e8 histories) needs the blocked `fast4` xs (M1-T4a-2); **canonical-burst gen/s + eigen-calls-per-burst** need M2/M3; **render ≥30 fps** needs M7. So C-945's `resolved_by="M4-T4"` is **optimistic** — its full resolution spans M4-T4 / M2–M3 / M7. Owner action: the same M1-T4a-2 unblock also opens the Godiva-gate-eigen G4 measurement. No budget/ADR change this session (can't measure the gated workloads).

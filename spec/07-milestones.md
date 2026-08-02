# 07 — Milestones, Tasks & Gates

Task IDs are stable and referenced everywhere (PROGRESS.md, commits, SESSIONS.md). Each task lists: **goal / primary files / Definition of Done (DoD)**. Global DoD additions: unit tests for new code (`11-testing.md`), build green, spec citations for any new constant, **per-task VERIFY probe** (a falsifiable command the next session can run — D7). Tasks are ordered within a milestone; `depends_on` in `PROGRESS.md` is the machine-checkable form. A DoD may never be "the gate passes" — it is "the gate executed and the report recorded" (circularity rule, MAJ-18).

## M0 — Foundation (no physics yet)

| ID | Goal | Files | DoD |
|---|---|---|---|
| M0-T1 | `git init` **in place at `C:\NUCLEAR`** (BLK-12: this IS the repo root — no nesting, no spec copy); `git remote add origin` (ADR-011 public repo; if absent, record `remote: pending`); create the `02 §2` skeleton with `.gitkeep`; `git mv` the research doc to `research/`; MIT `LICENSE`; `NOTICE.md`; `.gitignore` **verbatim from `02 §2`** (artifacts un-ignores included); root README points to spec | repo root | initial commit contains LICENSE + NOTICE + the `00 §3` scope statement in the root README (first public commit sets the tone); tree matches `02 §2`; exactly ONE spec tree at `<root>/spec/`; `git check-ignore -v artifacts/gate_reports/x.json` reports NOT ignored. Claim-protocol bootstrap exemption applies (README §5.3) |
| M0-T2 | Toolchain per `12-deployment.md` §1 (single source of truth: preset naming, vcpkg baseline SHA, `CMAKE_POLICY_VERSION_MINIMUM=3.5`, `CMAKE_CUDA_ARCHITECTURES`, warnings per `02 §4` first-party-only). **Port, don't invent:** `C:\Astrophage` already builds C++20 + CUDA 13.1 + sm_89 + MSVC on THIS machine (`CMakeLists.txt`, `scripts/build.ps1`); adopt its CMake/preset idioms and its `gate.ps1`-style milestone runner rather than rediscovering the CMake-4/vcpkg policy friction | `CMakeLists.txt`, `CMakePresets.json`, `vcpkg*.json`, `scripts/` | canonical loop in `12 §2` runs clean on dev machine from a CLEAN build dir + empty vcpkg cache; `ctest` runs (0 tests OK). VERIFY: `cmake --preset win-x64 && cmake --build --preset win-x64-rel` |
| M0-T3-a | **SPLIT from M0-T3 (§8 — oversized).** Author `spec/appendix/constants.data.toml` (strict sibling, 03 §1: values+lo/hi from the human appendix; PENDING for unresolved) + `tools/gen_constants` → `data/constants.toml` + headers | `tools/gen_constants/`, `tools/verify/`, `spec/appendix/`, `data/`, `src/core/constants/` | generator fails on missing cite/status; `constants_roundtrip` bijection passes under ctest; **every species named in any material resolves to a molar mass (hard error otherwise, appendix §3 rule)**; headers compile; generated outputs assert-current under ctest (`--check`). VERIFY: `ctest -R "^constants\."` |
| M0-T3-b | **SPLIT from M0-T3.** `tools/verify/oracle` → **`docs/VERIFICATION.md`**, GENERATED from `constants.data.toml` from first principles, without reading or linking `nscore` (`11 §5`) | `tools/verify/`, `docs/` | all six `11 §5` sections present (Φ_kt; kt/kg Pu-239 + U-235 with the ~5% C-042 basis gap stated; layer masses vs the appendix table incl. the C-102 2.4% over-determination; Tier-1 radius map at s ∈ {0,¼,½,¾,1} with endpoint checks; Fuchs–Nordheim post-peak fraction as the reason E6 cannot terminate at k=1; Λ ∝ 1/ρ over the compression range; the nine `03 §5` tally invariants on the canonical example); `VERIFICATION.md` regenerates byte-identically under ctest. **Note:** the layer-mass section needs `data/materials/*.json`, so it emits an explicit PENDING block until M2-T1 rather than being silently absent. VERIFY: `ctest -R "^oracle\."` |
| M0-T4 | `core/rng` Philox per `04 §2` normative layout + fork + (ctr,sub) | `src/core/rng/`, `tests/unit/` | 3 Random123 KATs + project-local vector + fork KAT + (ctr,sub) round-trip green. VERIFY: `ctest -R "^rng\."` |
| M0-T5 | Loaders: xs (schema v2 semantics), materials, scenario (full 03 §4 validation: `[data]` resolution, `[ui.*]` ranges, override grammar, `mode="td"` rejection, `[source]` rules); canonical example files parse | `src/core/`, `data/` | unit + negative tests green; precise diagnostics. VERIFY: `ctest -R "^loaders\."` |
| M0-T6 | CI: `tools/ci/local_ci.(ps1\|sh)` (configure+build+ctest for win-x64 + linux presets, non-zero on failure) + a GitHub Actions workflow. The repo exists (ADR-011/ADR-014, created 2026-08-03), so this is one task with no owner gate | `tools/ci/`, `.github/workflows/`, `.env.example` | local CI exits 0; Actions green on `main`; the workflow archives `artifacts/gate_reports/` as a build artifact **in addition to** the committed copy (QC-07 — the committed copy is primary); **`.env.example` committed** documenting `OPTIX_SDK_ROOT` plus commented `S3_*` / `RUNPOD_*` stubs — `12 §5` requires the file but no task owned it before, and CI is where secrets first matter (`12 §5`: "CI uses repo secrets only") |

## M1 — CPU reference transport + bare-sphere benchmarks

| ID | Goal | Files | DoD |
|---|---|---|---|
| M1-T1 | `core/geometry` LayerStack + AnalyticSphereTracker | `src/core/geometry/` | unit tests per `04 §7` green (incl. nudge direction both ways, kOutside, innermost inner-radius=0) |
| M1-T2 | `ref/` history-based transport (E1a–E1e, implicit capture) + fixed-source | `src/ref/` | DoD per `05 §1`: pure-capturer point-source leakage exp(−Σ_c·R) **within 3σ** at 3 optical depths **plus a 16-seed mean-pull bias check** (`05 §1` — the original 1σ was a 32%-false-failure coin flip) (Σ_f = Σ_s = 0 required) AND infinite-medium k_inf = ν̄Σ_f/(Σ_c+Σ_f) within 3σ |
| M1-T3 | `physics/eigen` power iteration + 8³-mesh entropy + dual σ + Λ estimator | `src/physics/eigen/` | DoD per `05 §2`: bad-source convergence test; Λ(2ρ)/Λ(ρ) ∈ [0.4, 0.6]; 2× batch ⇒ k within 3σ |
| M1-T4a | **Benchmark models from OPEN literature** (BLK-14): Godiva + Jezebel descriptions, citations, retrieval dates → `data/benchmarks/*.md`; finalize `godiva.toml`, `jezebel.toml`, `pu_ga_jezebel` + isotope-complete `fast4` xs (provenance-tracked collapse method + transport correction documented; D4 carve-out); enumerate full isotope set; one ADR per benchmark (MAJ-46) | `data/` | `[M1-T4a]` placeholders in `08 §1` replaced; ADRs logged; CHANGELOG; loader accepts scenarios; mass-from-geometry WARNs recorded |
| M1-T4b | OPTIONAL, **owner-gated**: replace with ICSBEP sheet values if owner grants access; record sheet + revision; ADR | `data/benchmarks/` | MUST NOT be claimed by an autonomous session |
| M1-T5 | `nukebench run`/`gate`/`diff` + `tools/gen_gates` generating `gates.toml` (03 §10) from `08-validation.md` + `g2_feasible_region.md` | `src/app/nukebench/`, `tools/gen_gates/` | `gate --gate G0a` runs end-to-end, writes 03 §11 report; `--seed` with `--gate` exits 2; `spec_sha256` mismatch fails. VERIFY: `nukebench gate --gate G0a --report …` |

**Milestone gates: G0a, G0b** (08 §2; CONDITIONAL fallback path exists, MAJ-31).

## M2 — Layered assembly + static criticality

| ID | Goal | Files | DoD |
|---|---|---|---|
| M2-T1 | Materials (pu_ga_delta per 03 §3 corrected fractions, u_natural, b10_acrylic, aluminum, be_po_urchin, HE surrogate) + `trinity_canonical.toml` | `data/` | per-layer |mass_from_geometry − appendix mass| ≤ 3% with computed values + deviations recorded in `data/materials/README.md`; pit checked against 6.15 kg specifically, derived density recorded (MAJ-28) |
| M2-T2 | Tier-1 parametric compression (fixed formula, `01 §5`) | `src/physics/hydro/tier1.*` | endpoint checks to 1e-12 AND mass conservation at s ∈ {0,.25,.5,.75,1} (BLK-01-strength test) |
| M2-T3 | k-vs-compression scan tool + CSV artifact | `src/app/nukebench/` | k(ρ/ρ₀) curve artifact over [1.0, 2.0] |

**Milestone gates: G1a (mass-fraction + derived k band), G1b (4-criterion, ratio-based).** G1a-tight frozen after M1-T4a.

## M3 — Burst kinetics + disassembly quench

| ID | Goal | Files | DoD |
|---|---|---|---|
| M3-T1 | `physics/kinetics` E3a–E3c (source term, ν̄_eff, Λ, q diagnostic, log-renorm) | `src/physics/kinetics/` | DoD per `05 §3` (closed-form, renorm invariance, weight-weighted, ν̄_eff hand-check) |
| M3-T2 | Tier-2 hydro (derived t_c, energy-conserving E4) | `src/physics/hydro/tier2.*` | DoD per `05 §4` (energy conservation < 1e-6, no-motion, constructor matching, α_G/γ consistency check) |
| M3-T3 | Coupling loop (`02 §3`) + tallies incl. per-isotope split + yield_split | `src/physics/couple/`, `src/physics/tally/` | canonical end-to-end artifact bundle; `tally_invariants` test (03 §5, all 9) green |
| M3-T4 | Initiator timing sensitivity (t_fire sweep, grid, axis_class=numerical) + default locked to peak compression | `data/scenarios/` | sweep artifact; canonical scenario updated if needed (CHANGELOG) |

**Milestone gate: G2** (7 criteria incl. consistency + timing + σ + max_q).

## M4 — GPU event-based transport

> **GPU-first (ADR-009):** M4-T1 SHOULD start immediately after M1-T1; M4-T2 proceeds in parallel with M2/M3; only M4-T3 hard-depends on M1-T3. By G2, the canonical burst MUST run on `--backend gpu`.

| ID | Goal | Files | DoD |
|---|---|---|---|
| M4-T1 | SoA buffers + device Philox (04 §2 layout) + deterministic slots/streams/reductions (05 §6) | `src/gpu/` | device KATs match CPU (incl. fork); same-backend bit-identity across thread counts |
| M4-T2 | Event partition (prefix-sum compaction) + branchless event kernels + deterministic fission bank | `src/gpu/` | T-diff subset on fixed-source toy case |
| M4-T3 | GpuTransport eigen path + `nukebench diff` harness | `src/gpu/`, `tests/differential/` | **G0c** executed, report recorded |
| M4-T4 | Perf pass: Nsight (incl. FP64 instruction fraction ≤ 1%), batch-vs-throughput curve | `src/gpu/` | **G4** executed; findings in SESSIONS.md + perf_history.jsonl |

**Milestone gates: G0c, G4.**

## M5 — Farm, sweeps, cloud

| ID | Goal | Files | DoD |
|---|---|---|---|
| M5-T1 | Checkpoint/resume (03 §8 v2: identity+CRC+native precision+eigen section) + T-resume | `src/core/checkpoint/` | kill at phase/generation boundaries → resume → bit-identical same-backend tally; mismatch/CRC rejection tests |
| M5-T2 | Artifact store + SQLite results DB; unit_id idempotency | `src/app/nukefarm/`, `src/core/store/` | schema 03 §7 tables; rerun by same unit_id is a no-op |
| M5-T3 | Sweep engine + grid/lhs/random samplers + FS queue + stale-lease rule (06 §2) | `src/app/nukefarm/` | 100-run local sweep completes; resume skips done units; no double-count under induced reclaim |
| M5-T4 | MCTS sampler plugin (PUCT; ScoreKind + axis_class enforced) | `src/app/nukefarm/samplers/` | converges faster than random on synthetic 2-param landscape (unit test); pedagogical-axis rejection test |
| M5-T5 | Dockerfile (verify base tag + record digest at build time) + `deploy/runpod.md` + cloud dry-run | `deploy/` | 500-run sweep on rented GPU incl. one induced kill+resume; artifacts synced; expected-cost line recorded (12 §4) |

## M6 — Lens geometry & asymmetry (Stage 3)

| ID | Goal | Files | DoD |
|---|---|---|---|
| M6-T1 | OptiXCSGTracker backend (NUKESIM_WITH_OPTIX) + parity vs analytic on sphere stacks | `src/gpu/optix/` | T-diff through OptiX backend |
| M6-T2 | Truncated-icosahedron 32-lens generator (20 hex + 12 pent, C-080) | `src/core/geometry/lens.*` | mesh validation: closed, non-overlapping, I_h symmetry check |
| M6-T3 | Asymmetry model: c_a derived per `01 §5` (C-071/C-072 rule, recorded as SIM) + jitter sweeps | `src/physics/hydro/`, `data/sweeps/` | c_a regression unit test green; **G3 executed and its result — pass or fail — recorded** (NOT "G3 passes") |

**Milestone gate: G3.**

## M7 — Rendering, studio, cinema (Stage 4)

| ID | Goal | Files | DoD |
|---|---|---|---|
| M7-T1 | Field grids (03 §9 f16) + dump wiring + budget guardrails | `src/render/fields/` | dumps parse; channel sums match tally totals within 1% |
| M7-T2 | Raymarcher + Planckian color + ACES + optional denoiser | `src/render/` | blackbody hue monotonic in T; per-backend golden frames (perceptual hash); temp calibration band check (09 §1) |
| M7-T3 | `nukestudio` per `10-ui.md` + snapshot threading + badges | `src/app/nukestudio/` | **G5** parity executed; G4 render criteria |
| M7-T4 | `nukecinema` shots + EXR/PNG + `tools/make_film` | `src/app/nukecinema/` | burst sequence rendered end-to-end in container |
| M7-T5 | Sandbox mode (ADR-018): per-detonator `[[lenses.detonators]]` schema + loader validation (`03 §4`); free clip plane on shells + volume (`09 §2`); deterministic seeded neutron trails (BURST, `09 §4`); timeline event markers from computed state (`10 §6`) | `src/core/scenario/`, `src/render/`, `src/app/nukestudio/` | loader validation tests (count match, `delay_s` range, absent = canonical all-fire) + clip-plane golden frame + trail determinism (same seed → identical trails) + marker-derivation unit tests; G5 parity unaffected. VERIFY: `ctest -R "^loaders\."` |

**Milestone gates: G5, G4 (render portion).**

## Gates

> **Labels only. Normative definitions: `08-validation.md` §2 + `data/benchmarks/gates.toml`. A gate is met only when `nukebench gate` exits 0. Never claim a gate from this table.**

| Gate | Purpose (one line) |
|---|---|
| G0a/G0b | Godiva / Jezebel k_eff vs PUBLIC-DERIVED benchmark models (±500 pcm class) |
| G0c | ref vs gpu statistical equivalence (fixed 3-seed set) |
| G1a | Uncompressed tamped assembly: critical-mass fraction + derived k band (+ report-only G1a-tight) |
| G1b | Compression scan: 3–4 critical masses claim, ratio + monotone-within-statistics |
| G2 | Canonical burst: yield/burn-up bands + consistency + timing + σ + max_q |
| G3 | Jitter sweep validates (not calibrates) the asymmetry model |
| G4 | Perf: render-with-advance, gen/s, eigen budgets, eigen-call cap, VRAM |
| G5 | Studio↔CLI bit-identical parity (same backend) |

## Milestone-boundary SYNC (C-19)

Before claiming the first task of a new milestone, run a SYNC audit: compare module APIs vs `04`/`05` signatures, scenario paths vs `03 §4`, constants vs appendix; log mismatches in SESSIONS.md and fix via amendment protocol. Record as `SYNC-M<n>` in PROGRESS.md notes.

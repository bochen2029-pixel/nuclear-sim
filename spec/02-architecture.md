# 02 — Architecture

Normative decisions D1–D9 (each has exactly one ADR in `DECISIONS.md`; `tools/verify/decision_index` asserts the mapping in ctest). Changing one requires the amendment protocol.

## 1. Decisions

- **D1 — Two-tier implementation + differential validation (ADR-001, addendum BLK-11).** `src/ref/`: CPU, history-based, double precision, minimal (~1.5k lines), exists only to be correct. `src/gpu/`: CUDA event-based, exists only to be fast. Within-backend bit-identity, cross-backend statistical identity (G0c). `ref/` is built FIRST and is the permanent oracle. **ADR-009 (GPU-first):** every capability MUST land on the GPU backend within the same milestone it lands in `ref/`; default `--backend gpu` on the dev machine. **Determinism addendum:** no design element may make results depend on thread count, block size, launch order, or atomic ordering; any proposal that does requires an ADR explicitly relaxing `11-testing.md` §2.
- **D2 — Geometry: analytic nested-sphere tracker first; OptiX CSG tracker only for Stage 3 (ADR-002).** `Tracker` interface, two backends: `AnalyticSphereTracker` (MUST; permanent oracle incl. cloud path) and `OptiXCSGTracker` (M6, 32-lens assembly). Dev machine (sm_89) HAS RT cores — OptiX viable locally; the no-RT-core constraint applies to cloud H100/H200 only.
- **D3 — Kinetics: α-mode default; TD-mode stretch (ADR-003).** α-mode: operator-split loop (§3), quasi-static eigen refresh with the `q` validity diagnostic (`01 §4`, R-13) — a phenomenological approximation whose error is MEASURED and reported, not assumed away. TD-mode (GUARDYAN-style) is the escalation path if q cannot be controlled; interface-only in v1; `mode="td"` is a loader validation error until then.
- **D4 — Few-group cross sections (ADR-004).** Curated 4-group fast dataset, schema v2 (03 §2), every value cited. **One-time carve-out:** M1-T4a MAY generate the dataset offline via a provenance-tracked multigroup collapse (documented weighting spectrum + transport correction), producing a checked-in dataset with a data card. No RUNTIME ENDF parsing. Adding groups/thermal = schema v3 via ADR (R-1).
- **D5 — Hydro tiers (ADR-005).** Tier-1 parametric, Tier-2 thin-shell + Guderley timing (default), Tier-3 1D Lagrangian (stretch, interface only). No 3D hydro; asymmetry = the §5 phenomenological scalar only.
- **D6 — Staged simulation clock (ADR-010).** Phases: BURST (per-generation), HYDRO (every_gens), FIREBALL (render-driven). Global `SimClock` maps wall/render time to sim time per phase; no fixed-dt integration across phases. `t=0` = outermost-HE initiation (03 §4).
- **D7 — One library, four frontends (ADR-006).** `nscore`: zero GUI deps, full determinism, serializable state. Frontends contain no physics: `nukebench`, `nukefarm`, `nukestudio`, `nukecinema`. Studio exports reproduce bit-identically in `nukebench` same-backend (gate G5). Any physics logic in `src/app/` is a defect.
- **D8 — Sweep/experiment infrastructure (ADR-007).** `sweep.toml`; samplers grid/lhs/random/mcts via in-tree C++ plugin registry; objectives mechanically constrained by `axis_class` + `ScoreKind` (03 §7).
- **D9 — Checkpoint/resume is a hard requirement (ADR-008).** Schema v2 (03 §8): identity fields, per-section CRC, native-precision bank, eigen state section. T-resume: kill→resume→bit-identical same-backend.

## 2. Repository layout (normative)

```
C:\NUCLEAR/                       # THIS DIRECTORY IS THE REPO ROOT (BLK-12; git init in place, M0-T1)
  CMakeLists.txt  CMakePresets.json  vcpkg.json  vcpkg-configuration.json  .gitignore  LICENSE(MIT)  README.md
  .env.example                    # documents required env vars; NEVER a real credential (12 §5)
  .github/workflows/              # CI (M0-T6); 11 §3 is the normative matrix
  data/
    constants.toml                # generated (03 §1)
    xs/*.json  materials/*.json
    scenarios/{godiva,jezebel,trinity_canonical,...}.toml
    benchmarks/                   # gates.toml (generated) + benchmark derivation notes (M1-T4a)
  spec/                           # THIS SPEC — exactly one copy, lives here permanently
    wip/                          # in-flight session journals (README §5; committed, cleaned at END)
    reviews/                      # external review reports
  research/                       # source research document (moved here by M0-T1)
  src/
    core/      constants/ rng/ xs/ geometry/ material/ scenario/ checkpoint/ store/
    ref/       transport + eigen (CPU oracle)            # 05-module-transport.md
    gpu/       event-based CUDA kernels; optix/ (M6, compile-guarded NUKESIM_WITH_OPTIX)
    physics/   eigen/ kinetics/ hydro/ couple/ tally/
    render/    fields/ raymarch/ color/                  # 09-rendering.md
    app/       nukebench/ nukefarm/samplers/ nukestudio/ nukecinema/
  scripts/     build.ps1 — developer wrappers around the 12 §2 canonical loop (M0-T2)
  tools/       gen_constants/ verify/ sync_artifacts/ make_film/ ci/ xs_prep(optional)/
  docs/        VERIFICATION.md — GENERATED first-principles oracle (11 §5, M0-T3) + design notes
  deploy/      Dockerfile, runpod.md                     # M5-T5
  tests/       unit/ golden/ differential/ perf/
  artifacts/   run bundles (gitignored) — EXCEPT:
    gate_reports/            # TRACKED: the evidence PROGRESS.md's gate table points at
    perf_history.jsonl       # TRACKED: cross-session perf trend (rotations are ignored)
```

This tree is the complete set of directories the project will contain; a task needing a new directory amends this section in the same commit (CHANGELOG line; no ADR needed for a directory). `.gitkeep` in each empty dir at creation.

**`.gitignore` (normative, QC-07).** Run bundles are large and disposable; **gate reports and the perf trend are neither** — `README §4` makes gates the root of trust across sessions, `PROGRESS.md`'s gate table records report *paths*, and `11 §1` calls `perf_history.jsonl` a cross-session trend. If they are untracked, every gate claim in `PROGRESS.md` points at a file the next session cannot open, and "trust gates, not prose" degrades to prose. CI artifact archives do not substitute: they expire, and a local session cannot read them.

```gitignore
build/
artifacts/**
!artifacts/gate_reports/
!artifacts/gate_reports/**
!artifacts/perf_history.jsonl
artifacts/perf_history_*.jsonl        # rotations (11 §1) stay out
*.chunks/
.env
```

Gate reports therefore live at `artifacts/gate_reports/<gate>/` (not inside run bundles), so the un-ignore is a directory rather than a glob across `artifacts/<unit_id>/`. Field dumps, `sweep.db`, and run bundles remain untracked and go to object storage (`12 §4`, R-21).

## 3. Main loop (α-mode, normative pseudocode; BLK-02/BLK-03/MAJ-21 fixed)

```
load scenario → validate (03 §4) → build LayerStack, materials, FewGroupXS
t = 0 (HE initiation per 03 §4); n = 0; supercritical_reached = false; F_peak = 0
k_eff, Λ, β_eff = eigen(geometry); k_p = k_eff·(1−β_eff)   # ADR-013; Λ init from generation_time_s_initial
loop:
    if (n % eigen_refresh_gens == 0) or hydro_moved_significantly:   # integer counters ONLY
        k_eff, Λ, β_eff, S_i, source = eigen(geometry)               # E2; Λ per E3b; q diagnostic recorded
        k_p = k_eff · (1 − β_eff)                                    # prompt multiplication, derived ONCE
    if k_p >= 1.0: supercritical_reached = true                      # PROMPT-supercritical
    N, F_gen, E_gen = E3_step(k_p, N, S_n, ν̄_eff, E_f)               # E3a (with source term)
    F_peak = max(F_peak, F_gen)
    deposit(E_gen, source) → field                                   # E5
    if (n % hydro_every_gens == 0):
        hydro.step(field) → geometry update                          # E4/E5 tier; sets hydro_moved_significantly
        rescale Λ by density ratio if no eigen refresh this gen      # E3b
    tallies.accumulate(F_gen per isotope, N, t)                      # weight-weighted; Pu/tamper separate
    renormalize if N > 1e30                                          # §4 01-physics
    publish SimSnapshot (immutable, ref-counted) for render/UI thread # MAJ-19/B-19 threading model
    n += 1; t += Λ
until (supercritical_reached and F_gen < quench_epsilon · F_peak)    # E6 — NOT k<1
   or t >= t_max
emit run.json + tally.json (+ checkpoint per D9; + fields if enabled)
```

**Threading model (normative, B-19/MAJ-19):** single-writer sim thread executes the loop; after each phase boundary it publishes an immutable `SimSnapshot` (ref-counted) that render/UI threads consume. Checkpoints are taken BETWEEN phases (the "phase boundaries" of `11 §4`). Studio fps counts render frames reading the latest snapshot; a live render with a stalled sim is an explicit G4 FAIL.

## 4. Engineering policies

- **Language/standards:** C++20. Version pins live ONLY in `12-deployment.md` §1 (single source of truth — do not restate versions here or anywhere else). `12 §1` also owns preset naming and `CMAKE_CUDA_ARCHITECTURES`.
- **Warnings:** `-Wall -Wextra -Werror` (MSVC `/W4 /WX`) apply to **first-party targets only**, set per-target via `target_compile_options`, never globally. Third-party headers via `SYSTEM` includes. Device warnings: `-Werror all-warnings`, first-party `.cu` only. **CUDA host flags (corrected M0-T2 against the emitted nvcc command line):** on GCC/Clang they are routed explicitly with `-Xcompiler`; on MSVC they are *not*, because the VS generator already hoists `/W4` onto the CUDA host pass and delivers `/WX` through MSBuild's project-level `TreatWarningAsError` property — so first-party `.cu` host code compiles at the same `/W4 /WX` as `.cpp`, and no routing is needed. Do not try to soften it with `-Xcompiler=/W3`: that raises D9025, which the same inherited property promotes to an error. **Known hole, deliberately accepted:** nvcc compiles its own generated `tmpxft_*.cudafe1.stub.c` inside every `.cu` translation unit and that stub trips C4211 at `/W4`, so the build suppresses C4211 there via `-Xcompiler=/wd4211` — which unavoidably also suppresses it for first-party code in the same TU. C4211 ("redefined extern to static") is the one warning `.cu` files are not protected against.
- **Dependencies:** vcpkg manifest mode with a committed builtin-registry baseline SHA, pinned in `vcpkg-configuration.json` as `default-registry.baseline` (`12 §1`); baseline changes require an ADR. New dependency ⇒ ADR first. Baseline set: fmt, tomlplusplus, nlohmann-json, Catch2, CLI11, SQLite3; M7 adds glfw3, glad, imgui[docking-experimental], implot, tinyexr.
- **Error handling:** loaders validate and fail with precise diagnostics (file, field, constraint). No silent physics defaults.
- **Logging:** fmt-based; ERROR/WARN/INFO/DEBUG; headless runs log `run.log` in the artifact bundle.
- **Determinism:** no `std::random_device` / wall-clock seeds except to GENERATE a recorded seed; effective seed always in `run.json`. Scheduling decisions on integer counters only — equality tests on accumulated floating-point time are forbidden (MAJ-21).
- **Testing:** `11-testing.md`; every module ships Catch2 tests in the same task.
- **Performance targets:** `08-validation.md` §G4. Profile before optimizing; log findings in SESSIONS.md.

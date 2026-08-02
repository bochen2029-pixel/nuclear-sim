# 12 — Deployment & Toolchain

## 1. Toolchain pins (set in M0-T2; bump only via amendment protocol)

Dev machine confirmed 2026-08-02 (nvcc / nvidia-smi / installer paths):

| Tool | Pin |
|---|---|
| OS | Windows 11 (dev), Ubuntu 22.04 (container) |
| CMake | **4.3.3** installed (≥ 3.28 required). Preset naming (normative): configure `win-x64`, `linux-x64`, `linux-cuda`; build `<configure>-deb` / `<configure>-rel`; test likewise. Presets set `CMAKE_POLICY_VERSION_MINIMUM=3.5` (CMake 4.x vs older port minimums), `VCPKG_ROOT=C:\vcpkg`, `CMAKE_CUDA_ARCHITECTURES=89-real;80-virtual;90-virtual` |
| MSVC | **14.44** (VS 2022) installed / gcc 13+ for container |
| CUDA | **13.1** installed (V13.1.80) — sm targets: **dev 89**, cloud 80/90 (PTX forward-compat via the virtual archs) |
| OptiX | **SDK 9.1.0** installed at `C:\ProgramData\NVIDIA Corporation\OptiX SDK 9.1.0` (M6+ only; compile-guarded). **One env var name, version-agnostic: `OPTIX_SDK_ROOT`** — CMake locates the SDK via `OPTIX_SDK_ROOT` (env or `-D` cache entry) pointing at that directory; the version is not encoded in any identifier (minimum version recorded in THIS table only) |
| vcpkg | `C:\vcpkg\vcpkg.exe` manifest mode. **`vcpkg-configuration.json` MUST pin a `builtin-baseline` commit SHA** (recorded here at M0-T2; changing it requires an ADR). Baseline: fmt, tomlplusplus, nlohmann-json, catch2, cli11, sqlite3; M7 adds: glfw3, glad, imgui[docking-experimental], implot, tinyexr |
| Python | **3.13.2** installed — tools (gen_constants, gen_gates, make_film, xs_prep optional) |
| Hosting | **Public GitHub repo + MIT license (ADR-011, owner decision 2026-08-02).** Actions free for public repos |

## 1a. Dev-machine GPU (confirmed) + GPU-first policy (ADR-009)

- **RTX 4070 Ti SUPER, sm_89, 16,376 MiB VRAM, driver 610.47, RT cores present** → OptiX fully viable locally; the no-RT-core caveat applies to cloud H100/H200 only (D2 unchanged: analytic tracker remains the oracle + cloud path).
- **GPU-first:** every simulation capability ships on the CUDA backend in the same milestone it ships on CPU. `ref/` (CPU) is the correctness oracle and keeps CPU-only builds alive — it is NOT the primary product. Default backend on this machine: `--backend gpu`.
- VRAM budget (16 GB): neutron SoA @1e7 particles ≈ 0.4 GB; fission bank ×2 ≈ 0.8 GB; 256³×4ch half-float fields ≈ 0.15 GB; studio framebuffers/denoiser ≤ 2 GB. Hard cap sim+render ≤ 12 GB; `--vram-report` logs usage per run.
- Sibling projects on this machine (Buddhabrot_CUDA, backrooms, Booster_Lander_Simulator, blackhole) confirm this toolchain builds and runs; reuse their CMake/preset idioms where applicable (backrooms uses vcpkg manifest presets).

## 2. Build

```
cmake --preset win-x64 && cmake --build --preset win-x64-rel && ctest --preset win-x64-rel
```

`NUKESIM_WITH_CUDA` defaults ON if `nvcc` is on PATH at configure time, else OFF (override `-DNUKESIM_WITH_CUDA=OFF`). OptiX targets build only when `OPTIX_SDK_ROOT` resolves to a directory containing `include/optix.h` (guard macro `NUKESIM_WITH_OPTIX`). A CPU-only machine MUST build `nscore`, `nukebench`, `nukefarm`, and run unit + CPU-backend golden tests.

## 3. Container (`deploy/Dockerfile`, M5-T5)

- Base: `nvidia/cuda:13.1-devel-ubuntu22.04` — **verify the exact tag/digest resolves at M5-T5 before writing the Dockerfile** (`docker pull`; record the digest, not just the tag; if the 13.1 line dropped 22.04, use the closest available distro and note it). Build stage compiles `nscore`, `nukebench`, `nukefarm`, `nukecinema` (CPU-raymarch fallback ok). Runtime stage: matching `runtime` image + binaries + `data/`.
- Entrypoint: `nukefarm worker --queue /work/queue`; volumes: `/work` (queue), `/artifacts`.
- `deploy/runpod.md` runbook: image build/push, pod template (1×H200, 100 GB volume), env vars, cost notes, **preemption handling = T-resume** (checkpoint every N runs per sweep manifest).

## 4. Cloud notes

- H100/H200 have no RT cores: cloud path uses AnalyticSphereTracker + delta tracking + plain-CUDA raymarch (D2). OptiXCSGTracker is excluded from cloud builds (compile guard `NUKESIM_WITH_OPTIX`).
- Spot/preemptible instances are the default assumption: every long op checkpoints (D9). Sweeps are idempotent — rerunning a completed unit is a no-op (dedup by scenario hash, M5-T2).
- Artifacts sync to object storage (S3-compatible) via `tools/sync_artifacts` (rclone wrapper; credentials via env, never in repo).

## 5. Secrets & environment

No secrets in repo. `.env.example` documents required vars (`OPTIX_SDK_ROOT`, `S3_*`, optional `RUNPOD_*`). CI uses repo secrets only. The repo is **public (ADR-011)** — double-check no credentials or access-restricted data files are ever committed; ICSBEP-derived values (if M1-T4b ever happens) are recorded only as already-published scalars with citations, never sheet reproductions.

## 6. Cost guidance (MIN-25)

Expected cloud cost: a 5000-run sweep at ~40 s/run ⇒ ~55 GPU-hours; at spot H200 rates (~$3–5/h class) ⇒ **order $150–275 per full sweep** — verify current pricing at M5-T5 and record the actual in `deploy/runpod.md`; `budget_runs`/`budget_wallclock_h` are enforced by nukefarm regardless (R-9, I=M).

# NUCLEAR-SIM

**A 3D, physics-based educational simulator of the 1945 Trinity "Gadget" / Fat Man device** — GPU Monte Carlo neutron transport → k-eigenvalue criticality → supercritical burst kinetics → simplified implosion/disassembly hydrodynamics → volumetric fireball rendering.

<p align="center">
  <img src="docs/gallery/core-view-peak-burst.jpg" width="940" alt="The demon-core chain-reaction view at peak burst — a true-3D volumetric fission cascade (schematic · synthetic data · no real weapon data)">
  <br><sub><i>The demon-core chain-reaction view at <b>peak burst</b>, captured headless from <a href="viz/core-view.html"><code>viz/core-view.html</code></a>. True-3D volumetric cascade · schematic, synthetic data · <b>no real weapon data</b>.</i></sub>
</p>

**The destination is an interactive 3D studio.** Zoom into the plutonium pit, slow time down, and watch the fission chain reaction and the fireball evolve — a bundled volumetric representation of the neutron cascade (nuclei splitting and releasing neutrons that strike others), every frame driven by the *same* validated transport math, never hand-painted. The hard part — the physics engine that has to be right for that picture to mean anything — is what most of this repository is. The rendering and UI (`nukestudio`) sit on top of it.

Two artifacts, one engine:

- **This MIT repository is the validated engine** — the CPU/GPU transport core, the criticality/kinetics/hydro solvers, and the `nukebench` validation harness (gates, differential checks, cross-section provenance). Headless and reproducible; the tests and gates *are* the product here.
- **The eventual Steam release is the visual studio** (`nukestudio`, milestone M7) — the real-time, hyperreal 3D sandbox built on top of this engine. The differentiator is that the picture is not art over a scripted timeline: it is a rendering *of the engine's own numbers*.

Built **exclusively on public and declassified literature**. Every physical constant carries a citation and a source-status tag, and the generator refuses to emit any constant lacking either.

```
Status:  Two validation gates now PASS on committed evidence — G0c (the GPU backend
         reproduces the CPU reference oracle) and G0a (the Godiva bare-HEU critical
         benchmark, mean −270 pcm, inside the ±500 pcm band). Both rest on the new
         `fast4` cross-section library: a documented ENDF/B-VIII.0 collapse
         (5 groups: 4 fast + 1 thermal) with per-assembly self-consistent openmc
         flux weights (tools/xs). The CPU + GPU Monte-Carlo transport, the
         k-eigenvalue solver, α-mode burst kinetics, both hydro tiers, the burst
         coupling loop, an emergent demon-core burst (detonate-vs-fizzle emerges
         from the physics, not a script), the fusion API (`evaluate`/`generate_run`)
         for the visualizer, bit-identical checkpoint/resume, and the batch sweep
         store are built and validated against the reference oracle — 193 tests
         green on the dev machine (177 CPU + 16 GPU), 177 in CI (CPU-only).
Honest:  The Jezebel bare-Pu gate (G0b) still misses the band at +1315 pcm —
         improved by the self-consistent weighting but not closed; it is the
         genuinely hard one and is recorded as an open ADR-022 residual, NOT faked
         to a pass. The device-scale canonical check (G1a) likewise over-estimates
         (reads slightly supercritical uncompressed where it should be subcritical)
         — an honest fail, consistent with the same coarse-group residual amplified
         by real reflection. No cross section is ever tuned to a benchmark.
Next:    close Jezebel (resonance self-shielding is the remaining lever) / formalize
         G1a as a gate; then M7 — the GPU raymarcher and the interactive cinematic
         demon-core view (`nukestudio`).
Target:  C++20 + CUDA 13.1, sm_89 (RTX 4070 Ti SUPER) primary, sm_80/90 cloud
Licence: MIT (code) — see NOTICE.md for sources and scope
```

Progress in detail — task table, dependency graph and the single next action — lives in [`spec/PROGRESS.md`](spec/PROGRESS.md); it is the authority, this line is a snapshot.

---

## Validation — the gate ledger

A milestone is met only when a numbered gate command exits 0 against a normative seed set; the thresholds carry a constant ID and a one-line derivation (full text in [`spec/08-validation.md`](spec/08-validation.md), evidence in [`artifacts/gate_reports/`](artifacts/gate_reports/)). The current standing:

| Gate | What it checks | Result | Measured | Band |
|---|---|---|---|---|
| **G0c** | GPU backend ≡ CPU reference oracle (k, per-shell source, population series) | ✅ **PASS** | mean \|Δk\| ≈ 6 pcm | ≤ 100 pcm (+ ≤ 3σ) |
| **G0a** | **Godiva** — bare HEU sphere, k = 1 | ✅ **PASS** | mean **−270 pcm**, σ ≈ 5.5 pcm | ±500 pcm |
| **G0b** | **Jezebel** — bare δ-phase Pu sphere, k = 1 | ⚠️ **honest residual** | **+1315 pcm** (was +1586) | ±500 pcm |
| **G1a** | Canonical device, uncompressed → subcritical | ⚠️ **honest fail** | ~+1000s pcm over-estimate | subcritical band |

**Why G0a passes and G0b does not — and why that is reported, not fixed.** Both benchmarks are collapsed from the *same* cited ENDF/B-VIII.0 data. The lever that moved Godiva into the band is [ADR-025](spec/DECISIONS.md): the coarse group constants are re-weighted with each assembly's **own self-consistent fundamental-mode flux**, computed by openmc continuous-energy transport on the exact benchmark geometry. openmc is used as a **flux-shape calculator only — it never touches a cross section, and the weight is never adjusted toward k = 1** (openmc's own k comes out ≈ 1.000, confirming the geometry is right; that number is never copied). The same treatment only improves Jezebel (+1586 → +1315) because its harder spectrum and larger leakage leave a residual that neither self-consistent weighting nor finer fast groups close alone. Per [ADR-022](spec/DECISIONS.md), that honest miss stands as recorded evidence rather than being tuned away — fitting a cross section to a gate is the exact failure the whole provenance discipline exists to prevent.

### The `fast4` cross-section pipeline

`data/xs/fast4.json` — the multigroup library every gate reads — is produced *only* by running [`tools/xs/`](tools/xs/), never by hand and never tuned to a benchmark result:

```
ENDF/B-VIII.0 (public, NNDC)  →  pointwise σ(E), ν(E), χ(E), scatter kernels
        │                                 │
        │                    weight with each assembly's self-consistent flux φ(E)
        │                    (openmc fundamental-mode; fast groups + one thermal group)
        ▼                                 ▼
   per reaction, per isotope   →   group averages   →   data/xs/fast4.json
                                          │
                                 measure G0a/G0b  →  report the REAL pcm
```

The group structure is 5 groups — 4 fast plus one thermal group below 1 keV added in [ADR-024](spec/DECISIONS.md) to give moderated device-scale neutrons a physical thermalization-to-capture sink. Full per-isotope provenance is stamped into `data/xs/PROVENANCE-fast4.md`; the discipline is spelled out in [`tools/xs/README.md`](tools/xs/README.md).

## Scope & boundaries

Hard constraints from [`spec/00-overview.md`](spec/00-overview.md) §2–§3, enforced mechanically wherever enforcement is possible. Full text in [`NOTICE.md`](NOTICE.md).

- **Public/declassified sources only.** Los Alamos Primer, LA-3067, Nuclear Weapon Archive, Wellerstein, Coster-Mullen (as open reconstruction), ENDF/NNDC, openly published Godiva/Jezebel descriptions. Any number entering code exists first in the cited constants appendix. The ICSBEP Handbook is **not** used (its evaluated sheets are distributed under an intended-use agreement); benchmark models here are derived from open literature and tagged `PUBLIC-DERIVED`.
- **Classified gaps are not filled.** Exact peak k-effective, Bethe–Feynman numerical coefficients, lens internal contours, and implosion velocity profiles beyond published values are classified. Where such a quantity is needed the spec defines a parameter with a public-plausible range, or a validation *band* derived from published statements — never an invented point value.
- **Bethe–Feynman is a display-only overlay.** It never feeds simulation state.
- **Exploration yes, optimisation no.** Interactive single-point exploration of counterfactual parameters is in scope and is the pedagogical point; such runs are marked `non_canonical` and can never serve as gate evidence. *Automated search* over physical parameters toward a performance objective is out of scope, and is blocked by a type-level restriction on sweep axes rather than by convention.
- **Yield is a range, never a point.** 18.6 kt (1945 radiochemistry) / 21 kt (DOE) / 24.8 ± 2 kt (Selby et al. 2021).

**Not** a weapon design tool and not extensible into one — it models one specific historical device whose parameters are ~80-year-old public knowledge. Not a full-physics hydrocode. Not a game.

## Why the specification looks like this

The project is built to be implemented across dozens of independent agent sessions, so correctness cannot rest on anyone remembering anything:

- **Gates, not prose.** Numbered gates (G0a–G5) whose thresholds each carry a constant ID and a one-line derivation; a milestone is met only when the gate command exits 0. Normative seed sets make seed-shopping mechanically impossible rather than merely discouraged. Honest fails are committed as evidence (`artifacts/gate_reports/`), never quietly dropped.
- **One living state file.** [`spec/PROGRESS.md`](spec/PROGRESS.md) holds the task table, dependency graph, ready-queue and a *falsifiable* VERIFY command. Claimed state that VERIFY cannot confirm is not real.
- **Append-only decisions.** [`spec/DECISIONS.md`](spec/DECISIONS.md) — changing a gate or a physical constant requires an ADR with cited evidence. (The recent arc: ADR-021 transport correction → ADR-022 honest coarse-group residual → ADR-024 thermal group → ADR-025 self-consistent weighting.)
- **A generated verification oracle.** Constants, headers, test goldens and a first-principles verification document are emitted from one source, so a number cannot drift between code, test and documentation.

The specification has been through three independent adversarial reviews (kept in [`spec/reviews/`](spec/reviews/)), an omnibus triage, and a QC pass. Their findings — among them a quench criterion that discarded roughly half the yield, a compression formula that collapsed the geometry to a point at t₀, and a generation time that failed to scale with density — are recorded in the changelog rather than quietly fixed.

## The main course — the demon-core chain-reaction view

The project's stated centrepiece ([ADR-023](spec/DECISIONS.md), a normative first-class M7 deliverable) is the interactive 3D visualization of the demon-core fission chain reaction: **10,000–50,000 representative "atoms" as genuine 3D volumetric bodies** (not points, not sprites) in a true-3D pit you can zoom and pan *inside* as well as outside, with a time slider that stretches the burst from seconds to minutes — *so that nothing is ever faked.* Four load-bearing clauses:

1. **One holistic spacetime** — a single continuous clock (implosion → burst → disassembly → fireball) and one continuous scene, time dilated most during the burst so the cascade is watchable. No faked cut between phases.
2. **True-3D volumetric particles** — GPU-instanced meshes, never flat billboards.
3. **Zoom = camera focus, not a mode** — the burst view is a camera move within the one scene (defaults to a close framing on the core; you keep full zoom/orbit/pan), never a mode-switch that hides the device.
4. **Representative sampling that tracks the physics** — each rendered fission is a representative sample of the real `ref::FissionSource::sites` from the O(N) GPU Monte-Carlo transport, never an O(N²) all-pairs toy.

The data seam for this already exists and is proven live: the engine streams real per-generation fission **sites** and the temporal `population_series` through the studio `generate_run` API (a 9 kg / 2.5× pit → detonate → 1.62 kt, 628 generations, 411,645 real fission sites over HTTP). The render layer is now in the repo. [`viz/core-view.html`](viz/core-view.html) is a self-contained, cinematic WebGL module — true-3D `InstancedMesh` nuclei and neutrons, ACES tone-mapping, bloom, rack-focus depth-of-field, god-rays and a filmic grade pass, with time auto-dilating through the burst. Every frame shown here is captured **straight from that module, headless**. Its physics is **synthetic but spec-shaped**: 24,000 representative nuclei stand in for the pit's ~1.5×10²⁵ atoms, evolving on the same `samples[].sites[]` seam the real `nscore` engine will feed — so swapping the synthetic driver for the engine's live `generate_run` stream changes the *data*, not the render.

<table>
<tr>
<td width="33%"><img src="docs/gallery/core-view-critical-assembly.jpg" alt="Critical assembly — the cold pit at idle"><br><sub><b>Critical assembly</b> — the cold pit, k = 1.000</sub></td>
<td width="33%"><img src="docs/gallery/core-view-prompt-chain.jpg" alt="Prompt chain — supercritical excursion"><br><sub><b>Prompt chain</b> — supercritical, k ≈ 1.66</sub></td>
<td width="33%"><img src="docs/gallery/core-view-volumetric-detail.jpg" alt="Volumetric detail — genuine instanced 3D nuclei"><br><sub><b>Volumetric detail</b> — genuine instanced 3D nuclei, not sprites</sub></td>
</tr>
</table>

> **Schematic · synthetic data · representative sampling · no real weapon data.** The look targets a hyperreal, Unreal-Engine / *Modern Warfare*-class register and is still being pushed further on the viz track. More frames in the [visualization gallery](docs/gallery/).

### Earlier sketch — GadgetLab

[`GadgetLab`](docs/gallery/) is the earlier in-browser (Three.js) sketch of the interactive **device** view — the schematic front-end (casing, the 32-lens HE array, the pit microscope) the physics engine is being wired to drive.

![GadgetLab pit microscope — the fission chain reaction (schematic · synthetic)](docs/gallery/05-pit-microscope.png)

> **Schematic · all data synthetic.** Museum-style proportions and a placeholder physics curve — **no real weapon data**. This preview's front-end is not yet wired to the `nscore` Monte-Carlo engine; the fusion (the real `evaluate` / `generate_run` API replacing the synthetic stub) is the work in [`src/api/`](src/api/). More frames in the [visualization gallery](docs/gallery/).

## Where to start

| You want to | Read |
|---|---|
| Orient, or start a session | [`spec/README.md`](spec/README.md) — router + session protocol |
| See where the project actually is | [`spec/PROGRESS.md`](spec/PROGRESS.md) |
| Understand the physics | [`spec/01-physics.md`](spec/01-physics.md) — equations E1–E7 |
| Understand the validation | [`spec/08-validation.md`](spec/08-validation.md) — gates G0a–G5 |
| See the cross-section provenance | [`tools/xs/README.md`](tools/xs/README.md) + `data/xs/PROVENANCE-fast4.md` |
| Find a number | [`spec/appendix/constants.md`](spec/appendix/constants.md) |

New agent session? Paste the bootstrap prompt from `spec/README.md` §9.

## Repository

This directory is the repository root; there is exactly one `spec/` tree and it lives here permanently. Layout is normative in [`spec/02-architecture.md`](spec/02-architecture.md) §2.

## Credits

Source research synthesised from the works cited in [`NOTICE.md`](NOTICE.md). Specification, reviews and triage authored collaboratively with [Claude Code](https://claude.com/claude-code).

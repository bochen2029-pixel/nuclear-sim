# Sim ⇄ Viz Hookup — State & Orientation

*A **living** note, authored from the **main (physics) track** for the **-e (viz) track** and the
owner. It captures where the simulation→visualization hookup stands, the delicate points, and the
data contracts the viz can rely on. The main track does **not** edit `viz/`; this is orientation +
a contract pledge, not a change to viz code.*

- **Maintained by:** the main track (last updated 2026-08-05, `session-2026-08-05-a`).
- **Companion docs:** `docs/FUSION-BINDING.md` (the seam + the mechanism decision + how to run it),
  `docs/viz-adapter-worklog.md` (the build log of `studio-adapter.js` + the exact `main.js` recipe).
- **Memories:** `[[viz-seam-coordination]]`, `[[core-vision-3d-chain-reaction]]`.
- **Spec:** `03 §5` (tally), `03 §6` (run.json), `05 §5` (`TallySink`/`GenerationSample`),
  `09-rendering`, `10-ui`.

> If anything here disagrees with the code, the **code wins** — re-verify against the file:line
> anchors below and update this note.

---

## 1. The seam in one picture

`viz/js/main.js` (the Three.js app) only ever calls **two functions**. Everything else —
`applyRun(t)`, the device animation, the pit microscope — is a pure function of their returns.

```
                       ┌──────────────────────────────────────────────┐
  viz/js/main.js  ──▶  │  sim.evaluate(cfg)                            │
  (unchanged)          │  sim.generateRun(cfg, detonators, blockDirs)  │
                       └──────────────────────────────────────────────┘
                              ▲                          ▲
                     simstub.js  (SYNTHETIC)     studio-adapter.js  (REAL)
                     invented formulae           fetch → studio_server.py
                                                       → studio_bridge
                                                       → src/api (real MC engine)
```

`simstub.js` and `studio-adapter.js` are **interchangeable** — identical shape in, identical shape
out. Swapping one for the other **is** the fusion. `main.js` stays unchanged *except* that the two
calls become `await` (real physics is async/seconds — see §6).

Anchors: `viz/js/main.js:154` (`createSimStub()`), `:189` (`evaluate` in `refreshGauge`),
`:257` (`generateRun` in the commit handler), `:321` (`applyRun(t)`, the pure-f(t) visuals).

---

## 2. Where the hookup is (state)

| Layer | State | Evidence |
|---|---|---|
| C++ data surface (`src/api` `evaluate_json` / `generate_run_json`) | ✅ done | emits real `tally` (03 §5), `run` (03 §6), **`samples[]` + `sites[]`** |
| `studio_bridge` (exe) + `tools/studio_server.py` (HTTP dev-server) | ✅ done, proven live | `POST /evaluate`, `/generate-run`, `GET /health` |
| `viz/js/studio-adapter.js` (the real JS adapter) | ✅ written, proven live | a 9 kg / 2.5× pit → **detonate → 1.62 kt, 628 gens, 411,645 real fission sites** over HTTP |
| `viz/js/pitscope.js` (chain-reaction microscope) | ✅ **already consumes `samples[].sites[]`** | renders each real site as a fission flash + ν neutrons |
| **The `main.js` swap** (use the adapter, not the stub) | ⬜ **remaining — the -e track's domain** | 4 edits + async/debounce; recipe in `docs/viz-adapter-worklog.md` |

**Net:** the engine already streams real physics to a proven adapter, and the microscope is already
wired to consume it. What's left is flipping `main.js` from `createSimStub()` to
`createStudioAdapter()` and handling the async cost. The hookup is ~90% done and *proven*, not
theoretical.

---

## 3. The core-vision connection (the "main course")

`[[core-vision-3d-chain-reaction]]` — zoom into the pit, slow time, watch a representative cascade of
real fissions — lands **here**, in `pitscope.js`:

- During the burst window it reads `run.samples[]` and, once per sample, applies the spatial
  fission `sites`: for each site it maps `pos` (cm) → world coords, finds the nearest idle nucleus,
  **flashes it (a fission)**, and spawns ν neutrons (`viz/js/pitscope.js:181-208`).
- It renders `N_NUCLEI = 24000` representative nuclei (in the owner's 10–50k target) — a *sampling*
  of the pit's ~1.5e25 atoms, exactly the Monte-Carlo weight-carrying philosophy.

With `simstub.js` those `sites` are **synthetic**. With `studio-adapter.js` they become the engine's
**real `ref::FissionSource::sites`** — the same 411,645-site stream proven in the demo. **No pitscope
change is needed to make the DATA real** — only the data source flips. That is the payoff the whole
discipline exists for. *(Meeting the full visual bar in §3.1 is a separate, larger render effort —
the data hookup and the render rework are two different things.)*

### 3.1 The core requirement — one holistic spacetime + true 3D (owner, non-negotiable)

The owner's defining statement (2026-08-05): **the chain-reaction-in-the-demon-core moment IS the
project.** Two hard requirements:

1. **ONE holistic spacetime — not separate in time NOR in space.** The demon-core cascade is **not a
   carved-out demo**. It is continuous with everything before it (implosion) and after it
   (fireball/aftermath) on **one single timeline** (the SimClock / D6 staged clock), and it lives in
   the **same one scene** as the rest. During the burst, time is slowed **WAY down** (more than
   anywhere else in the run) — in both the sim and the viz. The camera **DEFAULTS to a close zoom on
   the 3D core** during that band, but the user keeps **full control — zoom out (to see the whole
   device in context), zoom in further, and pan around to really see it all** — because it is a
   **camera move within the SAME continuous scene**, never a mode-switch to a separate view.
2. **True 3D volumetric particles** — the atoms/neutrons are genuine 3D particles (volumetric
   geometry, e.g. instanced spheres), **NOT dots, NOT 2D, NOT flat sprites.**

**Honest gap vs. the current code (`viz/js/pitscope.js`, read 2026-08-05):**

| Requirement | Current pitscope | Gap |
|---|---|---|
| True 3D particles | nuclei + neutrons are `THREE.Points` / `PointsMaterial` — **dots** (`:54`, `:101`) | ❌ needs instanced 3D geometry |
| One scene, zoom = focus | `toggle()` sets `dev.root.visible = !active` — **hides the rest** (`:147-148`), a separate mode | ❌ needs an embedded camera-zoom, device still present |
| One timeline (temporal holism) | `micro.tick(dt, runT)` runs on the **same `runT`** clock + consumes real `samples[]` | ✅ already shared |
| Time slowed WAY down in the burst | the general `#speed` slider (powers of 10) | ◑ present but not yet the emphasized, phase-integrated slow-mo |

So the **data/timeline plumbing is holistic already** (same clock, same real fission stream); the
**render** is split across unreconciled forks (below).

**Correction (2026-08-05, viz-branch archaeology).** True-3D volumetric particles **already exist** —
but only on the **`viz/atom-proto`** branch (`C:/nuclear-atomproto/viz/js/pitscope.js`, commits
`1c62bbd` + `7516532`): nuclei are `InstancedMesh` MeshStandard spheres that heat/swell and **split
into `InstancedMesh` fragments**, and neutrons are `LineSegments` **3D streaks** — 24,000 nuclei, in
the 10–50k target. The **live `main` pitscope is the older `THREE.Points` dots version** (untracked
on main). So the two forks were never reconciled: **`viz/atom-proto` has the true-3D render but no
real-physics adapter; `main` has the real-physics `studio-adapter.js` but ships the dots pitscope.**
And the **zoom-as-focus-not-mode requirement is met by NONE** of the copies — all three do
`toggle() → dev.root.visible = !active` (hide the device = a mode switch), not an embedded zoom.

**Remaining render work, precisely:** (a) bring atom-proto's `InstancedMesh` pitscope onto `main`
beside `studio-adapter.js` (marry true-3D render + real-physics data — the two forks), and (b) rework
the microscope from a mode-switch into an **embedded camera-zoom within the one scene** (default a
close zoom on the core; keep full zoom-out/zoom-in/pan control). **Main-track pledge:** keep the data
a **single continuous clock**, spatially + temporally resolved (`samples[].sites[]` with `t_s`,
`population_series`, `fields.f16` `fission_rate`, one SimClock) so this vision is expressible without
any physics rewrite.

### 3.2 Reconciliation DONE — branch `viz/reconcile-true3d` (2026-08-05, a reviewable hand-off)

Both (a) and (b) above are done, in an **isolated git worktree** (zero contact with the -e session's
live uncommitted `viz/` WIP). Branch **`viz/reconcile-true3d`** (`705dcb9`): `viz/js/pitscope.js`
(513 L) + `viz/js/scenario.js` (67 L). All four ADR-023 clauses met in one file:
- **② true 3D:** atom-proto's `InstancedMesh` sphere-nuclei that heat/swell/**split into instanced
  fragments** + `LineSegments` neutron streaks (no `THREE.Points`).
- **③ zoom = focus, not a mode (the new work):** the old `toggle()` `dev.root.visible = !active` is
  **gone**. The device stays visible; entering **dollies the camera in along the sightline** to a
  close default framing on the pit; the instant the glide lands, **OrbitControls regains full
  control** (zoom out to the whole device, zoom in further, orbit/pan); exit dollies back out. A
  camera-facing cutaway clip (`09 §2`) reveals the core **without** hiding the device — one scene.
- **①/④ one clock + real sites:** consumes `run.samples[].sites[]` on the same `runT`; the site
  contract (pos cm / group / isotope / layer) is **confirmed identical** for `studio-adapter` (real)
  and `simstub` (synthetic), O(N).

**Adoption recipe (for the -e track — a drop-in, no `main.js` change required):** the reconciled
`pitscope.js` keeps the exact `createPitScope({THREE,scene,camera,controls,dev,getRun,getCfg})`
contract the -e `main.js` already calls. (1) drop in `viz/js/pitscope.js` + ensure
`viz/js/scenario.js` present; (2) optional bump `./pitscope.js?v=2`→`?v=3`; (3) optional restore
labels post-exit (`updateLabels`: `|| micro)` → `|| (micro && micro.active))`); (4) if `main.js`
already drives the device clip on open, pass `cutawayDevice:false`. **Verified:** `node --check`-clean,
true-3D present, mode-switch gone, contract matches. **Visual render-verify is the -e adoption step**
(the host wiring is the -e's uncommitted `main.js`, so it can't run standalone here). `main` is
unchanged; this is a reviewable branch, not a merge.

---

## 4. The contract map (verified end-to-end 2026-08-05)

The main track built the C++ data surface to match the viz contract **byte-for-byte** (the comment at
`src/api/studio.cpp:224` says so: *"One GenerationSample → the simstub.js `samples[]` shape (the
pitscope tap)"*). Verified chain:

```
ref::FissionSource::sites            (real MC fission positions, cm)
  └▶ GenerationSample.sites          couple.h:83  (Vec3 pos, int group/isotope/layer; cap 4096/gen, :175)
       └▶ sample_to_json(...)        studio.cpp:224-244  → {pos:[x,y,z], group, isotope, layer}
            └▶ generate_run_json      studio.cpp: j["samples"] = [...]
                 └▶ HTTP /generate-run  (studio_server.py → studio_bridge)
                      └▶ studio-adapter.js  run.samples[] pass-through (no reshaping)
                           └▶ pitscope.js:195  site.pos[0..2] * WORLD_PER_CM → flash + ν neutrons
```

Per-sample fields also match: `n`, `t_s`, `lambda_s`, `k_eff`, `k_prompt`, `log10_population`,
`log10_fissions`, `isotope_shares`, `shell_shares`, `refreshed`, `q`. **The viz can rely on this
shape** — see §9.

---

## 5. Real vs reconstructed (after the swap)

`studio-adapter.js` passes real data through and reconstructs only the presentation layer:

| Real (from nscore) | Reconstructed (presentation) |
|---|---|
| `tally` (03 §5), `run` (03 §6) | `phases` / `duration` (fixed render timeline; names driven by the real `detonate`) |
| `samples[].sites[]` (the chain reaction) | `compression(t)` = `ratio^(-1/3)` — the **real** mass-conserving radius fraction |
| `detonate`, `reasons`, `yield_kt`, `k_eff` | `flux(t)` = the **real** `log10_population` interpolated over the excursion phase |
| `population_series`, per-shell / per-isotope | `fireTimes` / `asym` — detonator geometry (see §6.2) |

---

## 6. The delicate points (the careful, precise act)

1. **Sync → async.** `main.js` calls `evaluate` (per slider tick) and `generateRun` (on commit)
   **synchronously**; the real adapter `fetch`es — a real MC eigen is **seconds**. The swap must
   `await`, **debounce** the gauge (no 2 s eigen per slider drag), and show a "running…" state
   (`02 §3`). This edits `main.js`, which the -e session currently has **uncommitted** — coordinate,
   don't clobber.

2. **Demon-core ⇄ full-device impedance mismatch.** The viz renders the full **32-detonator Fat
   Man** (lenses, tamper, pusher, boron). The real physics (`generate_run`) is a **bare Pu sphere +
   a compression ratio** — it does *not* model the lens array. So the detonator choreography
   (`fireTimes`/`asym`, fault-induced asymmetry) stays **presentation-only** until step 5 + `fast4-b`
   (structural isotopes) bring full-device transport online. The hookup makes the **core** real (k,
   yield, fission sites, population); the implosion asymmetry is honest reconstruction for now.

3. **Data volume.** ~90 MB for a 628-generation burst (the site stream). `pitscope` should sample
   the sites it renders; the C++ side already caps at 4096 sites/generation. A future
   `--max-*` / streaming option on `studio_bridge` can bound it further.

4. **Still SIM cross sections in the demon core.** The algorithm is real + emergent, but
   `DemonCoreAssembly` (M3-T3-g) uses SIM Pu-239/240 xs. `fast4-a` produced **real** Pu-239/240/241
   xs — wiring the demon core to those is a future main-track connection point that makes the viz
   *numbers* cited, not just the *algorithm* real. (Independent of the `main.js` swap.)

5. **Shared working tree.** The -e session develops in `C:\NUCLEAR` with uncommitted `viz/` edits.
   The main track stages **explicit paths only** and never touches `viz/`. When the -e track does the
   `main.js` swap, keep it as its own change so the two tracks don't collide.

---

## 7. Lineage & the not-yet-merged render layer

`C:\nuclear-viz` (earliest: `main`/`simstub`/`device`) → `C:\nuclear-atomproto` (added `pitscope`,
`scenario`, the gadgetlab standalone) → `C:\NUCLEAR\viz` (added the fusion binding:
`studio-adapter.js` + `studio-real-demo.html`).

**Both forks also carry `src/render/{color,fields,raymarch}` + `src/app/nukestudio`** — the **M7
volumetric-fireball render layer** — which is **not yet in the main `C:\NUCLEAR` repo**. That is the
raymarched-fireball / `nukestudio` work (M7-T1/T2/T3), still living only in the prototypes. When it
migrates in, it consumes the same field dumps (`03 §9` / `09-rendering`) the main track keeps stable.

---

## 8. What's left — the `main.js` swap (-e track)

The exact recipe is in `docs/viz-adapter-worklog.md` ("main.js wiring recipe"). In short:

1. import `createStudioAdapter` instead of `createSimStub`.
2. `const sim = createStudioAdapter('http://127.0.0.1:8100')`.
3. `await sim.evaluate(...)` + make `refreshGauge` async + debounce + a "refreshing" state.
4. `run = await sim.generateRun(...)` + async enclosing fn + a "running a real burst…" state.
   - Offline fallback: `location.search.includes('real') ? createStudioAdapter(url) : createSimStub()`.

Run the engine side with: `cmake --build --preset win-x64-rel --target studio_bridge` then
`python tools/studio_server.py --port 8100` (see `docs/FUSION-BINDING.md`).

---

## 9. Main-track contract pledge (what the viz can rely on)

The main track keeps these **spec-stable** so the -e track tracks without churn:

- **`evaluate_json(cfg)`** → `{ k_eff, k_prompt, sigma_pcm, ready }`.
- **`generate_run_json(cfg)`** → `{ detonate, reasons, yield_kt, k_eff_peak, supercritical, quenched,
  non_canonical, tally (03 §5), run (03 §6), samples[] }`.
- **`samples[].sites[]`** = `{ pos:[x,y,z] cm, group, isotope, layer }` — the pitscope tap.
- **cfg keys** are the flat dotted keys `StudioConfig::from_json` reads (`pit.mass_kg`,
  `compression.ratio`, `materials.pu_ga_delta.Pu240`, `initiator.strength_n_per_s`,
  `kinetics.generation_time_s_initial`, `seed`, …).

Any change to these shapes will be flagged loudly in `CHANGELOG`/`SESSIONS` so the viz can follow.
Richer real data later (real `fast4` xs, full-device transport, field dumps for M7) arrives through
these **same** contracts — no viz rewrite.

---

## 10. Open questions & owner / -e notes

*(Append-only — the owner/-e track add directives, questions, and decisions here; the main track
folds answers into the sections above.)*

- **2026-08-05 — owner directive (the core requirement).** The demon-core chain reaction is the
  actual core of the project: **one holistic spacetime** (not separate in time or space — continuous
  with the implosion before and the fireball after, in the same one scene), **time slowed WAY down**
  during the burst (sim + viz), the user **zooms to focus** on the 3D core (a camera move, not a
  separate mode), and the particles are **true 3D volumetric** (not dots / 2D / sprites). Folded into
  **§3.1** with the honest gap-vs-current-`pitscope` table. Main-track keeps the data a single
  continuous clock, spatially + temporally resolved, so the vision is expressible without a physics
  rewrite.

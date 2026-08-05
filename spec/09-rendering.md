# 09 — Rendering Spec

Applies to M7. Consumes field dumps (`03 §9`) and live `TallySink` streams (studio). Everything renders from physics fields — no hand-painted effects.

## 1. Fields

- Grid: 256³ half-float, bbox = 1.5 × outer radius (schema §9). Channels: T[K], ρ, fission_rate, shock_mask.
- Population→temperature mapping: `T[K] = render_temp_scale · (γ−1) · e_deposited / (n_air · k_B)` with γ = 5/3, `n_air = 2.5e19 /cm³` (STP), both cited in `constants.toml`. This is a *visualization* mapping, NOT a physics tally; UI labels it as such (C-20).
- **Calibration (B-17):** at burst t = 1 µs, the peak emission-weighted temperature MUST fall in [7500, 9500] K on the canonical scenario (public anchor: Trinity Brixner footage / Selby-2021 ~8,430 K). Set `render_temp_scale` (C-905) to satisfy this; record the fitted value as SIM with that citation. Unit test: golden frame at 1 µs → hue → inferred T in band.

## 2. Volumetric raymarcher

- Emission–absorption model: per-voxel emission `j = ε·B(T)`, absorption `σ_a ∝ ρ` (scale factor `render_density_scale`, SIM). Step size adaptive: Δx/2 inside high-gradient regions (shock_mask or |∇T| above threshold).
- Blackbody color: Planckian locus approximation (Krystek/CIE-xy polynomial fit is acceptable; exact Planck integrate→XYZ optional) → linear RGB.
- Optional: single-scatter from a key light for the casing era shots (pre-detonation cutaways).
- **Clip plane (ADR-018, M7-T5):** a world-space plane (point + normal, studio view state — NOT scenario data, never in `canonical_hash()`) clips both shell/device rasterization and the volume march: clipped-side voxels contribute no emission or absorption; step-size logic is unchanged. A pure view transform — it reveals computed fields and adds nothing, so the §4 physical-honesty rule is unaffected.
- OptiX AI denoiser MAY be used on studio frames; cinema MUST also ship an undenoised EXR.

## 3. Post

- Pipeline: linear HDR → exposure (manual + auto with `render_exposure` SIM default) → bloom (threshold 1.0, 3 mips) → ACES filmic tonemap → sRGB. Golden-frame regression on the post stack (`11-testing.md`).

## 4. Staged clock visualization (D6)

- BURST phase (0–2 µs): log-time playback (time dilated MOST here, ADR-023); camera defaults to a close zoom on the core; the **demon-core 3D chain-reaction view** dominates — true-3D volumetric nuclei + neutrons (ADR-023, **never flat points**) + the fission heat map. **Neutron trails (ADR-018, M7-T5):** a deterministic sample of transported histories MAY render as fading trajectory trails — the sample is selected by a stream seeded from the run seed (bit-identical across replays) and the sample count is a studio display setting. Trails are exact computed history segments: computed data, not aesthetic noise, so the §4 rule permits them.
- HYDRO phase (µs–ms): cutaway shell motion; shock front from hydro state. The §2 clip plane applies to every phase (shells and volume alike).
- FIREBALL phase (ms–s): exterior volumetric fireball; color per §2 palette evolution (blue-white → white/yellow → orange → red-brown). Timebase switch + scrub (studio).
- **Physical-honesty rule (G4-review):** every visible structure MUST trace to a computed field channel. The physics model is 1D-spherical + scalar ε (05 §4) — it produces NO 3D asymmetry field, so no toroidal/asymmetric structure may be rendered "from asymmetry". Mottling comes ONLY from the fission_rate channel's own MC variance; no aesthetic noise is layered unless an explicit `noise_amplitude` SIM constant (default 0, labeled visualization-only) is set. Early-fireball structure renders from deposition/density/T fields as computed — nothing else.
- **Demon-core chain-reaction view (ADR-023 — the primary deliverable, "the main course").** The signature view: zoom into the pit and watch the fission cascade. Normative requirements: **(1) ONE holistic spacetime** — the same continuous `SimClock` timeline (D6) and the same one scene as the whole device; NOT a separate mode/demo. Time is slowed most in the BURST band (a `~5 s–5 min` playback slider) but the clock is continuous — never a faked cut between phases. **(2) True 3D volumetric particles** — the ~10,000–50,000 bundled nuclei and the neutrons are genuine 3D geometry (e.g. GPU-instanced meshes), **never points / billboards / sprites / 2D.** **(3) Zoom = camera focus, not a mode** — the view is entered by moving the camera; it DEFAULTS to a close zoom on the core, but the user keeps full control (zoom out to the whole device in context, zoom in further, orbit/pan); it MUST NOT hide the rest of the scene. **(4) Representative sampling that tracks the physics** — each rendered atom/fission is a representative sample of the real `ref::FissionSource::sites` from the O(N) GPU MC transport (the `couple.h` `GenerationSample.sites` / studio `samples[].sites[]` tap), **never an O(N²) all-pairs atom sim**; the graphics track the math, nothing faked.

## 5. Cinema looks (`nukecinema --shot`)

- `rapatronic`: ~10 ns-equivalent exposure frames, extreme contrast, visible mottling (from fission_rate channel variance ONLY, §4 rule), near-monochrome warm palette, long-lens compression framing.
- `fastax`: Trinity-footage timing references (frames at 0.016 s / 0.090 s look), color, film grain + gate weave (subtle, SIM-tunable, default on; grain is post-process, labeled non-physical).
- `wide`: tower/landscape scale shot; `cutaway`: layered device cross-section.
- Aesthetic directive (research doc §7): high-contrast, physically grounded, organic light; **no clean synthetic gradients**. Every visible structure must trace to a field channel or a documented, labeled noise/post model.

## 6. Performance

- Studio: ≥ 30 fps at 1920×1080 on the dev GPU (RTX 4070 Ti SUPER, sm_89) **with the simulation advancing** — the authoritative criterion is `08-validation.md` §2 G4 (snapshot threading, 02 §3). Adaptive: reduce march steps before resolution.
- Cinema: no real-time constraint; deterministic per-frame RNG (seed = run seed + frame index).

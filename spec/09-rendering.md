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
- OptiX AI denoiser MAY be used on studio frames; cinema MUST also ship an undenoised EXR.

## 3. Post

- Pipeline: linear HDR → exposure (manual + auto with `render_exposure` SIM default) → bloom (threshold 1.0, 3 mips) → ACES filmic tonemap → sRGB. Golden-frame regression on the post stack (`11-testing.md`).

## 4. Staged clock visualization (D6)

- BURST phase (0–2 µs): log-time playback; camera inside/cutaway; neutron points + fission heat map dominate.
- HYDRO phase (µs–ms): cutaway shell motion; shock front from hydro state.
- FIREBALL phase (ms–s): exterior volumetric fireball; color per §2 palette evolution (blue-white → white/yellow → orange → red-brown). Timebase switch + scrub (studio).
- **Physical-honesty rule (G4-review):** every visible structure MUST trace to a computed field channel. The physics model is 1D-spherical + scalar ε (05 §4) — it produces NO 3D asymmetry field, so no toroidal/asymmetric structure may be rendered "from asymmetry". Mottling comes ONLY from the fission_rate channel's own MC variance; no aesthetic noise is layered unless an explicit `noise_amplitude` SIM constant (default 0, labeled visualization-only) is set. Early-fireball structure renders from deposition/density/T fields as computed — nothing else.

## 5. Cinema looks (`nukecinema --shot`)

- `rapatronic`: ~10 ns-equivalent exposure frames, extreme contrast, visible mottling (from fission_rate channel variance ONLY, §4 rule), near-monochrome warm palette, long-lens compression framing.
- `fastax`: Trinity-footage timing references (frames at 0.016 s / 0.090 s look), color, film grain + gate weave (subtle, SIM-tunable, default on; grain is post-process, labeled non-physical).
- `wide`: tower/landscape scale shot; `cutaway`: layered device cross-section.
- Aesthetic directive (research doc §7): high-contrast, physically grounded, organic light; **no clean synthetic gradients**. Every visible structure must trace to a field channel or a documented, labeled noise/post model.

## 6. Performance

- Studio: ≥ 30 fps at 1920×1080 on the dev GPU (RTX 4070 Ti SUPER, sm_89) **with the simulation advancing** — the authoritative criterion is `08-validation.md` §2 G4 (snapshot threading, 02 §3). Adaptive: reduce march steps before resolution.
- Cinema: no real-time constraint; deterministic per-frame RNG (seed = run seed + frame index).

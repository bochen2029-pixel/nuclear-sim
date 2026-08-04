# GadgetLab — visualization gallery

Screenshots of **GadgetLab**, the schematic browser (Three.js) visualization layer
for NUCLEAR-SIM. The source is in [`viz/`](../../viz/) (run it: `cd viz && python
-m http.server 8099`). This is an **early mock-up / sketch** — a quick in-browser
proof-of-concept for the interactive device view. It is **not yet wired to the
simulation core**; the physics readouts shown (`k_eff`, yield) are the app's own
**synthetic stub**, not the `nscore` Monte-Carlo engine.

> **All data synthetic · schematic only.** Every geometry and number here is
> museum-style / illustrative, from public educational framing — **no real weapon
> data**, consistent with `NOTICE.md` and `00-overview.md §2–§3`. The device is
> rendered at schematic proportions; the "physics" is a placeholder curve. If the
> project later moves to an Unreal Engine renderer, this browser sketch remains a
> useful low-friction mock-up.

## The headline — pit microscope (the fission chain reaction)

Zoom into the plutonium pit during the α-mode excursion and watch the chain
reaction: a bundled volumetric cloud of representative fission events (each marker
≈ 10²¹ atoms), with live readouts — neutrons alive, cumulative fissions, generation
number, log₁₀ N, k_eff, and the generation time Λ. This is the preview of
NUCLEAR-SIM's "main course." The real `nscore` engine already streams the
underlying per-generation fission **sites** (`ref::FissionSource::sites`) and the
temporal `population_series` (03 §5); the frame below is the synthetic stub's,
pending the fusion.

![GadgetLab — pit microscope, the fission chain reaction](05-pit-microscope.png)

## Views

### Armed idle — assembled device
The assembled Fat-Man-type implosion device (casing + tail fins) on its stand;
parameter panel (core mass, enrichment, reflector, compression, HE timing jitter,
initiator), synthetic `k_eff` readout, and the READY / COMMIT–FIRE controls.

![GadgetLab — armed idle, assembled device](01-armed-idle.png)

### Exploded 32-lens HE array
Casing and pusher/tamper toggled off, exploded-view slider raised: the
truncated-icosahedron **32-block HE lens array** (20 hex + 12 pent) around the
core/initiator. Individual lens blocks are clickable (the "lens faults" panel
disables/restores them).

![GadgetLab — exploded 32-lens HE array](02-lens-array-exploded.png)

### Supercritical excursion
Mid-sequence after COMMIT–FIRE: the schematic core glows through the
"supercritical excursion" phase on the staged timeline (time-warp slider slows to
~0.02×).

![GadgetLab — supercritical excursion](03-supercritical-excursion.png)

### Detonation (synthetic)
End of the sequence: the fireball flash and the DETONATION summary card —
*"convergent compression achieved — runaway excursion · estimated yield 20.1 kt ·
synthetic stub — not a real calculation."*

![GadgetLab — detonation summary (synthetic)](04-detonation.png)

### Fizzle (synthetic)
The other emergent outcome — asymmetric implosion (lens blocks disabled) breaks the
compression wave: *"2 lens blocks inert — implosion wave breaks symmetry · nuclear
yield ≈ 0."* In the real engine, detonate-vs-fizzle is not a scripted branch; it
**emerges** from whether the core goes prompt-supercritical and then self-quenches
(`generate_run`, M3-T3-h).

![GadgetLab — fizzle (synthetic)](06-fizzle.png)

---

*Layer: [`viz/`](../../viz/) — `index.html` + Three.js r160 + a synthetic
`simstub.js`. Rendered proportions and physics are schematic placeholders pending
integration with the `nscore` engine.*

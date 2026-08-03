# viz/ — GadgetLab browser visualization (experimental mock-up)

A quick **browser (Three.js r160) sketch** of an interactive device view for
NUCLEAR-SIM — an early proof-of-concept, **not a spec'd module** (it is outside
the `02 §2` core tree by design) and **not yet wired to the simulation core**.
Developed in parallel and brought in here (it was always headed into the repo).

> **All data synthetic · schematic only.** Geometry is museum-style / illustrative
> from public educational framing, and the physics readouts (`k_eff`, yield) are
> the app's **synthetic `simstub.js` placeholder** — NOT the `nscore` Monte-Carlo
> engine. No real weapon data (consistent with `NOTICE.md` and `00-overview.md
> §2–§3`). Screenshots: [`docs/gallery`](../docs/gallery/README.md).

## Run it

The importmap pulls Three.js from a CDN and ES modules need HTTP (not `file://`):

```bash
cd viz && python -m http.server 8099
# then open http://127.0.0.1:8099
```

## Files
- `index.html` — layout, importmap (Three.js r160 via unpkg), disclaimer.
- `js/main.js` — scene, renderer, UI wiring, staged clock / time-warp.
- `js/device.js` — schematic layered device + 32-block HE lens array geometry.
- `js/simstub.js` — **synthetic** stand-in for the physics (k̂, staged phases,
  placeholder yield). Replace with a real `nscore` binding at integration.

## Status / next
Interaction-layer sketch only (exploded view, cross-section, per-lens fault
toggles, commit→fire timeline). Integration with the calculation is future work;
if the project later adopts an Unreal Engine renderer, this stays a low-friction
in-browser mock-up. See `spec/06-frontends.md` (`nukestudio`) for the eventual
first-class studio, and ADR-018 (sandbox-mode interaction extensions).

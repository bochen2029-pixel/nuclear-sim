# The fusion binding — wiring the real engine into the visualizer

*Started 2026-08-04 (`session-2026-08-03-d`), on the owner's direction to begin the
fusion binding. This is the C++↔viz seam that replaces the visualizer's synthetic
stub (`viz/js/simstub.js`) with the REAL demon-core Monte-Carlo physics.*

## The picture

`viz/js/main.js` calls only `evaluate(cfg)` and `generateRun(cfg, …)`. Today those
come from `viz/js/simstub.js` — a **synthetic** placeholder (every viz screenshot
says "all data synthetic"). The engine that makes them real already exists:
`src/api/` (`evaluate` / `generate_run`, M3-T3-g/h) runs a genuine MC eigen + the
emergent disassembly burst and returns **data only** — the `03 §5` tally, the
`03 §6` run provenance, and the per-generation **sample/fission-site stream** (the
volumetric chain-reaction "main course" tap).

The binding has two halves:

1. **The C++ DATA surface (this track — DONE).** `evaluate_json(cfg)` /
   `generate_run_json(cfg)` (already in `src/api/studio.h`) are the
   mechanism-independent JSON in/out. Exposed as a callable bridge:
   - **`studio_bridge`** (`src/app/studio_bridge/`, a non-test exe): `studio_bridge
     evaluate` / `studio_bridge generate-run`, cfg JSON on stdin → result JSON on
     stdout. Proven live: a 6.5 kg / 2.4× pit → `k_eff 0.97, ready false`; a 9 kg /
     2.5× pit → **detonate**, `k_peak 1.013` → self-quench → `1.62 kt`, with the
     628-generation sample/site stream.
   - **`tools/studio_server.py`** (stdlib, no deps): a local HTTP dev-server that
     shells to the bridge — `POST /evaluate`, `POST /generate-run`, `GET /health`.
     Proven live over HTTP. This is the drop-in the browser viz fetches from, with
     **zero new C++ deps and no Emscripten**.

2. **The JS adapter (viz/ track — the -e session's domain, NOT done here).** Replace
   `viz/js/simstub.js` with an adapter that (a) `fetch`es `/evaluate` +
   `/generate-run` from `studio_server` (debounced — a real eigen takes seconds, the
   honest cost of real physics), and (b) reconstructs the **presentation closures**
   `run.compression(t)` / `run.flux(t)` from the returned data (interpolate the real
   `population_series` / `samples` for flux; the geometry-radius trajectory for
   compression). `main.js` stays unchanged; **`simstub.js` is the thing replaced**
   (the viz-seam note / the `viz-seam-coordination` memory).

## Mechanism decision (owner-delegated, 2026-08-04)

The browser calls C++ one of three ways; all wrap the SAME
`evaluate_json`/`generate_run_json` data surface, so the bridge above is common to
all of them:

| Mechanism | Verdict |
|---|---|
| **Local dev-server** (`studio_server.py` → `studio_bridge`) | **Chosen for now.** No new deps, no exotic toolchain, uses the full engine (CPU now, CUDA when built). Lights up the existing browser viz immediately. |
| **WASM** (Emscripten) | Deferred — Emscripten isn't on the dev box, and WASM can't use CUDA (the demon core is CPU/ref, so it *could* work later for a pure-web shippable build). |
| **Native / Electron addon** | The **Steam product** path (M7, `nukestudio`) — embeds the engine in-process. Biggest lift; comes with the desktop app. |

## Run it

```bash
cmake --build --preset win-x64-rel --target studio_bridge   # build the bridge
python tools/studio_server.py --port 8100                    # serve it
# then, from JS:  fetch('http://127.0.0.1:8100/generate-run', {method:'POST', body: cfgJson})
```

## Notes / caveats

- **Debounce.** `evaluate` ≈ 1–3 s, `generate-run` ≈ seconds (a real MC solve). The
  UI must debounce; simstub's instant formula does not exist here.
- **`generate-run` output is large** (the sample/site stream — ~90 MB for a full
  628-generation burst). The JS binding should sample/stream it for rendering; the
  C++ side already caps sites per generation (`GenerationSample.sites` is a
  stratified sample), but the count × generations is still big. A future
  `--max-*` / streaming option on the bridge can bound it if needed.
- **CORS** is wide-open in the dev-server (dev-only; the viz is a separate origin).
- **cfg keys** are the flat dotted keys `StudioConfig::from_json` reads
  (`pit.mass_kg`, `compression.ratio`, `materials.pu_ga_delta.Pu240`,
  `initiator.strength_n_per_s`, `kinetics.generation_time_s_initial`, `seed`, …).
- Still **SIM cross sections** until `fast4` (M1-T4a-2): the mechanism/algorithm is
  real and emergent; the cited numbers arrive with `fast4`.

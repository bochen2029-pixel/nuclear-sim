# Viz JS adapter — worklog (session-2026-08-03-d, 2026-08-04)

Owner directive: "try the viz JS adapter and note everything you do with that on disk as file."
This file records EVERYTHING I do with the viz JS adapter (the -e/viz-track half of the fusion
binding — replacing the synthetic `viz/js/simstub.js` with a real adapter that fetches the
`studio_server` (over `studio_bridge`) and reconstructs the presentation closures).

## Standing hazard (why I am careful + recording this)
`viz/` is normally the -e session's domain, and right now it has UNCOMMITTED -e edits
(`git status`: M viz/README.md, viz/index.html, viz/js/device.js, viz/js/main.js,
viz/js/simstub.js, viz/style.css). To avoid clobbering that in-progress work I will:
- Prefer NEW files (a self-contained adapter module) over editing -e's uncommitted files.
- If I must touch an -e file, make the change minimal + record the exact diff here.
- NOT `git commit` viz/ changes (that would bundle -e's uncommitted work) unless the owner
  says so — the deliverable here is the on-disk adapter + this record.

## Log
- (start) Created this worklog. Next: survey the seam.

### Findings (survey)
- **simstub interface** (`viz/js/simstub.js`): `createSimStub()` → `{ evaluate, generateRun }`.
  - `evaluate(cfg)` → `{ k_eff, k_prompt, sigma_pcm, ready }` — EXACTLY the `studio_bridge evaluate` output.
  - `generateRun(cfg, detonators, blockDirs)` → a rich object:
    `{ phases, duration, detonate, reasons, fireTimes, asym, asymAmt, compTarget, flux, compression,
       tally, run, samples, context, report, non_canonical, kEff, yieldKt }`.
    `flux`/`compression` are CLOSURES f(t); `samples[]` carry per-generation `sites[]` (the pitscope
    "main course"); its sample fields (n,t_s,lambda_s,k_eff,k_prompt,log10_population,log10_fissions,
    isotope_shares,shell_shares,refreshed,q,sites{pos,group,isotope,layer}) EXACTLY match the C++
    `generate_run_json` samples.
- **C++ `generate_run_json`** returns the REAL DATA: `detonate, reasons, yield_kt, k_eff_peak,
  k_prompt_peak, supercritical, quenched, non_canonical, tally (03 §5), run (03 §6), samples[]`.
- **Mapping** (real ← C++ / reconstruct ← presentation):
  - REAL: tally, run, samples (real fission sites!), detonate, reasons, yieldKt, kEff, non_canonical, report.
  - RECONSTRUCT: `phases`/`duration` (fixed render timeline; names driven by real `detonate`);
    `fireTimes`/`asym`/`asymAmt` (detonator geometry — the bare demon core doesn't model lenses);
    `compTarget` = ratio^(-1/3) (REAL compression radius fraction); `compression(t)` = 1→compTarget
    over the comp phase; `flux(t)` = the REAL samples' log10_population interpolated over the
    excursion phase (the honest reconstruction the viz-seam note names).
- **ASYNC issue (decisive):** main.js calls `sim.evaluate` (line 189) + `sim.generateRun` (line 257)
  SYNCHRONOUSLY. The real adapter must `fetch` (async, seconds). A drop-in needs `await` at those
  sites (+ async enclosing fns + a debounce/loading state). That edits `main.js` — an -e-uncommitted
  file. **Decision: DON'T edit main.js/simstub.js. Deliver NEW files** — `viz/js/studio-adapter.js`
  (the async fetch-backed adapter, simstub-shaped) + `viz/studio-real-demo.html` (a self-contained
  page proving it against studio_server) — and DOCUMENT the main.js wiring recipe here for the -e track.

### Built (NEW files — no -e file touched)
- `viz/js/studio-adapter.js` — `createStudioAdapter(baseUrl)` -> `{ evaluate, generateRun }` (async).
  evaluate fetches POST /evaluate; generateRun fetches POST /generate-run then reconstructs the
  simstub-shaped object: REAL tally/run/samples(+sites)/detonate/reasons/yield pass through;
  `compression(t)=1->ratio^(-1/3)` and `flux(t)` = the real samples' log10_population interpolated
  over the excursion phase; detonator geometry (fireTimes/asym) is presentation-only reconstruction.
- `viz/studio-real-demo.html` — a self-contained proof page (evaluate / generate-run buttons).

### Fixed
- `tools/studio_server.py`: added `do_OPTIONS` (CORS preflight). The browser's cross-origin POST with
  a JSON content-type sends an OPTIONS preflight first; the server had only GET/POST -> 501 -> the
  browser blocked the POST ("Failed to fetch"). curl skips preflight, so the host smoke had passed.

### PROVEN LIVE in the browser (studio_server :8100, viz served :8099, Chrome-pane fetch)
- `evaluate({pit 9kg, comp 2.5})` -> k_eff 1.094, k_prompt 1.092, ready TRUE (prompt-supercritical).
- `generateRun` -> detonate TRUE, yield 1.621 kt, k_peak 1.013, quenched TRUE, 628 generations,
  411,645 REAL fission sites across 628 sample-generations (the pitscope "main course"), real reasons.
  Reconstructed closures: flux(3.0/3.6/4.2)=1/124/1398 (rising along the real population);
  compression(1.6/3.0)=1/0.737 (the real ratio^(-1/3)). First real site pos=[1.60,0.14,2.68] cm.
  ~17 s wall (real eigen + 628-gen burst -- the honest cost; the UI must debounce / show "running").

### main.js wiring recipe (for the -e track -- NOT applied here, to protect -e's uncommitted main.js)
Drop-in EXCEPT evaluate/generateRun become async. In `viz/js/main.js`:
1. line 14:  import createSimStub from './simstub.js?v=2'  ->  import createStudioAdapter from './studio-adapter.js'
2. line 154: const sim = createSimStub();  ->  const sim = createStudioAdapter('http://127.0.0.1:8100');
3. line 189: const {...} = sim.evaluate({...});  ->  const {...} = await sim.evaluate({...});
   (+ make the enclosing fn async; debounce -- a real eigen is ~2 s -- + show a "refreshing" state, 02 §3)
4. line 257: run = sim.generateRun({...}, detonators, dirs);  ->  run = await sim.generateRun(...);
   (+ async enclosing fn + a "running a real Monte-Carlo burst…" state; ~seconds; trigger on COMMIT/fire)
   Offline fallback: `const sim = location.search.includes('real') ? createStudioAdapter(url) : createSimStub();`
   Note: generateRun output is large (~90 MB for 628 gens) -- pitscope should sample the sites it renders.

### Commit decision
Staged by EXPLICIT path (studio-adapter.js, studio-real-demo.html, studio_server.py, this worklog) so the
-e session's uncommitted main.js/simstub.js/etc. are NEVER swept in. main.js integration left to -e (its domain).

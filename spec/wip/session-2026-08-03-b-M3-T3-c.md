# WIP — session-2026-08-03-b — M3-T3-c (real-transport adapter + fission-site stream)

Strategic pivot (owner-delegated 2026-08-03): build toward the visualizer's
fusion contract, prioritizing the demon-core chain-reaction data pipeline.

## Findings (append BEFORE acting)
- **Fusion contract is defined by `viz/js/simstub.js`** ("the integration seam"):
  UI calls only `evaluate(cfg) → {k_eff,k_prompt,sigma_pcm,ready}` and
  `generateRun(cfg,detonators,dirs) → {tally(03§5), run(03§6)}`. nscore must
  eventually expose these; swap the stub → front-end unchanged. The stub's
  `detonate = faults≤1 && symmetry>0.55` is the HARDCODED rule to make emergent.
- **Transport ALREADY emits spatial sites:** `ref_generation.cpp` fills
  `source.sites` = `FissionSite{pos(Vec3),group,isotope,layer}` per generation.
  The volumetric chain-reaction data exists in-engine; it just isn't streamed.
- **fast4-INDEPENDENT:** real transport runs on ANY loaded xs (synthetic-real for
  tests, e.g. the mixed Pu/U sphere); fast4 supplies CITED numbers, not the
  algorithm. Honesty ladder: simstub(fake) → real-transport+synthetic-xs(now) →
  real-transport+cited-xs(fast4).
- **Adapter shape:** `RefTransport(geom, materials, xs, seed)` per call + `run_eigen`.
  materials/xs captured by ref (must outlive the EigenFn). EigenSpec captured.
- **Site stream design (NEW contract — 05 §5 / 03 §9 area):** extend
  GenerationSample with a stratified SAMPLE of source.sites (cap ~N per gen; the
  10-50k budget lives on the render side). Keep pos+isotope+layer+generation.
  Coordinate the shape with -e (they render it). Amendment: document in spec.
- **Shared working tree with -e:** commit ONLY my files (git add explicit paths);
  never `git add -A` from root. Recommended -e get its own worktree.

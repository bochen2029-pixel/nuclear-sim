# WIP — M1-T4a-2a (fast4-a: ENDF/B-VIII.0 collapse for the 8 benchmark isotopes)

Session `session-2026-08-04-a` (2026-08-04). Owner authorized **Path B** (ENDF collapse).
Append findings BEFORE acting (README §5.4). Fold + delete at END.

## The task
Produce `data/xs/fast4.json` (schema v2, `03 §2`) for the **8 benchmark isotopes** by
collapsing **ENDF/B-VIII.0** (public, NNDC) to the fixed 4-group fast structure, then
**measure G0a/G0b** and report the real pcm.

- Isotopes (a-set): **U-234, U-235, U-238, Pu-239, Pu-240, Pu-241, Ga-69, Ga-71**.
- Group bounds (fixed, `03 §2`): `[20, 3, 1, 0.1, 1e-3] MeV` → 4 groups, 0-based high→low.
- Per isotope: `nu` (TOTAL ν̄), `chi`, `sigma_f`, `sigma_c` (capture only), `sigma_s`,
  `sigma_n2n` (opt), `mu_bar`, scalar `beta`, 4×4 no-upscatter `transfer`, `cite`, `status`.
  `sigma_t` is loader-computed — MUST NOT be in the file. Barns.

## Targets (from data/benchmarks/*.md; band from 08 §1)
| Gate | Assembly | k target | atom densities (nuclei/b-cm) | R (cm) |
|---|---|---|---|---|
| G0a | Godiva HEU sphere | 1.0000 ± 0.0010 | U235 0.045000, U238 0.002498, U234 0.000492 | 8.741 |
| G0b | Jezebel Pu-Ga sphere | 1.0000 ± 0.0020 | Pu239 0.037050, Pu240 0.001751, Pu241 0.000117, Ga69 8.26485e-4, Ga71 5.48515e-4 | 6.385 |

Gate band = `500 pcm + benchmark_uncertainty_pcm`; PUBLIC-DERIVED unverified unc → **0**,
so effective **±500 pcm** (k ∈ [0.99500, 1.00500]). pcm = (k−1)·1e5.

## HARD discipline (owner, restated — do not cross)
1. **Fast-spectrum weighting, documented.** These are fast metal assemblies. Weight the
   group collapse with a representative fast spectrum (fission + slowing-down tail, or a
   fundamental-mode fast spectrum) — NOT generic 1/E or thermal. State the spectrum + why.
2. **Measure, don't assume.** Run the eigen on Godiva/Jezebel, read k, report actual pcm.
   Iterate the **weighting**, never the xs.
3. **NEVER fit the xs to the benchmark k.** Numbers come only from the documented ENDF
   collapse. An honest 4-group set that can't clear ±500 pcm is the RESULT — surface it
   (possibly an ADR to revisit the group structure). Fudging is the exact failure the
   provenance discipline exists to prevent (same rule as c_a never being gate-fitted).
4. **Provenance per isotope:** `cite` = "ENDF/B-VIII.0" + collapse method/weighting;
   honest `status`. `sigma_t` loader-computed, out of the file.

## Plan
- [ ] Scaffold (route-independent): fill-in template, per-isotope provenance manifest,
      the weighting definition, the JSON emitter, the group-collapse math.
- [ ] Tooling route (research agent running): pick the installable ENDF-reading path
      (openmc.data / WSL / direct ACE) → install (owner-authorized) → download ENDF/B-VIII.0.
- [ ] Collapse: pointwise σ(E) per reaction × fast φ(E) → 4-group constants → fast4.json.
- [ ] Measure: build Godiva/Jezebel assembly (as test_benchmarks does), run_eigen → k → pcm.
- [ ] Iterate weighting if outside band; if honest 4-group misses, surface + consider ADR.
- [ ] Cross-check vs Hansen-Roach if obtainable.

## Findings (append as I go)
- `data/xs/` is empty (the blocker). Materials (u_godiva, pu_ga_jezebel) + benchmark cards
  exist; `scenarios/*.toml` do NOT — benchmark assembly is programmatic (test_benchmarks).
- (research agents dispatched: ENDF tooling route; Hansen-Roach LAMS-2543 availability.)

# `tools/xs` — the `fast4` cross-section collapse pipeline (M1-T4a-2)

Produces `data/xs/fast4.json` — 4-group fast cross sections — by collapsing public
**ENDF/B-VIII.0** evaluated nuclear data. Owner-authorized Path B (2026-08-04). This
directory is the *method of record*: the numbers in `fast4.json` come ONLY from running
this pipeline, never by hand, and never tuned to a benchmark result.

## What it does

```
ENDF/B-VIII.0 (public, NNDC)  ->  pointwise sigma(E), nu(E), chi(E), scatter kernels
        |                                    |
        |                          weight with a documented FAST spectrum phi(E)
        v                                    v
   per reaction, per isotope   -->   4-group averages  -->  data/xs/fast4.json
                                              |
                                     measure G0a/G0b  -->  report real pcm
```

- **Group structure (fixed, `03 §2`):** bounds `[20, 3, 1, 0.1, 1e-3] MeV`, 4 groups,
  0-based high->low. Group `g` spans `(bounds[g+1], bounds[g]]`.
- **a-set (M1-T4a-2a):** U-234/235/238, Pu-239/240/241, Ga-69/71 -> opens G0a/G0b.
- **b-set (M1-T4a-2b):** Al-27, B-10, Be-9, C-12, H-1, N-14, O-16, Po-210 -> full device.
- Per-field extraction + provenance: `data/xs/PROVENANCE-fast4.md`.

## Weighting (the decision that makes or breaks a fast collapse)

A 4-group group-constant is `sigma_g = integral_g sigma(E) phi(E) dE / integral_g phi(E) dE`.
The weight `phi(E)` is an *approximation of the real in-medium flux*; a coarse 4-group
set is only as good as this choice. Godiva and Jezebel are **bare, unmoderated fast-metal
critical assemblies**, so their flux is a HARD FAST spectrum, and the weight reflects that:

- **High energy (E >~ 0.82 MeV): a fission (Watt) source spectrum** —
  `chi(E) = C * exp(-E/a) * sinh(sqrt(b*E))`, the neutrons are born here and the flux is
  dominated by the fission source. (Generic fast source: U-235 Watt parameters
  a = 0.988 MeV, b = 2.249 /MeV — a standard, citable fast-fission spectrum; Pu-239 is
  marginally harder, handled by the weighting-iteration step if needed.)
- **Slowing-down region (1 keV <~ E <~ 0.82 MeV): 1/E** — flux per unit lethargy is
  ~flat as neutrons slow by inelastic/elastic scattering on the heavy metal. Harder than
  a moderated system (no light moderator; energy loss is in large inelastic steps).
- **No thermal peak.** The assembly is bare fast metal; there is negligible flux below
  ~1 keV — which is also the lowest group bound (1e-3 MeV), so the thermal region is
  entirely off-grid and irrelevant here.

**This is explicitly NOT a generic 1/E or thermal weight.** A thermal/1/E weight would
over-weight the low-energy tail — e.g. inflating U-238 / Pu-240 capture in resonances a
fast assembly barely samples — and bias every group constant. The chosen weight is the
standard "infinite-medium fast" starting point.

**Iteration rule (owner discipline):** if the measured G0a/G0b pcm misses the +/-500 pcm
band, iterate `phi(E)` toward the assembly's *self-consistent* spectrum (compute the
assembly flux, re-weight, repeat) — **iterate the WEIGHTING, never the cross sections.**
Cross sections are the documented collapse output; fitting them to the gate is the exact
failure the provenance discipline exists to prevent (same rule as `c_a` never being
gate-fitted). An honest 4-group set that cannot clear the band is a legitimate result
(and may mean 4 groups is too coarse for these benchmarks — an ADR to revisit the group
structure), to be surfaced, not fudged.

`phi(E)` is implemented in `weighting.py` (route-independent; the same tabulation feeds
either a hand-rolled group average or an NJOY GROUPR weight card).

## Running

The ENDF-reading front end (which library, where the data is pulled from) is selected in
`build_fast4.py` once the tooling route is fixed (see the session WIP journal / SESSIONS).
Candidates: `openmc.data` (pure-Python ENDF/ACE/HDF5 reader) with a hand-rolled group
average, or NJOY GROUPR under WSL. Either way the output is byte-for-byte the same JSON
and consumes the same `weighting.py` spectrum. The collapse writes `data/xs/fast4.json`
and stamps `data/xs/PROVENANCE-fast4.md` with the recorded MAT/eval/temperature and the
measured pcm.

## Discipline (non-negotiable)

1. Numbers come ONLY from the documented ENDF collapse. Never hand-edit `fast4.json`.
2. Document the weighting; state the spectrum and why.
3. Measure G0a/G0b, report the real pcm, iterate the weighting (not the xs).
4. Full per-isotope provenance; `status` honest; `sigma_t` stays loader-computed.

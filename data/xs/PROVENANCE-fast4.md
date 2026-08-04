# `fast4` — provenance manifest (per isotope)

**Dataset:** `data/xs/fast4.json` — few-group (4-group) fast neutron cross sections,
schema v2 (`03 §2`). **Authored by M1-T4a-2a** (owner-authorized Path B, 2026-08-04).

> Records WHERE every number comes from and HOW it was produced. Every value is from the
> documented collapse (`tools/xs/build_fast4.py` + `weighting.py`); NONE is tuned to a
> benchmark (the c_a discipline — see ADR-022).

## Source (all isotopes)

- **Evaluated library:** **ENDF/B-VIII.0** (public; U.S. CSEWG/NNDC — NOT ICSBEP, so BLK-14
  is respected, no restricted-access handbook data used).
- **Form used:** the **pre-reconstructed HDF5** distribution (openmc.org official ENDF/B-VIII.0
  library, low-temperature set), Zenodo mirror record **8410375** (`endfb80-lowtemp.tar.xz`),
  retrieved **2026-08-04**. Fully resonance-reconstructed + Doppler-broadened to **293.6 K**
  (`294K` grid) — the assemblies are at room temperature. *(The raw ENDF-6 files were also
  fetched from IAEA (`www-nds.iaea.org/public/download-endf/ENDF-B-VIII.0/n/`) but this
  openmc build lacks the resonance-reconstruction extension — raw ENDF's smooth background is
  zero inside the resonance region — so the pre-reconstructed HDF5 is the source of record.)*
- **Processing:** pointwise σ(E) at 293.6 K → group-averaged to the 4-group structure with the
  documented fast-metal weight φ(E) (below) → `data/xs/fast4.json`. Pipeline: `tools/xs/`.
- **Group structure (fixed, `03 §2`):** bounds `[20, 3, 1, 0.1, 1e-3] MeV`; groups 0-based,
  high→low; group g spans `(bounds[g+1], bounds[g]]`.
- **`status` = `"PUBLIC"`** — the underlying evaluation is public ENDF/B-VIII.0. The `cite`
  string additionally names the collapse. A 4-group collapse of a hard-fast system is an
  APPROXIMATION whose fidelity is measured against G0a/G0b and reported (ADR-022), never assumed.

## Isotopes (a-set: M1-T4a-2a → G0a/G0b)

| Isotope | ENDF/B-VIII.0 MAT | Temp | Used by |
|---|---|---|---|
| U-234  | 9225 | 293.6 K | Godiva |
| U-235  | 9228 | 293.6 K | Godiva |
| U-238  | 9237 | 293.6 K | Godiva |
| Pu-239 | 9437 | 293.6 K | Jezebel |
| Pu-240 | 9440 | 293.6 K | Jezebel |
| Pu-241 | 9443 | 293.6 K | Jezebel |
| Ga-69  | 3125 | 293.6 K | Jezebel (Ga alloy) |
| Ga-71  | 3131 | 293.6 K | Jezebel (Ga alloy) |

*(MATs verified against the IAEA ENDF/B-VIII.0 filenames. b-set — Al-27, B-10, Be-9, C-12,
H-1, N-14, O-16, Po-210 — is M1-T4a-2b; not needed for G0a/G0b.)*

## Per-field method

| Field | ENDF source | Collapse |
|---|---|---|
| `sigma_f`   | MT=18 fission                 | σ_g = ∫_g σ(E)φ(E)dE / ∫_g φ(E)dE (trapezoid on the nuclide's own grid) |
| `sigma_c`   | MT=102 radiative capture only | flux-weighted group average |
| `sigma_s`   | MT=2 elastic + MT=4/51-91 inelastic | flux-weighted group average (elastic + inelastic) |
| `sigma_n2n` | MT=16 (n,2n)                  | flux-weighted group average |
| `nu`        | MT=452 TOTAL ν̄               | fission-rate weighted: ν̄_g = ∫ν σ_f φ / ∫σ_f φ |
| `chi`       | MT=18 prompt fission spectrum (ContinuousTabular), incident-averaged | fraction emitted into group g; Σχ_g = 1 |
| `beta`      | delayed (MT=455) / total ν̄   | scalar, fission-rate weighted over the fast range |
| `mu_bar`    | MT=2 elastic angular (Tabular) | ⟨μ_el·σ_el·φ⟩ / ⟨(σ_el+σ_inel)·φ⟩ — the transport-correction cosine (elastic anisotropy, diluted by ~isotropic inelastic) |
| `transfer`  | elastic kinematics + MT=51-90 (LevelInelastic) + MT=91 (CorrelatedAngleEnergy/KalbachMann) | elastic ~within-group (heavy-A); inelastic downscatter from the ENDF secondary spectra; rows sum to 1; no upscatter |

`sigma_t` is loader-computed (`σ_t = σ_f + σ_c + σ_s + σ_n2n`, `03 §2`) — NOT in the file.
`transfer` is a real matrix (gate runs reject null-transfer).

## The weighting spectrum (documented — `tools/xs/README.md §Weighting`)

Godiva/Jezebel are bare, unmoderated fast-metal critical assemblies, so φ(E) is a **hard fast
spectrum** — a Watt fission source (a=0.988 MeV, b=2.249 /MeV) for E ≥ 0.82 MeV joined to a
1/E slowing-down tail, with NO thermal peak — explicitly NOT a generic 1/E or thermal weight.
A single documented weight is used (the fixed-weight choice); iterating it toward each
assembly's self-consistent spectrum is a deferred path to close the residual (ADR-022 path a).

## Validation — measured, not assumed (via `gate_probe`, 200k × 40+150)

The reference MC eigen (`ref/`, with the ADR-021 consistent transport correction) on the
committed benchmark scenarios. Band = 500 pcm + benchmark_uncertainty_pcm; the PUBLIC-DERIVED
unverified uncertainty → 0 (`08 §1`), i.e. **±500 pcm**.

- **G0a Godiva:**  k = **1.02562 ± 12 pcm**  →  **+2562 pcm**  (band ±500) — **outside** (high)
- **G0b Jezebel:** k = **1.01581 ± 24 pcm**  →  **+1581 pcm**  (band ±500) — **outside** (high)

**Honest result (ADR-022):** both ~1.5–2.5% high — the coarse 4-group residual with a fixed
weight, NOT an xs defect and NOT fudged. The physics is right (ADR-021 took the same set from
~15–22% off to this); the fixed 4-group structure is simply too coarse to clear ±500 pcm for
these fast-metal benchmarks. Deferred paths to close it: self-consistent weighting, or a finer
group structure — never tuning the cross sections.

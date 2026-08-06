# `fast4` — provenance manifest (per isotope)

**Dataset:** `data/xs/fast4.json` — few-group fast neutron cross sections, **schema v3
(5-group: 4 fast + 1 thermal, `03 §2` / ADR-024)**. **Authored by M1-T4a-2a** (a-set, 8
isotopes; owner-authorized Path B, 2026-08-04); **extended to 16 isotopes by M1-T4a-2b**
(b-set); **thermal group added by M1-T4a-3** (ADR-024); **collapse weight made
self-consistent per benchmark assembly by M1-T4a-5** (ADR-025, 2026-08-06).

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

## Isotopes (b-set: M1-T4a-2b → full-device transport, step 5)

Structural / tamper-pusher / high-explosive / initiator isotopes. Same source (ENDF/B-VIII.0
pre-reconstructed HDF5, 293.6 K) + same pipeline. **NOT used by G0a/G0b** (bare U/Pu spheres).

| Isotope | ENDF/B-VIII.0 file | Role |
|---|---|---|
| Al-27  | Al27.h5  | pusher / structural |
| B-10   | B10.h5   | neutron absorber (its role is (n,α)) |
| Be-9   | Be9.h5   | initiator (Po-Be source), reflector |
| C-12   | C12.h5   | high explosive (Comp B), moderator |
| H-1    | H1.h5    | high explosive (Comp B), the strong moderator |
| N-14   | N14.h5   | high explosive (Comp B) |
| O-16   | O16.h5   | high explosive (Comp B) |
| Po-210 | Po210.h5 | initiator alpha source |

**Pipeline enhancement the b-set required (M1-T4a-2b), and its a-set impact:** the a-set collapse
was written for actinides and mishandled two channels the light/absorber b-set exposes — it counted
only (n,γ) as absorption (dropping B-10's dominant (n,α)) and treated ALL elastic as within-group
(dropping H-1's moderation). Both were fixed in `build_fast4.py`. The fixes are **gate-safe by
construction**: the multi-channel absorption changes the a-set only where the charged-particle
channels are non-zero (Ga's (n,p)/(n,α), a real correction — but Ga is NOT in Godiva/Jezebel; U-235
shifts one 6th-decimal, U-238/Pu-239/Pu-240 are byte-identical), and the finite-A elastic downscatter
is applied only for A<30 so the heavy a-set elastic is byte-identical to the committed set. Net:
**Jezebel (G0b) byte-identical; Godiva (G0a) <<1 pcm; G0c (a differential) unaffected.** *Known
limitation / follow-up:* the A<30 isotropic-CM elastic-downscatter approximation is accurate for the
light b-set but the exact treatment is the anisotropic kernel from the ENDF elastic p(μ); it matters
for the (peripheral) HE region in full-device transport, not for the current gates.

## Per-field method

| Field | ENDF source | Collapse |
|---|---|---|
| `sigma_f`   | MT=18 fission                 | σ_g = ∫_g σ(E)φ(E)dE / ∫_g φ(E)dE (trapezoid on the nuclide's own grid) |
| `sigma_c`   | disappearance: MT=102 (n,γ) + MT=103-107 (n,p)/(n,d)/(n,t)/(n,³He)/**(n,α)** | flux-weighted group average of the SUM. For the a-set actinides the charged-particle channels are ~0 (sigma_c unchanged to <1e-6 b); for the b-set they DOMINATE — B-10's absorber role is entirely (n,α) (~0.22 b @1 MeV), not (n,γ) (~6e-5 b) (M1-T4a-2b) |
| `sigma_s`   | MT=2 elastic + MT=4/51-91 inelastic | flux-weighted group average (elastic + inelastic) |
| `sigma_n2n` | MT=16 (n,2n)                  | flux-weighted group average |
| `nu`        | MT=452 TOTAL ν̄               | fission-rate weighted: ν̄_g = ∫ν σ_f φ / ∫σ_f φ |
| `chi`       | MT=18 prompt fission spectrum (ContinuousTabular), incident-averaged | fraction emitted into group g; Σχ_g = 1 |
| `beta`      | delayed (MT=455) / total ν̄   | scalar, fission-rate weighted over the fast range |
| `mu_bar`    | MT=2 elastic angular (Tabular) | ⟨μ_el·σ_el·φ⟩ / ⟨(σ_el+σ_inel)·φ⟩ — the transport-correction cosine (elastic anisotropy, diluted by ~isotropic inelastic) |
| `transfer`  | elastic kinematics + MT=51-90 (LevelInelastic) + MT=91 (CorrelatedAngleEnergy/KalbachMann) | **light nuclei (A<30): finite-A elastic downscatter** (isotropic-CM, E′ uniform in [αE, E], α=((A-1)/(A+1))²) — H-1 moderates; **heavy nuclei (A≥30): elastic within-group** (forward-peaked, negligible energy loss — and byte-identical to the committed a-set). Inelastic downscatter from the ENDF secondary spectra. Rows sum to 1; no upscatter (M1-T4a-2b) |

`sigma_t` is loader-computed (`σ_t = σ_f + σ_c + σ_s + σ_n2n`, `03 §2`) — NOT in the file.
`transfer` is a real matrix (gate runs reject null-transfer).

## The weighting spectrum (documented — `tools/xs/README.md §Weighting`)

Godiva/Jezebel are bare, unmoderated fast-metal critical assemblies, so the **analytic** weight
φ(E) is a **hard fast spectrum** — a Watt fission source (a=0.988 MeV, b=2.249 /MeV) for
E ≥ 0.82 MeV joined to a 1/E slowing-down tail (+ a 293.6 K Maxwellian in the v3 thermal group),
explicitly NOT a generic 1/E or thermal weight.

**Self-consistent per-assembly weighting (M1-T4a-5, ADR-025 — ADR-022 path a).** The analytic
weight above is now the DEFAULT (retained for the 8 structural/light isotopes). For the **8
benchmark isotopes** it is replaced by **that assembly's own fundamental-mode flux spectrum**,
computed by openmc continuous-energy k-eigenvalue transport on the exact benchmark geometry
(`tools/xs/assembly_spectrum.py` → committed `data/xs/weights/{godiva,jezebel}.spectrum.json`,
seeded; map `fast4_weights.json`): **U-234/235/238 ← Godiva flux** (median 0.94 MeV);
**Pu-239/240/241 + Ga-69/71 ← Jezebel flux** (median 1.32 MeV — much harder than the analytic
weight, which the fixed weight could not represent). The thermal group keeps the analytic
Maxwellian (spliced below the 1 keV fast floor). openmc supplies the flux SHAPE only — no cross
section is touched, and no weight is tuned toward a benchmark k (openmc's own k = 1.00019 /
0.99988 ≈ 1.000, confirming geometry, but is never copied). The 8 structural isotopes stay
byte-identical to the ADR-024 set (verified).

## Validation — measured, not assumed (via `gate_probe`, reference MC eigen)

The reference MC eigen (`ref/`, with the ADR-021 consistent transport correction) on the
committed benchmark scenarios. Band = 500 pcm + benchmark_uncertainty_pcm; the PUBLIC-DERIVED
unverified uncertainty → 0 (`08 §1`), i.e. **±500 pcm**. Deviation progression across the arc:

| set | Godiva | Jezebel |
|---|---|---|
| 4-group analytic weight (M1-T4a-2a, ADR-022) | +2562 | +1581 |
| 5-group + thermal, analytic weight (M1-T4a-3, ADR-024) | +2560 | +1601 |
| **5-group + self-consistent weight (M1-T4a-5, ADR-025)** | **−270 (WITHIN ±500, G0a PASS)** | **+1315 (outside)** |

(ADR-025 row: committed **C-900** `gate_report.json` (1e6×250, 5-seed mean; backend gpu,
git d0dfdd6, dirty=false — ref-equivalent by the committed G0c differential). G0a is the 2nd
passing gate; the de-risk `gate_probe` 120k pass was −276 / +1344.)

**Honest result:** self-consistent weighting brings **Godiva into the band** (the soft analytic
weight under-estimated μ̄/leakage; the correct hard flux fixes it — see ADR-025) and **improves
Jezebel** (+1601 → +1344) but does not close it — Jezebel is the genuinely hard gate that neither
weighting nor finer groups closes alone (memory `fast-group-residual-derisk`, corrected). No cross
section was tuned; the weight is each assembly's real physical flux, and the result is reported as
measured (Godiva slightly under, Jezebel still out), not engineered.

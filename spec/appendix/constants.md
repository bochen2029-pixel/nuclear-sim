# Appendix — Cited Constants & Canonical Data

Human-readable master; the **machine-readable strict sibling is `constants.data.toml`** in this directory (03 §1 — M0-T3 authors it FROM this file: nominal `value` + optional `lo`/`hi`; ranges/notes stay here). Status tags: `PUBLIC` / `DECLASSIFIED` / `RECONSTRUCTED` / `SIM` / `PENDING` (`00 §3.1`). `use` tags: `compute` vs `crosscheck` (crosscheck values MUST NOT enter tallies/gates — MAJ-11). Citation keys in §6. Any critical-mass number MUST carry its density (MAJ-29).

## 1. Physics constants

| ID | Name | Value (lo–hi) | Unit | Status | Use | Cite |
|---|---|---|---|---|---|---|
| C-010 | σ_f Pu-239 (fast, ~1 MeV) | 1.8 (1.7–1.8) | barns | PUBLIC | compute | ENDF, Primer |
| C-011 | σ_f U-235 (fast) | 1.2 | barns | PUBLIC | compute | ENDF |
| C-012 | σ_f U-238 (fast, threshold ~1 MeV) | 0.5 | barns | PUBLIC | compute | ENDF |
| C-013 | σ_f Pu-239 (thermal, reference only) | 750 | barns | PUBLIC | crosscheck | ENDF |
| C-020 | ν̄ Pu-239 — **TOTAL** (prompt + delayed), fast | 2.9 | n/fission | PUBLIC | compute | NWFAQ-8 |
| C-021 | ν̄ U-235 — **TOTAL**, fast | 2.4 | n/fission | PUBLIC | compute | NWFAQ-8 |
| C-021b | ν̄ U-238 — **TOTAL**, fast | 2.8 (2.5–2.9) | n/fission | PUBLIC | compute | ENDF |
| C-022 | β Pu-239 (delayed fraction ν_d/ν_total) | 0.0020 | — | PUBLIC | compute | keepin, ENDF |
| C-022b | β U-235 | 0.0065 | — | PUBLIC | compute | keepin, ENDF |
| C-022c | β U-238 (fast fission) | 0.0148 | — | PUBLIC | compute | keepin, ENDF |
| C-022d | β Pu-240 | 0.0026 | — | PUBLIC | compute | keepin, ENDF |
| C-022e | β Pu-241 | 0.0049 | — | PUBLIC | compute | keepin, ENDF |
| C-030 | Prompt generation time, uncompressed (shake) | 10 | ns | DECLASSIFIED | compute | Primer, NWFAQ |
| C-031 | Neutron speed at ~1 MeV | 1.4e9 | cm/s | PUBLIC | compute | Primer |
| C-032 | Fission MFP in Pu (normal density) | 12.7 (13 class) | cm | DECLASSIFIED | compute | Primer |
| C-033 | Scattering MFP | 2.5 | cm | DECLASSIFIED | crosscheck | Primer |
| C-040 | E_f prompt deposited per fission | 180 | MeV | PUBLIC | compute | Primer |
| C-041 | Φ_kt fissions per kiloton | **derived** = C-918/(C-040·C-917) = 1.4508e23 | 1/kt | PUBLIC | compute | derived |
| C-042 | kt/kg fully fissioned Pu-239 (Sher & Beck basis ≈190 MeV) | 18.29 | kt/kg | PUBLIC | **crosscheck** | NP-1771 |
| C-043 | kt/kg fully fissioned U-235 (same basis) | 17.74 | kt/kg | PUBLIC | **crosscheck** | NP-1771 |
| C-050 | Critical mass, bare Pu-239 sphere, **α-phase ρ = 19.8 g/cm³** | 10 | kg | DECLASSIFIED | crosscheck | Wellerstein-2015 |
| C-050b | Critical mass, bare Pu sphere, **δ-phase ρ = 15.6 g/cm³** | 16–17 | kg | PUBLIC | crosscheck | Jezebel PMF-001 |
| C-051 | Urchin initiator output at firing | 1.0e8–2.0e8 (≈1 n per 5–10 ns) | n/s | DECLASSIFIED | compute | NWFAQ-8.1.1 |
| C-052 | Pit criticality uncompressed, tamped — **fraction of critical MASS (not k)** | 0.78 (0.68–0.88) | fraction of M_c | DECLASSIFIED | compute | NWFAQ-8.1.1 |
| C-053 | Critical masses at ~2× compression — **derived from C-052, not independent** (QC-12) | 3.12 nominal (2.6–4.4) | — | DECLASSIFIED | compute | NWFAQ-2 prose ("3–4"), reconciled via ρ⁻² scaling from C-052 |
| C-060 | Implosion compression ratio | 2.2 (2.0–2.5) | ratio | DECLASSIFIED | compute | Wellerstein-2015, NWFAQ |
| C-061 | Comp B detonation velocity | 7.9 | km/s | PUBLIC | compute | NWFAQ-8.1.1 |
| C-062 | Baratol detonation velocity | 4.9 | km/s | PUBLIC | compute | NWFAQ-8.1.1 |
| C-070 | Guderley exponent α_G, **spherical, γ = 5/3** | 0.688377 | — | PUBLIC | compute | Guderley-1942, Lazarus-1981 |
| C-071 | X-Unit detonation simultaneity | ±10 | ns | DECLASSIFIED | compute | NWFAQ-8.1.1 |
| C-072 | HE assembly symmetry tolerance | 5 | % | DECLASSIFIED | compute | NWFAQ-8.1.1 |
| C-090 | Trinity/Fat Man Pu burn-up | 0.16 (0.15–0.17) | fraction of core | PUBLIC | crosscheck | Wellerstein-2013 |
| C-091 | Trinity yield (1945 radiochemistry) | 18.6 | kt | PUBLIC | crosscheck | Selby-2021 |
| C-092 | Trinity yield (DOE official) | 21 | kt | PUBLIC | crosscheck | DOE |
| C-093 | Trinity yield (2021 reassessment) | 24.8 ± 2 | kt | PUBLIC | crosscheck | Selby-2021 |
| C-094 | U-238 tamper share of total yield | 0.20 (0.10–0.30) | fraction | DECLASSIFIED | crosscheck | NWFAQ-8.1.1 |

**ν̄ convention note (C-020…C-022e, ADR-013 / QC-01, normative).** All ν̄ constants and all `nu` arrays in xs sets are **TOTAL** ν̄ (prompt + delayed). The MC eigenvalue is therefore `k_eff` on total ν̄ — the quantity the Godiva/Jezebel benchmark values (1.0000) refer to, and the quantity every k-comparing gate (G0a, G0b, G1a, G1b) uses. The prompt multiplication that drives burst kinetics is **derived once, downstream**: `k_prompt = k_eff·(1 − β_eff)`, with β_eff the fission-source-weighted mixture of the per-isotope β above. Applying the (1−β) factor to data that is already prompt-only double-counts it — for U-235 that is ~650 pcm, larger than G0a's entire ±500 pcm tolerance. v1 approximates β_eff ≈ Σ_i β_i·S_i / Σ_i S_i (source-weighted β); the true β_eff carries a delayed-neutron importance factor that differs by a few percent in fast metal systems — recorded as a known approximation, not silently ignored.

**Convention note (C-040…C-043, MAJ-11):** C-041 derives from C-040 (180 MeV prompt-deposited) and is the ONLY constant converting fissions to yield (E6). C-042/C-043 are Sher & Beck total-energy-basis values (≈190 MeV for Pu-239), provided for cross-check readouts only; readouts using them MUST label the ~5% basis difference.

**Density note (C-050/C-050b, MAJ-29):** critical mass scales as ρ⁻²; the α- and δ-phase figures are the same physics at different densities (10 kg @19.8 → ~16.1 kg @15.6 ≈ Jezebel's ~17 kg). Not a contradiction.

## 2. Canonical geometry (layer table — outside diameters; research doc §2)

| ID | Layer | OD (cm) | Mass | Status | Cite |
|---|---|---|---|---|---|
| C-100 | Urchin initiator (Po-210+Be, ~50 Ci) | 2.0 | ~7 g | DECLASSIFIED | NWFAQ-8.1.1 |
| C-101 | Initiator cavity | 2.1 | — | RECONSTRUCTED | CM |
| C-102 | Pu-Ga δ core | 9.17 | 6.15 kg | DECLASSIFIED | LA-3067 |
| C-103 | Natural-U tamper | 22.86 | 108–111 kg | RECONSTRUCTED | CM, NWFAQ-8.1.1 |
| C-104 | B-10 acrylic shell (~0.32 cm) | 23.50 | — | DECLASSIFIED | NWFAQ-8.1.1 |
| C-105 | Aluminum pusher (~12 cm) | 46.99 | 128–130 kg | RECONSTRUCTED | CM |
| C-106 | Inner HE booster (Comp B) | 92.075 | ~608 kg | RECONSTRUCTED | CM |
| C-107 | 32-lens HE layer (20 hex + 12 pent) | 137.8 | ~1.8–1.9 t | DECLASSIFIED | NWFAQ-8.1.1 |
| C-108 | Cork liner | 140.3 | — | RECONSTRUCTED | CM |
| C-109 | Duralumin case ("1561") | 145.4 | — | RECONSTRUCTED | CM |

**C-102 note (MAJ-28, normative):** pit **mass 6.15 kg** and **OD 9.17 cm** are the authoritative declassified values; density is DERIVED: ρ = 15.23 g/cm³ solid (15.39 excluding the 2.0 cm initiator cavity). The commonly quoted "ρ ≈ 15.6" is the δ-phase Pu-Ga ALLOY density and is inconsistent with the mass/OD pair at ~2.4%; attributed to initiator cavity, inter-hemisphere gasket, and rounding in public figures — recorded, not resolved. Composition: 3.35 at% Ga total (≈1.0 wt%), Pu-240 1.0 wt% of Pu (super-grade).

Tamper 108 vs 111 kg and similar reconstruction spreads: pick per-layer values in `data/materials/`, record choice + citation in `data/materials/README.md` (M2-T1 DoD, 3% tolerance).

## 3. Molar masses & exact constants (BLK-09; added for gen_constants)

| ID | Name | Value | Unit | Status | Cite |
|---|---|---|---|---|---|
| C-910 | M(Pu-239) | 239.0522 | g/mol | PUBLIC | NNDC/AME2020 |
| C-911 | M(Pu-240) | 240.0538 | g/mol | PUBLIC | NNDC/AME2020 |
| C-912 | M(U-235) | 235.0439 | g/mol | PUBLIC | NNDC/AME2020 |
| C-913 | M(U-238) | 238.0508 | g/mol | PUBLIC | NNDC/AME2020 |
| C-914 | M(U-234) | 234.0410 | g/mol | PUBLIC | NNDC/AME2020 |
| C-915 | M(Ga natural) — **readout only**, see rule below | 69.723 | g/mol | PUBLIC | IUPAC |
| C-915a | M(Ga-69) | 68.9256 | g/mol | PUBLIC | NNDC/AME2020 |
| C-915b | M(Ga-71) | 70.9247 | g/mol | PUBLIC | NNDC/AME2020 |
| C-916 | Avogadro constant | 6.02214076e23 | 1/mol | PUBLIC | SI (exact) |
| C-917 | MeV → J | 1.602176634e-13 | J/MeV | PUBLIC | SI (exact) |
| C-918 | kt(TNT) → J | 4.184e12 | J/kt | PUBLIC | convention (exact) |
| C-919 | M(Pu-241) | 241.0568 | g/mol | PUBLIC | NNDC/AME2020 |
| C-920 | M(B-10) | 10.0129 | g/mol | PUBLIC | NNDC/AME2020 |
| C-921 | M(B-11) | 11.0093 | g/mol | PUBLIC | NNDC/AME2020 |
| C-922 | M(C-12) | 12.0000 | g/mol | PUBLIC | SI (definitional) |
| C-923 | M(H-1) | 1.00783 | g/mol | PUBLIC | NNDC/AME2020 |
| C-924 | M(O-16) | 15.9949 | g/mol | PUBLIC | NNDC/AME2020 |
| C-925 | M(N-14) | 14.0031 | g/mol | PUBLIC | NNDC/AME2020 |
| C-926 | M(Al-27) | 26.9815 | g/mol | PUBLIC | NNDC/AME2020 |
| C-927 | M(Be-9) | 9.01218 | g/mol | PUBLIC | NNDC/AME2020 |
| C-928 | M(Po-210) | 209.9829 | g/mol | PUBLIC | NNDC/AME2020 |
| C-929 | M(W natural) — monoisotopic-treatment element, see rule | 183.84 | g/mol | PUBLIC | IUPAC |

**Molar-mass completeness rule (QC-03, normative).** `03 §3` forbids natural-abundance expansion by the loader, so every species named in a material file needs its OWN molar mass. The set above covers the full isotope list enumerated in `08 §1`. Two tiers:
- **Per-isotope (REQUIRED)** for any species whose cross sections are resolved separately in the xs set: U-234/235/238, Pu-239/240/241, Ga-69/71, B-10/11.
- **Element-averaged (PERMITTED)** for structural/moderating species where the xs set carries one entry for the element and isotopic detail does not affect the neutronics at the spec's fidelity: C, H, O, N, Al, Be, W, Po. Such an entry MUST be tagged `element_averaged = true` in the strict file, and the loader WARNs if an element-averaged mass is used for a species that has per-isotope xs data.

`tools/verify/constants_roundtrip` additionally asserts that **every species named in any `data/materials/*.json` resolves to a molar-mass constant** — a material naming a species with no molar mass is a hard error, not a WARN (it silently corrupts every macroscopic cross section, `04 §5`).

## 4. Simulation knobs (SIM — tunable, not physics)

| ID | Name | Value |
|---|---|---|
| C-900 | Eigen GATE config: batch / inactive / active | 1e6 / 50 / 200 |
| C-900b | Eigen INTERACTIVE (studio/burst refresh) config | 1e5 / 10 / 30 — never valid for gate claims |
| C-900c | Eigen CPU-ref smoke config | 1e5 / 10 / 50 |
| C-901 | eig tolerances (H window, k) | 1e-3 / 1e-4 |
| C-902 | weight cutoff w_min / survival w_surv | 1e-4 / 1e-2 |
| C-903 | α-mode eigen refresh interval | 10 generations |
| C-904 | population renormalization ceiling | 1e30 |
| C-905 | render_temp_scale / render_density_scale / render_exposure | set at M7 per `09 §1` calibration (peak emission-weighted T ∈ [7500, 9500] K at burst+1 µs; cite Selby-2021/Brixner footage) |
| C-906 | asymmetry coefficient c_a | DERIVED at M6-T3: c_a = 0.05·t_rise/10 ns (C-071/C-072 rule, `01 §5`) — never gate-fitted |
| C-907 | RNG stream registry | source=1, flight=2, collision=3, fission=4, scatter=5, hydro=6, render=7 |
| C-908 | eigen entropy mesh (fixed Cartesian) | 8×8×8 over outermost-layer bbox |
| C-909 | quench ε_quench | 1e-4 (F_n < ε·F_peak, E6) |
| C-909b | eigen_refresh_dr_frac | 0.005 |

## 5. Gate bands (MAJ-13 — every gate threshold has an ID + derivation; SIM where model-derived)

| ID | Name | Value | Basis |
|---|---|---|---|
| C-930 | G0a/G0b k deviation tolerance | 500 pcm | gate design choice, SIM; + benchmark_uncertainty (08 §1 rule) |
| C-931 | G0a/G0b σ ceiling | 25 pcm | gate design choice, SIM (batch sized to reach it, C-900) |
| C-932 | G0c absolute equivalence bound | 100 pcm | SIM |
| C-933 | G1a mass-fraction band | [0.68, 0.88] | C-052 ± 0.10 (prose-source width), SIM |
| C-934 | G1a derived k band | [0.84, 0.96] | one-group image of C-933; derivation recorded in gates.toml notes; SIM |
| C-935 | G1b critical-masses band at 2ρ₀ | [2.6, 4.4] | C-053 ±40% (prose source), SIM |
| C-936 | G1b k(2.0) band | [1.35, 1.85] | one-group image of C-935; SIM |
| C-937 | G1b k ratio band | [1.5, 2.0] | k(2.0)/k(1.0); SIM |
| C-940 | G2 yield band | [16.6, 26.8] kt | envelope of C-091/092/093 incl. Selby ±2 |
| C-941 | G2 Pu burn-up band | [0.12, 0.20] | C-090 (0.15–0.17) widened ±3 pp for few-group + Tier-2 error; SIM |
| C-942 | G2 tamper-yield band | [0.10, 0.30] | C-094 ±10 pp; SIM |
| C-943 | G2 timing bands | FWHM [5,100] ns; t_fire→peak [100 ns, 1.5 µs]; peak α [2e7, 3e8] s⁻¹ | ±(2–3)× around ~57 e-foldings at α≈1e8 (research doc §3); SIM |
| C-944 | G3 degradation criteria | median(500ns) ≤ 0.9×median(0); median(10ns) ≥ 0.97×median(0) | (b) visible-degradation requirement; (c) published-tolerance claim (C-071/C-072), NOT calibrated; SIM |
| C-945 | G4 perf budgets | per `08 §2` G4 list | SIM (dev hardware-derived, ADR-009) |
| C-946 | q validity thresholds | warn 0.02 / conditional 0.05 | R-13; SIM |

## 6. Citation keys

- **Primer** — Serber, *The Los Alamos Primer* (LA-1; UC Press 1992 annotated ed.), declassified.
- **LA-3067** — Paxton, "Los Alamos Critical Mass Data," 1964 (declassified).
- **NWFAQ-2 / -8 / -8.1.1 / -12** — Sublette, *Nuclear Weapon Archive* sections.
- **Wellerstein-2013 / -2015** — blog.nuclearsecrecy.com ("Kilotons per kilogram"; "Critical mass").
- **CM** — Coster-Mullen, *Atom Bombs* (reconstruction — best-available open estimate).
- **ENDF** — ENDF/B via NNDC/JANIS (public). **NNDC/AME2020** — atomic mass evaluation. **IUPAC** — standard atomic weights. **keepin** — delayed-neutron systematics (public).
- **NP-1771** — Sher & Beck, "Fission Energy Release for 16 Fissioning Nuclides," Stanford 1981.
- **Selby-2021** — Selby et al., *Nuclear Technology* 207, S321–S325 (24.8 ± 2 kt).
- **Guderley-1942 / Lazarus-1981** — converging-shock self-similar literature (α_G = 0.688377 spherical γ=5/3).
- **Jezebel PMF-001 / Godiva HMF-001** — public benchmark descriptions and published cross-evaluations (M1-T4a records exact open citations + retrieval dates in `data/benchmarks/`; ICSBEP Handbook sheet + revision recorded ONLY by owner-gated M1-T4b — the Handbook is access-restricted via the NEA Data Bank, BLK-14).
- **WARP / Shift** — Bergmann & Vujić GPU MC; Hamilton & Evans 2019.

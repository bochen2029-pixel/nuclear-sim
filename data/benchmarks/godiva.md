# Godiva — bare HEU sphere (benchmark data card)

`status = "PUBLIC-DERIVED"` — derived from openly published sources, **not** from the
ICSBEP Handbook. The Handbook is access-restricted (NEA Data Bank, named users,
intended-use statement) and BLK-14 forbids an autonomous session from applying for
it; no Handbook access was sought or used. Replacing these with Handbook sheet
values is the owner-gated M1-T4b.

Authored by **M1-T4a-1**, session-2026-08-02-b.

## Source

**JEFF Report 16 — "Intercomparison of Calculations for Godiva and Jezebel"**,
OECD Nuclear Energy Agency (open publication).
<https://www.oecd-nea.org/science/docs/pubs/jeff_16.pdf> — retrieved **2026-08-02**.

JEFF Report 16, Annex 3 ("Model Descriptions for GODIVA and JEZEBEL") adopts the
specification from the **CSEWG Benchmark Book, BNL 19302, ENDF 202, Revised 11-81**,
where Godiva is *fast reactor benchmark no. 5 (F5)*. That CSEWG specification, not
the report's own calculations, is what is reproduced below.

Cross-referenced against the ICSBEP designation **HEU-MET-FAST-001** as used in
open LANL validation documentation (`LA-UR-17-29219`,
<https://mcnp.lanl.gov/pdf_files/TechReport_2017_LANL_LA-UR-17-29219_MartzKulesza.pdf>,
retrieved 2026-08-02), which quotes 93.71 wt% U-235, 52.42 kg and ρ = 18.74 g/cm³
— consistent with the derivations below.

## Published model (quoted exactly)

> A homogeneous bare sphere of enriched uranium, measured eigenvalue = 1.000 ± 0.001
>
> Radius: 8.741 cm
>
> | Isotope | Density (nuclei/b-cm) |
> |---|---|
> | 235U | 0.045000 |
> | 238U | 0.002498 |
> | 234U | 0.000492 |

**Benchmark k_eff = 1.0000 ± 0.0010.**

## Derived quantities

Atom densities are the primary datum — they are what the transport consumes
directly. Everything below is *derived* from them, which makes each value an
independent check on the approximate figures quoted elsewhere in the literature
rather than a restatement of them. Molar masses are C-912/C-913/C-914; Avogadro
is C-916.

```
sum N            = 0.047990 nuclei/b-cm
mass density     = 18.7421 g/cm^3        (published figures quote 18.74)
sphere volume    = 2797.512 cm^3         (R = 8.741 cm)
total mass       = 52.431 kg             (published figures quote 52.42 kg)
```

| Isotope | Atom fraction | wt% | Commonly quoted |
|---|---|---|---|
| U-235 | 0.937695353 | 93.7112 | ~93.7 |
| U-238 | 0.052052511 |  5.2686 | ~5.2 |
| U-234 | 0.010252136 |  1.0202 | ~1.0 |

All three agree with the independently published weight percentages, and the
derived density and mass agree with the published 18.74 g/cm³ and 52.42 kg. The
`benchmarks.godiva …` tests in `tests/unit/test_benchmarks.cpp` recompute every
number in this section from `data/materials/u_godiva.json`, so the card cannot
drift from the data.

## Benchmark uncertainty

`08 §1`'s operational rule: gate tolerance = 500 pcm + `benchmark_uncertainty_pcm`.
The published measured eigenvalue carries ± 0.001, i.e. **100 pcm**. That figure is
the *experimental* uncertainty quoted with the CSEWG model; it is **not** a
verified ICSBEP benchmark-model uncertainty. Per `08 §1`, an unverified
uncertainty on a PUBLIC-DERIVED model is taken as **0** for gate purposes and the
gate report must say so. M1-T4b may replace it with the Handbook value.

## Scope note

This is a criticality benchmark: a bare metal sphere at a published critical
radius, used to check that the transport and eigen solver reproduce a documented
k_eff. It carries no device information and is the standard validation case for
every open neutron-transport code.

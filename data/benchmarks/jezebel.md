# Jezebel — bare δ-phase Pu-Ga sphere (benchmark data card)

`status = "PUBLIC-DERIVED"` — derived from openly published sources, **not** from the
ICSBEP Handbook. No Handbook access was sought or used (BLK-14). Replacing these
with Handbook sheet values is the owner-gated M1-T4b.

Authored by **M1-T4a-1**, session-2026-08-02-b.

## Source

**JEFF Report 16 — "Intercomparison of Calculations for Godiva and Jezebel"**,
OECD Nuclear Energy Agency (open publication).
<https://www.oecd-nea.org/science/docs/pubs/jeff_16.pdf> — retrieved **2026-08-02**.

Annex 3 adopts the **CSEWG Benchmark Book, BNL 19302, ENDF 202, Revised 11-81**
specification, where Jezebel is *fast reactor benchmark no. 1 (F1)*.
ICSBEP designation for cross-reference: **PU-MET-FAST-001**.

## Published model (quoted exactly)

> A homogeneous bare sphere of plutonium metal, measured eigenvalue = 1.000 ± 0.002.
>
> Radius: 6.385 cm.
>
> | Isotope | Density (nuclei/b-cm) |
> |---|---|
> | 239Pu | 0.037050 |
> | 240Pu | 0.001751 |
> | 241Pu | 0.000117 |
> | Ga    | 0.001375 |

**Benchmark k_eff = 1.0000 ± 0.0020.**

## Derived quantities

```
sum N            = 0.040293 nuclei/b-cm
mass density     = 15.6112 g/cm^3        (commonly quoted ~15.6)
sphere volume    = 1090.364 cm^3         (R = 6.385 cm)
total mass       = 17.022 kg             (commonly quoted ~17.0 kg)
```

| Isotope | Atom fraction | wt% |
|---|---|---|
| Pu-239 | 0.919514556 | 94.2092 |
| Pu-240 | 0.043456680 |  4.4710 |
| Pu-241 | 0.002903730 |  0.3000 |
| Ga-69  | 0.020511876 |  0.6059 |
| Ga-71  | 0.013613159 |  0.4138 |

Total Ga = **1.0197 wt%**, matching the ~1.02 wt% quoted in the open literature.

### The "4.5% Pu-240" figure — both readings agree

Sources differ on whether Jezebel's Pu-240 content is quoted in atom percent or
weight percent, and `08 §1` says weight percent. There are in fact **three**
distinct quantities here, and they must not be confused:

| Quantity | Value |
|---|---|
| Pu-240 wt% of the **whole material** | 4.4710 |
| Pu-240 wt% of the **plutonium only** | 4.5171 |
| Pu-240 at% of the **plutonium only**  | 4.4990 |

All three round to 4.5%, so the published "4.5% Pu-240" is unambiguous in
practice whichever basis a source meant. `composition_check.Pu240_wt_pct_of_Pu`
in the material file is the **second** of these, because that is what the loader
recomputes (`03 §3`). Recorded so it is not re-litigated.

### Gallium is split explicitly, by choice

The published model gives natural Ga as a single entry. `03 §3` forbids the
**loader** from expanding natural abundance — the rule exists precisely to force
an explicit, recorded choice by the data author rather than a silent default. So
this card makes the choice: natural gallium is **Ga-69 60.108% / Ga-71 39.892%**
(IUPAC standard isotopic composition), giving

```
Ga-69 = 0.001375 * 0.60108 = 8.26485e-4 nuclei/b-cm
Ga-71 = 0.001375 * 0.39892 = 5.48515e-4 nuclei/b-cm
```

Consistency check: the split reproduces the natural-Ga mass contribution to seven
digits — 0.0958691 vs 0.0958692 g·mol⁻¹ per b-cm — so the derived density is
unchanged by the choice. C-915 (natural Ga) remains `use = "crosscheck"`,
readout-only, and is not used here.

## This is NOT the Trinity pit material

`03 §3` makes the loader **error** if a scenario named `jezebel` references
`pu_ga_delta`, and that guard exists because the two materials look similar and
are not:

| | Jezebel (`pu_ga_jezebel`) | Trinity pit (`pu_ga_delta`) |
|---|---|---|
| Pu-240 | **4.47 wt%** | **1.0 wt%** |
| density | 15.611 g/cm³ (published) | 15.23 g/cm³ (derived, C-102 note) |

A 4.5× difference in Pu-240 moves k well beyond G0b's tolerance while looking
entirely plausible in a diff, which is why the prohibition is enforced in code
rather than left to care.

## Benchmark uncertainty

Published measured eigenvalue ± 0.002, i.e. **200 pcm** — an *experimental*
uncertainty, not a verified ICSBEP benchmark-model uncertainty. Per `08 §1` an
unverified uncertainty on a PUBLIC-DERIVED model is taken as **0** for gate
purposes and the gate report must state so.

## Scope note

A bare metal sphere at a published critical radius, used to validate the
transport and eigen solver against a documented k_eff. It carries no device
information and is a standard open validation case.

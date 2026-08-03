# Canonical assembly materials (M2-T1)

The reconstructed 1945 Trinity "Gadget" layer materials. Compositions are **atom
fractions** (`03 §3`, Σ = 1 ± 1e-6); densities are cited or derived. Per-layer
mass = density × shell volume (geometry from appendix §2, C-100..C-109) is
cross-checked against the appendix Mass column here and in `docs/VERIFICATION.md`
§2 (`oracle.py`). Reconstructed masses carry real spread, so the tolerance is 3%
(a WARN, never an error — appendix §2 note; MAJ-28).

> **Blocked downstream:** the canonical transport/eigen (`trinity_canonical.toml`)
> references `xs_set = "fast4"`, which does not exist yet (M1-T4a-2, owner cited
> data). These material files + the mass checks are **xs-free** and complete now;
> the Σ-dependent uses (M2-T2 k-scan, G1/G2) wait on `fast4`.

## Per-layer mass check (solid-layer model)

Neutronic stack modelled as a solid pit through the HE booster (`trinity_canonical.toml`
header explains the solid-pit / omitted-cavity / omitted-lens choices). Radii = OD/2.

| Layer | Material | ρ (g/cm³) | mass (kg) | appendix | Δ | status |
|---|---|---|---|---|---|---|
| pit (C-102) | pu_ga_delta | 15.23 | 6.148 | 6.15 | −0.03% | DECLASSIFIED |
| tamper (C-103) | u_natural | 19.05 | 111.45 | 108–111 | +0.40% | RECONSTRUCTED |
| B-shell (C-104) | b10_acrylic | 1.20 | 0.648 | — (none) | — | SIM |
| pusher (C-105) | aluminum | 2.70 | 128.33 | 128–130 | in band | PUBLIC |
| HE booster (C-106) | he_compb | 1.70 | 602.1 | 608 | −0.97% | RECONSTRUCTED |

All four mass-gated layers are within 3%. Values recomputed by `oracle.py` §2 on
every build (`ctest -R "^oracle\."`).

## Choices, citations, and recorded deviations

- **pu_ga_delta (pit)** — DECLASSIFIED (LA-3067). Mass 6.15 kg and OD 9.17 cm are
  authoritative; **density is DERIVED = 15.23 g/cm³** (MAJ-28), so the pit mass
  check is consistent by construction. The commonly quoted δ-phase alloy 15.6 is
  inconsistent by ~2.4% (recorded, not resolved — cavity + gasket + rounding).
  3.35 at% Ga (Ga-69/71 split 60.108/39.892, IUPAC); Pu-240 = 1.0 wt% of Pu
  (super-grade). Verified: Ga 1.0008 wt%, Pu-240 1.000 wt% of Pu.
- **u_natural (tamper)** — RECONSTRUCTED. Natural-U isotopics (U-238/235/234 =
  99.2742/0.7204/0.0054 at%, public). Density **19.05 g/cm³** (α-U metal, public)
  → 111.45 kg, **+0.40% above the 111 kg upper bound** (within 3%). The appendix
  108–111 kg band with the OD-derived 5850 cm³ shell implies ρ ≈ 18.5–19.0; the
  research doc's "~6.56 cm thick" is thinner than the OD-table 6.845 cm — the
  reconstruction spread. Chose the standard metal density and recorded the +0.40%.
- **aluminum (pusher)** — PUBLIC. Al-27, 2.70 g/cm³ → 128.33 kg, in the 128–130
  band exactly. (Independently, the research doc's Al/HE density ratio 1.64 with
  2.70 implies ρ_HE ≈ 1.646 — see he_compb note.)
- **he_compb (HE booster)** — RECONSTRUCTED. Composition B 60/39/1 RDX/TNT/wax by
  mass (research doc); elemental C/H/N/O. Density **1.70 g/cm³** (public Comp B
  cast range 1.65–1.72) → 602.1 kg, **−0.97% vs 608 kg** (within 3%). NOTE the
  tension: the research doc's Al/HE ratio 1.64 gives ρ_HE ≈ 1.646 → 583 kg
  (−4.1%, would exceed 3%); the standard cast density (1.70) is both more physical
  and consistent with C-106. Recorded.
- **b10_acrylic (B-shell)** — **SIM**. PMMA (C5H8O2) + B-10. The **B-10 loading
  fraction is NOT cited numerically** in open sources (research doc: "~0.32 cm
  B-10 in acrylic"); **5 wt% B-10 is a SIM placeholder** pending a cited value.
  Layer C-104 has no appendix mass, so it is not mass-gated. A future cited
  loading is a `03 §6` amendment. Density 1.20 g/cm³ (acrylic + B, SIM).
- **be_po_urchin (initiator)** — DECLASSIFIED. Po-Be Urchin: 6.989 g Be-9 +
  0.011 g (50 Ci) Po-210 (~7 g). Be metal density 1.85 g/cm³. The ~7 g is a
  **hollow** Be shell + pellet, so a solid-density estimate overshoots (~8 g at
  r = 1 cm); the urchin is not a solid-layer mass check and the initiator enters
  the burst as a timed source (`[initiator]`), not a fissile geometry layer.

## Not authored here (out of M2-T1 scope)
- HE **lens** (C-107, Comp B + Baratol mix) and **cork liner** / **duralumin
  case** (C-108/C-109): implosion geometry (M6) + rendering (M7), outside the
  neutronic region. The lens' Comp B/Baratol mix density differs from he_compb.

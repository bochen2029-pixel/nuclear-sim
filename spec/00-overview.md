# 00 — Overview, Scope & Boundaries

## 1. Purpose

Build a 3D, physics-based **educational and historical simulator** of the 1945 Trinity "Gadget" / Fat Man device — the class of calculation first done by the Los Alamos T-Division in 1943–45, reproducible today from public literature on a GPU. The simulator reproduces, from published/declassified physics:

1. **Neutronics:** Monte Carlo neutron transport in the layered spherical assembly; k-eigenvalue criticality validated against public bare-sphere benchmarks (Godiva, Jezebel).
2. **Burst kinetics:** exponential supercritical multiplication (~10 ns generations), initiator injection, energy deposition.
3. **Hydro coupling:** parametric/thin-shell implosion compression and disassembly-driven quench of the chain reaction; yield and burn-up consistent with public estimates (18.6–24.8 kt; 15–17% Pu burn-up; ~20% of yield from U-238 tamper fast fission).
4. **Implosion geometry:** 32-lens truncated-icosahedron HE assembly; detonation-timing jitter → asymmetry → degraded yield.
5. **Rendering:** volumetric blackbody fireball, neutron/fission-density fields, staged clock from nanoseconds to seconds.

Deliverables: one physics library (`nscore`) + four frontends (`nukebench` headless CLI, `nukefarm` batch/cloud sweep driver, `nukestudio` interactive app, `nukecinema` offline cinematic renderer). See `02-architecture.md`.

## 2. Non-goals

- **Not a design tool.** The simulator models one specific historical device whose parameters are ~80-year-old public knowledge. The operative distinction is *explaining* a design space vs *searching* one (MAJ-36):
  - **Interactive, single-point exploration** of counterfactual parameters (a tungsten tamper, a larger pit, more lenses) is IN SCOPE and is the pedagogical core of the studio frontend — it shows *why* the historical choices were made. Such runs are marked `non_canonical: true` (03 §4 overrides) and MUST NOT be used as gate evidence; the UI shows a persistent non-canonical badge.
  - **Automated search** over physical parameters toward a performance objective is OUT OF SCOPE, prevented mechanically by the `axis_class` restriction (`03 §7`), not by convention.
- Not a full-physics hydrocode. No 3D CFD, no real equations of state beyond Tier-3 1D Lagrangian scope (`05-module-transport.md` §Hydro), no radiation-hydrodynamics coupling beyond the specified deposition model.
- Not continuous-energy nuclear data processing at runtime (curated few-group data, `03-data-contracts.md` §xs.json; one-time offline provenance-tracked multigroup generation is permitted by the D4 carve-out, M1-T4a).
- Not a game. No gameplay layer; the studio frontend is an instrument panel.

## 3. Scope & Boundaries (HARD CONSTRAINTS)

These mirror the caveats of the source research document. Every session MUST comply:

1. **Public/declassified data only.** All geometry, material, and physics parameters come from published sources (Los Alamos Primer, LA-3067, Nuclear Weapon Archive/NWFAQ, Wellerstein, Coster-Mullen, ICSBEP, ENDF/NNDC, peer-reviewed reassessments). Every constant carries a citation tag and a source-status tag (`PUBLIC` / `DECLASSIFIED` / `RECONSTRUCTED`) — see `appendix/constants.md`.
2. **Do not fill classified gaps.** Exact peak k-effective, Bethe–Feynman numerical coefficients, lens internal contours, and implosion timing/velocity profiles beyond public values are classified. Where such a quantity is needed, the spec defines either (a) a user parameter with a public-plausible range, or (b) a validation **band** derived from public statements (e.g., "pit ~78% critical uncompressed" → G1a band 0.70–0.95). Never substitute invented point values for classified ones.
3. **Bethe–Feynman is an overlay, never the engine.** Its public scaling form may be shown as a cross-check readout; the simulator's results come from transport + kinetics + hydro only.
4. **Batch objectives are calibration/sensitivity only, enforced on the SEARCH SPACE, not the label.** `nukefarm` sweeps exist to study parameter sensitivity and to calibrate against public benchmark ranges (e.g., "which public uncertainties dominate the 18.6→24.8 kt yield spread?"). Enforcement is mechanical (`03-data-contracts.md` §7): every sweep axis declares `axis_class` ∈ {numerical, uncertainty, pedagogical}; optimizing samplers (mcts/lhs/random) may only search `numerical` and `uncertainty` axes (physical parameters within their published [lo, hi] bands); `calibrate` scores toward band CENTER. Interactive single-point exploration is governed by §2, not by this rule.
5. **Keep conflated quantities separate.** Pu burn-up fraction (~15–17% of the core fissioned) and the U-238 tamper contribution to total yield (~20%) are distinct; the tally schema (`03-data-contracts.md` §tally.json) reports them separately and UI MUST NOT merge them.
6. **Yield is a range.** 18.6 kt (1945 radiochemistry) / 21 kt (DOE official) / 24.8±2 kt (Selby et al. 2021). The canonical scenario targets the band, never a hard-coded point.
7. **Citation discipline.** Any number introduced into code or data that is not in `appendix/constants.md` must be added there first, with citation and status tag (amendment protocol, `README.md` §6).

## 4. Conventions

- **RFC-2119 keywords** (MUST/SHOULD/MAY) are normative.
- Units: cgs-dominant internally (cm, g, s) with energies in MeV and time in shakes (1 shake = 10 ns) where natural; schemas declare units explicitly. SI at UI boundaries.
- Identifiers: milestones `M0–M7`, tasks `Mx-Ty`, gates `G0–G5`, decisions `D1–D9` (+ADRs `ADR-nnn`), equations `E1–E7`, constants `C-nnn`.
- All file formats are versioned (`schema_version` field, `03-data-contracts.md`).
- Terminology: a **shake** = 10 ns (public, historical Los Alamos unit). **α-mode** and **TD-mode** as defined in `02-architecture.md` D3.

## 5. Definition of "implementation-ready"

This spec is written so that any session can execute any task from `07-milestones.md` using only: the task row, the referenced spec sections, and the schemas. If a session finds it must invent a decision not recorded here, that is a spec defect → amendment protocol (`README.md` §6).

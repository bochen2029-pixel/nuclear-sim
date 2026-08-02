# NOTICE — sources, scope, and what this is not

## What this is

A specification (and, from milestone M0 onward, an implementation) of an **educational and historical simulator** of the 1945 Trinity "Gadget" — the class of calculation first done by the Los Alamos T-Division in 1943–45, reproducible today on a consumer GPU from published literature.

The code is MIT licensed. This notice covers the **sources and the scope**, which the licence does not.

## Sources — public and declassified only

Every physical constant in this project carries a citation tag and a source-status tag (`PUBLIC` / `DECLASSIFIED` / `RECONSTRUCTED` / `SIM`), enforced by a generator that fails on any constant lacking either. The source set is:

- Serber, *The Los Alamos Primer* (LA-1; UC Press 1992 annotated edition) — declassified
- Paxton, *Los Alamos Critical Mass Data* (LA-3067, 1964) — declassified
- Sublette, *Nuclear Weapon Archive* / NWFAQ
- Wellerstein, *Restricted Data* (nuclearsecrecy.com)
- Coster-Mullen, *Atom Bombs* — an open reconstruction, treated as best-available open estimate, never as official specification
- Sher & Beck, NP-1771 (1981); Selby et al., *Nuclear Technology* 207 (2021)
- ENDF/B via NNDC/JANIS; openly published descriptions of the Godiva and Jezebel critical assemblies

The ICSBEP Handbook is **not** used. It is distributed by the OECD/NEA Data Bank to named users under an intended-use agreement, and reproducing its evaluated sheets in a public repository would not respect those terms. Benchmark models here are derived from open literature and tagged `PUBLIC-DERIVED`, with citation and retrieval date recorded per value. See `spec/08-validation.md` §1.

## Scope boundaries — hard constraints, not aspirations

These are normative in `spec/00-overview.md` §2–§3 and are enforced mechanically where enforcement is possible:

1. **Public/declassified data only.** Any number entering the code must first exist in `spec/appendix/constants.md` with a citation and a status tag.
2. **Classified gaps are not filled.** Exact peak k-effective, Bethe–Feynman numerical coefficients, explosive-lens internal contours, and implosion timing/velocity profiles beyond published values are classified. Where such a quantity is needed the spec defines a user parameter with a public-plausible range, or a validation *band* derived from published statements — never an invented point value.
3. **Bethe–Feynman is a display-only overlay.** Its public scaling form may be shown as a cross-check readout; it never feeds simulation state.
4. **Exploration yes, optimisation no.** Interactive single-point exploration of counterfactual parameters (a different tamper material, a larger pit) is in scope and is the pedagogical point — such runs are marked `non_canonical` and can never serve as gate evidence. *Automated search* over physical parameters toward a performance objective is out of scope, and is prevented by a type-level restriction on sweep axes (`spec/03-data-contracts.md` §7 `axis_class`), not by convention.
5. **Distinct quantities stay distinct.** Plutonium burn-up fraction (~15–17% of the core) and the U-238 tamper contribution to total yield (~20%) are different things; popular sources routinely conflate them. The tally schema reports them as separate mandatory fields and the UI may not merge them.
6. **Yield is a range, never a point.** 18.6 kt (1945 radiochemistry) / 21 kt (DOE) / 24.8 ± 2 kt (Selby et al. 2021).

## What this is not

- Not a weapon design tool, and not extensible into one. It models one specific historical device whose parameters are ~80-year-old public knowledge.
- Not a full-physics hydrocode, not continuous-energy nuclear data processing, and not a game.

## Third-party

The research document in `research/` is a synthesis of the public sources listed above; the cited works remain the property of their authors and publishers. Referenced open-source precedents (WARP, Shift, OpenMC, Serpent, MCNP) are named for architectural provenance only; no code from them is included.

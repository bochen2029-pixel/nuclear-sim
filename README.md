# NUCLEAR-SIM

**A 3D, physics-based educational simulator of the 1945 Trinity "Gadget" / Fat Man device** — GPU Monte Carlo neutron transport → k-eigenvalue criticality → supercritical burst kinetics → simplified implosion/disassembly hydrodynamics → volumetric fireball rendering.

Built **exclusively on public and declassified literature**. Every physical constant carries a citation and a source-status tag, and the generator refuses to emit any constant lacking either.

```
Status:  specification v0.3 — implementation has NOT started; first task is M0-T1
Target:  C++20 + CUDA 13.1, sm_89 (RTX 4070 Ti SUPER) primary, sm_80/90 cloud
Licence: MIT (code) — see NOTICE.md for sources and scope
```

---

## Scope & boundaries

Hard constraints from [`spec/00-overview.md`](spec/00-overview.md) §2–§3, enforced mechanically wherever enforcement is possible. Full text in [`NOTICE.md`](NOTICE.md).

- **Public/declassified sources only.** Los Alamos Primer, LA-3067, Nuclear Weapon Archive, Wellerstein, Coster-Mullen (as open reconstruction), ENDF/NNDC, openly published Godiva/Jezebel descriptions. Any number entering code exists first in the cited constants appendix.
- **Classified gaps are not filled.** Exact peak k-effective, Bethe–Feynman numerical coefficients, lens internal contours, and implosion velocity profiles beyond published values are classified. Where such a quantity is needed the spec defines a parameter with a public-plausible range, or a validation *band* derived from published statements — never an invented point value.
- **Bethe–Feynman is a display-only overlay.** It never feeds simulation state.
- **Exploration yes, optimisation no.** Interactive single-point exploration of counterfactual parameters is in scope and is the pedagogical point; such runs are marked `non_canonical` and can never serve as gate evidence. *Automated search* over physical parameters toward a performance objective is out of scope, and is blocked by a type-level restriction on sweep axes rather than by convention.
- **Yield is a range, never a point.** 18.6 kt (1945 radiochemistry) / 21 kt (DOE) / 24.8 ± 2 kt (Selby et al. 2021).

**Not** a weapon design tool and not extensible into one — it models one specific historical device whose parameters are ~80-year-old public knowledge. Not a full-physics hydrocode. Not a game.

## Why the specification looks like this

The project is built to be implemented across dozens of independent agent sessions, so correctness cannot rest on anyone remembering anything:

- **Gates, not prose.** Nine numbered gates whose thresholds each carry a constant ID and a one-line derivation; a milestone is met only when the gate command exits 0. Normative seed sets make seed-shopping mechanically impossible rather than merely discouraged.
- **One living state file.** [`spec/PROGRESS.md`](spec/PROGRESS.md) holds the task table, dependency graph, ready-queue and a *falsifiable* VERIFY command. Claimed state that VERIFY cannot confirm is not real.
- **Append-only decisions.** [`spec/DECISIONS.md`](spec/DECISIONS.md) — changing a gate or a physical constant requires an ADR with cited evidence.
- **A generated verification oracle.** Constants, headers, test goldens and a first-principles verification document are emitted from one source, so a number cannot drift between code, test and documentation.

The specification has been through three independent adversarial reviews (kept in [`spec/reviews/`](spec/reviews/)), an omnibus triage, and a QC pass. Their findings — among them a quench criterion that discarded roughly half the yield, a compression formula that collapsed the geometry to a point at t₀, and a generation time that failed to scale with density — are recorded in the changelog rather than quietly fixed.

## Where to start

| You want to | Read |
|---|---|
| Orient, or start a session | [`spec/README.md`](spec/README.md) — router + session protocol |
| See where the project actually is | [`spec/PROGRESS.md`](spec/PROGRESS.md) |
| Understand the physics | [`spec/01-physics.md`](spec/01-physics.md) — equations E1–E7 |
| Understand the validation | [`spec/08-validation.md`](spec/08-validation.md) — gates G0a–G5 |
| Find a number | [`spec/appendix/constants.md`](spec/appendix/constants.md) |

New agent session? Paste the bootstrap prompt from `spec/README.md` §9.

## Repository

This directory is the repository root; there is exactly one `spec/` tree and it lives here permanently. Layout is normative in [`spec/02-architecture.md`](spec/02-architecture.md) §2.

## Credits

Source research synthesised from the works cited in [`NOTICE.md`](NOTICE.md). Specification, reviews and triage authored collaboratively with [Claude Code](https://claude.com/claude-code).

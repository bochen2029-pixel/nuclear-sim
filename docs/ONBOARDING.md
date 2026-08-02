# ONBOARDING — practical context for a fresh session

**This is NOT the bootstrap prompt.** The canonical prompt lives at `spec/README.md` §9
and stays the single source of truth (MIN-12). Paste that; it points here.

This document is the *accumulated practical context* — the things that are true about
this repository but are not derivable from the spec, plus the traps that have already
cost real time. Read it once after the §3 read order. It is written for a session with
zero prior context.

Last updated: **2026-08-02**, after M1-T4a-1, by `session-2026-08-02-b`.

---

## 1. The 60-second picture

A 3D, math/physics-based **educational simulator of the 1945 Trinity device**, built
**exclusively from public and declassified literature**. Public GitHub repo, MIT
licensed: <https://github.com/bochen2029-pixel/nuclear-sim>.

The project is run as a **spec-first, multi-session protocol**. No session finishes the
project; every session finishes *one task* and a clean handoff. The spec in `spec/` is
the authority — **code that contradicts the spec is a bug**, and if reality contradicts
the spec you *amend the spec*, you do not fork the truth.

Three files carry the living state, and you must read all three:

| File | What it is |
|---|---|
| `spec/PROGRESS.md` | task table, ready-queue, the single `NEXT ACTION`, the `VERIFY` command |
| `spec/SESSIONS.md` | append-only log; the last 3 entries are the real handoff |
| `spec/DECISIONS.md` | ADRs. **Do not re-litigate these.** Wrong ADR ⇒ amendment protocol (§6) |

---

## 2. Where the project actually is

**M0 (Foundation) is complete. M1 is four-fifths done.**

| Task | State |
|---|---|
| M0-T1 … M0-T6 | done — repo, toolchain, constants, RNG, loaders, CI |
| M1-T1 geometry + tracker + `canonical_hash` | done |
| M1-T2 `ref/` transport (implicit capture) | done |
| M1-T3 `physics/eigen` (power iteration, entropy, Λ) | done |
| M1-T4a-1 benchmark models (Godiva, Jezebel) | done |
| **M1-T4a-2 the cited `fast4` xs dataset** | **open — read §7 before touching it** |
| M4-T1 GPU buffers + device Philox | **runnable now**; ADR-009 says it *should* already have started |

**75 tests green**, CI green on `main` (windows-2022 + ubuntu-latest).

### Build and verify

```bash
cmake --preset win-x64 && cmake --build --preset win-x64-rel && ctest --preset win-x64-rel
```

That is the `12 §2` canonical loop and the `VERIFY` in PROGRESS.md. It takes about a
minute warm. Run it **before** trusting anything PROGRESS.md claims (README §4:
*trust gates, not prose*).

Per-module probes are **anchored** and must stay that way:

```bash
ctest --preset win-x64-rel -R "^eigen\."
```

A useful invariant: the anchored probes are **disjoint and exhaustive** — summing their
counts equals the total test count. Check that whenever you add a module.

---

## 3. The session protocol, as actually practised

`spec/README.md` §5 is normative. What it feels like in practice:

1. **Read order** (§3), then check `spec/wip/` for orphaned journals.
2. **Run the VERIFY command.** Do not skip this.
3. **Claim before working:** branch `task/Mx-Ty-short-name`, edit PROGRESS.md
   (`in_progress`, `claimed_by`, `claimed_at`), **one** `claim:` commit, push.
   *One claim commit, not two* — §7's race tiebreak reads their timestamps.
4. **WIP journal:** create `spec/wip/<session-id>-<task-id>.md` at claim time and append
   a line per non-obvious finding **before acting on it**. This is not busywork: several
   of the best findings in `SESSIONS.md` exist because they were written down before
   being acted on, when they were still uncertain.
5. **END protocol** (§5.8–11), always, even if incomplete: green state, PROGRESS updated
   with a fresh falsifiable VERIFY and exactly one imperative `NEXT ACTION`, a SESSIONS
   entry, fold-and-delete the WIP journal, commit referencing the task ID, merge to
   `main` only green.

### Amendments

Typo/clarification ⇒ edit + one CHANGELOG line. Behaviour, API, schema, gate or constant
⇒ **ADR in DECISIONS.md** + spec edit + CHANGELOG line. Before completing any amendment,
**grep the whole spec for the fact you changed** and update every occurrence (MAJ-40) —
the CHANGELOG line must list every file touched.

### Splitting

If a task is too big, **split it** (`Mx-Ty-a`/`-b` rows + CHANGELOG) rather than
half-finishing. This has been done three times and was right each time: M0-T3 → a/b,
M1-T4a → -1/-2. A clean half beats a rushed whole.

---

## 4. Traps that have already cost time

These are the ones that recur. Every single one was found by *running* something.

1. **Read the `04`/`05` module spec, not just the `03` data contract.** M0-T3-a was
   authored against `03 §1` alone and shipped four defects against `04 §1` — including
   one functional gap (`ns::consts::crosscheck` missing, so `11 §4`'s misuse grep had
   nothing to grep). README §3 step 6 says to read the module spec; do it.

2. **A "within 1σ" acceptance threshold is a coin flip.** M1-T2's DoD specified it for
   the leakage check. Per-check false-failure is **31.7%**; all three depths pass only
   **31.8%** of the time — and **more samples cannot help**, because the pull is N(0,1)
   regardless of N. Amended to 3σ plus a 16-seed mean-pull bias check, which is *more*
   sensitive to real bias. **If any DoD you meet states 1σ or 2σ, it has this defect.**

3. **Never tune a test to make it pass.** Two tempting non-fixes were rejected in M1-T2:
   raising the history count (does nothing) and hunting for a seed that passes (fitting
   the test to an outcome — the same thing the spec forbids for `c_a`).

4. **A vacuous check proves nothing about itself.** `constants_roundtrip.py`'s
   molar-mass check read a `composition` key where `03 §3` says `isotopes`. With zero
   material files it passed for four tasks; it failed the instant real data existed.
   **`tools/verify/crosscheck_misuse.py` is still in that state** (0 compute-path
   sources) — the first `src/` code that could reference a crosscheck constant is its
   real first test.

5. **Measure, do not estimate.** In M1-T3 I wrote a spec note claiming a ~7/N entropy
   noise floor from the multinomial variance formula. That formula's leading term
   cancels only for a *uniform* distribution, and a real fission source is peaked.
   Measuring gave σ_H = 0.0249 at batch 2.5e3 and 0.0215 at 1e4 — the noise is
   **correlation-dominated, not batch-dominated**. The wrong note nearly shipped.

6. **gcc finds what MSVC does not.** The Linux CI job caught `-Wdangling-reference` on a
   reference-returning helper and an unused static function; `-Wsign-compare` on
   `size_t` vs `int` was pre-empted. Expect this on every new module.

7. **`windows-latest` is not the pinned toolchain.** It has moved past VS 2022, which the
   `win-x64` preset's generator cannot find — vcpkg builds happily, then `project()`
   fails. CI pins `windows-2022`. Moving off it is a `12 §1` amendment first.

8. **Shell/tooling papercuts.** Use `git commit -F -` with a heredoc — backticks in a
   `-m` string get executed by the shell and silently eat words from your commit message.
   `ctest --preset` writes `Testing/Temporary/` into the repo root (now gitignored).

9. **Probe names must be anchored.** `ctest -R ref` matched
   `constants.runtime lookup resolves, and **ref**uses what it must`. `-R` is an
   unanchored regex over free-prose Catch2 names. `11 §1` now requires `^<module>\.`.

10. **Do not fabricate cited data.** See §7.

---

## 5. Module-by-module notes

Things that are load-bearing and easy to undo by accident.

### `core/constants` — generated, never hand-written
- Source of truth is `spec/appendix/constants.data.toml`; `data/constants.toml` and
  `src/core/constants/constants.{h,cuh}` are **generated and committed**. Never edit them.
- Namespace is `ns::consts`. `use = "crosscheck"` constants live in
  `ns::consts::crosscheck` **so that `11 §4`'s misuse grep has a qualifier to find**.
  C-042/C-043 and the C-09x published yields are figures the model is supposed to
  *predict* — one in a compute path makes the model agree with the answer by construction.
- Three array types (**ADR-015**): `[[constant]]` scalar, `[[band]]` lo/hi with **no**
  nominal, `[[registry]]` named tuples. A gate band has no physical centre, so `[[band]]`
  deliberately emits only `_lo`/`_hi` — there is no bare identifier to misuse.
- Every entry mirrors its appendix Value cell verbatim in `appendix_text`; the roundtrip
  compares it exactly. That is the drift detector, and it has caught real drift.
- Banded constants emit a generated `static_assert(lo <= value <= hi)`; PENDING constants
  emit `double name() = delete;` so reading one is a **compile** error.

### `core/rng` — Philox4x32-10
- `rng::Stream` has **no default constructor**, deliberately (`04 §2` makes seed and
  stream id required). A default-seeded stream is a determinism hazard. Aggregate-init
  `Particle` instead of adding one.
- `normal_f()` **discards** the Box–Muller partner rather than caching it. Caching is the
  obvious optimisation and a latent D9 bug: `(ctr, sub)` is the entire serialised state,
  so a cached deviate would make kill→resume differ *only sometimes*.
- The project-local KAT is **not self-recorded**. `tools/verify/rng_kat_gen.py` is a
  second, independent Philox that must reproduce the three published Random123 vectors
  before it may emit the golden file. Keep that property.
- `fork(42,1000,3) = 12597386599640143736` is frozen. **M4-T1 must reproduce it on the
  device.**

### `core/xs`, `core/material`, `core/scenario` — loaders
- Positive tests parse the **spec's own example documents**, extracted from
  `03-data-contracts.md` at run time (`tests/unit/spec_examples.h`). A copied fixture
  would drift from the document people actually read.
- `[source]` under a non-`fixed_source` mode **warns, it does not error** — the strict
  "REQUIRED iff" reading rejects the spec's own canonical example. Same ignore-with-WARN
  pattern `03 §4` uses for `compression.t_c_s` at tier 2. Do not "fix" this back.
- `layers` is **top-level**, not inside `[data]`. TOML reparents any bare key written
  after a table header; the spec's example used to have this bug.
- A material may name a species with a molar mass but **no** cross sections (structural
  elements in an actinide-only set) — `Constituent::iso` is null then. The *molar mass*
  is the hard error, per appendix §3, because that is what silently corrupts Σ.

### `core/geometry` + `core/hash`
- The boundary nudge follows the **direction of travel** (MIN-14). Backwards, it returns
  an escaping neutron to the body it just left: no crash, no obviously wrong number, and
  it survives to a gate. M1-T2's pure-capturer leakage check is the first thing that
  would notice.
- `locate()` puts a point exactly on a boundary in the **inner** layer, and
  `distance_to_boundary` requires roots strictly above ε. **These two conventions are a
  pair** — together they stop a particle on a surface from being handed a zero-length
  step forever. Changing one without the other reintroduces the loop.
- SHA-256 is **in-tree** (no vcpkg baseline port provides one, and `02 §4` makes a new
  dependency an ADR). Checked against the published NIST vectors. It is used for identity
  and change detection, never security.
- `canonical_hash()` hashes the **parsed struct**, not the source text — which is why
  every `04 §6` stability property holds by construction. Absent optionals emit an
  explicit `none`; tallies are sorted (a set, not a sequence).

### `ref/` transport
- E1a–E1e with **full implicit capture** (ADR-012 item 1). The neutron is never killed at
  fission; `sigma_c` never terminates a history directly.
- Flight uses the **transport-corrected** Σ_tr; collision sampling and the weight
  reduction use Σ_t. That asymmetry is intentional (P0 transport correction).
- `run_fixed_source` tallies fission production but **does not propagate progeny** —
  propagation is a fission-source iteration, i.e. the eigen solver. That is what makes
  the analytic k_inf check possible.
- One stream per history keyed by history index, so what a history sees depends on its
  own identity, never execution order. This is what makes the optional OpenMP-per-history
  parallelism in `05 §1` safe to add later without changing results.

### `physics/eigen`
- Returns **`k_eff` on total ν̄ and never a β-corrected k** (ADR-013). `k_prompt()` is the
  single derivation point. A solver returning k_prompt would be off by ~650 pcm on a
  U-235 system — larger than G0a's entire tolerance, and entirely plausible-looking.
- Note the pcm gap is `k·β·1e5`, so it **scales with k**. An absolute pcm bound written
  for a critical system is wrong on a supercritical one.
- σ is the **larger** of the active-cycle and batched-means estimates (MAJ-32).
- It is a free function taking the transport, deliberately — that keeps it
  backend-agnostic for M4-T3. `RefTransport::run_eigen`/`last_source()` from `05 §1` are
  not declared; declaring an unimplemented method is what SYNC-M1 caught for `mix()`.
- **Open obligation, recorded in `05 §2`:** before G0a is claimed, M1-T5 must measure σ_H
  at the real gate configuration (C-900, batch 1e6) on a real benchmark and either
  confirm `eig_h_tol = 1e-3` is reachable or amend C-901. A convergence criterion that
  can never fire silently degrades E2b to an `I_min`-only test while still reporting
  "converged".

### `data/benchmarks` — Godiva and Jezebel
- Source is **JEFF Report 16 Annex 3** (OECD/NEA, open), reproducing **CSEWG BNL 19302
  ENDF 202 Rev. 11-81** — Godiva F5, Jezebel F1. `status = "PUBLIC-DERIVED"`.
- **Atom densities are the primary datum**; ρ, mass and wt% are derived. That inversion
  is what makes the derived values independent cross-checks. `test_benchmarks` recomputes
  every card number from the committed material, so the cards cannot drift.
- "4.5% Pu-240" has three readings (4.4710 wt% of material, 4.5171 wt% of Pu, 4.4990 at%
  of Pu) and they all round the same. Settled in ADR-017; do not re-litigate.
- Jezebel's Ga is split Ga-69/Ga-71 by **explicit author choice** — `03 §3` forbids the
  *loader* doing it, precisely to force that recorded decision.

---

## 6. Boundaries that are not negotiable

Read `spec/00-overview.md` §2–§3 before writing code — it is a hard constraint, not a
suggestion. In short:

- **Public and declassified sources only.** Every constant carries `cite` + `status`.
- **BLK-14: an autonomous session must never seek ICSBEP Handbook access.** It is
  access-restricted (NEA Data Bank, named users, intended-use statement). Benchmark
  models come from open literature and are tagged `PUBLIC-DERIVED`. Replacing them with
  Handbook values is the **owner-gated** M1-T4b.
- **ADR-014 — publication authority.** Pushing to `origin/main` on *this* repo is
  ordinary work. Creating a new public repo, changing visibility, publishing releases, or
  pushing to any other remote each need a **fresh owner instruction** in that
  conversation. Standing permission does not exist.
- Interactive single-point counterfactual exploration is in scope (marked
  `non_canonical`, never gate evidence). Automated search over *physical* axes toward an
  objective is out — enforced mechanically by `axis_class` + `ScoreKind`.

---

## 7. The one task that can legitimately end in `blocked`

**M1-T4a-2 — the cited `fast4` cross-section dataset.**

It needs ~450 physical values across the 16 species `08 §1` enumerates, each with a
citation and status, plus `beta` per isotope and `mu_bar` per group, plus a provenance
data card documenting the weighting spectrum, collapse method and transport correction
(the one-time D4 carve-out).

Acceptable sources: a **published multigroup library**, or **ENDF/B processed through a
documented collapse**.

**If you cannot obtain cited data, mark it `blocked`, say why in SESSIONS.md, and switch
to M4-T1.** Do not fill the file with plausible numbers. Fabricated cross sections would
make G0a and G0b compare the code against values someone invented — the single failure
mode this project's entire provenance discipline exists to prevent, and one that would be
invisible in review because the numbers would look right.

This is a case where **stopping and asking the owner is the correct outcome.**

---

## 8. What to do next

1. **M1-T4a-2** — the dataset above. Blocks M1-T5/G0a and all of M2. May end blocked.
2. **M4-T1** — GPU buffers + device Philox. Fully unblocked, and ADR-009 says it *should*
   already have started in parallel. This is the right pick if M1-T4a-2 stalls, and it
   has everything it needs: `04 §2`'s layout, the frozen KATs, and the `fork` value to
   reproduce on the device.

Before claiming the first task of a **new milestone**, run the SYNC audit (`07 §SYNC`) —
module APIs vs `04`/`05`, scenario paths vs `03 §4`, constants vs the appendix — and
record it as `SYNC-M<n>` in PROGRESS.md. SYNC-M1 found two real gaps; it is not a
formality.

---

## 9. Standing obligations, collected

Things owed by a future task that are easy to lose track of:

| Owed by | What |
|---|---|
| M1-T5 | Measure σ_H at C-900 on a real benchmark; confirm or amend C-901's `eig_h_tol` **before G0a** (`05 §2`) |
| M1-T5 | `gen_gates` must read `[[band]]` entries and **must not** synthesise a nominal from one (ADR-015) |
| M2-T1 | Extend `oracle.py` §2 (layer masses) rather than writing a separate check; it emits an explicit PENDING block today |
| M2-T1 | The C-102 pit 2.4% density over-determination check lands here, where the densities exist |
| M4-T1 | Reproduce `fork(42,1000,3)` on the device (`04 §2c`) |
| M4-T4 | C-945 (G4 perf budgets) is `PENDING`, resolved by measurement on the dev GPU |
| M5-T5 | Verify the `nvidia/cuda:13.1-devel-ubuntu22.04` tag/digest **before** writing the Dockerfile (`12 §3`) |
| M6-T3 | C-906 (`c_a`) is `PENDING` and is **derived**, never gate-fitted |
| M7-T2 | C-905 (render scales) is `PENDING`, set by the `09 §1` temperature calibration |
| any | `tools/verify/crosscheck_misuse.py` is still vacuous — its first real test is the first `src/` code that could reference a crosscheck constant |

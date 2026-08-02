# NUCLEAR-SIM — Spec Router & Session Protocol

> **You are a new agent session with zero prior context. This file is your entry point.**
> Read it fully (it is deliberately short), then follow the Read Order. Do not read the whole spec — load only what your current task needs.

---

## 1. What this project is

A 3D, math/physics-based **educational simulator of the 1945 Trinity "Gadget" / Fat Man device**: GPU Monte Carlo neutron transport → criticality (k-eigenvalue) → supercritical burst kinetics → simplified implosion/disassembly hydrodynamics → volumetric fireball rendering. Built **exclusively on public/declassified literature** (Los Alamos Primer, LA-3067, Nuclear Weapon Archive, Wellerstein, public Godiva/Jezebel benchmark descriptions). Source material: `research/` (the research document lives there after M0-T1). **Public GitHub repo, MIT license (ADR-011).**

**Before writing any code, read `00-overview.md` §2–§3 (Scope & Boundaries). Hard constraint, not a suggestion.**

## 2. Spec map

| File | Contents | When to read |
|---|---|---|
| `00-overview.md` | Purpose, non-goals, **scope & boundaries**, conventions | Always (first session) |
| `01-physics.md` | Notation, equations E1–E7, constants policy, precision policy | Any physics work |
| `02-architecture.md` | Decisions D1–D9, layering, repo tree, main loop, coding standards | Any structural work |
| `03-data-contracts.md` | All file schemas (constants, xs, materials, scenario, tally, run, sweep, checkpoint, fields, gates.toml, gate_report) | Any IO/schema work |
| `04-module-core.md` | constants, rng, xs, geometry, material, scenario APIs | Core module tasks |
| `05-module-transport.md` | ref + gpu transport, eigen, kinetics, hydro, tallies | Transport tasks |
| `06-frontends.md` | nukebench / nukefarm / nukestudio / nukecinema specs | Frontend tasks |
| `07-milestones.md` | Milestones M0–M7, task IDs, DoDs, gate labels, SYNC audits | Every session (your task lives here) |
| `08-validation.md` | Benchmark sheets, gate procedures, statistics rules | Gate/benchmark tasks |
| `09-rendering.md` | Field grids, raymarch, color, staged clock | M7 |
| `10-ui.md` | Parameter panel spec (annotation-driven) | M7 |
| `11-testing.md` | Test pyramid, goldens, differential, perf, resume | Any task (DoD references it) |
| `12-deployment.md` | **Toolchain pins (single source of truth for versions)**, build, container, cloud, cost | M0, M5 |
| `13-risks.md` | Risk register with mitigations + residual ratings | When a risk triggers |
| `appendix/constants.md` | Full cited constants + geometry + gate bands | When you need a number |
| `appendix/constants.data.toml` | Strict machine-readable constants (M0-T3 authors) | M0-T3 |
| `PROGRESS.md` | **Living state.** Tasks, gates, ready-queue, NEXT ACTION, VERIFY | **Every session, 2nd file** |
| `SESSIONS.md` | Append-only session log | **Every session, 3rd file** |
| `DECISIONS.md` | Append-only ADR log; decisions final unless amended | **Every session, 4th file (skim)** |
| `CHANGELOG.md` | Spec amendment log | When amending spec |
| `wip/` | In-flight session journals (§5.4b) | Check for orphans at START |
| `reviews/` | External review reports (triage via ADR-012) | Reference |

## 3. Read order (every new session)

1. **This file.**
2. **`PROGRESS.md`** — milestone, gates, **ready-queue**, NEXT ACTION, VERIFY.
3. **`SESSIONS.md`** — last 2–3 entries.
4. **`DECISIONS.md`** — skim; do not re-litigate logged ADRs. Wrong ADR? Amendment protocol (§6).
5. **`07-milestones.md`** — find your task; read goal/files/DoD/gate.
6. Module spec(s) your task touches + `03-data-contracts.md` as needed.
7. `appendix/constants.md` — only if you need constants.

Context budget: >5 spec files for one task = over-reading.

## 4. Verify-first rule

`PROGRESS.md` may be stale. Before building on claimed state, run the `VERIFY:` command in PROGRESS.md. If it fails, the claimed state is not real: fix or revert first, record in SESSIONS.md. **Trust gates, not prose.**

Rules: a VERIFY command **MUST be falsifiable** — `echo`, `true`, and comments are prohibited as VERIFY commands (B-16/MIN-11). Before implementation exists, the convention is `test ! -d .git && echo "pre-M0-T1 state confirmed"` (fails once the repo exists). After M0-T2, VERIFY is a real build/test probe. Every task DoD should install a per-task VERIFY probe (e.g. `ctest -R rng`) so verify-first has teeth pre-gate (D7). **Check `spec/wip/` for orphaned journals before starting** — an orphan means a session died mid-task; read it first (§5.4b).

## 5. Session protocol

### START
1. Read order (§3) + wip-orphan check (§4).
2. Pick a task from the **ready-queue** in PROGRESS.md (a task is *runnable* iff status `todo` and all `depends_on` are `done`). The NEXT ACTION is the recommended pick — if you disagree, say why and update it first.
3. **Claim:** create branch `task/Mx-Ty-short-name`, edit PROGRESS.md (status `in_progress`, `claimed_by`, `claimed_at`), and push an empty commit `claim: Mx-Ty by <session-id>` **before work begins**. The claim = the edit + the pushed commit.
   **Bootstrap exemption (QC-15):** the claim protocol requires a git repository with a remote, and **M0-T1 is the task that creates it** — so M0-T1 is explicitly exempt: claim it by editing PROGRESS.md alone. From M0-T2 onward the full protocol applies. The owner SHOULD create the empty public repo (ADR-011) *before* M0-T1 so that M0-T1's own sequence is `git init` → `git remote add origin` → initial commit → push; if the remote does not yet exist, M0-T1 commits locally, records `remote: pending` in PROGRESS.md, and the first session after the remote appears pushes and removes the note. A local-only repo makes the §7 timestamp tiebreak unusable — until the remote exists, treat all tasks as single-session and do not run parallel sessions.
4. Session id: `session-YYYY-MM-DD-<letter>`; scan SESSIONS.md for today's date, take the next unused letter. If your letter is taken when you write your END entry, re-letter and note the collision (it means a parallel session ran).

### DURING
5. Work only on the claimed task. Spec problem → amendment protocol (§6). No silent improvisation.
6. **No magic numbers:** constants from `data/constants.toml` (01 §8); gate thresholds from `gates.toml` (08).
7. Keep changes scoped to the task's declared files. No opportunistic refactoring.

**4b. WIP journal (MAJ-41):** at claim time create `spec/wip/<session-id>-<task-id>.md`; append one line per non-obvious finding, dead end, or rejected approach — **before** acting on it, not after. Committed, disposable. At END: fold durable items into your SESSIONS.md entry, then delete the journal. An orphaned journal (task not `in_progress` by its owner, or claim > 24 h stale) means a session died holding it — read it, fold anything useful into your notes, then delete it.

### END (mandatory, even if incomplete)
8. **Green-state rule:** build passes + previously-passing tests pass, or your changes are reverted/feature-flagged, or the task marked `blocked`. Never hand off red.
9. Update PROGRESS.md: task status, gate evidence paths, fresh falsifiable VERIFY, and the NEXT ACTION — **exactly one** imperative sentence naming task ID + files + spec section (format: `NEXT ACTION: Execute Mx-Ty — <imperative>; see spec/<file> §<section>; DoD in 07-milestones.md`).
10. Append to SESSIONS.md (format there): date, task, did/evidence/state/blockers/notes.
11. Commit referencing the task ID (`M3-T2: add thin-shell disassembly ODE`); merge to `main` only green.

## 6. Amendment protocol

- **Typos/clarifications:** edit + one CHANGELOG line.
- **Behavior, API, schema, gate, or constant changes:** ADR in DECISIONS.md (rationale + evidence), spec edit, CHANGELOG line. Gates/constants ONLY change this way, with cited evidence.
- The spec is the authority; code contradicting the spec is a bug. If reality contradicts the spec, amend the spec — don't fork the truth.
- **One spec tree (BLK-12):** exactly one `spec/`, at the repo root, version-controlled in this repository. Creating a second copy (export, vendoring, archive) is forbidden; distribution copies are generated into `build/` and gitignored.
- **Impact analysis (MAJ-40, mandatory):** before completing an amendment, grep the ENTIRE spec for the fact you changed (device name, version pin, threshold, field name, identifier) and update every occurrence. The CHANGELOG line MUST list every file touched. An amendment that changes a fact in fewer files than the fact appears in is incomplete — treat as a defect.

## 7. Parallel sessions

- One task per session. Claim only runnable tasks (§5.2–3).
- **Race resolution without a human (MAJ-40):** if two claims exist for the same task, the one whose `claim:` commit has the EARLIER timestamp wins; the later session MUST abandon, revert its claim row, and pick another runnable task. Identical timestamps → lexicographic session id. PROGRESS.md merge conflicts: keep both task rows; the NEXT ACTION line is resolved by the winning claim's session.
- Stale claim: lock older than 24 h wall-clock AND no new commits on the task branch ⇒ claim may be reaped (note in SESSIONS.md).

## 8. Context discipline (1M-window reality)

- This project spans dozens of sessions. **No session completes the project; every session completes a task and a clean handoff.**
- Spec files are sized for partial reads — use Read with offset/limit. Use `C:\chunker` for anything huge.
- Running low on context mid-task? Stop early, execute the END protocol, leave a precise NEXT ACTION. A clean handoff beats a rushed finish.
- Oversized task? Split it in PROGRESS.md (`Mx-Ty-a/-b` rows + CHANGELOG line) instead of half-finishing.

## 9. Bootstrap prompt (paste into a fresh session/instance)

```
You are continuing the NUCLEAR-SIM implementation at C:\NUCLEAR.
Read spec/README.md fully and follow its read order (§3): spec/PROGRESS.md,
spec/SESSIONS.md (last 3 entries), spec/DECISIONS.md (skim — respect logged ADRs).
Check spec/wip/ for orphaned journals. Then read docs/ONBOARDING.md — it carries
the accumulated practical context and the traps that have already cost time.
Follow the session protocol in spec/README.md §5 exactly. Verify state with the
VERIFY command in PROGRESS.md, then pick from the ready-queue (recommended: the
NEXT ACTION).
```

(Canonical copy lives HERE; SESSIONS.md points at this section — do not duplicate the text.)

`docs/ONBOARDING.md` is **supplementary, never a second prompt**: it holds what is true
about the repository but not derivable from the spec (the build/verify loop, module
gotchas, recurring failure patterns, standing obligations owed by future tasks). This
section remains the only place the prompt text lives.

## 10. Spec status

Version **0.3** (2026-08-03 — QC pass on the v0.2 triage; ADR-013 + QC-01…QC-16). Implementation has NOT started. `PROGRESS.md`: all tasks `todo`; recommended NEXT ACTION = M0-T1. Hosting: public GitHub + MIT (ADR-011); **the owner SHOULD create the empty public repo before M0-T1** (README §5.3 bootstrap exemption).

**Review is closed at v0.3.** This spec has been through three independent external reviews (`reviews/`), one omnibus triage (ADR-012) and one QC pass (ADR-013). From here, spec changes originate from **implementation contact** via the amendment protocol (§6) — not from further review. The next defect class is only findable by running code.

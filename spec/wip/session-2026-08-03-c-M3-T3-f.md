# WIP — session-2026-08-03-c — M3-T3-f (run.json 03 §6 provenance contract)

Append-only, one line per non-obvious finding BEFORE acting (README §5.4b). Folded into
SESSIONS at END, then this file is deleted.

## Task
Step 4 (the fusion API) split → M3-T3-f = the `03 §6` run.json contract (bounded first
increment, M3-T3-a pattern) → M3-T3-g = the `evaluate`/`generate_run` surface.

## Findings / decisions

- **State check:** HEAD is `9d19fb9` ("docs: … step-4 handoff", = `87241c2` + the handoff
  doc), NOT `87241c2` as the handoff/PROGRESS prose say. Both are the same code; VERIFY
  green 117/117 (104 CPU + 13 gpu.). So the handoff doc was committed on top; not a problem.

- **Module location (design decision I own, owner-delegated):** new top-level `src/api/`
  nscore module. Rationale: `evaluate`/`generate_run` orchestrate physics (`run_burst`,
  `ref_eigen_fn_masscons`), so per D7 they can NOT live in a frontend (`src/app/` is
  physics-free); they are nscore. `src/api/` is a clean facade layer between `physics/` and
  `app/` — the handoff's suggested location, and the one candidate (`src/api/`) that does not
  duplicate `src/app/` (unlike `src/frontends/`). run.json provenance + the surface both live
  here (one new anchor `^api\.` for both f and g). Amends the `02 §2` tree (CHANGELOG line, no
  ADR — a directory addition per §2's own rule).

- **Scope decision (Option A — minimal, clean):** M3-T3-f implements EXACTLY the `03 §6`
  example schema (the 15 fields), purely additive code, no spec SCHEMA edit, so the round-trip
  test parses the `03 §6` example verbatim. Defer the `scenario_overrides` struct field to
  M3-T3-g (see next finding). This keeps the first increment bounded like M3-T3-a.

- **Spec inconsistency found (defer to M3-T3-g):** `03 §4` normatively says scenario overrides
  are "Recorded verbatim in run.json", but the `03 §6` run.json EXAMPLE has no
  `scenario_overrides` field. Reconciling (adding the field to the §6 example — a clarification,
  not a behaviour change, since §4 already mandates it) belongs with M3-T3-g, which also needs
  overrides for the viz. Recorded here so it is not lost. `compute_unit_id` still honours
  `03 §6`'s "sha256(canonical_hash ‖ overrides ‖ seed)" by taking overrides as a parameter.

- **simstub `run` object is a SUBSET of `03 §6`** (has: schema_version, run_id, scenario_file,
  scenario_overrides, seed, code_version, backend, non_canonical, note; MISSING: unit_id,
  scenario_sha256, data_hashes, spec_version, git, dirty, device, started, finished). So the
  canonical run.json (M3-T3-f) is a superset the viz reads a few fields of; M3-T3-g maps the
  viz-facing projection. `non_canonical` stays in tally.json (`03 §4`), not run.json.

- **`compute_unit_id` serialization (I define it; M5-T2 will consume it):** overrides are
  SORTED (order-independent dedup key, matching `canonical_hash`'s own sorted override block),
  then streamed via `ns::hash::Sha256` as
  `"canonical_hash=" ‖ canon ‖ "\n"` then per override `"override=" ‖ ov ‖ "\n"` then
  `"seed=" ‖ dec(seed) ‖ "\n"`. Unambiguous, stable, documented in the header.

- **`canonical_hash()` already incorporates overrides (sorted) + seed** (`canonical_hash.cpp`),
  so `03 §6`'s "‖ overrides ‖ seed" is partly redundant with the canonical hash — harmless;
  `compute_unit_id` follows `03 §6` literally regardless.

- **Test count / anchored-probe invariant:** new `test_api` with `TEST_PREFIX "api."`.
  Update the PROGRESS VERIFY sum (…+8(couple)+N(api)) once N is final.

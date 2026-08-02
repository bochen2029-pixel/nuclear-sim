# 11 — Testing Strategy

Every task's DoD includes its tests. `ctest` is the single entry point; suites are labeled (`quick`, `full`, `golden`, `differential`, `perf`, `resume`) so CI can select. MC-based tests live in `full` (the `quick` label stays < 5 s total; MIN-24).

## 1. Pyramid

- **Unit (Catch2, per module):** per `04 §7`, `05`, `07` task DoDs. Core/ref: no GPU required.
- **Test naming (normative, M0-T2).** Every Catch2 executable is registered as `catch_discover_tests(<target> TEST_PREFIX "<module>.")`. This is load-bearing, not cosmetic: `catch_discover_tests` names each ctest entry after the Catch2 *test-case text*, so without a module prefix the per-task VERIFY probes this spec already commits to match nothing — and because the test presets set `noTestsAction=error`, `ctest -R <module>` then FAILS on a green tree. That is precisely the lying probe `README §4` exists to prevent. A task that adds a test executable adds its row here.

  **Probes MUST be anchored** (`^<module>\.`), not bare substrings (M1-T2). `-R` is an unanchored regex over the whole test name, and Catch2 names entries after free-prose test cases: `ctest -R ref` matched `constants.runtime lookup resolves, and refuses what it must`, and `-R geometry` matched `constants.canonical geometry radii are strictly nested`. Anchoring is what the `TEST_PREFIX` convention exists to make possible, and it keeps the probes disjoint by construction rather than by policing test prose forever.

  | Module (task) | Target | `TEST_PREFIX` | VERIFY probe |
  |---|---|---|---|
  | toolchain (M0-T2) | `test_toolchain` | `toolchain.` | `ctest -R "^toolchain\."` |
  | constants (M0-T3-a) | `test_constants` | `constants.` | `ctest -R "^constants\."` |
  | oracle (M0-T3-b) | *(python)* | `oracle.` | `ctest -R "^oracle\."` |
  | rng (M0-T4) | `test_rng` | `rng.` | `ctest -R "^rng\."` |
  | loaders (M0-T5) | `test_loaders` | `loaders.` | `ctest -R "^loaders\."` |
  | geometry (M1-T1) | `test_geometry` | `geometry.` | `ctest -R "^geometry\."` |
  | ref (M1-T2) | `test_ref` | `ref.` | `ctest -R "^ref\."` |
  | eigen (M1-T3) | `test_eigen` | `eigen.` | `ctest -R "^eigen\."` |
- **Golden (per-backend, MAJ-42):** goldens recorded as `tests/golden/<artifact>.<backend>.sha256`; a golden test compares against the golden for the backend under test and SKIPs (not FAILs) when none exists. `tally.json` goldens ALSO compare structurally: all fields present, all 9 invariants of `03 §5` hold, floating values within declared tolerance (1e-12 same-backend, 3σ cross-backend). Cinema frame goldens: perceptual-hash with declared threshold, never exact hashes. Intentional changes ⇒ regenerate + CHANGELOG entry + visible diff.
- **Differential (T-diff):** ref vs gpu per `08 §G0c`; OptiX-vs-analytic parity (M6-T1). Mandatory before M4/M6 gate claims.
- **Perf (T-perf):** timed gate scenarios per `08 §G4`; results appended to `artifacts/perf_history.jsonl` — **committed** (`02 §2`), because its whole value is the cross-session trend. Rotated at 100 MB into `perf_history_<YYYYMM>.jsonl`, which IS gitignored (history beyond the current file goes to object storage).
- **Resume (T-resume):** scripted kill at phase boundaries and generation boundaries (25/50/75% of a canonical α-mode run; mid-eigen resumes at last completed generation boundary per 03 §8 §6) → resume → bit-identical final `tally.json` **same backend**; checkpoint mismatch/CRC-rejection negative tests. Required for M5-T1.

## 2. Determinism tests

- Same seed, same backend, any thread count/block size ⇒ bit-identical tallies (validates 05 §6 slots/streams/reductions).
- Same seed, ref vs gpu ⇒ statistical identity (G0c criteria).
- Studio export ⇒ CLI bit-identical same backend (G5).

## 3. CI matrix

| Runner | Scope |
|---|---|
| windows-latest (MSVC) | build + quick/full unit + CPU-backend golden |
| linux container (gcc, CUDA) | build + quick/full unit + CPU-backend golden |
| self-hosted/cloud GPU (on demand, M4+) | differential + perf + GPU golden |

CI red on main ⇒ no new task claims until fixed (green-state rule applies project-wide). CI also runs `tools/verify/*` (constants roundtrip, decision_index) and the static crosscheck-constant misuse grep (`01 §8`).

## 4. Mandatory coverage of the tricky spots (compiled from review triage)

- RNG: 3 Random123 KATs + project-local vector + fork KAT + (ctr,sub) round-trip + stream independence + device/CPU parity (M4-T1).
- E3a: closed-form at fixed k; renorm×3 invariance; weight-weighted F_n; mixed-assembly ν̄_eff; source-term injection.
- Transport: k_inf analytic (01 §2); leakage at 3 optical depths (05 §1); ε-nudge direction both ways.
- Loaders (negative tests, one per rule): invalid TOML class; `sigma_a` migration diagnostic; `sigma_t` present in file; null transfer non-SIM; upscatter; missing mu_bar; non-descending bounds; `mode="td"` rejection; unknown override key; `layers[tamper]` positional vs `layers.tamper` id-lookup; jezebel↔pu_ga_delta mismatch; xs_set name mismatch; dump-budget warning.
- Contracts: `tally_invariants` (all 9, 03 §5) on the canonical artifact — **the illustrative example in `03 §5` is itself a test fixture and MUST satisfy all nine** (QC-04); `canonical_hash` stability matrix (key order, comments, CRLF↔LF, float spellings); `[ui.*]`↔`10` table resolution + defaults-within-ranges; `constants_roundtrip` bijection; `decision_index` D↔ADR mapping.
- Gates: `nukebench gate --seed 7` exits 2; `gates.toml` `spec_sha256` mismatch fails; append-only attempts preserved across re-runs; `dirty` tree caps verdict at conditional.
- Hydro: Tier-1 endpoints + mass at 5 s-values; Tier-2 energy conservation <1e-6, no-motion, constructor matching; c_a regression vs C-071/C-072 rule.
- Geometry ε-degeneracies (grazing rays) — `04 §4`.
- Checkpoint round-trip at EVERY phase boundary (D6) + eigen-section restore.

## 5. Independent verification oracle (QC-16, M0-T3)

Gates test **code against benchmarks**. They do not test **the spec's own numbers against physics**, and every physics defect the three external reviews found (Tier-1 collapsing to r=0; quenching at peak power; Λ held constant under 2.2× compression; the E1c estimator double-counting Σ_f/Σ_t) was of that second kind — visible only to someone recomputing the quantity a different way. All four survived review-by-reading and died to arithmetic.

`tools/verify/oracle` therefore generates `docs/VERIFICATION.md` from `constants.data.toml` **from first principles, without reading or linking `nscore`**:

- Φ_kt recomputed from C-918/(C-040·C-917); kt/kg for Pu-239 and U-235 recomputed independently and the ~5% C-042 basis gap stated explicitly (MAJ-11).
- Layer masses recomputed from the `02 §2` geometry × material densities and compared to the appendix table (the pit's 2.4% over-determination reproduced, not hidden — C-102 note).
- The Tier-1 radius map evaluated at s ∈ {0, ¼, ½, ¾, 1} and checked for endpoint correctness (this alone would have caught BLK-01).
- Fuchs–Nordheim post-peak energy fraction stated as the reason E6 cannot terminate at k=1 (BLK-03).
- Λ ∝ 1/ρ tabulated over the compression range (BLK-04).
- All nine `03 §5` tally invariants evaluated symbolically on the canonical example.
- Every `C-9xx` gate band restated with its derivation, so a widened band is visible as a diff.

Rules: the oracle is **generated, never hand-written**; a constant cannot drift between code, test and documentation because none of the three is authored. It runs in CI (`tools/verify/*`, §3) and a regeneration diff is a reviewable artifact. Precedent: `C:\Astrophage` `scripts/canon.py` + `docs/VERIFICATION.md`, which caught four real errors on that project including a 47× integrator error the spec itself had specified — port the pattern rather than re-deriving it.

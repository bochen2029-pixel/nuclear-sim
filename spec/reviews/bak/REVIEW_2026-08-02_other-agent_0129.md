# NUCLEAR-SIM Specification Audit — REVIEW_2026-08-02

**Reviewer role:** senior pre-implementation auditor (MC neutron-transport/criticality physicist + GPU/CUDA architect + multi-session spec auditor).
**Scope reviewed:** `spec/README.md`, `00`–`13`, `appendix/constants.md`, `PROGRESS/SESSIONS/DECISIONS/CHANGELOG`, and the source research doc `compass_artifact_…md`. Spec version 0.1; implementation not started.
**Mandate:** adversarial. Find what is wrong. Do not edit project files. Write findings so a later triage session can act on each **independently**.

## How to read this report

- Every finding has a stable **ID** (dimension-prefixed), **severity**, **file+section**, **what's wrong**, **why it matters**, **fix** (proposed text where possible), and a **Triage** note confirming it can be resolved on its own.
- Severity: **BLOCKER** (makes a task/gate impossible or corrupts the canonical foundation many sessions build on) / **MAJOR** (must fix before the relevant milestone; wrong result or wasted sessions otherwise) / **MINOR** / **NIT**.
- Numbers checked against public literature are cited. Where I estimate, I mark **UNCERTAIN** and say so — I do not assert.
- A **PRESERVE** list at the end flags what is genuinely good; triagers should not "fix" those.

### Verification confidence note
I verified against: Lazarus & Richtmyer (1981) converging-shock exponents; ICSBEP HEU-MET-FAST-001 / PU-MET-FAST-001 (Godiva/Jezebel) canonical values; Selby et al. 2021 (24.8±2 kt); Coster-Mullen / NWFAQ geometry; standard fission-energy/kt arithmetic; and the project's own research doc. Monte-Carlo variance-vs-history-count claims are **empirical rules of thumb** and are marked UNCERTAIN on exact counts.

---

## Summary table

| ID | Sev | Dim | One-line |
|---|---|---|---|
| B1 | BLOCKER | B/H | Scenario TOML example (`03 §4`) is invalid TOML — won't parse, yet declared "tests parse verbatim" |
| B2 | BLOCKER | B/H | `tally.json` example (`03 §5`) internally inconsistent ~10× (fissions/tamper) + `fission_mesh` length/sum wrong |
| F1 | BLOCKER | F/A | G0a/G0b ±500 pcm with 4-group **isotropic-in-lab** scatter is likely unachievable honestly → gate fails or is tuned-to-pass (circular) |
| C4 | BLOCKER | C/F | M1-T4 has no in-repo/openly-distributed ICSBEP data **and** must produce a ±500 pcm 4-group set with "no ENDF parsing" — not a citation task |
| A1/B3 | MAJOR | A/B | Guderley exponent `0.688377` is the **γ=5/3** value but labelled **γ=1.4** (`01 §5`, `appendix C-070`); `05 §4` says γ=5/3 → 3-file contradiction |
| A2 | MAJOR | A | Generation time `g` held **constant** under 2× compression; physically shrinks ∝ mfp → biases α, burst timescale, yield, quench |
| A3 | MAJOR | A | Quasi-static eigen-refresh validity **overstated** at disassembly (timescales converge); error unbounded/undocumented |
| A4 | MAJOR | A | Tier-1 compression formula (`01 §5`) is wrong: `s(t)` factor sends r→0 at t₀; "1+x−1" is dead algebra. Used for G1a/G1b/G2 |
| E1/E2 | MAJOR | E | Bit-identical determinism & resume (D9/G0c/T-resume) not achievable with float block reductions + **race-ordered atomic-cursor** fission bank; `05 §6` "atomic cursor" contradicts `04 §2` "no shared cursor" |
| E3 | MAJOR | E | G4 ≥30 fps studio vs ~0.7 s/eigen refresh (from `tally` example) — a blocking refresh stalls ~20 frames; async/incremental eigen unspecified |
| E4 | MAJOR | E/F | σ ≤ 25 pcm unreachable at batch `1e5`/50-active (~5e6 active histories); need ~1e6–1e7/gen (UNCERTAIN exact) |
| F2 | MAJOR | F/A | G1a band `[0.70,0.95]` mis-maps a critical-**mass** ratio (C-052 "78%") to k_eff; source says k≈0.9 → low end indefensible; passes under-reactive model |
| G1 | MAJOR | G/B | `00 §2` forbids novel configs, but `10-ui` & `03 §7` expose W/Be/none tamper + material sweeps → boundary self-contradicting |
| G2 | MAJOR | G | Scope guard constrains **objective** only; `[[space]]` axes can leave the public envelope; MCTS+yield-target ≈ design-optimization |
| B4 | MAJOR | B | CUDA "12.x" (`02 §4`) vs "13.1" (`12 §1`/machine); CHANGELOG claims `02` was updated — ADR-009 propagated incompletely |
| B5 | MAJOR | B/C | OptiX env var named `OptiX_ROOT`/`OptiX_INSTALL_DIR` vs `OPTIX80_SDK`; "80" contradicts SDK **9.1.0** |
| B6/B7 | MAJOR | B/C | Override/param-path syntax inconsistent across `03/04/06/10`; `10-ui` paths (`materials.*`,`xs.*`,`initiator.sim_count`) not in the `03` schema |
| B8 | MAJOR | B/H | `initiator.strength` default `1e8` **exceeds** its own `ui_range` max `1e7`; gate runs use defaults → self-violating validation |
| C1 | MAJOR | C/D | M0-T1 copies `spec/` into `nuclear-sim/spec/` → **two** living PROGRESS/SESSIONS trees; authority undefined |
| C2 | MAJOR | C/H | Appendix constants are **ranges** ("1.7–1.8", "3–4", "180 (total ~200)") but `constants.toml` has scalar `value` — gen_constants ambiguous |
| C3 | MAJOR | C/E | Philox counter/key partitioning over (seed,stream,particle,event,block) unspecified → substream-collision / correlated-neutron risk |
| D1 | MAJOR | D | Single `NEXT ACTION` can't represent ADR-009's parallel M4-with-M2/M3 track; no dependency DAG |
| D2 | MAJOR | D/H | `checkpoint.bin` (`03 §8`) omits in-flight **eigen** state; T-resume "mid-eigen" can't be bit-identical |
| D3/F3 | MAJOR | D/F | Anti-seed-shopping is honor-system; not a mechanism |
| A5 | MAJOR | A | Single ν̄ / single-k point kinetics for a **mixed Pu/U-238** system; prompt-vs-total ν̄ (β) not distinguished |
| G4 | MAJOR | G | Rendering `§4` promises 3D "asymmetry field" structure the physics (1D + scalar ε) never computes → violates `§5` "trace to a field channel" |
| I1/I2/I3 | MAJOR | I | R-1 likelihood understated; R-4 mitigation circular; several high-impact risks missing |
| A7 | MINOR | A/B | E_f inconsistency: C-041 (180 MeV) vs C-042/043 kt/kg (~190/181 MeV) → two yield routes differ ~5% |
| A8 | MINOR | A/B | "refresh on every hydro update" + `hydro_dt = g` ⇒ eigen every generation, contradicts `tally` `eigen_calls=57` |
| B9 | MINOR | B | Ga fraction `0.0005` (`03 §3`) vs `3.35% molar` (10-ui/C-102/research) — ~67× off |
| B10/B11 | MINOR | B/F | burn-up band 15–17% vs gate 12–20%; yield bands `[15,25]` gate vs `[18.6,24.8]` sweep/overview |
| B12 | MINOR | B/C | CMake preset names differ across `07`/`12 §1`/`12 §2` |
| B13 | MINOR | B | fps reference GPU: `09 §6` 3070-class vs G4 4070 Ti SUPER |
| B14/F7 | MINOR | B/F | "identical tallies"/"population series identical" vs statistical G0c |
| B15 | MINOR | B | `tally` `generations=566` > `t_max/g = 500` |
| C5/C6/C7 | MINOR | C | vcpkg baseline SHA missing; `canonical_hash` normalization undefined; ε-nudge "always inward" can trap outbound particles |
| D5/D6/D7 | MINOR | D | VERIFY line is a no-op echo; lease-requeue + timestamped run_id can double-count; non-gate M0 tasks have no independent verification |
| E5/E6/E7/E8 | MINOR | E | Godiva <30 s vs full-assembly <1 s inverted difficulty; float XS vs "double everywhere"; `extern "C"` passes C++ types; CUDA 13.1 ubuntu22.04 image UNCERTAIN |
| G3 | MINOR | G | compression `ui_range [1,3]` exceeds public 2–2.5× (C-060) |
| F4/F6 | MINOR | F | G2 burst-duration ≤2 µs non-binding; canonical physics params tunable to pass ("default-shopping") |
| B16/B17 | NIT | B | `fields.f32` holds f16; bootstrap prompt & machine specs duplicated (drift risk) |
| A6/F8 | (good) | A/F | E3a fission bookkeeping is **correct**; benchmark values & geometry match public sources → PRESERVE |

---

## A. Physics & numerical correctness

### A6 — (VERIFIED CORRECT) E3a per-generation bookkeeping — PRESERVE
The prompt asks specifically whether `N_{n+1}=k·N_n`, `F_n=k·N_n/ν̄` is right. **It is.** The probability that a generation-*n* neutron induces fission before dying is `P_f = k/ν̄` (since `k = ν̄·P_f`), so fissions `F_n = N_n·k/ν̄`; and `N_{n+1}=ν̄·F_n=k·N_n`. Consequently `F_cum=ΣF_n`, `yield = F_cum/Φ_kt`, and burn-up `= F_cum^Pu·m_atom/M_pit` are mutually consistent identities. No error here. Keep it. (Caveats about a *single* ν̄/k for a mixed core are A5, not a bookkeeping error.)

### A1 — Guderley exponent value mislabelled for its γ  — MAJOR
**File:** `01-physics.md §5` (Tier-2), `appendix/constants.md C-070`.
**Wrong:** `α_G = 0.688377 (spherical, γ=1.4)`. The value **0.688377** is the canonical spherical converging-shock similarity exponent for **γ = 5/3** (Lazarus & Richtmyer 1981; this six-digit value is the well-known monatomic-gas result). For **γ = 1.4** the spherical exponent is **≈ 0.717174**, not 0.688377.
**Why it matters:** The exponent sets the entire converging-shock timing law `R_s(t)=A(t_c−t)^{α_G}` that drives the Tier-2 compression schedule (G1b/G2 physics). A mislabelled γ will send an implementer to the wrong constant if they ever recompute it, and it is internally contradicted by `05 §4` (see B3), which uses γ=5/3 (correct pairing) for both the exponent *and* `P_int`.
**Fix:** Since `05 §4` already commits to γ=5/3 for `P_int`, **relabel** C-070 and `01 §5` to `(spherical, γ=5/3)` and keep `0.688377`. If γ=1.4 is genuinely intended, change the value to `0.717174`. Add a one-line note that this is a phenomenological timing model (real converging front runs through metal/HE products, not an ideal gas). Cite: Lazarus, *SIAM J. Numer. Anal.* 18 (1981).
**Triage:** Independent; a constant relabel + one `01/05` reconciliation line.

### A2 — Generation time `g` treated as a fixed constant under compression — MAJOR
**File:** `01-physics.md §1/§4` (`g ~10 ns`, `C-030`), `scenarios §4 [kinetics] generation_time_s=1e-8`, `10-ui` ("Generation time 10 ns, range 1–100").
**Wrong:** `g` is a fixed constant used in `α=(k−1)/g` and in E3a's per-generation clock, and is even a user slider. Physically the prompt generation time scales with the neutron mean free path, i.e. **∝ 1/ρ** (roughly). Under the canonical **2.2× compression** the mfp shrinks ~2.2× and `g` drops correspondingly (from ~10 ns toward ~4–5 ns).
**Why it matters:** The Rossi-α, the number of generations in the burst, the disassembly-quench timing, and therefore the **yield** all depend on `g(t)`. Holding `g` at the uncompressed 10 ns during the supercritical phase systematically mis-times the burst. (The research doc's constant-g is a *hand-calculation rule of thumb*; a transport-resolving code that already tracks density must not inherit it.)
**Fix:** Derive `g` per refreshed geometry from the transport solve (e.g. `g = 1/(v·ν̄·Σ_f·...)` or from the eigen solver's estimate of prompt lifetime), or at minimum scale `C-030` by `(ρ/ρ₀)^{-1}` each hydro update. Keep the UI value as an *initial/uncompressed* value labelled as such, not the value used during the burst.
**Triage:** Independent; touches `01 §4`, kinetics API (`05 §3`), and C-030 semantics.

### A3 — Quasi-static validity overstated during disassembly — MAJOR
**File:** `02 D3` / `DECISIONS.md ADR-003` ("valid while geometry evolves slower than a shake"); `01 §4`.
**Wrong:** During disassembly the shell moves at ~10⁸ cm/s; the compressed core radius (~3.5 cm) changes by a non-negligible fraction **per shake** — the geometry-change time, the generation time, and the flux-equilibration time all become **comparable** exactly when the reactivity is collapsing. The stated validity condition ("geometry evolves slower than a shake") is marginal-to-violated in the regime that determines yield. (Disassembly-velocity figure is order-of-magnitude, **UNCERTAIN** to ~×3.)
**Why it matters:** The spec asserts the approximation is "honest" without bounding its error, and G2 (yield/burn-up) rides entirely on it. The quench point (where `k<0.998`) is precisely where quasi-static is weakest.
**Fix:** Reframe ADR-003 as a *phenomenological* approximation, not a validated one; add a required cross-check: run **one** canonical case in TD-mode (or a finer eigen-refresh cadence) and report the yield delta in `SESSIONS.md`; if it exceeds, say, the G2 band width, tighten the refresh cadence during disassembly. Add this as a risk (see I3).
**Triage:** Independent; edits ADR-003 wording + adds one validation cross-check.

### A4 — Tier-1 compression radius formula is wrong — MAJOR
**File:** `01-physics.md §5` Tier-1: `r(t) = r_0 · (1 + (ρ/ρ₀)^{1/3} − 1)^{-1} · s(t)`, `s` a smoothstep 0→1.
**Wrong:** Two defects. (1) `1 + x − 1 ≡ x`, so the bracket is just `(ρ/ρ₀)^{1/3}` — the "1+…−1" is dead algebra masking intent. (2) With the `s(t)` multiplier, at `t=t₀` (`s=0`) the radius is **0**, and it *grows* to the compressed radius at `s=1`. That is backwards and unphysical: the core should start at `r_0` and **shrink** to `r_0·(ρ/ρ₀)^{-1/3}`.
**Why it matters:** Tier-1 is the geometry model for **G1a, G1b, and UI exploration** (`01 §5`). A core that starts at r=0 gives nonsense k-vs-compression curves for the M2-T3 scan and the G1a/G1b gates.
**Fix:** Replace with an interpolation from uncompressed to compressed radius, e.g.
`r(t) = r_0 · [ 1 − (1 − (ρ/ρ₀)^{-1/3}) · s(t) ]`, `s: 0→1` smoothstep over `[t₀, t₀+t_c]`, densities mass-conserving. Verify: `s=0 ⇒ r=r_0`; `s=1 ⇒ r=r_0(ρ/ρ₀)^{-1/3}`.
**Triage:** Independent; a formula replacement + the M2-T2 unit test ("2× compression conserves mass per layer") will catch it.

### A5 — Single ν̄ / single-k point kinetics for a mixed Pu/U-238 core — MAJOR
**File:** `01 §4` (E3a uses one `ν̄`, `C-020=2.9`), `05 §3` (per-isotope split is *post-hoc* "∝ layer fission fractions").
**Wrong:** (a) The burst neutron economy uses one flux-averaged `ν̄` and one `k`, but the assembly mixes δ-Pu (ν̄≈2.9–3.0 fast, prompt) with a U-238 tamper (fast-fission ν̄≈2.5, threshold ~1 MeV) contributing ~20% of yield. A single ν̄ misattributes the economy; the per-isotope tally is reconstructed afterward from MC fractions and is not guaranteed consistent with the E3a total. (b) **Prompt vs total ν̄:** the ~500 ns burst runs on prompt neutrons, so E3a's `k` must be the **prompt** multiplication `k_p = k_eff·(1−β)` with **prompt** ν̄; but the MC eigen k_eff and `C-020` conflate prompt/total. β_eff(Pu-239)≈0.0020, so the difference is small mid-burst but matters at the initiation and quench **crossings** (where `k_p−1` is near zero and the ~0.2% shift is a large *fractional* error).
**Why it matters:** Affects yield split (Pu burn-up vs tamper share — a quantity `00 §3.5` insists be kept honest) and quench timing.
**Fix:** State explicitly that E3a's `k` is `k_prompt` and its `ν̄` is prompt/flux-averaged over the *mixture*; document the mapping from the MC k_eff (total ν̄) to k_prompt. Consider a two-term economy (Pu vs U-238) or at least require the E3a total `F_cum` to be reconciled against the MC isotope-resolved fission tally (add to the M3-T3 DoD).
**Triage:** Independent; wording + one reconciliation check.

### A7 — Per-fission energy inconsistent between C-041 and C-042/043 — MINOR
**File:** `appendix C-040/041/042/043`, `10-ui` ("180–200 MeV").
**Wrong:** `Φ_kt = 1.452e23/kt` is exactly 180 MeV/fission (4.184e12 J/kt ÷ 180 MeV = 1.451e23 ✓). But `C-042 = 18.29 kt/kg` for Pu-239 implies **~190 MeV** (18.29 kt/kg ⇒ E_f≈189.6 MeV), and `C-043 = 17.74 kt/kg` (U-235) implies ~181 MeV. At 180 MeV the Pu figure should be **17.36 kt/kg**, not 18.29 — a **~5% gap**. `10-ui` compounds it by showing "180–200 MeV, fixed".
**Why it matters:** Two yield routes (fission-count via C-041 vs mass-fissioned via C-042) disagree ~5%; if any code path or the E7 overlay uses kt/kg it will diverge from the tally.
**Fix:** Pin one E_f for the yield conversion (180 MeV prompt is the correct choice for explosion yield). Mark C-042/043 as *reference/overlay only, per-isotope total-energy accounting* and note they are **not** used for `yield_kt`. Fix `10-ui` to "180 MeV (prompt; yield basis)".
**Triage:** Independent constant annotation.

### A8 — Eigen-refresh trigger over-fires given `hydro_dt = g` — MINOR (borderline MAJOR)
**File:** `01 §4` / `05 §3` ("refresh every M gens **or immediately upon any hydro geometry update**"), `03 §4 [hydro] dt_s=1e-8 = g`.
**Wrong:** With hydro ticking every generation and "refresh on any hydro update," the eigen refreshes **every generation** (~566×), contradicting `tally.json timing.eigen_calls = 57` (≈ every 10 gens).
**Why it matters:** Either the tally example is wrong or the refresh rule is; a cold agent implementing `02 §3`'s `if … or hydro_updated:` will get ~10× the intended eigen cost (see E3/E4).
**Fix:** Decouple: refresh on a **significant** geometry change (e.g. Δ(radius) or Δ(ρr) above a threshold) or every `M` gens, not on every hydro tick; or raise `hydro_dt`. Make `02 §3`'s condition explicit.
**Triage:** Independent; edit the trigger condition + reconcile the tally example.

---

## B. Internal consistency

### B1 — Scenario TOML example is not valid TOML — BLOCKER
**File:** `03-data-contracts.md §4`.
**Wrong:** The `[[layers]]` array-of-tables header is immediately followed by **inline-table braces** on the next lines:
```toml
[[layers]]
{ id = "initiator", r_outer_cm = 1.00, material = "be_po_urchin", ... }
```
That is a parse error (you cannot mix an AoT header with brace-inline tables like this). `03 §3` explicitly says **"Examples are canonical — tests parse them verbatim."**
**Why it matters:** M0-T5 DoD is "canonical example files parse"; it is impossible as written, and every session that copies this scenario shape inherits a non-parsing file.
**Fix:** Use one of the two legal forms. Either repeated blocks:
```toml
[[layers]]
id = "initiator"
r_outer_cm = 1.00
material = "be_po_urchin"
status = "DECLASSIFIED"
[[layers]]
id = "pit"
...
```
or an inline array: `layers = [ { id="initiator", ... }, { id="pit", ... } ]`. Pick one and use it consistently (the loader/`Scenario` struct in `04 §6` must match).
**Triage:** Independent; a syntax fix in one example.

### B2 — `tally.json` example is internally inconsistent by ~10× + `fission_mesh` malformed — BLOCKER
**File:** `03-data-contracts.md §5`.
**Wrong:** Given the example's own `yield_kt=20.4` and `Φ_kt=1.452e23`:
- `fissions_total` should be `20.4 × 1.452e23 = 2.96e24`, but the example says **`2.96e23`** (10× low).
- For `burnup.pu_fraction=0.151` with the stated ~6.15 kg pit (~1.56e25 Pu atoms), Pu fissions ≈ **2.37e24**; and `tamper_yield_fraction=0.20` ⇒ tamper fissions ≈ 0.2×2.96e24 = **5.9e23**, but the example says **`5.9e22`** (10× low). With the corrected 2.96e24 / 5.9e23, everything closes: Pu fissions 2.37e24 / 1.56e25 = **15.2% ✓**.
- `fission_mesh`: `shells_cm=[0.0,4.585,11.43]` defines **2** bins but `fissions` lists **3** values `[2.1e23,5.9e22,2.7e23]`; and their sum (5.39e23) ≠ `fissions_total` under *either* scale.
**Why it matters:** `03 §3` declares examples canonical/verbatim-tested. A golden test (`11 §1`) generated from this encodes a device whose burn-up is ~10× inconsistent with its fission count — future sessions will "match the golden" and propagate the error.
**Fix:** Restate a self-consistent example, e.g. `fissions_total = 2.96e24`, `u238_tamper_fissions = 5.9e23`, `fission_mesh.shells_cm = [0.0, 4.585, 11.43, 23.495]` (3 bins) with 3 fission values that **sum to `fissions_total`**, and state the invariant explicitly: `sum(fission_mesh.fissions) == fissions_total` and `len(fissions) == len(shells_cm) − 1`. Add these as loader validations.
**Triage:** Independent; correct the example + add two invariants.

### B3 — Guderley γ contradiction across files — MAJOR (pairs with A1)
**File:** `01 §5` (γ=1.4) vs `05 §4` (γ=5/3) vs `appendix C-070` (γ=1.4) — all quoting `0.688377`.
**Fix:** As A1: standardize on **γ=5/3** in all three (the value's true γ, and the one `05 §4` uses for `P_int`).
**Triage:** Independent.

### B4 — CUDA version mismatch (ADR-009 propagated incompletely) — MAJOR
**File:** `02-architecture.md §4` ("CUDA 12.x") vs `12-deployment.md §1` / machine ("**13.1**"). `CHANGELOG` claims `02` was updated by ADR-009; it was not.
**Why it matters:** M0-T2 sets toolchain pins; two "authoritative" CUDA versions is exactly the drift the process is meant to prevent — and it's already present in v0.1.
**Fix:** Change `02 §4` to "CUDA 13.x (pinned 13.1, `12-deployment §1`)". More broadly: the ADR-009 amendment left stale artifacts (see B5) — see D4 for the process fix.
**Triage:** Independent one-word edit.

### B5 — OptiX env-var naming is inconsistent and version-wrong — MAJOR
**File:** `12 §1` (`OptiX_ROOT`/`OptiX_INSTALL_DIR`), `12 §2` & `12 §5` (`OPTIX80_SDK`), `02 §4` ("SDK env var"), `05 §6`/`13 R-6` (`NUKESIM_WITH_OPTIX`). Installed SDK is **9.1.0**, but the var says **80** (OptiX 8.0).
**Why it matters:** M0-T2/M6 CMake will look for the wrong variable; `.env.example` (M0/M5) documents a var (`OPTIX80_SDK`) that neither matches the CMake locator nor the installed version.
**Fix:** Choose one variable name that is version-agnostic, e.g. `OPTIX_SDK_ROOT` (or the CMake-idiomatic `OptiX_ROOT`), use it in `12 §1/§2/§5`, `02 §4`, `.env.example`; keep the build feature flag `NUKESIM_WITH_OPTIX` distinct and consistent. Remove the "80".
**Triage:** Independent rename.

### B6 — Override / parameter-path syntax is specified three different ways — MAJOR
**File:** `04 §6` (`layers[pit].material`, dotted+bracket), `03 §7` sweep (`tamper_override.material`), `06 §1` (`--override key=value`), `10-ui` (`materials.pu_ga_delta.Pu240`, `xs.Pu239.nu`).
**Why it matters:** `Scenario::apply_overrides` (used by sweeps and `--override`) hard-errors on unknown keys (`04 §6`); the sweep example's `tamper_override.material` would then be a hard error under the `layers[pit]` convention. Sessions implementing sweeps/UI can't agree on the addressing model.
**Fix:** Define **one** canonical path grammar (recommend `layers[<id>].<field>`, `materials.<name>.<field>`, `xs.<isotope>.<field>`, plus scalar `section.key`) in `03` or `04`, and make `03 §7`, `06 §1`, `10-ui` all use it. Delete the ad-hoc `tamper_override.*`.
**Triage:** Independent; a grammar definition + three call-site edits.

### B7 — `10-ui` parameter paths are not addressable in the `03` scenario schema — MAJOR
**File:** `10-ui.md` table vs `03 §4` scenario schema.
**Wrong:** The UI is "generated from scenario schema + `ui_range` annotations," but half its rows (`initiator.sim_count`, `materials.pu_ga_delta.Pu240`, `materials.pu_ga_delta.Ga`, `xs.Pu239.nu`, `xs.Pu239.sigma_f`, `layers[pit].r_outer_cm`, `layers[tamper].material`) reference material/XS/derived state that the scenario TOML does not contain (materials and XS live in separate files; `initiator.sim_count` isn't in the schema at all).
**Why it matters:** The "schema-driven UI" cannot be generated from the current schema; the UI panel (M7-T3) has no source of truth.
**Fix:** Decide the UI's binding model: either (a) promote the needed knobs into the scenario schema as overrides (`materials_override`, `xs_override`, `initiator.sim_count`), with `ui_range`, or (b) make the UI bind to the resolved *effective* parameter set (scenario ∪ referenced material/xs), and define that resolved model in `03`. Add `initiator.sim_count` to `03 §4`.
**Triage:** Independent; extends the `03` schema and/or defines the resolved model.

### B8 — `initiator.strength` default exceeds its own `ui_range` — MAJOR
**File:** `03 §4 [initiator]` (`strength_n_per_s = 1.0e8`, `ui_range = [0.0, 1.0e7]`), `10-ui` ("0–1e7…1e8").
**Wrong:** Default `1e8` > `ui_range` max `1e7`. `03 §4` validation says "all `ui_range` bounds respected for gate runs (gates use schema defaults)" — so gate runs using the default `1e8` would **fail their own ui_range validation**. `10-ui`'s "0–1e7…1e8" is malformed. (The contradiction is inherited from the research-doc UI table, which lists canonical "~1 n/5–10 ns" ≈ 1e8 n/s but range "0–10⁷" — the spec copied both without reconciling.)
**Fix:** Set `ui_range = [0.0, 1.0e9]` (or `[0,1e8]`) so the default is inside it, or lower the default. Physically, `1e8 n/s × pulse_width 1e-8 s = 1 neutron` — note the initiator integrates to ~1 neutron in the canonical scenario vs the public "~10–100 neutrons"; widen `pulse_width_s` or raise strength if predetonation statistics matter (see also A5). Fix `10-ui` range text.
**Triage:** Independent; one bound edit (+ optional pulse-width note).

### B9 — Gallium fraction contradicts itself and the sources — MINOR (borderline MAJOR)
**File:** `03 §3` material `pu_ga_delta` (`Ga69_71: 0.0005`) vs `10-ui` ("3.35% molar") vs `appendix C-102` ("3.35% molar") vs research doc ("3.35% gallium (molar; ~1% by weight)").
**Wrong:** `0.0005` (0.05 at%) is ~67× below the correct ~3.35 at% (= ~1 wt%, which is what stabilizes δ-phase). Gallium is neutronically near-inert, so k is barely affected, but it's a "cited" material that's simply wrong and internally contradictory.
**Fix:** Set `Ga69_71 ≈ 0.0335` and **rebalance** so atom fractions sum to 1.0 (e.g. `Pu239 0.9565, Pu240 0.010, Ga 0.0335`) — the loader's "sum to 1.0 ± 1e-6" rule (`03 §3`) will otherwise reject a naive one-field edit.
**Triage:** Independent; edit + re-sum.

### B10 / B11 — Band mismatches for burn-up and yield — MINOR
**File:** burn-up: `00 §1.3`/`C-090` "15–17%" vs `07`/`08` G2 "12–20%". yield: `08` G2 `[15,25]` vs `03 §7` sweep `target_yield_kt=[18.6,24.8]` vs `00 §3.6` "[18.6,24.8]".
**Why it matters:** Three different yield bands for one quantity; the G2 floor (15 kt) is **below the lowest public estimate** (18.6 kt), so an under-predicting model passes.
**Fix:** State the relationship explicitly: cited *public* values (15–17% burn-up; 18.6–24.8 kt) vs *gate tolerance* bands (wider to absorb model error), with a one-line rationale for the widening. Consider raising the G2 yield floor to ≥17–18 kt. Align the sweep `calibrate` target with the gate band or explain why they differ.
**Triage:** Independent; annotate bands.

### B12 / B13 / B14 / B15 / B16 / B17 — smaller consistency defects
- **B12 (MINOR):** CMake preset names: `07 M0-T2` ("win-msvc, linux-gcc, cuda") vs `12 §1` ("win-x64, linux-x64, linux-cuda") vs `12 §2` ("win-x64", "win-x64-rel"). Pin one scheme; used verbatim in M0-T2.
- **B13 (MINOR):** fps reference GPU: `09 §6` "RTX-3070-class" vs G4 "RTX 4070 Ti SUPER (dev)". Reconcile the device the ≥30 fps target is measured on.
- **B14 (MINOR):** determinism wording: `01 §9` & `11 §2` say "**identical** tallies … any thread count"; `02 D1`/G0c say "**statistically** identical"; G0c step 2 says population series "**identical**". These can't all hold across double-CPU vs float-GPU or across nondeterministic reductions (see E1). Replace "identical" with "identical within the deterministic-reduction guarantee (same backend/config)" and "statistically identical (cross-backend)".
- **B15 (MINOR):** `tally.json timing.generations=566` > `t_max_s/g = 5e-6/1e-8 = 500`. Clarify what "generations" counts (burst only? includes eigen sub-iterations?) and make it consistent with `t_max`.
- **B16 (NIT):** `fields.f32` (`03 §9`) stores **half-float** (f16) — the `.f32` extension is misleading. Rename to `.f16`/`.fld`.
- **B17 (NIT):** bootstrap prompt is duplicated (`README §9` and `SESSIONS §17`) and machine specs are duplicated (`12 §1a` and `PROGRESS.md`). Make one canonical and have the other point to it, or they will drift (they already did — see B4/B5).

---

## C. Implementability by a cold agent (M0-T1 → M1-T5)

**Readiness ratings** (mentally executing each task with only the spec):

| Task | Rating | Blocking gaps |
|---|---|---|
| M0-T1 | **NEEDS-AMENDMENT** | C1 duplicate spec tree; where does `nuclear-sim/` sit vs the existing `spec/`? which PROGRESS.md is live afterward? |
| M0-T2 | **NEEDS-AMENDMENT** | B12 preset names; B4 CUDA version; C5 vcpkg baseline SHA; B5 OptiX var |
| M0-T3 | **NEEDS-AMENDMENT** | C2 appendix values are ranges, schema wants scalars → gen_constants underspecified |
| M0-T4 | **NEEDS-AMENDMENT** | C3 Philox counter/key layout; KAT inputs under-specified |
| M0-T5 | **NEEDS-AMENDMENT** | B1 invalid TOML; B8 default>ui_range; B2 tally example; B9 sum-to-1 |
| M1-T1 | **READY** | (C7 ε-nudge nit; handle innermost layer inner-radius=0) |
| M1-T2 | **READY** | (fixed-source multiplying-media handling is loose, but the DoD test — pure-absorber e^{−ΣR} — is clear) |
| M1-T3 | **READY-with-caveats** | I=10 inactive may be too few for source convergence; σ≤25 pcm is a *gate* issue (E4), not this DoD |
| M1-T4 | **NEEDS-AMENDMENT** | **C4** (blocker): data access + few-group generation method |
| M1-T5 | **READY** (after M1-T4) | depends on gates.toml + benchmark data being real |

### C1 — M0-T1 creates a duplicate, ambiguous spec tree — MAJOR
**File:** `07 M0-T1`, `02 §2` (`nuclear-sim/ … spec/ # copied by M0-T1`), `README §1`.
**Wrong:** `02 §2` puts `spec/` **inside** `nuclear-sim/`, and M0-T1 "copies `spec/`" there. But the live spec already sits at `C:\NUCLEAR\spec`. After M0-T1 there are two `spec/` trees (`C:\NUCLEAR\spec` and `C:\NUCLEAR\nuclear-sim\spec`), each with its own `PROGRESS.md`/`SESSIONS.md`. The bootstrap prompt points at "`C:\NUCLEAR` … `spec/README.md`" — ambiguous once the nested copy exists.
**Why it matters:** Sessions will edit different PROGRESS.md files → state divergence, exactly the failure the living-state protocol exists to prevent.
**Fix:** Either (a) `git init` at `C:\NUCLEAR` itself and keep the *existing* `spec/` as the single tree (don't nest under `nuclear-sim/`); or (b) declare the in-repo `nuclear-sim/spec` canonical, replace the outer one with a stub README pointer, and update the bootstrap path. State it in `README` and `07 M0-T1`.
**Triage:** Independent; a layout decision + path fix.

### C2 — Appendix constants are ranges but the schema wants scalars — MAJOR
**File:** `07 M0-T3`, `04 §1`, `03 §1`, `appendix §1`.
**Wrong:** Many appendix entries are ranges/approximate: `C-010 1.7–1.8`, `C-040 180 (total ~200)`, `C-053 3–4`, `C-060 ~2–2.5×`, `C-090 15–17`, etc. `constants.toml` (`03 §1`) has a single scalar `value`, and `gen_constants` must emit `inline constexpr double`. Nothing says how a range becomes a number (midpoint? min? nominal?).
**Why it matters:** M0-T3 can't be executed deterministically; two agents will pick different values → different physics.
**Fix:** Extend the schema to `value` + optional `min`/`max` (or `nominal`+`range`), require `value` to be the nominal used by code, and record the range for sensitivity/UI. Specify the appendix→toml rule ("pick the cited nominal; if only a range is public, use the midpoint and record it"). Update `04 §1` accessors to expose nominal.
**Triage:** Independent; schema + rule.

### C3 — Philox counter/key partitioning unspecified → substream collisions — MAJOR
**File:** `04 §2` (`Philox::operator()(uint64_t counter)`, "128-bit counter, 64-bit key", "derive streams from global particle index").
**Wrong:** Philox4x32 has a **128-bit counter** and **64-bit key**, but the API exposes only `uint64 seed`, `uint64 stream`, `uint64 counter`. How these map onto the 128 counter bits + 64 key bits — and how the per-particle index, per-event stream id, and per-call block index are packed without overlap — is not defined. Overlapping substreams ⇒ correlated neutrons ⇒ biased k.
**Why it matters:** This is the RNG that guarantees reproducibility (D9) and correctness; an ad-hoc packing risks silent correlation and defeats the KAT's purpose. Also interacts with E1/E2 (bank-position-derived streams).
**Fix:** Specify an explicit layout, e.g. key = seed(64→2×32); counter128 = [ stream_id : 32 | particle_global_id : 64 | block_ctr : 32 ]; and define `particle_global_id` as a **deterministic** function of (generation, source ordinal, deterministic child ordinal) — **not** the atomic-bank position (see E2). Cite the Random123 Philox4x32-10 KAT triplets for M0-T4's expected outputs.
**Triage:** Independent; RNG layout spec.

### C4 — M1-T4 is under-resourced (data + method) — BLOCKER
**File:** `07 M1-T4`, `08 §1`, `ADR-004`.
**Wrong:** M1-T4 must "pull exact Godiva/Jezebel ICSBEP sheet values" **and** "finalize 4-group XS with citations" to meet ±500 pcm — but (a) the **ICSBEP handbook is not openly/freely distributed** and is not in the repo, so a cold/offline agent cannot obtain the exact sheets; and (b) `ADR-004`/`D4` forbid ENDF parsing, yet a validated ±500 pcm 4-group fast set for these benchmarks does not exist as a citable published table — producing one is *multigroup generation* (spectrum weighting + transport correction), not a citation.
**Why it matters:** G0a/G0b — the project's entire validation foundation — cannot be reached honestly by the stated path. Every downstream gate inherits whatever XS results.
**Fix:** (1) Check the *public* benchmark scalars into `data/benchmarks/` now (k_eff=1.0000 ±0.0010 Godiva / ±0.0020 Jezebel; the geometry/composition are widely reproduced in secondary literature) so M1-T4 doesn't depend on restricted PDFs; (2) carve a one-time, offline, **provenance-tracked** multigroup-generation exception to D4 (NJOY/JANIS → transport-corrected 4-group), producing a checked-in dataset with a data card — *not* runtime ENDF parsing; (3) require the collapse method (weighting spectrum, transport correction) to be documented. See F1 for the gate-calibration half of this.
**Triage:** Independent; amends M1-T4 scope + D4 carve-out.

### C5 / C6 / C7 — smaller implementability gaps — MINOR
- **C5:** `02 §4`/M0-T2 say deps are "pinned in vcpkg.json" but give no **builtin-baseline** git SHA; without it "manifest mode" is not reproducible. Specify a baseline SHA.
- **C6:** `Scenario::canonical_hash()` hashes "normalized TOML" but normalization (key order, float formatting, unicode) is undefined; run.json provenance and sweep dedup (M5-T2) depend on stable hashes across platforms. Define the canonical serialization.
- **C7:** `04 §4` ε-degeneracy handling nudges "**inward** by ε". A particle crossing *outward* at the outer boundary nudged inward can be trapped (leakage suppressed). Make the nudge follow the direction of travel (sign-aware), not unconditionally inward.

---

## D. Multi-session process design

### D1 — Single `NEXT ACTION` cannot express ADR-009 parallelism — MAJOR
**File:** `README §5`/`§7`, `PROGRESS.md`, `ADR-009`/`07 M4` note.
**Wrong:** ADR-009 mandates M4-T1/T2 run **in parallel** with M2/M3, but `PROGRESS.md` carries exactly one `NEXT ACTION` and the protocol is "one task per session, in_progress<24h = owned." A single imperative can't direct two concurrent tracks, and there's no dependency DAG to tell a fresh session which parallel task is safe to claim.
**Why it matters:** Under crash/compaction, a session reads one NEXT ACTION and may serialize work the spec wanted parallel, or two sessions collide on the "obvious" next task.
**Fix:** Replace the single `NEXT ACTION` with a short **ready-queue** (tasks whose deps are green) + a machine-checkable dependency list per task in `07` (`deps: [M1-T1]`). Keep a human-readable "recommended next" but allow N. State the M2/M3 ∥ M4 fork explicitly.
**Triage:** Independent; PROGRESS format + `07` deps.

### D2 — Checkpoint omits in-flight eigen state → mid-eigen resume can't be bit-identical — MAJOR
**File:** `03 §8`, `11 §1` (T-resume "mid-eigen"), `D9`.
**Wrong:** `checkpoint.bin` sections are SimClock / RNG / neutron bank / geometry / hydro / tallies / sweep cursor. The **eigen solver's** intermediate state — current power-iteration generation, `k_history`, `H_history` window (W=5), and the current fission-source distribution — is **not** captured. `11 §1` requires a kill "mid-eigen" to resume to a bit-identical result.
**Why it matters:** Resuming mid-eigen cannot reconstruct the power iteration; T-resume (M5-T1 DoD) will fail for that kill point.
**Fix:** Add an "eigen-in-progress" section (iteration index, k/H history, source-distribution snapshot, batch RNG cursor) versioned like the others; or forbid mid-eigen kills and checkpoint only at generation boundaries (and change `11 §1` accordingly). Reconcile with E1/E2 (bit-identicality prerequisites).
**Triage:** Independent; add a checkpoint section or restrict kill points.

### D3 / F3 — Anti-seed-shopping is a policy, not a mechanism — MAJOR
**File:** `08 §3`.
**Wrong:** "Re-running with a different seed … requires investigation … record all attempts" is honor-system; a session can run N seeds and record only the passing one. The gate seed is a free parameter.
**Why it matters:** Directly undermines gate credibility, the linchpin of "trust gates, not prose" (`README §4`).
**Fix:** Fix a **canonical seed (or seed-set)** in `gates.toml`; the gate uses only those; passing requires the fixed seed to pass (or, for stochastic gates, a fixed multi-seed set with a median/worst-case rule as G3 already does). Have `nukebench gate` embed the seed(s) it used and refuse a caller-supplied seed for gate mode.
**Triage:** Independent; add fixed seeds to gates.toml + CLI enforcement.

### D4 / D5 / D6 / D7 — process MINORs
- **D4 (MINOR):** ADR-009 was applied incompletely (B4 CUDA, B5 OptiX) — the amendment protocol has no verification step. Add to `README §6`: after an amendment, `grep` the repo for the old value and confirm zero stale hits; log the grep in the CHANGELOG line.
- **D5 (MINOR):** `PROGRESS.md` `VERIFY:` is `echo "…"` (always exits 0). "Verify-first" is vacuous at M0. Fine for the empty repo, but state that the *first real* VERIFY appears after M0-T2 builds.
- **D6 (MINOR):** `06 §2` lease-requeue ("stale > 2×median ⇒ requeue") + timestamped `run_id` (`03 §5`) + "dedup by scenario hash" (M5-T2) can **double-count** a long-tail unit (two workers, same scenario+seed, different timestamps → two run_ids → both stored). Define an **idempotent unit id** (hash of scenario+overrides+seed) as the dedup key and the artifact directory name.
- **D7 (process gap, MINOR→MAJOR for M0):** verify-first relies on **gates**, but M0 tasks (constants, RNG, loaders) are pre-gate; a session that lies "M0-T3 done" isn't caught by any gate. Add lightweight per-task VERIFY commands for non-gate tasks (e.g., `tools/gen_constants --check` exits nonzero on missing cite/status; a loader smoke-test target) so verify-first has teeth before G0.

---

## E. GPU / CUDA realism

### E1 — Bit-identical determinism/resume not achievable as specified — MAJOR (as-written unachievable)
**File:** `01 §9`, `03 §8`/`D9`, `11 §2`, `05 §6` ("Tallies in double via block-shared reductions").
**Wrong:** "Same seed, any thread count ⇒ **identical** tallies" and "kill→resume→**bit-identical**" require deterministic floating-point **reduction**. Block-shared reductions summed across blocks via atomics (or any scheduling-dependent order) are **not** associative in float/double → run-to-run bit differences even at fixed config. Philox gives reproducible *per-particle streams*, but not reproducible *sums*.
**Why it matters:** T-resume (M5-T1) and the determinism tests (`11 §2`) are stated as **hard** requirements/gates; as written they will fail on GPU.
**Fix:** Mandate a **deterministic reduction** for anything that must be bit-reproducible: fixed-order tree reduction, or integer/fixed-point tally accumulation (scale weights to integers), or per-particle deterministic partial sums combined in a fixed order. Budget its perf cost in G4. Alternatively downgrade the specific claims to "statistical" and drop "bit-identical" from T-resume. Decide this **before M4-T1** (it's architectural).
**Triage:** Independent; a determinism-strategy decision + gate rewording.

### E2 — Atomic-cursor fission bank contradicts "no shared cursor" and threatens determinism — MAJOR
**File:** `05 §6` ("fission progeny appended to bank buffer with **atomic cursor**") vs `04 §2` ("GPU kernels derive streams from global particle index (**no shared cursor state**)").
**Wrong:** If progeny land at race-ordered bank positions and the RNG stream is derived from bank position ("global particle index"), the **same** neutron gets a **different** stream run-to-run → non-reproducible tallies (and E1 can't be salvaged).
**Fix:** Derive child streams from a **deterministic** identity — parent stream/id + generation + a deterministic child ordinal (e.g. parent_id·ν_max + child_index) — independent of where the atomic cursor happens to place the progeny. The atomic cursor may still allocate storage; it just must not seed the RNG. Make `04 §2` and `05 §6` consistent on this.
**Triage:** Independent; child-stream derivation spec.

### E3 — 30 fps studio vs blocking eigen refresh — MAJOR
**File:** `08 G4` (≥30 fps studio **and** eigen refresh <1 s), `tally` example (`eigen_calls=57`, `wall_s=41.2` ⇒ ~0.72 s/eigen).
**Wrong:** 30 fps = 33 ms/frame; a ~0.7 s eigen refresh stalls ~20 frames. Sustaining 30 fps *through* a burst that refreshes eigen requires the eigen to run **async/incremental** (background CUDA stream, or amortized power iteration across frames), which the spec never describes.
**Why it matters:** G4 as written is self-contradictory for the interactive path.
**Fix:** Specify async eigen (double-buffered k, background stream) or incremental power iteration (few iterations/frame, k interpolated between refreshes), and state that studio may show a "refreshing" state. Or scope G4's 30 fps to phases without a live eigen refresh and state so.
**Triage:** Independent; add an interactivity design note + adjust G4.

### E4 — σ ≤ 25 pcm not reachable at the specified batch — MAJOR
**File:** `01 §3`/`03 §4 [eigen]`/`C-900` (batch `1e5`, inactive 10, active 50), `08 G0a/G0b` (σ ≤ 25 pcm), `G4`/`08 §2` ("Godiva gate 1e5×60 gens < 30 s").
**Wrong (UNCERTAIN on exact counts):** 1e5 × 50 active ≈ **5e6 active histories**. Empirically, bare fast-metal eigenvalues reach σ_k ≈ 30–70 pcm at ~5e6 active histories; **≤ 25 pcm typically needs ~1e7–1e8 active** (i.e. ~1e6/gen × ~50, or 1e5/gen × ~500). So the pinned batch likely **misses** the σ target. (This is a rule-of-thumb; mark UNCERTAIN — but the 1e5 floor is clearly optimistic.)
**Why it matters:** G0a/G0b require σ ≤ 25 pcm; a cold agent taking "batch ≥ 1e5" at face value will report σ ~50–100 pcm and the gate fails for a *statistical*, not physics, reason. It also inverts E5.
**Fix:** Separate **gate-precision** eigen (size for σ ≤ 25 pcm; ~1e6–1e7/gen — verify empirically) from **burst-refresh** eigen (size for speed, accept larger σ mid-burst). State both in `03 §4` and `C-900`. Re-check the <1 s refresh and <30 s Godiva targets against the gate-precision count (the VRAM budget's "SoA @1e7" already hints the real batch is ~1e7).
**Triage:** Independent; split the batch parameters + re-derive perf targets.

### E5 / E6 / E7 / E8 — GPU MINORs
- **E5 (MINOR):** `08 §2` targets Godiva (single sphere, simpler) at **<30 s** but the harder full-assembly eigen at **<1 s** — the easy problem's budget is 30× looser. This is only coherent if the Godiva *gate* uses far more histories (for 25 pcm) than the burst refresh; state the history counts each target assumes (ties to E4).
- **E6 (MINOR):** `04 §3 GroupData { float … }` and `Stream::uniform()→float` put **float** in the CPU `ref` path, contradicting `01 §9` "ref: IEEE double everywhere." Use double for `ref` XS/uniforms (the oracle must not carry avoidable float error).
- **E7 (MINOR):** `06 §2` sampler "plugin ABI" is `extern "C"` but passes C++ types (`toml::table&`, `optional<ParamSet>`, `Tally`) — not ABI-stable. Either drop the ABI pretense (in-tree C++ plugins) or define a real C ABI (opaque handles + C structs + version field).
- **E8 (UNCERTAIN):** `12 §3` base image `nvidia/cuda:13.1-devel-ubuntu22.04` — CUDA 13 may not publish 22.04 images (13.x leaned to 24.04). Verify the tag exists before M5-T5; adjust base distro if not.

---

## F. Gates & validation

### F1 — G0a/G0b at ±500 pcm with 4-group isotropic-in-lab scatter: unachievable-honestly or circular — BLOCKER
**File:** `08 §2` G0a/G0b, `01 §2 E1c` ("isotropic-in-lab … known limitation R-4"), `ADR-004`, `R-1`/`R-4`.
**Wrong:** Godiva/Jezebel are bare **leakage-dominated fast** systems where the scattering **anisotropy** (μ̄ ≠ 0; fast elastic scattering off heavy nuclei is forward-peaked) directly sets the transport cross section and thus leakage and k. **Isotropic-in-lab** scattering forces μ̄ = 0 with un-corrected Σ_s, biasing leakage; combined with a broad **4-group** collapse, honest errors on k for these systems are commonly **~1000–3000 pcm**, i.e. **larger than the ±500 pcm gate**. The only ways to pass are (a) **transport-corrected** group constants (reduce Σ_s by μ̄Σ_s — an "extended transport approximation") — which the spec never requires; or (b) **tuning** the 4-group data until Godiva/Jezebel pass — which makes the "validation" **circular** (fidelity fabricated to the gate; R-4's mitigation "gates empirically bound the bias" is then self-referential).
**Why it matters:** This is the credibility foundation of the whole simulator. Either the gate is unreachable, or it's passed dishonestly and every downstream result rests on benchmark-tuned data with no independent check. (UNCERTAIN on exact pcm magnitude — but the direction and risk are solid.)
**Fix:** (1) Require **transport-corrected** group constants and document the correction in `01 §2`/`04 §3` (add a `sigma_tr`/`mu_bar` field or a corrected Σ_s). (2) Add an **independent** validation not used for tuning — e.g. match the *leakage spectrum* or a *third* benchmark (e.g. a reflected system) held out from the collapse — so passing G0a/G0b is not self-fulfilling. (3) Consider widening the gate to a defensible few-group number (e.g. ±1000 pcm) with cited rationale, or explicitly label G0a/G0b as *calibration* rather than *independent validation* and add a separate validation gate. Update R-1 (likelihood H) and R-4 (impact H, non-circular mitigation).
**Triage:** Independent; the fix is a validation-design amendment (transport correction + a held-out check).

### F2 — G1a band mis-maps a critical-mass ratio to a k_eff band, and sits too low — MAJOR
**File:** `08 G1a` (`k ∈ [0.70, 0.95]`), `C-052` ("~78% of critical"), `C-053` ("3–4 critical masses at ~2×"), research doc §3.
**Wrong:** "78% of critical" is a **critical-mass ratio** (0.78 critical masses), **not** k_eff = 0.78. Two independent confirmations: (a) it's *consistent with C-053* under density scaling — 0.78 × (2×)² ≈ 3.1 critical masses ✓ (critical mass ∝ 1/ρ²); (b) the **research doc itself** states "sub-critical (~0.9; pit ~78% critical)", i.e. k ≈ 0.9 for the 78%-mass tamped assembly. A one-group diffusion estimate for a bare 0.78-critical-mass sphere gives k ≈ 0.91, and a U-238 reflector pushes it **up** toward ~0.92–0.96 (UNCERTAIN ±0.03). So the physically expected k_eff is ≈ **0.90–0.95**, and the band's lower half `[0.70, ~0.88]` is indefensible.
**Why it matters:** As written, an **under-reactive** model (say k=0.74) passes G1a while contradicting the source's own k≈0.9. The gate fails to discriminate exactly the error it should catch.
**Fix:** Tighten to a source-consistent band, e.g. **`k ∈ [0.88, 0.97]`** (subcritical but reflector-close to 1), and add a note deriving it from "0.78 critical masses, tamped ⇒ k≈0.9 (research doc §3)". State the mass-vs-k distinction so it isn't re-conflated.
**Triage:** Independent; re-band + derivation note.

### F3 — (see D3) anti-seed-shopping — MAJOR.

### F4 / F6 — gate MINORs
- **F4 (MINOR):** G2 "burst duration (population > ½ peak) ≤ 2 µs" is **non-binding** — a real power-pulse FWHM is ~10–50 ns, so 2 µs (≈200 generations) passes trivially and catches nothing. Tighten to catch unphysically long bursts (e.g. FWHM ≤ ~200 ns), or drop it.
- **F6 (MINOR):** "gates use schema defaults," but the *defaults* (compression 2.2, `c_a`, HE energy, etc.) can themselves be chosen to land G2 in-band → "default-shopping." Require the canonical-scenario physics params to be the **cited public nominals** (with derivation recorded), not free knobs, so G2 tests the *model*, not the tuning.

### F7 — (see B14) "population series identical" vs statistical G0c — MINOR.

### F8 — (VERIFIED GOOD) benchmark scalars & geometry — PRESERVE
Godiva (HEU-MET-FAST-001: ~93.7% U-235, R≈8.74 cm, ρ≈18.7 g/cm³, m≈52 kg, k=1.0000±0.0010) and Jezebel (PU-MET-FAST-001: δ-Pu, R≈6.4 cm, ρ≈15.6, m≈17 kg, k=1.0000±0.0020) match public/ICSBEP values. The device layer radii in `scenarios §4` match `appendix §2` **exactly** (pit OD 9.17→r 4.585; tamper 22.86→11.43; boron 23.50→11.75; pusher 46.99→23.495). This cross-file geometry consistency is a genuine strength — keep it.

---

## G. Scope & boundary integrity

### G1 — The scope boundary in `00 §2/§3.2` is self-contradicted by the UI and sweep specs — MAJOR
**File:** `00 §2` ("**MUST NOT** be extended to explore novel device configurations outside the public-parameter envelope"), vs `10-ui` ("Tamper material … u_natural/**tungsten/beryllium**/none") and `03 §7` sweep (`tamper_override.material ∈ {u_natural, tungsten, beryllium}`).
**Wrong:** Swapping the tamper to tungsten/beryllium/none is precisely *novel-configuration exploration* — Trinity used natural U. The options were inherited from the research doc's "Wellerstein-style" UI table, but the research doc's **own caveats** ("stay out" of non-public design space) and `00 §2` forbid it. The spec ships both the prohibition and its violation.
**Why it matters:** Dimension-G integrity: the "hard constraint" is not actually enforced by the artifacts that would let a user cross it.
**Fix:** Decide and make consistent. Either (a) **remove** non-historical material options from `10-ui` and `03 §7` (keep u_natural, and vary only *thickness/ρ* within public spread); or (b) **explicitly** amend `00 §3` to permit *bounded pedagogical material substitution among common tamper/reflector materials as a sensitivity illustration*, with a rationale — and cap it (no arbitrary materials, no geometry outside the public envelope). Pick one; don't leave the contradiction.
**Triage:** Independent; a boundary decision reflected in `00`, `10-ui`, `03 §7`.

### G2 — Scope guard constrains the objective, not the parameter space — MAJOR
**File:** `ADR-007`/`D8`/`03 §7` (`objective.kind ∈ {sensitivity, calibrate}`), `06 §2` (MCTS/PUCT).
**Wrong:** The enforceable guard is on `objective.kind`, but the `[[space]]` axes are unconstrained — a sweep can drive pit radius, tamper material, compression, lens count into non-historical regimes while declaring `kind="calibrate"`. And **MCTS optimizing toward `target_yield_kt`** is functionally yield-optimization even when the target is a public band.
**Why it matters:** The boundary that `00 §3.4` claims to enforce "via `objective.kind`" doesn't restrain what actually gets searched.
**Fix:** (1) Constrain `[[space]]` axes to **public-plausible ranges** (validated against `ui_range`/cited bounds) for calibrate/sensitivity sweeps; reject axes/values outside the envelope unless an ADR authorizes it. (2) Define `calibrate` as "minimize distance to the public **band** (penalize *outside*)", not "maximize toward an endpoint," and restrict optimizing samplers (MCTS/Bayes) accordingly. (3) Log any axis that touches a `status` boundary (R-11 hook).
**Triage:** Independent; sweep-schema validation + objective semantics.

### G3 — UI ranges exceed cited public envelope — MINOR
`compression.ratio ui_range [1,3]` exceeds public ~2–2.5× (`C-060`). Exploring 3× drifts beyond the envelope. Either cap at the public value or clearly label >2.5× as *extrapolation beyond public data*. (Jitter to 1000 ns is intentional for the G3 degradation sweep — leave it, but label it as a stress axis.)

### G4 — Rendering fabricates 3D asymmetry structure the physics doesn't compute — MAJOR
**File:** `09 §4` ("early-time toroidal/mottled structure emerges from **deposition asymmetry fields**") vs `01 §5`/`05 §4` (asymmetry = **scalar** ε on a **spherical** model) and `09 §5` ("**every** visible structure must trace to a field channel or documented noise model").
**Wrong:** There is no 3D asymmetry field — the model is 1D-spherical with a scalar drive-perturbation ε. So the promised toroidal/asymmetric fireball structure has no physical source, violating the spec's own "trace to a field channel" directive and edging past the phenomenological ceiling `00 §3.2`.
**Why it matters:** Either the renderer invents structure (scope/honesty problem) or the physics owes a bounded 3D asymmetry field it doesn't have.
**Fix:** Either (a) restrict M7 rendering to structure that *does* trace to computed fields (spherical + documented fission-variance noise for mottling), and drop "toroidal from asymmetry fields"; or (b) if asymmetric visuals are wanted, define a **bounded, labelled, non-physical** decorative field (clearly marked SIM/visualization-only, like `render_temp_scale`) and forbid it from feeding any tally — and say so in `09` and `00 §3`.
**Triage:** Independent; rendering-scope clarification.

### G5 — (GOOD) Bethe-Feynman quarantine — PRESERVE
`01 §7`/`00 §3.3` correctly keep B-F as a display-only overlay that "MUST NOT feed any simulation state." Good boundary discipline; keep it.

---

## H. Schemas & contracts

Most schema issues are captured above (B1 TOML, B2 tally, B7 UI paths, B8 ui_range, C2 constants ranges, C6 canonical hash, D2 checkpoint). Additional:

- **H1 (MINOR):** `03 §5` requested `output.tallies` includes `"fissions_by_isotope"`, but the `tally.json` schema has no general per-isotope map (only `burnup.pu_fraction` + `u238_tamper_fissions`). Add a `fissions_by_isotope: {isotope: count}` object or remove the requested key.
- **H2 (MINOR):** All schemas are v1 with "reject unknown versions" but no **migration** story (beyond checkpoints' "old ones never required to load"). Fine for v1, but state the forward policy (bump + loader branch) so v2 isn't ad-hoc.
- **H3 (NIT):** `run.json code_version:"0.4.0"` vs spec v0.1 — define how code_version relates to spec version and who bumps it.

---

## I. Risks (`13-risks.md`)

### I1 — R-1 likelihood understated; R-1×R-4 interaction missing — MAJOR
R-1 (few-group misses ±500 pcm) is rated **L=M**; given F1 it is the single most likely gate failure — rate **L=H**. And the *joint* effect of few-group **and** isotropic-in-lab (R-4) is uncaptured; adding groups (R-1 mitigation) does **not** fix anisotropy. Add a combined entry and the transport-correction mitigation.

### I2 — R-4 mitigation is circular — MAJOR
R-4 says "Godiva/Jezebel gates **empirically bound** the bias." If the XS are tuned to pass those gates (F1), the gate bounds nothing — it's satisfied by construction. Raise **I to H** and replace the mitigation with an *independent* check (transport correction + a held-out benchmark or leakage-spectrum comparison).

### I3 — Missing risks — MAJOR
Add register entries (with owners) for:
- **Quasi-static kinetics validity at disassembly** (A3) — I=H.
- **Generation-time held constant under compression** (A2) — I=H.
- **Non-deterministic GPU reductions / bank ordering** breaking T-resume & G0c (E1/E2) — I=H, owner M4.
- **σ ≤ 25 pcm unreachable at pinned batch** (E4) — I=M, owner M1/M4.
- **Eigen-refresh cost vs 30 fps** (E3) — I=M, owner M7.
- **M1-T4 data access + few-group generation** (C4) — I=H, owner M1.
- **Canonical schema examples invalid/inconsistent** (B1/B2) — I=M, owner M0.
- **RNG substream collision** (C3) — I=M, owner M0-T4.
- **Sweep/UI parameter-space escaping the public envelope** (G1/G2) — I=H (scope), owner continuous.

Also: **R-9 (cloud cost)** at I=L is optimistic for 5000-run MCTS sweeps on spot H200 — consider I=M.

---

## Cross-cutting root cause

Several MAJOR consistency defects (B4 CUDA 12.x, B5 OPTIX80_SDK, and the stale "CUDA 12.x"/"OptiX 8.0" fingerprints) are **the same event**: ADR-009 re-pinned the toolchain but the edit didn't propagate to every file, and the `CHANGELOG` *claims* files were updated that weren't. This is direct evidence the amendment protocol needs the D4 verification step (grep-for-stale + confirm-zero-hits before logging the CHANGELOG line). Triagers: fixing B4/B5 without adding that step will let the next amendment re-introduce drift.

---

## VERDICT

**AMEND-THEN-SHIP.** The architecture is sound and unusually well-organized for a multi-session agentic build; the physics *scope* is honest and the geometry/benchmark numbers are genuinely faithful to public sources. But it is **not** ready for M0-T1 as written: there are 4 BLOCKER-class issues (two of them — the invalid TOML and the 10×-inconsistent tally — are trivial to fix but will corrupt the canonical/golden foundation if not; two — the G0a/G0b validation circularity and the M1-T4 data/method gap — are substantive and go to the project's core credibility), plus a cluster of MAJORs that will each burn ≥1 session if a cold agent hits them mid-task. None require a rewrite; all are localized amendments. Fix the top items, run the amendment protocol, then ship the spec into implementation.

### TOP 5 fixes that MUST happen before M0-T1 starts

1. **Fix the canonical examples (B1 + B2).** Make `scenarios §4` parse as valid TOML and make `tally.json §5` internally consistent (fissions_total 2.96e24, tamper 5.9e23, fission_mesh length+sum invariants). These are declared verbatim-tested and seed the golden tests — a 30-minute fix that otherwise poisons M0-T5 and every downstream golden.
2. **Resolve the validation foundation (F1 + C4).** Decide *now*, before any transport code: require transport-corrected group constants + an independent (held-out) check, and provide an in-repo path to benchmark data + a provenance-tracked one-time multigroup-generation carve-out to D4. Otherwise G0a/G0b are either unreachable or circular, and everything built on them is suspect.
3. **Decide GPU determinism strategy (E1 + E2 + C3).** Mandate deterministic reduction + deterministic child-stream keying (not atomic-bank-position), or downgrade "bit-identical" to "statistical." This is architectural — it must be settled before M4-T1/M0-T4, or T-resume/G0c can never pass and the RNG design may need reworking.
4. **Fix the physics errors that bias the whole burst (A1/B3 Guderley γ; A2 constant g; A4 Tier-1 formula).** Relabel the Guderley exponent to γ=5/3, make `g` density-dependent, and correct the Tier-1 radius formula (it currently sends r→0). These feed G1a/G1b/G2 directly.
5. **Unify the addressing/toolchain/scope contradictions (B4/B5/B6/B7/B8 + G1).** Pin one CUDA version, one OptiX var name, one override-path grammar; make `10-ui` paths real in the schema; fix `initiator.strength` default>ui_range; and resolve the tamper-material scope contradiction (`00 §2` vs UI/sweep). These are the ones a cold agent hits in M0-T2/M0-T5/M7 and can't self-resolve.

(Runner-up, do early: **F2** re-band G1a to `[0.88,0.97]`; **E4** split gate-precision vs refresh batch; **D1** ready-queue for ADR-009 parallelism; **D3** fixed gate seeds; **C1** single spec tree; **C2** constant-range representation.)

### PRESERVE — genuinely good, do not "fix"

- **The routed-spec + living-state protocol** (README router, verify-first, gates-over-prose, claim-by-edit, append-only SESSIONS/DECISIONS, amendment protocol). This is the right backbone for dozens of sessions — patch its gaps (D1–D4), don't replace it.
- **E3a fission bookkeeping (A6)** — mathematically correct; leave it.
- **Scope honesty**: PUBLIC/DECLASSIFIED/RECONSTRUCTED status tags, the Bethe-Feynman-as-overlay quarantine (G5), keeping Pu burn-up vs U-238 tamper-yield **separate** (`00 §3.5`, tally schema), yield-as-a-range. This is the correct posture for this project.
- **Benchmark & geometry fidelity (F8)** — Godiva/Jezebel scalars and the exact scenario↔appendix layer-radius agreement are correct and consistent; a real asset.
- **Two-tier ref/gpu + differential validation (D1/ADR-001)** and **analytic-sphere-first geometry (D2/ADR-002)** — sound engineering; the oracle-first discipline is exactly right (just make the "statistical/bit-identical" language precise per E1).
- **SESSIONS.md entries** authored so far — detailed, evidence-bearing, honest about "docs only, no repo yet." A good template for future sessions.

---

*End of REVIEW_2026-08-02. Findings are individually IDed and self-contained for independent triage via `spec/README.md §6`.*

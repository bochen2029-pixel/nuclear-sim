# 08 — Validation: Benchmarks & Gate Procedures

Gate thresholds are normative and live in `data/benchmarks/gates.toml` (03 §10 — generated from THIS file by M1-T5's generator; `spec_sha256` mismatch ⇒ `nukebench` fails). Every threshold carries a constant ID + derivation (MAJ-13; appendix §5). Changing a threshold, band, or seed set requires an ADR with cited evidence. Replacing a `[M1-T4a]` placeholder is a gate change and follows `README.md` §6 in full.

## 1. Bare-sphere benchmarks (Stage 0)

> `[M1-T4a]` = placeholder finalized by M1-T4a from OPEN literature (BLK-14: the ICSBEP Handbook is access-restricted — NEA Data Bank, named users, intended-use statement; autonomous sessions MUST NOT apply). M1-T4a derives models from openly published descriptions (LANL reports, published benchmark summary/cross-evaluation papers, OpenMC/MCNP validation-suite documentation), records every number + open citation + URL + retrieval date in `data/benchmarks/{godiva,jezebel}.md`, tags the model `status = "PUBLIC-DERIVED"`, and logs one ADR per benchmark (MAJ-46). **M1-T4b (optional, owner-gated):** replace with ICSBEP sheet values if the owner obtains access; record sheet + revision.

**Godiva — HEU-MET-FAST-001** (bare HEU sphere)
- ~93.7 wt% U-235, ~5.2% U-238, ~1.0% U-234 `[M1-T4a]`; ρ ≈ 18.7–18.8 g/cm³ `[M1-T4a]`; radius ≈ 8.74 cm; mass ≈ 52–54 kg `[M1-T4a]`
- Benchmark k_eff = 1.0000 ± 0.0010 `[M1-T4a]`

**Jezebel — PU-MET-FAST-001** (bare δ-phase Pu-Ga sphere)
- **4.5 wt% Pu-240**, ~1.02 wt% Ga, balance Pu-239 `[M1-T4a]` (open cross-evaluation literature); ρ ≈ 15.6 g/cm³; radius ≈ 6.385 cm; mass ≈ 17.0 kg `[M1-T4a]`
- Benchmark k_eff = 1.0000 ± 0.0020 `[M1-T4a]`
- Scenario MUST use material `pu_ga_jezebel`, NEVER `pu_ga_delta` (loader-enforced, 03 §3). Trinity material ≠ Jezebel material (Pu-240 1.0% vs 4.5%).

Required isotope set for the xs set (M1-T4a enumerates fully): U-234, U-235, U-238, Pu-239, Pu-240, Pu-241(trace), Ga-69/71, B-10, C, H, O, N, Al, W, Be, Po.

Benchmark uncertainty rule (operational, MIN-06): gate tolerance = `500 pcm + benchmark_uncertainty_pcm` (from gates.toml); if the benchmark uncertainty is unverified (PUBLIC-DERIVED model), it is taken as 0 and the gate report MUST state so.

## 2. Gate procedures

> `07-milestones.md`'s gate table is labels only; THIS section + gates.toml is normative. A gate is met only when `nukebench gate` exits 0. Never claim a gate from a summary table. Gate runs use the normative seed set (default `[1,2,3,4,5]` per gate, MAJ-22) — `--seed` with `--gate` is exit 2.

**G0a / G0b** — bare-sphere benchmarks.
1. Eigen at gate config: `batch ≥ 1e6, inactive ≥ 50, active ≥ 200` (C-900). σ reported as max(active-cycle SE, batched-means SE).
2. Pass iff, for EVERY normative seed: |k − 1.0000| ≤ 500 pcm + benchmark_uncertainty AND σ ≤ 25 pcm (C-930/C-931).
3. **Fallback (no deadlock, MAJ-31):** if after transport correction + ONE group-structure refinement the deviation sits in (500, 1500] pcm, the gate MAY be claimed **CONDITIONAL**: deviation recorded, sign/magnitude consistent across Godiva AND Jezebel required, all downstream absolute-k gates switch to ratio-based criteria, and `conditional` blocks M6/M7 claims until resolved.

**G0c (differential)** — `nukebench diff` (hash/seed-checked).
Pass iff, on the fixed 3-seed set from gates.toml, ALL:
a. |k_ref − k_gpu| ≤ 3·√(σ_ref²+σ_gpu²) **and** ≤ 100 pcm absolute (equivalence bound — more statistics cannot tighten the gate);
b. per-shell fission source equivalence: |f_ref − f_gpu| ≤ max(3·√(σ²sum), 0.02·f_ref) per shell;
c. population series statistical: |log₁₀N_ref(n) − log₁₀N_gpu(n)| ≤ 3·n·σ_k/(k·ln10) ∀n. Cross-backend bit-identity is NOT required and MUST NOT be claimed (01 §9).

**G1a (static criticality, uncompressed)** — canonical scenario, `compression.ratio = 1.0`, Tier-1 static. The public claim is a MASS fraction (C-052: pit = 0.78 of a tamped critical mass), not a k — so the gate measures both (MAJ-14):
1. k_eff of the tamped assembly, and
2. M_pit/M_c(tamped) by bisecting pit radius at fixed density until k = 1.000 ± 0.002.
Pass iff M_pit/M_c ∈ [0.68, 0.88] (C-052 band) AND k ∈ [0.84, 0.96] (SIM-derived image of the mass band; derivation recorded in gates.toml notes).
**G1a-tight (report-only regression, B-01):** once the xs set is committed at M1-T4a, freeze k_ref = the committed canonical-assembly k; later milestones report Δpcm vs k_ref. Self-consistency windows measure the CODE, not the device — allowed (R-11 note).

**G1b (compression)** — Tier-1 scan ρ/ρ₀ ∈ {1.0, 1.2, …, 2.0}. Pass iff ALL (MAJ-15):
1. M_pit/M_c(2ρ₀) ∈ [2.6, 4.4] (public "3–4 critical masses" C-053, ±40% — the source is prose);
2. k(2.0) ∈ [1.35, 1.85] (SIM-derived band, recorded);
3. monotone within statistics: k_{i+1} − k_i ≥ −3·√(σ_i²+σ_{i+1}²) for consecutive points;
4. k(2.0)/k(1.0) ∈ [1.5, 2.0] (ratio cancels most few-group bias; keep this one if only one).

*Coupling note (QC-11):* criteria 2 and 4 are not independent of G1a — k(1.0) ∈ [0.84, 0.96] × ratio ∈ [1.5, 2.0] implies k(2.0) ∈ [1.26, 1.92], slightly wider than C-936's [1.35, 1.85], so the extreme corners of the three bands cannot be occupied simultaneously. This is deliberate (the same coupling G2 documents), not an error; a failure MUST report which criterion bound and what the others implied. *Criterion 1 is also partly implied by G1a* (QC-12): C-053's band is itself derived from C-052 via ρ⁻² scaling, and 4 × [0.68, 0.88] = [2.72, 3.52] ⊂ [2.6, 4.4]. What criterion 1 actually tests is that the **model reproduces ρ⁻² scaling** — real, but it is not independent evidence from a second source, and the appendix must not present it as such.

**G2 (canonical burst)** — α-mode, Tier-2, canonical scenario defaults, normative seed set.
**Eigen configuration (normative, QC-06):** in-burst refreshes use the INTERACTIVE config **C-900b** (1e5/10/30), not the gate config — at ≤ 80 refreshes × 5 seeds the gate config would cost ~60× more (≈1e11 vs ≈1.6e9 histories) and turn G2 from minutes into days. The σ on yield/burn-up is propagated from the interactive-batch per-refresh σ (03 §5) and criterion 6 absorbs the wider band. The initial (t=0) eigen call uses **C-900** gate config so the reported `k_eff.at_initiator` is gate-quality. Recorded in G2's `[gate.eigen]` block in gates.toml.
Pass iff ALL:
1. yield ∈ **[16.6, 26.8] kt** (C-940: envelope of C-091/C-092/C-093 incl. Selby ±2 — the published upper edge must NOT fail);
2. `burnup.pu_fraction` ∈ [0.12, 0.20] (C-941: C-090 15–17% widened ±3 pp for model error, SIM);
3. `burnup.tamper_yield_fraction` ∈ [0.10, 0.30] (C-942) — always separate from pu_fraction (`00 §3.5`);
4. **consistency (primary):** `|yield·Φ_kt − fissions_total|/fissions_total ≤ 1e-6` **and** `|yield·(1−tamper_yield_fraction)·Φ_kt·M_Pu239/(N_A·M_pit) − pu_fraction| ≤ 3σ`. Both forms use ONLY compute-path constants (C-041, C-910, C-916). **C-042/C-043 (kt/kg) MUST NOT appear in this or any other gate expression** — they are `use = crosscheck` and `11 §4`'s static check greps for exactly this misuse (QC-02). The three bands are coupled, not an independent box (MAJ-12) — see `data/benchmarks/g2_feasible_region.md`, produced by M1-T5;
5. **timing:** fission-rate FWHM ∈ [5, 100] ns; t_fire → peak ∈ [100 ns, 1.5 µs]; peak α ∈ [2e7, 3e8] s⁻¹ (C-943; replaces the vacuous ≤2 µs criterion, MAJ-16);
6. each gated value carries σ (03 §5); band must contain value AND value±2σ must overlap the band by ≥ half its width;
7. max_q ≤ 0.05 else verdict CONDITIONAL (R-13).
Canonical-scenario physics parameters are the CITED public nominals with derivations recorded (anti-default-shopping, F6).

**G3 (jitter → asymmetry, M6)** — σ_jitter ∈ {0, 50, 100, 200, 500, 1000} ns × ≥ 9 seeds. c_a was calibrated ONLY from C-071/C-072 (01 §5) — G3 is validation, not a fit (MAJ-18). Pass iff: (a) median yield non-increasing within statistics (median_{i+1} ≤ median_i + 1.5·IQR/√n); (b) median(500 ns) ≤ 0.9 × median(0 ns); (c) median(10 ns) ≥ 0.97 × median(0 ns) — negligible degradation at the historical tolerance, the published claim c_a was NOT calibrated on; (d) recorded c_a matches the C-071 rule to 1e-6. M6-T3's DoD is "G3 executed and recorded", never "G3 passes" (circularity rule).

**G4 (performance)** — dev GPU (RTX 4070 Ti SUPER, sm_89, 16 GB) and recorded cloud device:
- Render: ≥ 30 fps at 1920×1080 WITH the simulation advancing (snapshot model, 02 §3; stalled sim + live render = FAIL);
- Simulation rate: ≥ 20 generations/s sustained over a full canonical burst at interactive eigen batch (C-900b);
- Eigen refresh (burst, interactive batch): < 1 s dev GPU; < 1 s H200;
- Godiva gate eigen (C-900 gate config, 2.5e8 histories): < 180 s dev GPU / < 60 s H200 / CPU ref smoke config (C-900c: 1e5/10/50) < 30 min. *(QC-14: 180 s implies ~1.4e6 histories/s, one to two orders below what sm_89 should reach on 4-group single-region geometry — this is a deliberately loose first-light budget, not a target. M4-T4 MUST record the measured rate; if it lands more than 10× under budget, tighten the budget by ADR rather than leave a rubber stamp.)*
- Eigen calls per canonical burst ≤ 80 (reported as `timing.eigen_calls`);
- VRAM: canonical studio run ≤ 12 GB (`--vram-report`);
- Perf history appended to `artifacts/perf_history.jsonl` (rotated at 100 MB, `11 §1`).

**G5 (studio↔CLI parity, formerly G0c-ext)** — `nukestudio` export reproduces `tally.json` bit-identically via `nukebench` same-backend. First-class gate; rows in `07`, `PROGRESS.md`, gates.toml.

## 3. Statistical & integrity rules (all gates)

- Normative seed sets live in gates.toml (default [1..5]); every seed must pass; `--seed` rejected with `--gate` (exit 2). Changing a seed set = ADR.
- `gate_report.json` per 03 §11: append-only attempts; `dirty` tree caps verdict at conditional. Reports live at `artifacts/gate_reports/<gate>/` and are **committed** (QC-07) — a gate claim whose report is not in the repository is void. CI archives are a secondary copy.
- A gate passes only via `nukebench gate` exit 0 — never by inspection.
- σ in pcm beside every k. No seed-shopping, no default-shopping (canonical = cited nominals).
- Verify-first: `PROGRESS.md` VERIFY commands must be falsifiable (`README §4`); gate claims without a report path are void.

## 4. Cross-checks (non-gating, reported)

- E7 overlay: predicted efficiency vs computed `burnup.pu_fraction` — efficiency-to-efficiency ratio, order-of-magnitude expected (MIN-03).
- Post-peak yield fraction (E6): below 0.15 ⇒ premature termination warning.
- Quasi-static cross-check (B-05/A3): one canonical burst re-run with `eigen_refresh_dr_frac` halved; report Δyield; > 5% ⇒ note in run + consider default amendment.
- Analytic leakage sanity (diffusion estimate) — documentation only.

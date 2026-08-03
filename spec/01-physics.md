# 01 — Physics Basis

All equations are numbered and normative (E1–E7). Constants are referenced as `C-nnn` from `appendix/constants.md`; code MUST read them from `data/constants.toml` (§8). Nothing in this file exceeds public/declassified literature (`00-overview.md` §3).

## 1. Notation

| Symbol | Meaning | Typical public value |
|---|---|---|
| Σ_t, Σ_c, Σ_f, Σ_s, Σ_tr | macroscopic total/capture/fission/scatter/transport-corrected XS (1/cm) | material+group dependent (03 §2 semantics) |
| ν̄_i | mean **TOTAL** (prompt + delayed) neutrons per fission, isotope i — ADR-013 | Pu-239 ≈ 2.9 (`C-020`) |
| ν̄_eff | fission-source-weighted mixture ν̄ (E3a) | computed |
| β_i, β_eff | delayed-neutron fraction, isotope / source-weighted mixture | Pu-239 ≈ 0.0020 (`C-022`) |
| k_eff | multiplication factor on TOTAL ν̄ — benchmark-comparable; what every k-gate uses | — |
| k_prompt | prompt multiplication `= k_eff·(1 − β_eff)` — drives E3a ONLY | — |
| Λ | prompt generation time [s] — DENSITY-DEPENDENT (BLK-04) | ~10 ns uncompressed (`C-030`) |
| α | Rossi alpha = (k−1)/Λ (E3c) | — |
| E_f | prompt deposited energy per fission | ~180 MeV (`C-040`) |
| ρ, r | density, radius | layer table (`C-100`+) |
| N_n | neutron population at generation n | — |
| S_n | initiator source neutrons injected at generation n | C-051 schedule |

## 2. Transport (E1)

Few-group Monte Carlo with **implicit capture** (weight-based; no analog absorption):

- **E1a (free flight):** `s = −ln ξ / Σ_tr,g` with ξ ~ U(0,1) — note the **transport-corrected** Σ_tr = Σ_t − μ̄·Σ_s (03 §2, MAJ-31). Delta-tracking against a majorant is a MAY for Stage-3 lens geometry only.
- **E1b (boundary):** if `s` exceeds distance-to-boundary, advance to boundary, update material via `Tracker::locate`, resample.
- **E1c (collision, implicit capture — normative, BLK-05):** sample the isotope ∝ n_i·Σ_t,i. Bank `⌊ w·ν̄_i·(Σ_f,i/Σ_t,i)·(1/k_gen) + ξ ⌋` progeny (eigenvalue) or push the same expected count onto the stack (fixed source). Then reduce weight for absorption: `w ← w·(Σ_s,i/Σ_t,i)`. **The neutron is NOT killed at fission.** Scatter isotropically-in-lab against the transport-corrected frame (angular approximation; recorded limitation R-4) with group change sampled from `transfer[from][·]`. Analog absorption is NOT used; `sigma_c` never terminates a history directly.
- **E1d (leakage):** particle leaving the outermost layer is tallied as leaked.
- **E1e (weight cutoff / Russian roulette):** when `w < w_min` (1e-4), kill with probability `1 − w/w_surv`, else continue with `w = w_surv` (1e-2). Fission bookkeeping accumulates **weight-weighted** events. (`05` references this as E1e; "weight windows" are a different technique and are NOT used.)

Unit test (M1-T2 DoD): infinite homogeneous medium ⇒ estimated k_inf = ν̄·Σ_f/(Σ_c+Σ_f) analytically within 3σ — catches estimator-class errors the leakage test cannot.

## 3. Eigenvalue (E2)

- **E2a (power iteration):** `k_{i+1} = F_{i+1} / (F_i / k_i)`.
- **E2b (source convergence):** Shannon entropy `H = −Σ_j p_j ln p_j` on a **fixed uniform Cartesian 8×8×8 mesh** over the bounding box of the outermost layer (SIM `C-908`) — decoupled from geometry, because a layer-aligned mesh is degenerate on single-layer benchmarks (BLK-10). Convergence: |mean H over last W=5 gens − mean over preceding W| < `eig_h_tol` AND at least `I_min` inactive generations; both necessary. Gate configurations per `08-validation.md` (C-900: 1e6/50/200).
- σ: report active-cycle standard error AND a batched-means estimate (10 batches); the gate uses the LARGER (inter-cycle correlation, MAJ-32).
- **Λ estimation (BLK-04):** every eigen run also returns the prompt generation time: track-length time estimator — accumulate Σ(path length)/v per history from birth to progeny birth, divide by k. Also returns per-isotope fission source S_i (drives ν̄_eff and E5).

## 4. Burst kinetics (E3) — α-mode

Quasi-static point kinetics driven by MC eigenvalues (D3). **`k` in E3a is `k_prompt`, and it is derived exactly once (ADR-013 / QC-01):**

```
xs `nu` = TOTAL ν̄  ⇒  MC eigen returns k_eff (benchmark-comparable; used by G0a/G0b/G1a/G1b)
β_eff = Σ_i β_i·S_i / Σ_i S_i          # source-weighted, from EigenResult (C-022…C-022e)
k_prompt = k_eff · (1 − β_eff)         # applied ONCE, downstream of the eigen solve
```

Applying (1−β) to data that is already prompt-only double-counts the correction: ~200 pcm for a Pu core, **~650 pcm for Godiva (β(U-235) = 0.0065) — larger than G0a's entire ±500 pcm tolerance.** The eigen solver MUST NOT return a β-corrected k, and no gate that compares against a benchmark k_eff may consume `k_prompt`. `tally.json` reports both (`k_eff` and `k_prompt`, 03 §5). The distinction is material at the criticality crossings (A5): prompt-critical is `k_eff = 1/(1−β_eff)`, not `k_eff = 1`.

Discrete generation update, exactly:

```
per generation n (duration Λ(t)):             # E3a
    N_{n+1} = k · N_n + S_{n+1}               # S = initiator schedule (C-051); often 0
    F_n     = k · N_n / ν̄_eff                 # fissions in generation n
    E_n     = F_n · E_f                       # energy deposited in generation n
    F_cum  += F_n ;  E_cum += E_n ;  t += Λ(t)
```

- **ν̄_eff** = Σ_i ν̄_i·S_i / Σ_i S_i, from the per-isotope fission source of the last eigen refresh (B-06). The per-isotope split of F_n is ∝ each isotope's ν̄_iΣ_f,i share of the source — Pu vs U-238 tallied separately (`00 §3.5`) and MUST reconcile with `fissions_by_isotope` (invariant 6).
- **E3b (Λ):** Λ is returned by every eigen refresh and held between refreshes, rescaled by the instantaneous density ratio (Λ ∝ 1/ρ) on hydro updates without a refresh. `kinetics.generation_time_s_initial` is used only before the first eigen call.
- **E3c (Rossi α):** α = (k−1)/Λ, reported per refresh; this is the UI readout (`10 §Panel 3`).
- **Refresh cadence (normative):** recompute eigen when `n % eigen_refresh_gens == 0` (integer counter — scheduling on floating-point time is FORBIDDEN) OR on the first generation after a hydro update that moved any layer radius by more than `eigen_refresh_dr_frac` (default 0.005, SIM) — whichever first. Small hydro steps do not force refreshes. Gate property: ≤ 80 eigen calls per canonical burst (G4).
- **Validity diagnostic (R-13):** each refresh records `q = |Δk| / (k · generations_since_refresh)`; if q > 0.02 the refresh interval auto-halves (logged); `tally.json` records max_q; G2 reports CONDITIONAL if max_q > 0.05.
- **Numerical safety:** N carried as double; if N > 1e30, renormalize to 1.0 and accumulate an exponent offset. Live tallies that scale with current weight apply the offset; cumulative counters (F_cum, E_cum) are accumulated BEFORE renormalization and are unaffected by it. Unit test (M3-T1): 3 mid-burst renormalizations leave F_cum unchanged vs a half-scale no-renorm control.

## 5. Hydro (E4, E5) — tiers (D5)

- **E4 (energy → expansion, MAJ-10):** internal energy is a state variable: `dE_int/dt = Ė_dep − P_int·dV/dt`, `P_int = (γ−1)·E_int/V`, γ = 5/3 default (SIM-tagged EOS choice, consistent with C-070). Shell motion: `M·d²R/dt² = 4πR²·(P_int − P_drive)` — the interior gas pressure drives the shell OUTWARD; an external drive pressure opposes it (**ADR-019 sign correction**: the earlier `(P_drive − P_int)` did not conserve `E_int + ½MṘ²` and made the gas decelerate its own expansion). Then `d/dt(E_int + ½MṘ²) = −P_drive·dV/dt`, so with Ė_dep = 0 AND P_drive = 0 (disassembly) `E_int + ½MṘ²` is conserved to integrator tolerance. There is NO "binding/explosive threshold" term (deleted).
- **E5 (deposition, MAJ-01):** generation energy `E_n` distributes over layers ∝ the shell-resolved fission source of the last eigen run: `E_n,layer = E_n · S_layer/Σ_j S_j`; per-isotope split within a layer ∝ ν̄_iΣ_f,i shares (keeps Pu/U-238 separate). `E_n,layer`/V_layer is the energy density consumed by E4.
- **Tier-1 (parametric, BLK-01):** `r(t) = r_0 · [ 1 + ( (ρ/ρ₀)^(−1/3) − 1 ) · s(t) ]`, s: 0→1 **smootherstep** `6u⁵−15u⁴+10u³` over [t0, t0+t_c]. Check: s=0 ⇒ r_0; s=1 ⇒ r_0·(ρ/ρ₀)^(−1/3); densities mass-conserving. M2-T2 DoD asserts endpoints to 1e-12 AND mass conservation at s ∈ {0, .25, .5, .75, 1}.
- **Tier-2 (default):** converging phase timing from the Guderley self-similar form `R_s(t) = A·(t_c − t)^α_G`, α_G = 0.688377 (**spherical, γ = 5/3** — C-070, consistent with E4's γ). **`t_c` is DERIVED, not input** (MAJ-09): `t_c = t0 + α_G·R_s(t0)/v_in`, `A = R_s(t0)/(t_c−t0)^α_G`, v_in from scenario HE energy + shell mass (C-061/C-062); the derived t_c is written to run.json. The Guderley form sets ONLY the timing profile — it is not a solution of the layered metal problem; validity ends at shell contact. Disassembly: E4 with P_drive = 0.
- **Tier-3 (MAY):** 1D spherical Lagrangian; interface only (`05 §4`).
- **Asymmetry (M6, MAJ-18):** `ε = c_a·σ_jitter/t_rise`. **c_a is calibrated ONLY against the public simultaneity claim**: the jitter producing ε = 0.05 (C-072 tolerance) must equal C-071 (±10 ns) within a factor of 2, giving `c_a = 0.05·t_rise/10 ns`. c_a MUST NOT be fitted to any gate outcome; G3 is a validation, not a fit. Phenomenological ceiling — do not exceed (`00 §3.2`).

## 6. Quench & yield (E6, BLK-03)

- The chain reaction **peaks** when k(t) crosses 1 and continues into a decaying phase; up to half the energy releases after that crossing (Fuchs–Nordheim linear-feedback result). The burn terminates when `F_n < ε_quench · max_m F_m` (ε_quench = 1e-4, SIM C-909), or `t > t_max`. `quench_k`-style criteria at k≈1 mark entry into the decay phase and MUST NOT terminate integration.
- `tally.json` reports `yield_split` pre-/post-peak (healthy Tier-2 disassembly: post-peak fraction of order 0.3–0.5; below 0.15 ⇒ premature termination, reported non-gating).
- **Yield (kt) = F_cum / Φ_kt**, Φ_kt = C-041 (derived: C-918/(C-040·C-917) = 1.4508e23 at E_f = 180 MeV). **Pu burn-up** from per-isotope fissions (E3a split); U-238 tamper fissions tallied separately (`00 §3.5`). kt/kg constants C-042/C-043 are **crosscheck-only** (different energy basis, ~5% — MAJ-11); they MUST NOT enter any tally or gate computation.

## 7. Cross-check overlay (E7)

Bethe–Feynman public scaling — Serber's published form `f ~ (1/6)(v′²/ετ²)R_c²Δ`, `Δ = 2(R₂−R₀)/R_c` (symbols defined here; "efficiency ∝ (bc)²" shorthand is NOT used). Display-only comparison of **efficiency to efficiency** vs computed `burnup.pu_fraction`; report ratio, order-of-magnitude expected. It MUST NOT feed simulation state (`00 §3.3`).

## 8. Constants policy

1. `spec/appendix/constants.data.toml` (strict) → generator → `data/constants.toml` + headers (03 §1). Code MUST NOT contain physical literals.
2. Every entry: id, value (+optional lo/hi), unit, status ∈ {PUBLIC, DECLASSIFIED, RECONSTRUCTED, SIM, PENDING}, `use` ∈ {compute, crosscheck}, cite. **Gate thresholds additionally carry constant IDs and one-line derivations** (MAJ-13; C-93x/C-94x block).
3. A static check asserts no compute-path source references a `crosscheck` constant (`11 §4`).

## 9. Precision & determinism policy

- `ref/` CPU oracle: IEEE double everywhere (including XS storage and RNG uniforms — `uniform_d()`).
- `gpu/`: float for positions/directions and per-event arithmetic. Tally accumulation in **fixed-point int64 (documented scale) or float-in-block promoted to double once per block per generation**; per-collision double arithmetic is FORBIDDEN (sm_89 FP64 = 1/64 rate; H200 full rate — MAJ-33). Fixed-point block accumulation is exactly associative (BLK-11 prerequisite).
- **Within a backend**: same seed ⇒ bit-identical tallies regardless of thread count/block size/launch order (`11 §2`). **Across backends** (ref vs gpu): statistical identity ONLY (G0c); bit-identity across backends is impossible by design (mixed precision) and MUST NOT be asserted.
- No design element may make results depend on thread scheduling, block size, launch order, or atomic ordering (D1 addendum, BLK-11): progeny written at prefix-sum-reserved deterministic slots; RNG streams derive from parent identity (`04 §2`), never from buffer position; reductions use fixed tree shapes; floating-point atomicAdd on accumulators is forbidden.
- RNG: Philox4x32-10 (`04 §2` normative layout/KAT).

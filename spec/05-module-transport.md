# 05 — Module Spec: Transport, Eigen, Kinetics, Hydro (`src/ref/`, `src/gpu/`, `src/physics/`)

## 1. `ref/` — CPU reference transport (oracle; build FIRST — D1)

History-based: one neutron per loop iteration, explicit stack for fission progeny. Double precision everywhere. ~1.5k lines; clarity over speed (OpenMP-per-history parallelism MAY be added if the CPU smoke config misses its budget — C-06 note in `08`; keep clarity).

```cpp
struct Particle { Vec3 pos, dir; int group; double weight; rng::Stream stream; };
struct TallyAcc { /* k, leakage, fission by isotope (weight-weighted), per-layer track-length flux,
                     H_src on the fixed 8³ mesh, per-isotope source S_i */ };

struct SourceSpec {                              // mirrors 03 §4 [source]
  enum class Kind { PointIsotropic, UniformSphere, UniformShell, FissionSourceReplay };
  Kind kind = Kind::PointIsotropic;
  Vec3 center{0,0,0};
  double r_inner = 0.0, r_outer = 0.0;
  std::array<double,4> group_weights{1,0,0,0};   // birth spectrum; normalized at load
  int64_t histories = 100000;
};

class RefTransport {
public:
  RefTransport(const LayerStack&, const MaterialLib&, const FewGroupXS&, uint64_t seed);
  void run_fixed_source(const SourceSpec&, TallyAcc&);
  EigenResult run_eigen(const EigenSpec&);
  const FissionSource& last_source() const;      // shell- and isotope-resolved (E5, ν̄_eff)
};
```

Algorithm per history: E1a–E1e exactly as `01-physics.md` §2 (implicit capture; bank `⌊w·ν̄_i·(Σ_f,i/Σ_t,i)/k + ξ⌋`; `w ← w·Σ_s,i/Σ_t,i`; never kill at fission). M1-T2 DoD: `SourceSpec{PointIsotropic, center=origin}` in a single-layer **pure-capturer** sphere — `sigma_s = sigma_f = sigma_n2n = 0`, so `Σ_t = Σ_c` — ⇒ leaked weight fraction = exp(−Σ_c·R) **within 3σ** at Σ_c·R ∈ {0.5, 2.0, 5.0}, **plus a 16-seed mean-pull bias check** (|mean of (measured−expected)/σ| ≤ 3/√16); AND the infinite-medium k_inf unit test (`01 §2`). **The 1σ criterion this replaced was unachievable (M1-T2):** a within-1σ acceptance test has a 31.7% false-failure rate per check — all three depths pass only 31.8% of the time — and raising the history count does not help, because the pull is N(0,1) regardless of N. 3σ matches the k_inf half of this same DoD, and the mean-pull check restores (and exceeds) the sensitivity to systematic error that 1σ was reaching for, resolving bias at 0.75σ with a 0.27% false-failure rate. **Σ_f = 0 is required, not optional** (QC-09): with implicit capture and Σ_s = 0 the weight zeroes at the first collision, so what is measured is exp(−Σ_t·R), which equals exp(−Σ_c·R) only when fission and (n,2n) are absent. The symbol is `Σ_c` throughout — `Σ_a`/`sigma_a` was retired in schema v2.

## 2. Eigen solver (`physics/eigen`, backend-agnostic)

```cpp
struct EigenSpec { int64_t batch; int inactive, active; double k0 = 1.0; };
struct EigenResult {
  double k, sigma_pcm;              // σ = max(active-cycle SE, batched-means SE) (01 §3)
  double beta_eff;                  // source-weighted delayed fraction (ADR-013); k_prompt = k*(1-beta_eff)
  double lambda_s;                  // prompt generation time Λ = ℓ/k   [s]   (BLK-04)
  double lifetime_s;                // mean prompt lifetime ℓ           [s]
  double alpha_per_s;               // (k−1)/Λ                          (E3c)
  std::vector<double> k_history, H_history;
  FissionSource source;             // fixed 8³ Cartesian mesh + per-layer + per-isotope S_i
};
```

Power iteration E2a; convergence E2b (fixed 8³ mesh, mean-H window W=5, `I_min` floor). Gate configs from `gates.toml` (C-900); interactive from scenario `[eigen]` (C-900b). Λ via track-length time estimator (`01 §3`). M1-T3 DoD additions: (a) deliberately bad initial source (all at r=0) needs ≥5 more inactive gens than uniform, H non-constant in both cases; (b) Λ decreases monotonically with pit density at fixed geometry, Λ(2ρ)/Λ(ρ) ∈ [0.4, 0.6].

## 3. Kinetics (`physics/kinetics`) — α-mode (E3)

```cpp
class AlphaKinetics {
public:
  AlphaKinetics(const Scenario&, EigenFn eigen);   // EigenFn = std::function<EigenResult(const LayerStack&)>
  BurstResult run(HydroModel&, TallySink&);
};
struct BurstResult { double yield_kt, yield_sigma, fissions_total; std::vector<float> log10_N; /* … */ };
```

Implements `02 §3` + `01 §4` exactly: source term S_n; ν̄_eff from `EigenResult::source`; integer-counter refresh + `eigen_refresh_dr_frac` trigger; Λ from E3b; q diagnostic + auto-halving (R-13); log-renormalization with cumulants unaffected (`01 §4`); termination per E6 (ε_quench·F_peak, post-peak split reported). M3-T1 DoD: fixed k=2, ν̄_eff=2.9 ⇒ F_n closed form; renorm×3 invariance test; weight-weighted F_n test; mixed-assembly ν̄_eff hand-check.

## 4. Hydro (`physics/hydro`)

```cpp
class HydroModel {
public:
  virtual ~HydroModel() = default;
  virtual void   step(const EnergyField& deposit) = 0;      // called every hydro_every_gens
  virtual void   apply(LayerStack&) const = 0;
  virtual bool   at_peak_compression() const = 0;           // drives initiator t_fire (03 §4 t=0 rule)
  virtual double max_radius_change_frac_since_mark() = 0;   // drives eigen_refresh_dr_frac trigger
  virtual void   mark_geometry_refreshed() = 0;
  virtual CheckpointBlob serialize() const = 0;
  virtual void   restore(const CheckpointBlob&) = 0;
};
std::unique_ptr<HydroModel> make_hydro(const Scenario&);    // tier 1|2 (3 = stretch interface)
```

- **Tier-1:** per `01 §5` (fixed formula, smootherstep); DoD endpoint + mass checks as stated there.
- **Tier-2 (default):** Guderley timing with DERIVED t_c (`01 §5`, MAJ-09); snowplow shell ODE with the energy-conserving E4 state equation (`dE_int/dt = Ė_dep − P_int·dV/dt`); RK4; `P_drive` from HE energy (C-061/C-062) then 0 at t_c. DoD: with Ė_dep=0 and Ṙ(0)>0, E_int+½MṘ² conserved < 1e-6 rel over 1e4 steps; with Ṙ(0)=0, Ė_dep=0 ⇒ no motion; constructor satisfies |R_s(t0)−r_lens_inner| < 1e-9 and |Ṙ_s(t0)−v_in|/v_in < 1e-9.
- **Tier-3 (MAY):** 1D Lagrangian; interface only.
- **Asymmetry (M6):** `ε = c_a·σ_jitter/t_rise`, `c_a = 0.05·t_rise/10 ns` (calibration rule `01 §5`, NOT gate-fitted); compression ratio scales by (1−ε). Unit test: c_a regresses against the C-071/C-072 rule.

## 5. Tallies (`physics/tally`)

Analog estimators for k/fissions/leakage; track-length per-layer flux; fission mesh aligned to LayerStack boundaries (edges → `shell_edges_cm`, 03 §5 invariants); population series log10. All fission tallies **weight-weighted** (E1e). `TallySink` interface shared by ref/gpu/studio live streaming — studio never reaches into transport internals (D7).

## 6. `gpu/` — event-based CUDA transport (M4)

Design (normative; BLK-11/E1/E2 determinism clauses):

1. SoA persistent buffers (`pos, dir(θ,φ), group, weight, stream state`).
2. Per superstep: sample tentative event per particle → event tag; **prefix-sum partition** into per-event queues; branchless kernels per event type.
3. **Progeny at deterministic slots:** exclusive prefix sum over per-particle progeny counts reserves `bank_offset[i]` — NO atomic cursor. A progeny's RNG stream = `rng::fork(parent_stream, parent_ctr, ordinal)` — parent identity, never buffer position.
4. **Deterministic reductions:** per-block accumulation in fixed-point int64 (documented scale) or float; promoted to double once per block per generation; blocks combined by a fixed-shape two-stage tree. Floating-point `atomicAdd` on accumulators is FORBIDDEN (`01 §9`; sm_89 FP64 1/64-rate note, MAJ-33).
5. Russian roulette identical to ref (E1e — "weight windows" are NOT used).
6. Batch size is a GPU-efficiency parameter as well as a statistics one: the backend targets ≥1e6 in-flight particles per generation; M4-T4 records particles/s vs batch in `artifacts/perf_history.jsonl`.

`GpuTransport` exposes the SAME interface as `RefTransport` (§1); eigen/kinetics/hydro are backend-agnostic.

- Differential harness (T-diff): same seed → G0c criteria (`08 §2`; statistical, fixed 3-seed set). Same backend, any thread count ⇒ bit-identical (`11 §2`).
- Targets: primary `sm_89` (dev), cloud `sm_80/90` virtual (PTX); no RT-core dependency (D2).
- M4-T4 DoD addition: Nsight report includes FP64 instruction fraction; > 1% of issued ⇒ record reason in SESSIONS.md.

## 7. TD-mode (Stage-2b, MAY)

`class TDTransport` interface only: time census every Λ; population control by **Serpent-style comb** (single random offset, evenly spaced survivors, weight-preserving) capped at `N_max = 1e7`. Not required for any gate; implement only after G2 passes. Until then `mode="td"` is a loader validation error (03 §4).

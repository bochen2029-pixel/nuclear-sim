// α-mode burst coupling loop (02 §3 / 01 §4-6) — the M3-T3-b core.
//
// Wires the pieces the earlier M3 cores built into the operator-split burst loop:
// kinetics (M3-T1 `BurstAccumulator`, ν̄_eff, per-isotope shares), the eigen as a
// pluggable `EigenFn` of geometry (backend-agnostic, ADR-013 k_prompt), Tier-1
// hydro (M2-T2 compression → the E3b Λ density-rescale + the refresh-on-motion
// trigger), and a streaming `TallySink` that assembles the 03 §5 `TallyResult`.
// Terminates on E6 (`F_n < ε_quench·F_peak`), never at k = 1 (BLK-03).
//
// SCOPE (the M3-T3 split, SYNC-M1 discipline): this is the fast4-INDEPENDENT loop
// core. It does NOT implement the full `05 §4` `HydroModel` virtual hierarchy /
// `make_hydro` / `AlphaKinetics::run(HydroModel&, TallySink&)` — those need
// `CheckpointBlob` (M5) and `at_peak_compression` (M3-T4 initiator), whose
// consumers do not exist yet. Tier-2 energy-driven disassembly (E4) + E5
// deposition and the *canonical* end-to-end bundle (real transport `EigenFn` on
// `trinity_canonical`) need `fast4` (M1-T4a-2). The loop is exercised now with an
// injectable `EigenFn` (a scripted k trajectory) so all nine `tally_invariants`
// run without fast4.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/xs/xs.h"
#include "physics/eigen/eigen.h"
#include "physics/tally/tally.h"
#include "ref/ref_transport.h"

namespace ns::physics {

/// The eigen as a pluggable function of geometry (05 §3 `EigenFn`). The real
/// backend (`run_eigen` over a `RefTransport`/`GpuTransport`) plugs in for the
/// canonical bundle; tests inject a scripted stand-in. MUST return `k_eff` on
/// total ν̄ (ADR-013) — `k_prompt` is derived once, here, via `EigenResult`.
using EigenFn = std::function<EigenResult(const ns::geom::LayerStack&)>;

/// Static per-burst context the sink needs to turn the POSITIONAL fission source
/// (`by_isotope`, `by_layer`) into a named/shelled 03 §5 tally. Physical
/// constants come from `ns::consts` (01 §8), never magic numbers.
struct BurstContext {
    std::vector<std::string> isotope_names;    // aligned to EigenResult::source.by_isotope
    std::vector<double> isotope_molar_mass_g;  // aligned; for the burn-up (invariant 4)
    std::vector<int> core_pu_isotopes;         // indices of Pu-239/240/241 (burn-up)
    int tamper_isotope = -1;                   // index of the U-238 tamper, or -1
    std::vector<double> shell_edges_cm;        // fission_mesh edges (len = shells + 1)
    double phi_kt = 0.0;                        // C-041 fissions/kt
    double n_a = 0.0;                           // C-916 Avogadro
    double m_pit_g = 0.0;                       // C-102 pit mass, grams
    std::string run_id;
    bool non_canonical = false;
};

/// One generation pushed to a `TallySink` (05 §5). Fission quantities are log10
/// (E3a growth spans ~24 decades); shares are O(1) fractions of F_n.
struct GenerationSample {
    int n = 0;
    double t_s = 0.0;
    double lambda_s = 0.0;
    double k_eff = 0.0;
    double k_prompt = 0.0;
    double log10_population = 0.0;
    double log10_fissions = 0.0;               // F_n this generation, log10
    std::vector<double> isotope_shares;        // production shares of F_n (Σ = 1)
    std::vector<double> shell_shares;          // fission shares by shell (Σ = 1)
    bool refreshed = false;                    // eigen recomputed this generation
    double q = 0.0;                            // refresh diagnostic (R-13), 0 if none

    /// A stratified, capped SAMPLE of this generation's fission SITES — the
    /// spatial positions (+ isotope/layer) where fissions occur, for the 3D
    /// volumetric chain-reaction view (the owner's core deliverable). Populated
    /// on refresh generations only: the spatial pattern is quasi-static between
    /// eigen refreshes (α-mode), while the amplitude (`log10_population`) evolves
    /// every generation — so a renderer caches the cloud on refresh and scales
    /// its intensity/instance-count by the population each frame, up to its own
    /// 10-50k budget. Empty when `!refreshed`. This is the honest tap: every
    /// site is a real Monte-Carlo fission from the emergent run.
    std::vector<ns::ref::FissionSite> sites;
};

/// Streaming tally sink (05 §5), shared by ref/gpu/studio live rendering (D7 —
/// studio consumes this, never transport internals). Default hooks are no-ops so
/// a renderer can override only `on_generation`.
///
/// This is also the live seam for the eventual 3D volumetric CHAIN-REACTION view
/// (the project's core deliverable: zoom into the pit, slow time down, watch the
/// fission cascade). A renderer subscribes here to animate the burst generation
/// by generation. `GenerationSample` is deliberately the extension point for a
/// per-generation spatial fission-site stream (`ref::FissionSource::sites`) when
/// that view is built — keep it extensible; do not collapse away spatial/temporal
/// fidelity the renderer will need.
class TallySink {
public:
    virtual ~TallySink() = default;
    virtual void on_begin(const BurstContext& /*ctx*/) {}
    virtual void on_generation(const GenerationSample& /*g*/) {}
    virtual void on_end() {}
};

/// Concrete sink: assembles a 03 §5 `TallyResult`. Per-isotope and per-shell
/// fissions accumulate in the log domain (log-sum-exp) so the ~24-decade E3a
/// growth cannot overflow and `Σ shares == total` holds by construction
/// (invariants 2 and 6). `yield_split` tracks the cumulative at the F_n peak
/// (invariant 9 / BLK-03). Finalized by `on_end()`; read via `result()`.
class BurstTally : public TallySink {
public:
    void on_begin(const BurstContext& ctx) override;
    void on_generation(const GenerationSample& g) override;
    void on_end() override;
    const TallyResult& result() const noexcept { return result_; }

private:
    BurstContext ctx_;
    TallyResult result_;

    // log10 running sums (−inf ⇒ 0), via log-sum-exp.
    double log_total_ = 0.0;                    // set in on_begin
    std::vector<double> log_by_isotope_;
    std::vector<double> log_by_shell_;

    // peak tracking for k readouts + yield_split.
    double log_f_peak_ = 0.0;
    double log_total_at_peak_ = 0.0;
    double k_eff_peak_ = 0.0, k_prompt_peak_ = 0.0;
    // k at first / last generation (initiator / quench readouts).
    double k_eff_first_ = 0.0, k_prompt_first_ = 0.0;
    double k_eff_last_ = 0.0, k_prompt_last_ = 0.0;
    double last_t_ = 0.0;
    int generations_ = 0;
    bool begun_ = false;
};

/// Loop configuration (03 §4 `[kinetics]`/`[compression]`/`[initiator]`), a plain
/// struct so the core is fast4-independent — the `Scenario → CoupleConfig` map +
/// `make_hydro` are the canonical-bundle wiring. Constants come from `ns::consts`.
struct CoupleConfig {
    double n0 = 1.0;                   // initial population
    double e_f_mev = 0.0;             // C-040 prompt fission energy (MeV) — E_cum scale
    double phi_kt = 0.0;             // C-041 fissions/kt
    double lambda_s_initial = 0.0;    // C-030 Λ_0 (s), before the first eigen refresh
    int eigen_refresh_gens = 10;      // 03 §4 [kinetics]
    double eigen_refresh_dr_frac = 0.005;  // refresh on the first gen after |Δr/r| > this
    double quench_epsilon = 1e-4;     // C-909 (E6)
    double t_max_s = 5e-6;            // 03 §4 [kinetics]
    int max_generations = 200000;     // hard safety cap (never the physical terminator)
    int max_sites_per_sample = 4096;  // cap on the per-generation fission-site sample (viz stream)

    // Tier-1 compression (M2-T2). `ratio = 1` ⇒ fixed geometry (hydro off).
    double compression_ratio = 1.0;   // C-060 final ρ/ρ_0
    double compression_t0_s = 0.0;
    double compression_t_c_s = 1.0e-6;

    // Initiator schedule (C-051): S_n neutrons injected around t_fire.
    double initiator_rate_n_per_s = 0.0;
    double initiator_t_fire_s = 0.0;
    double initiator_pulse_width_s = 1.0e-8;
};

/// One α-mode burst (02 §3). Drives `BurstAccumulator` (E3a) from `eigen_fn`'s
/// k_prompt with the INTEGER-counter refresh cadence (+ the Tier-1 radius-motion
/// trigger), holds/rescales Λ (E3b), streams each generation to `sink`, and
/// terminates on E6 (`F_n < ε_quench·F_peak`) or t_max — never at k = 1. `ctx`
/// supplies the names/geometry/constants the sink needs. `geom0` is the
/// uncompressed assembly. Returns a `TallyResult` when `sink` is a `BurstTally`.
struct BurstReport {
    double yield_kt = 0.0;
    double fissions_total = 0.0;
    bool supercritical_reached = false;
    bool quenched = false;             // E6 fired (false ⇒ hit t_max / gen cap)
    int generations = 0;
    int eigen_calls = 0;
    double max_q = 0.0;
};

BurstReport run_burst(const CoupleConfig& cfg, const BurstContext& ctx,
                      const ns::geom::LayerStack& geom0, const EigenFn& eigen_fn,
                      TallySink& sink);

/// Adapt the REAL reference transport into an `EigenFn`: each call rebuilds a
/// `RefTransport` on the given geometry (materials/xs fixed) and runs `run_eigen`
/// (05 §2). This is what makes `run_burst` drive a real Monte-Carlo eigenvalue
/// solve — fission sites and all — instead of a scripted stub; the honesty step
/// from "real contract, fake numbers" to "real transport" (cited numbers arrive
/// with the `fast4` xs). `materials`, `xs` and `spec` MUST outlive the returned
/// function. Backend-agnostic (05 §2): a GPU eigen (M4-T3) supplies an equivalent
/// `EigenFn` without changing the loop.
EigenFn ref_eigen_fn(const ns::material::MaterialLib& materials, const ns::xs::FewGroupXS& xs,
                     const EigenSpec& spec, std::uint64_t seed);

}  // namespace ns::physics

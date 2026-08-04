// α-mode burst coupling loop + streaming tally (02 §3 / 01 §4-6) — M3-T3-b core.

#include "physics/couple/couple.h"

#include "physics/hydro/tier1.h"
#include "physics/hydro/tier2.h"
#include "physics/kinetics/kinetics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ns::physics {

namespace {

constexpr double kNegInf = -std::numeric_limits<double>::infinity();

// log10(10^a + 10^b), overflow-safe, with -inf meaning a zero term.
double logaddexp10(double a, double b) {
    if (a == kNegInf) return b;
    if (b == kNegInf) return a;
    const double hi = std::max(a, b);
    const double lo = std::min(a, b);
    return hi + std::log10(1.0 + std::pow(10.0, lo - hi));
}

// Value of a log10-domain accumulator (−inf ⇒ 0).
double from_log10(double log_v) { return log_v == kNegInf ? 0.0 : std::pow(10.0, log_v); }

// Shell shares from the per-layer fission source (normalized to sum 1; all-zero
// source ⇒ all-zero, which the sink treats as no shell fissions this generation).
std::vector<double> shell_shares_of(const std::vector<double>& by_layer) {
    double total = 0.0;
    for (const double v : by_layer) total += v;
    std::vector<double> shares(by_layer.size(), 0.0);
    if (total > 0.0) {
        for (std::size_t i = 0; i < by_layer.size(); ++i) shares[i] = by_layer[i] / total;
    }
    return shares;
}

// Stratified, capped sample of fission sites for the viz stream: a uniform stride
// through the source's sites preserves the spatial/isotope distribution (a faithful
// representative subset, not the first-N). Deterministic.
std::vector<ns::ref::FissionSite> sample_sites(const std::vector<ns::ref::FissionSite>& all, int cap) {
    if (cap <= 0 || all.empty()) return {};
    if (static_cast<int>(all.size()) <= cap) return all;
    std::vector<ns::ref::FissionSite> out;
    out.reserve(static_cast<std::size_t>(cap));
    const double stride = static_cast<double>(all.size()) / cap;
    for (int i = 0; i < cap; ++i) {
        out.push_back(all[static_cast<std::size_t>(static_cast<double>(i) * stride)]);
    }
    return out;
}

// Tier-1 compressed geometry at time t (s(0)=0 ⇒ scale 1 ⇒ geom0).
ns::geom::LayerStack geometry_at(const ns::geom::LayerStack& geom0,
                                 const Tier1Compression& comp, double t_s) {
    return compress(geom0, comp.radius_scale(comp.s_of(t_s)));
}

// Mass-conserving density ratio ρ/ρ₀ from the uniform geometry scale: (r_ref/r)³.
// Mode-agnostic — works for tier-1 compression (r < r_ref ⇒ >1) and tier-2
// disassembly (r > r_ref ⇒ <1) alike; the E3b Λ rescale reads it.
double density_ratio_of(const ns::geom::LayerStack& geom, double r_ref_cm) {
    const double r = geom.outermost_radius();
    if (r <= 0.0 || r_ref_cm <= 0.0) {
        return 1.0;
    }
    const double s = r / r_ref_cm;
    return 1.0 / (s * s * s);
}

// Initiator source term S_n (C-051): neutrons injected during the fire pulse.
double initiator_source(const CoupleConfig& cfg, double t_s, double lambda_s) {
    if (cfg.initiator_rate_n_per_s <= 0.0) return 0.0;
    if (t_s >= cfg.initiator_t_fire_s &&
        t_s < cfg.initiator_t_fire_s + cfg.initiator_pulse_width_s) {
        return cfg.initiator_rate_n_per_s * lambda_s;  // neutrons in this Λ window
    }
    return 0.0;
}

// --- little-endian byte I/O for the checkpoint section codec (03 §8) ---
void put_u64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put_i64(std::vector<std::uint8_t>& v, std::int64_t x) { put_u64(v, static_cast<std::uint64_t>(x)); }
void put_f64(std::vector<std::uint8_t>& v, double x) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &x, 8);
    put_u64(v, bits);
}
void put_double_vec(std::vector<std::uint8_t>& v, const std::vector<double>& d) {
    put_u64(v, static_cast<std::uint64_t>(d.size()));
    for (const double x : d) put_f64(v, x);
}
std::uint64_t read_u64(const std::vector<std::uint8_t>& v, std::size_t& pos) {
    if (pos + 8 > v.size()) throw std::runtime_error("burst checkpoint: truncated payload");
    std::uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<std::uint64_t>(v[pos + i]) << (8 * i);
    pos += 8;
    return x;
}
std::int64_t read_i64(const std::vector<std::uint8_t>& v, std::size_t& pos) {
    return static_cast<std::int64_t>(read_u64(v, pos));
}
double read_f64(const std::vector<std::uint8_t>& v, std::size_t& pos) {
    const std::uint64_t bits = read_u64(v, pos);
    double x = 0.0;
    std::memcpy(&x, &bits, 8);
    return x;
}
std::vector<double> read_double_vec(const std::vector<std::uint8_t>& v, std::size_t& pos) {
    const std::uint64_t n = read_u64(v, pos);
    std::vector<double> out(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = read_f64(v, pos);
    return out;
}
std::uint8_t read_u8(const std::vector<std::uint8_t>& v, std::size_t& pos) {
    if (pos >= v.size()) throw std::runtime_error("burst checkpoint: truncated payload");
    return v[pos++];
}

}  // namespace

// ---------------------------------------------------------------------------
// BurstTally — assembles a 03 §5 TallyResult from the generation stream.
// ---------------------------------------------------------------------------

void BurstTally::on_begin(const BurstContext& ctx) {
    ctx_ = ctx;
    log_total_ = kNegInf;
    log_by_isotope_.assign(ctx.isotope_names.size(), kNegInf);
    const std::size_t shells =
        ctx.shell_edges_cm.size() > 1 ? ctx.shell_edges_cm.size() - 1 : 0;
    log_by_shell_.assign(shells, kNegInf);
    log_f_peak_ = kNegInf;
    log_total_at_peak_ = kNegInf;
    k_eff_peak_ = k_prompt_peak_ = 0.0;
    k_eff_first_ = k_prompt_first_ = 0.0;
    k_eff_last_ = k_prompt_last_ = 0.0;
    last_t_ = 0.0;
    generations_ = 0;
    result_ = TallyResult{};
    result_.population_series.log10_N.clear();
    begun_ = true;
}

void BurstTally::on_generation(const GenerationSample& g) {
    // Total fissions (log domain).
    log_total_ = logaddexp10(log_total_, g.log10_fissions);

    // Per-isotope split ∝ production shares (01 §4). Σ_i share_i == 1 each
    // generation, so Σ_i fissions_i == total by construction (invariant 6).
    for (std::size_t i = 0; i < log_by_isotope_.size() && i < g.isotope_shares.size(); ++i) {
        if (g.isotope_shares[i] > 0.0) {
            log_by_isotope_[i] =
                logaddexp10(log_by_isotope_[i], g.log10_fissions + std::log10(g.isotope_shares[i]));
        }
    }
    // Per-shell fissions (invariant 2).
    for (std::size_t s = 0; s < log_by_shell_.size() && s < g.shell_shares.size(); ++s) {
        if (g.shell_shares[s] > 0.0) {
            log_by_shell_[s] =
                logaddexp10(log_by_shell_[s], g.log10_fissions + std::log10(g.shell_shares[s]));
        }
    }

    result_.population_series.log10_N.push_back(g.log10_population);

    if (generations_ == 0) {
        k_eff_first_ = g.k_eff;
        k_prompt_first_ = g.k_prompt;
    }
    k_eff_last_ = g.k_eff;
    k_prompt_last_ = g.k_prompt;

    // Peak generation (max F_n): record the cumulative up to and INCLUDING it,
    // so yield_split.pre_peak = cumulative-at-peak (invariant 9 / BLK-03).
    if (g.log10_fissions > log_f_peak_) {
        log_f_peak_ = g.log10_fissions;
        log_total_at_peak_ = log_total_;  // includes this generation
        k_eff_peak_ = g.k_eff;
        k_prompt_peak_ = g.k_prompt;
    }

    if (g.q > result_.timing.max_q) result_.timing.max_q = g.q;
    if (g.refreshed) ++result_.timing.eigen_calls;

    last_t_ = g.t_s + g.lambda_s;  // time at the end of this generation
    ++generations_;
}

void BurstTally::on_end() {
    const double total = from_log10(log_total_);

    result_.schema_version = 1;
    result_.run_id = ctx_.run_id;
    result_.non_canonical = ctx_.non_canonical;
    result_.fissions_total = total;

    // Per-isotope fissions (named).
    result_.fissions_by_isotope.entries.clear();
    for (std::size_t i = 0; i < ctx_.isotope_names.size(); ++i) {
        result_.fissions_by_isotope.entries.emplace_back(ctx_.isotope_names[i],
                                                         from_log10(log_by_isotope_[i]));
    }

    // Fission mesh (radial shells).
    result_.fission_mesh.format = "radial_shells";
    result_.fission_mesh.shell_edges_cm = ctx_.shell_edges_cm;
    result_.fission_mesh.fissions.clear();
    double shell_sum = 0.0;
    for (const double log_s : log_by_shell_) {
        const double f = from_log10(log_s);
        result_.fission_mesh.fissions.push_back(f);
        shell_sum += f;
    }
    // Neutrons that fissioned no shell close the balance (invariant 2 exact).
    result_.fission_mesh.leaked_fissions = total - shell_sum;

    // Burn-up (invariant 4: each core Pu isotope with its own molar mass).
    double pu_fissions = 0.0;
    double pu_mass_g = 0.0;
    for (const int i : ctx_.core_pu_isotopes) {
        if (i >= 0 && static_cast<std::size_t>(i) < ctx_.isotope_names.size()) {
            const double f = from_log10(log_by_isotope_[static_cast<std::size_t>(i)]);
            pu_fissions += f;
            if (static_cast<std::size_t>(i) < ctx_.isotope_molar_mass_g.size()) {
                pu_mass_g += f * ctx_.isotope_molar_mass_g[static_cast<std::size_t>(i)];
            }
        }
    }
    result_.burnup.pu_fissions = pu_fissions;
    result_.burnup.pu_fraction =
        (ctx_.n_a > 0.0 && ctx_.m_pit_g > 0.0) ? pu_mass_g / (ctx_.n_a * ctx_.m_pit_g) : 0.0;
    result_.burnup.pu_fraction_sigma = 0.0;  // σ propagation is a canonical-bundle refinement

    const double tamper_fissions =
        (ctx_.tamper_isotope >= 0 &&
         static_cast<std::size_t>(ctx_.tamper_isotope) < log_by_isotope_.size())
            ? from_log10(log_by_isotope_[static_cast<std::size_t>(ctx_.tamper_isotope)])
            : 0.0;
    result_.burnup.u238_tamper_fissions = tamper_fissions;
    result_.burnup.tamper_yield_fraction = total > 0.0 ? tamper_fissions / total : 0.0;

    // Yield + split (Φ_kt = C-041; invariants 3 and 9).
    result_.yield_kt = ctx_.phi_kt > 0.0 ? total / ctx_.phi_kt : 0.0;
    result_.yield_kt_sigma = 0.0;
    const double pre_peak = from_log10(log_total_at_peak_);
    result_.yield_split.pre_peak_kt = ctx_.phi_kt > 0.0 ? pre_peak / ctx_.phi_kt : 0.0;
    result_.yield_split.post_peak_kt = ctx_.phi_kt > 0.0 ? (total - pre_peak) / ctx_.phi_kt : 0.0;

    // k readouts.
    result_.k_eff.peak = k_eff_peak_;
    result_.k_eff.at_initiator = k_eff_first_;
    result_.k_eff.at_quench = k_eff_last_;
    result_.k_eff.sigma_pcm = 0.0;  // from the eigen σ in the canonical bundle
    result_.k_prompt.peak = k_prompt_peak_;
    result_.k_prompt.at_quench = k_prompt_last_;

    // population_series: dt_s is the mean generation time; generations·dt_s =
    // t_final ≤ t_max when E6 terminated the burst (invariant 7).
    result_.population_series.dt_s = generations_ > 0 ? last_t_ / generations_ : 0.0;

    result_.timing.generations = generations_;
    result_.timing.wall_s = 0.0;  // not measured in the core
}

BurstTally::State BurstTally::state() const {
    State s;
    s.log_total = log_total_;
    s.log_by_isotope = log_by_isotope_;
    s.log_by_shell = log_by_shell_;
    s.log_f_peak = log_f_peak_;
    s.log_total_at_peak = log_total_at_peak_;
    s.k_eff_peak = k_eff_peak_;
    s.k_prompt_peak = k_prompt_peak_;
    s.k_eff_first = k_eff_first_;
    s.k_prompt_first = k_prompt_first_;
    s.k_eff_last = k_eff_last_;
    s.k_prompt_last = k_prompt_last_;
    s.last_t = last_t_;
    s.generations = generations_;
    s.population_log10_N = result_.population_series.log10_N;
    s.max_q = result_.timing.max_q;
    s.eigen_calls = result_.timing.eigen_calls;
    return s;
}

void BurstTally::load_state(const State& s) {
    log_total_ = s.log_total;
    log_by_isotope_ = s.log_by_isotope;
    log_by_shell_ = s.log_by_shell;
    log_f_peak_ = s.log_f_peak;
    log_total_at_peak_ = s.log_total_at_peak;
    k_eff_peak_ = s.k_eff_peak;
    k_prompt_peak_ = s.k_prompt_peak;
    k_eff_first_ = s.k_eff_first;
    k_prompt_first_ = s.k_prompt_first;
    k_eff_last_ = s.k_eff_last;
    k_prompt_last_ = s.k_prompt_last;
    last_t_ = s.last_t;
    generations_ = static_cast<int>(s.generations);
    result_.population_series.log10_N = s.population_log10_N;
    result_.timing.max_q = s.max_q;
    result_.timing.eigen_calls = s.eigen_calls;
}

std::vector<std::uint8_t> serialize_burst_state(const BurstCheckpoint& cp,
                                                const BurstTally::State& tally) {
    std::vector<std::uint8_t> out;
    // Loop scalars.
    put_f64(out, cp.t);
    put_i64(out, cp.n);
    out.push_back(cp.supercritical ? 1 : 0);
    put_f64(out, cp.log_f_peak);
    put_i64(out, cp.gens_since_refresh);
    put_i64(out, cp.refresh_gens);
    put_f64(out, cp.marked_radius);
    put_f64(out, cp.last_k);
    put_i64(out, cp.eigen_calls);
    put_f64(out, cp.max_q);
    put_f64(out, cp.shell_R);
    put_f64(out, cp.shell_Rdot);
    put_f64(out, cp.shell_E_int);
    // Accumulator state (reuse the kinetics codec, length-prefixed).
    const std::vector<std::uint8_t> acc_bytes = serialize_accumulator_state(cp.acc);
    put_u64(out, static_cast<std::uint64_t>(acc_bytes.size()));
    out.insert(out.end(), acc_bytes.begin(), acc_bytes.end());
    // Sink (BurstTally) state.
    put_f64(out, tally.log_total);
    put_double_vec(out, tally.log_by_isotope);
    put_double_vec(out, tally.log_by_shell);
    put_f64(out, tally.log_f_peak);
    put_f64(out, tally.log_total_at_peak);
    put_f64(out, tally.k_eff_peak);
    put_f64(out, tally.k_prompt_peak);
    put_f64(out, tally.k_eff_first);
    put_f64(out, tally.k_prompt_first);
    put_f64(out, tally.k_eff_last);
    put_f64(out, tally.k_prompt_last);
    put_f64(out, tally.last_t);
    put_i64(out, tally.generations);
    put_double_vec(out, tally.population_log10_N);
    put_f64(out, tally.max_q);
    put_i64(out, tally.eigen_calls);
    return out;
}

void deserialize_burst_state(const std::vector<std::uint8_t>& bytes, BurstCheckpoint& cp,
                             BurstTally::State& tally) {
    std::size_t pos = 0;
    cp.t = read_f64(bytes, pos);
    cp.n = read_i64(bytes, pos);
    cp.supercritical = read_u8(bytes, pos) != 0;
    cp.log_f_peak = read_f64(bytes, pos);
    cp.gens_since_refresh = read_i64(bytes, pos);
    cp.refresh_gens = read_i64(bytes, pos);
    cp.marked_radius = read_f64(bytes, pos);
    cp.last_k = read_f64(bytes, pos);
    cp.eigen_calls = read_i64(bytes, pos);
    cp.max_q = read_f64(bytes, pos);
    cp.shell_R = read_f64(bytes, pos);
    cp.shell_Rdot = read_f64(bytes, pos);
    cp.shell_E_int = read_f64(bytes, pos);
    const std::uint64_t acc_len = read_u64(bytes, pos);
    const std::vector<std::uint8_t> acc_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                                              bytes.begin() + static_cast<std::ptrdiff_t>(pos + acc_len));
    cp.acc = deserialize_accumulator_state(acc_bytes);
    pos += static_cast<std::size_t>(acc_len);
    tally.log_total = read_f64(bytes, pos);
    tally.log_by_isotope = read_double_vec(bytes, pos);
    tally.log_by_shell = read_double_vec(bytes, pos);
    tally.log_f_peak = read_f64(bytes, pos);
    tally.log_total_at_peak = read_f64(bytes, pos);
    tally.k_eff_peak = read_f64(bytes, pos);
    tally.k_prompt_peak = read_f64(bytes, pos);
    tally.k_eff_first = read_f64(bytes, pos);
    tally.k_prompt_first = read_f64(bytes, pos);
    tally.k_eff_last = read_f64(bytes, pos);
    tally.k_prompt_last = read_f64(bytes, pos);
    tally.last_t = read_f64(bytes, pos);
    tally.generations = read_i64(bytes, pos);
    tally.population_log10_N = read_double_vec(bytes, pos);
    tally.max_q = read_f64(bytes, pos);
    tally.eigen_calls = read_i64(bytes, pos);
}

// ---------------------------------------------------------------------------
// run_burst — the 02 §3 operator-split loop.
// ---------------------------------------------------------------------------

BurstReport run_burst(const CoupleConfig& cfg, const BurstContext& ctx,
                      const ns::geom::LayerStack& geom0, const EigenFn& eigen_fn,
                      TallySink& sink, BurstResume* resume) {
    const bool resuming = resume != nullptr && resume->restore != nullptr;
    if (!resuming) {
        sink.on_begin(ctx);  // on resume the caller already restored the sink (load_state)
    }

    const Tier1Compression comp{cfg.compression_ratio, cfg.compression_t0_s, cfg.compression_t_c_s};
    // Tier-2 disassembly state (used only when cfg.disassembly): a SnowplowShell
    // whose interior energy is fed by deposited fission energy (E5) and which
    // expands per E4, driving the geometry the mass-conserving eigen dilutes.
    const double r_ref_cm = geom0.outermost_radius();
    const double r0_m = cfg.core_radius0_cm > 0.0 ? cfg.core_radius0_cm / 100.0 : r_ref_cm / 100.0;
    const SnowplowShell shell{cfg.core_mass_kg, cfg.disassembly_gamma, 0.0, 0.0};

    // Loop state (fresh, or restored from a checkpoint below).
    ns::geom::LayerStack geom;
    ShellState shell_state{r0_m, 0.0, cfg.e_int0_j};
    EigenResult er;
    double k_p = 0.0;
    double nu = 0.0;
    double lambda = cfg.lambda_s_initial;
    double lambda_at_refresh = lambda;
    double rho_at_refresh = 1.0;
    double last_k = 0.0;
    BurstAccumulator acc(cfg.n0);
    double t = 0.0;
    double log_f_peak = kNegInf;
    bool supercritical = false;
    bool quenched = false;
    int n = 0;
    int gens_since_refresh = 0;
    int eigen_calls = 0;
    double max_q = 0.0;
    int refresh_gens = std::max(1, cfg.eigen_refresh_gens);
    double marked_radius = 0.0;

    if (resuming) {
        // Restore the loop state. The cached eigen (er/k_p/ν/Λ/shares) is NOT stored:
        // the first iteration re-runs the eigen (n is a refresh boundary), which is
        // deterministic (fixed seed + reconstructed geometry) → bit-identical.
        const BurstCheckpoint& cp = *resume->restore;
        t = cp.t;
        n = static_cast<int>(cp.n);
        supercritical = cp.supercritical;
        log_f_peak = cp.log_f_peak;
        gens_since_refresh = static_cast<int>(cp.gens_since_refresh);
        refresh_gens = std::max(1, static_cast<int>(cp.refresh_gens));
        marked_radius = cp.marked_radius;
        last_k = cp.last_k;
        eigen_calls = static_cast<int>(cp.eigen_calls);
        max_q = cp.max_q;
        acc = BurstAccumulator::from_state(cp.acc);
        shell_state = ShellState{cp.shell_R, cp.shell_Rdot, cp.shell_E_int};
        // Reconstruct the geometry from geom0 + the shell (disassembly) or time.
        if (cfg.disassembly) {
            const double rscale =
                r0_m > 0.0 && std::isfinite(shell_state.R) ? shell_state.R / r0_m : 1.0;
            geom = compress(geom0, std::max(rscale, 1e-6));
        } else {
            geom = geometry_at(geom0, comp, t);
        }
    } else {
        // Initial eigen (02 §3): k_eff, Λ, β_eff, source at t = 0.
        geom = geometry_at(geom0, comp, 0.0);
        er = eigen_fn(geom);
        eigen_calls = 1;
        k_p = er.k_prompt();
        nu = nu_eff(er.source);
        lambda = er.lambda_s > 0.0 ? er.lambda_s : cfg.lambda_s_initial;
        lambda_at_refresh = lambda;
        rho_at_refresh = density_ratio_of(geom, r_ref_cm);
        last_k = er.k;
        marked_radius = geom.outermost_radius();
    }

    while (n < cfg.max_generations) {
        // Checkpoint capture: at the first refresh boundary at-or-after checkpoint_at,
        // BEFORE the refresh (so a resume re-derives the eigen). Requires n > 0.
        if (resume != nullptr && resume->checkpoint_at >= 0 && n >= resume->checkpoint_at &&
            n > 0 && n % refresh_gens == 0) {
            resume->captured =
                BurstCheckpoint{t,        n,          supercritical, log_f_peak,     gens_since_refresh,
                                refresh_gens, marked_radius, last_k,   eigen_calls,  max_q,
                                acc.state(), shell_state.R, shell_state.Rdot, shell_state.E_int};
            resume->did_capture = true;
            break;
        }

        // Refresh cadence: integer counter OR a Tier-1 radius move > the fraction.
        const double cur_radius = geom.outermost_radius();
        const bool moved =
            marked_radius > 0.0 &&
            std::abs(cur_radius / marked_radius - 1.0) > cfg.eigen_refresh_dr_frac;
        bool refreshed = false;
        double q = 0.0;
        if (n > 0 && (n % refresh_gens == 0 || moved)) {
            er = eigen_fn(geom);
            ++eigen_calls;
            k_p = er.k_prompt();
            nu = nu_eff(er.source);
            if (er.lambda_s > 0.0) lambda = er.lambda_s;
            q = refresh_q(std::abs(er.k - last_k), er.k, gens_since_refresh);
            max_q = std::max(max_q, q);
            if (q > 0.02) refresh_gens = std::max(1, refresh_gens / 2);  // auto-halve (R-13)
            last_k = er.k;
            lambda_at_refresh = lambda;
            rho_at_refresh = density_ratio_of(geom, r_ref_cm);
            marked_radius = cur_radius;
            gens_since_refresh = 0;
            refreshed = true;
        } else if (n > 0) {
            // E3b: hold Λ, rescale by the density ratio since the last refresh (Λ ∝ 1/ρ).
            const double rho_now = density_ratio_of(geom, r_ref_cm);
            if (rho_now > 0.0) lambda = lambda_at_refresh * rho_at_refresh / rho_now;
        }

        if (k_p >= 1.0) supercritical = true;  // PROMPT-supercritical

        const double s_n = initiator_source(cfg, t, lambda);
        acc.step(k_p, nu, cfg.e_f_mev, s_n);  // E3a
        const double log_f_n = acc.log10_fissions_last();
        log_f_peak = std::max(log_f_peak, log_f_n);

        GenerationSample g;
        g.n = n;
        g.t_s = t;
        g.lambda_s = lambda;
        g.k_eff = er.k;
        g.k_prompt = k_p;
        g.log10_population = acc.log10_N();
        g.log10_fissions = log_f_n;
        g.isotope_shares = isotope_fission_shares(er.source);
        g.shell_shares = shell_shares_of(er.source.by_layer);
        g.refreshed = refreshed;
        g.q = q;
        // Spatial fission-site sample: fresh at the initial eigen (n==0) and at
        // each refresh (the quasi-static pattern), empty otherwise — the viz seam.
        if (refreshed || n == 0) {
            g.sites = sample_sites(er.source.sites, cfg.max_sites_per_sample);
        }
        sink.on_generation(g);

        t += lambda;
        ++n;
        ++gens_since_refresh;
        if (cfg.disassembly) {
            // E5: impulse-deposit this generation's fission energy (F_n·e_f, Joules)
            // into the interior, then E4-expand the shell over Λ. As R grows the
            // mass-conserving eigen sees ρ = ρ₀·(r₀/R)³ drop → k falls → E6 quench.
            const double e_n_j = std::pow(10.0, log_f_n) * cfg.e_f_joules;
            if (std::isfinite(e_n_j)) {
                shell_state.E_int += e_n_j;
            }
            shell_state = shell.rk4_step(shell_state, lambda);
            const double rscale = r0_m > 0.0 && std::isfinite(shell_state.R) ? shell_state.R / r0_m : 1.0;
            geom = compress(geom0, std::max(rscale, 1e-6));
        } else {
            geom = geometry_at(geom0, comp, t);
        }

        // E6 (BLK-03): terminate when the fission rate has fallen ε_quench below
        // its own peak — NOT at k = 1. Requires having gone prompt-supercritical.
        if (supercritical && log_f_n < std::log10(cfg.quench_epsilon) + log_f_peak) {
            quenched = true;
            break;
        }
        if (t >= cfg.t_max_s) break;
    }

    sink.on_end();

    const double total = from_log10(acc.log10_fissions_cumulative());
    BurstReport report;
    report.yield_kt = cfg.phi_kt > 0.0 ? total / cfg.phi_kt : 0.0;
    report.fissions_total = total;
    report.supercritical_reached = supercritical;
    report.quenched = quenched;
    report.generations = n;
    report.eigen_calls = eigen_calls;
    report.max_q = max_q;
    return report;
}

EigenFn ref_eigen_fn(const ns::material::MaterialLib& materials, const ns::xs::FewGroupXS& xs,
                     const EigenSpec& spec, std::uint64_t seed) {
    return [&materials, &xs, spec, seed](const ns::geom::LayerStack& geom) {
        ns::ref::RefTransport transport(geom, materials, xs, seed);
        return run_eigen(transport, spec);
    };
}

EigenFn ref_eigen_fn_masscons(const ns::material::MaterialLib& materials,
                              const ns::xs::FewGroupXS& xs, const EigenSpec& spec,
                              std::uint64_t seed, double r_ref_cm) {
    return [&materials, &xs, spec, seed, r_ref_cm](const ns::geom::LayerStack& geom) {
        const double s = r_ref_cm > 0.0 ? geom.outermost_radius() / r_ref_cm : 1.0;
        const double density_scale = s > 0.0 ? 1.0 / (s * s * s) : 1.0;  // ρ/ρ₀, mass conserved
        const ns::material::MaterialLib scaled = materials.with_density_scale(density_scale);
        ns::ref::RefTransport transport(geom, scaled, xs, seed);
        return run_eigen(transport, spec);
    };
}

}  // namespace ns::physics

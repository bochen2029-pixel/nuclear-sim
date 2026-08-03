// α-mode burst coupling loop + streaming tally (02 §3 / 01 §4-6) — M3-T3-b core.

#include "physics/couple/couple.h"

#include "physics/hydro/tier1.h"
#include "physics/kinetics/kinetics.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

// Initiator source term S_n (C-051): neutrons injected during the fire pulse.
double initiator_source(const CoupleConfig& cfg, double t_s, double lambda_s) {
    if (cfg.initiator_rate_n_per_s <= 0.0) return 0.0;
    if (t_s >= cfg.initiator_t_fire_s &&
        t_s < cfg.initiator_t_fire_s + cfg.initiator_pulse_width_s) {
        return cfg.initiator_rate_n_per_s * lambda_s;  // neutrons in this Λ window
    }
    return 0.0;
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

// ---------------------------------------------------------------------------
// run_burst — the 02 §3 operator-split loop.
// ---------------------------------------------------------------------------

BurstReport run_burst(const CoupleConfig& cfg, const BurstContext& ctx,
                      const ns::geom::LayerStack& geom0, const EigenFn& eigen_fn,
                      TallySink& sink) {
    sink.on_begin(ctx);

    const Tier1Compression comp{cfg.compression_ratio, cfg.compression_t0_s, cfg.compression_t_c_s};
    ns::geom::LayerStack geom = geometry_at(geom0, comp, 0.0);

    // Initial eigen (02 §3): k_eff, Λ, β_eff, source at t = 0.
    EigenResult er = eigen_fn(geom);
    int eigen_calls = 1;
    double k_p = er.k_prompt();
    double nu = nu_eff(er.source);
    double lambda = er.lambda_s > 0.0 ? er.lambda_s : cfg.lambda_s_initial;
    double lambda_at_refresh = lambda;
    double rho_at_refresh = comp.density_ratio(comp.s_of(0.0));
    double last_k = er.k;

    BurstAccumulator acc(cfg.n0);
    double t = 0.0;
    double log_f_peak = kNegInf;
    bool supercritical = false;
    bool quenched = false;
    int n = 0;
    int gens_since_refresh = 0;
    double max_q = 0.0;
    int refresh_gens = std::max(1, cfg.eigen_refresh_gens);
    double marked_radius = geom.outermost_radius();

    while (n < cfg.max_generations) {
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
            rho_at_refresh = comp.density_ratio(comp.s_of(t));
            marked_radius = cur_radius;
            gens_since_refresh = 0;
            refreshed = true;
        } else if (n > 0) {
            // E3b: hold Λ, rescale by the density ratio since the last refresh (Λ ∝ 1/ρ).
            const double rho_now = comp.density_ratio(comp.s_of(t));
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
        geom = geometry_at(geom0, comp, t);

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

#include "ref/ref_transport.h"

#include "core/constants/constants.h"
#include "core/diagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns::ref {
namespace {

// E1e (01 §2): C-902.
constexpr double kWeightMin = 1e-4;   // w_min
constexpr double kWeightSurv = 1e-2;  // w_surv

constexpr double kBarnToCm2 = 1e-24;

/// Isotropic direction in the lab frame.
geom::Vec3 sample_isotropic(rng::Stream& stream) {
    const double mu = 2.0 * stream.uniform_d() - 1.0;
    const double phi = 2.0 * 3.14159265358979323846 * stream.uniform_d();
    const double sin_theta = std::sqrt(std::max(0.0, 1.0 - mu * mu));
    return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), mu};
}

}  // namespace

// ---------------------------------------------------------------------------
// TallyAcc
// ---------------------------------------------------------------------------

void TallyAcc::resize_isotopes(std::size_t count) {
    fissions_by_isotope_.assign(count, 0.0);
    production_by_isotope_.assign(count, 0.0);
}

void TallyAcc::resize_layers(std::size_t count) { flux_by_layer_.assign(count, 0.0); }

void TallyAcc::begin_history(double source_weight) {
    source_weight_ += source_weight;
    history_leaked_ = 0.0;
    history_production_ = 0.0;
}

void TallyAcc::end_history() {
    ++histories_;
    sum_leak_ += history_leaked_;
    sum_leak_sq_ += history_leaked_ * history_leaked_;
    sum_prod_ += history_production_;
    sum_prod_sq_ += history_production_ * history_production_;
}

void TallyAcc::add_leaked(double weight) { history_leaked_ += weight; }

void TallyAcc::add_fission_production(double weight, int isotope_index) {
    history_production_ += weight;
    if (isotope_index >= 0 && static_cast<std::size_t>(isotope_index) < production_by_isotope_.size()) {
        production_by_isotope_[static_cast<std::size_t>(isotope_index)] += weight;
    }
}

void TallyAcc::add_fission_events(double weight, int isotope_index) {
    if (isotope_index >= 0 && static_cast<std::size_t>(isotope_index) < fissions_by_isotope_.size()) {
        fissions_by_isotope_[static_cast<std::size_t>(isotope_index)] += weight;
    }
}

void TallyAcc::add_track_length(double weight_times_distance, int layer) {
    if (layer >= 0 && static_cast<std::size_t>(layer) < flux_by_layer_.size()) {
        flux_by_layer_[static_cast<std::size_t>(layer)] += weight_times_distance;
    }
}

double TallyAcc::standard_error(double sum, double sum_sq, std::int64_t n) {
    if (n < 2) {
        return std::numeric_limits<double>::infinity();
    }
    const double count = static_cast<double>(n);
    const double mean = sum / count;
    // Sample variance of the per-history scores, then the standard error of
    // their mean. Clamped at zero because round-off can make an exactly-zero
    // variance come out slightly negative.
    const double variance = std::max(0.0, (sum_sq / count) - mean * mean) * count / (count - 1.0);
    return std::sqrt(variance / count);
}

double TallyAcc::leaked_fraction() const {
    return source_weight_ > 0.0 ? sum_leak_ / source_weight_ : 0.0;
}

double TallyAcc::leaked_fraction_sigma() const {
    return standard_error(sum_leak_, sum_leak_sq_, histories_);
}

double TallyAcc::k_estimate() const {
    return source_weight_ > 0.0 ? sum_prod_ / source_weight_ : 0.0;
}

double TallyAcc::k_sigma() const { return standard_error(sum_prod_, sum_prod_sq_, histories_); }

// ---------------------------------------------------------------------------
// RefTransport
// ---------------------------------------------------------------------------

RefTransport::RefTransport(const geom::LayerStack& stack, const material::MaterialLib& materials,
                           const xs::FewGroupXS& xs_set, std::uint64_t seed)
    : stack_(stack), tracker_(stack), xs_(xs_set), seed_(seed) {
    layers_.resize(static_cast<std::size_t>(stack.size()));

    for (int index = 0; index < stack.size(); ++index) {
        const auto& layer = stack.layer(index);
        require(layer.material_id >= 0
                    && static_cast<std::size_t>(layer.material_id) < materials.all().size(),
                "<RefTransport>", "layers[" + std::to_string(index) + "].material_id",
                "does not index a loaded material");
        const auto& mat = materials.all()[static_cast<std::size_t>(layer.material_id)];

        LayerData data;
        for (std::size_t c = 0; c < mat.fracs.size(); ++c) {
            const auto& constituent = mat.fracs[c];
            if (constituent.iso == nullptr) {
                continue;  // mass only; no cross sections in this set
            }
            IsotopeSlot slot;
            slot.iso = constituent.iso;
            slot.number_density = mat.number_density(c) * kBarnToCm2;

            const auto found = std::find(isotope_names_.begin(), isotope_names_.end(),
                                         constituent.species);
            if (found == isotope_names_.end()) {
                slot.global_index = static_cast<int>(isotope_names_.size());
                isotope_names_.push_back(constituent.species);
            } else {
                slot.global_index = static_cast<int>(found - isotope_names_.begin());
            }
            data.isotopes.push_back(slot);
        }

        for (std::size_t g = 0; g < xs::kGroupsN; ++g) {
            double sigma_t = 0.0, sigma_tr = 0.0;
            for (const auto& slot : data.isotopes) {
                const auto& gd = slot.iso->g[g];
                sigma_t += slot.number_density * gd.sigma_t();
                sigma_tr += slot.number_density * gd.sigma_tr();
            }
            data.sigma_t[g] = sigma_t;
            data.sigma_tr[g] = sigma_tr;
        }
        layers_[static_cast<std::size_t>(index)] = std::move(data);
    }
}

double RefTransport::analytic_k_inf(const material::Material& mat, const xs::FewGroupXS& xs_set,
                                    int group) {
    // nu*Sigma_f / (Sigma_c + Sigma_f), all macroscopic, in one group.
    //
    // Deliberately computed from the material and cross sections directly,
    // sharing no code with the transport loop — an oracle that reuses the thing
    // it checks proves only self-consistency (11 §5).
    const auto g = static_cast<std::size_t>(group);
    double nu_sigma_f = 0.0, sigma_f = 0.0, sigma_c = 0.0;
    for (std::size_t c = 0; c < mat.fracs.size(); ++c) {
        const auto& constituent = mat.fracs[c];
        if (constituent.iso == nullptr) {
            continue;
        }
        const double n = mat.number_density(c) * kBarnToCm2;
        const auto& gd = constituent.iso->g[g];
        nu_sigma_f += n * gd.nu * gd.sigma_f;
        sigma_f += n * gd.sigma_f;
        sigma_c += n * gd.sigma_c;
    }
    (void)xs_set;
    return (sigma_c + sigma_f) > 0.0 ? nu_sigma_f / (sigma_c + sigma_f) : 0.0;
}

void RefTransport::run_fixed_source(const SourceSpec& spec, TallyAcc& tally) {
    tally.resize_isotopes(isotope_names_.size());
    tally.resize_layers(layers_.size());

    // Birth spectrum, normalized here rather than trusting the caller.
    std::array<double, xs::kGroups> cdf{};
    double total = 0.0;
    for (std::size_t g = 0; g < xs::kGroupsN; ++g) {
        total += spec.group_weights[g];
        cdf[g] = total;
    }
    require(total > 0.0, "<SourceSpec>", "group_weights", "must not sum to zero");

    for (std::int64_t history = 0; history < spec.histories; ++history) {
        // One stream per history, indexed by history number: the sequence a
        // history sees depends on its own identity, never on execution order
        // (BLK-11/E2 — the same rule fork() enforces for progeny).
        rng::Stream stream(seed_, consts::rng_stream_registry::source,
                           static_cast<std::uint64_t>(history));

        // Aggregate-initialized rather than default-constructed: rng::Stream has
        // no default constructor by design (04 §2 makes seed and stream id
        // required), because a default-seeded stream is a determinism hazard.
        Particle p{geom::Vec3{}, geom::Vec3{}, 0, 1.0, stream};

        // Source position.
        switch (spec.kind) {
            case SourceSpec::Kind::PointIsotropic:
                p.pos = spec.center;
                break;
            case SourceSpec::Kind::UniformSphere:
            case SourceSpec::Kind::UniformShell: {
                // r^3 uniform between the bounds gives a uniform volume density.
                const double a = spec.r_inner * spec.r_inner * spec.r_inner;
                const double b = spec.r_outer * spec.r_outer * spec.r_outer;
                const double r = std::cbrt(a + (b - a) * p.stream.uniform_d());
                p.pos = spec.center + sample_isotropic(p.stream) * r;
                break;
            }
            case SourceSpec::Kind::FissionSourceReplay:
                // Needs a stored fission bank, which the eigen solver produces.
                require(false, "<SourceSpec>", "kind",
                        "FissionSourceReplay needs a fission bank from the eigen solver (M1-T3)");
                break;
        }

        p.dir = sample_isotropic(p.stream);

        const double xi_group = p.stream.uniform_d() * total;
        p.group = xs::kGroups - 1;
        for (std::size_t g = 0; g < xs::kGroupsN; ++g) {
            if (xi_group <= cdf[g]) {
                p.group = static_cast<int>(g);
                break;
            }
        }

        tally.begin_history(1.0);
        int layer = tracker_.locate(p.pos);

        while (true) {
            if (layer == geom::kOutside) {
                tally.add_leaked(p.weight);  // E1d
                break;
            }
            const auto& data = layers_[static_cast<std::size_t>(layer)];
            const std::size_t g = static_cast<std::size_t>(p.group);

            const double sigma_tr = data.sigma_tr[g];
            if (sigma_tr <= 0.0) {
                // A void: stream straight to the boundary rather than sampling
                // an infinite flight.
                const double to_boundary = tracker_.distance_to_boundary(p.pos, p.dir, layer);
                if (!std::isfinite(to_boundary)) {
                    tally.add_leaked(p.weight);
                    break;
                }
                p.pos = p.pos + p.dir * to_boundary;
                layer = tracker_.nudge_and_locate(p.pos, p.dir);
                continue;
            }

            // E1a — free flight on the TRANSPORT-CORRECTED total.
            const double flight = -std::log(p.stream.uniform_d()) / sigma_tr;
            const double to_boundary = tracker_.distance_to_boundary(p.pos, p.dir, layer);

            if (flight > to_boundary) {
                // E1b — advance to the surface, re-locate, resample.
                if (!std::isfinite(to_boundary)) {
                    tally.add_leaked(p.weight);
                    break;
                }
                tally.add_track_length(p.weight * to_boundary, layer);
                p.pos = p.pos + p.dir * to_boundary;
                layer = tracker_.nudge_and_locate(p.pos, p.dir);
                continue;
            }

            tally.add_track_length(p.weight * flight, layer);
            p.pos = p.pos + p.dir * flight;

            // E1c — collision on the TRANSPORT-CORRECTED medium (ADR-021). The flight
            // used sigma_tr, so the collision must too, or fission/absorption get scaled
            // by sigma_tr/sigma_t relative to the collision density. Select the isotope
            // prop. to n_i*sigma_tr,i and split reactions on sigma_tr,i, with the scatter
            // share REDUCED to sigma_s,tr = sigma_s - mu*sigma_s = sigma_s - (sigma_t -
            // sigma_tr) (the forward-scattered part is folded into the longer sigma_tr
            // flight). For an isotropic set (mu_bar = 0 => sigma_tr = sigma_t) this is
            // byte-identical to the analog split, so mu=0 sets are unchanged.
            double pick = p.stream.uniform_d() * sigma_tr;
            const IsotopeSlot* chosen = &data.isotopes.front();
            for (const auto& slot : data.isotopes) {
                pick -= slot.number_density * slot.iso->g[g].sigma_tr();
                if (pick <= 0.0) {
                    chosen = &slot;
                    break;
                }
            }

            const auto& gd = chosen->iso->g[g];
            const double sigma_tr_i = gd.sigma_tr();
            if (sigma_tr_i <= 0.0) {
                break;  // transparent isotope; nothing to score
            }

            // Fission production, weight-weighted and EXPECTED rather than
            // sampled. In fixed-source mode the progeny are not propagated —
            // propagating them is a fission-source iteration, i.e. the eigen
            // solver (M1-T3) — so what accumulates here is production per
            // source neutron, which is k_inf when there is no leakage.
            const double fission_share = gd.sigma_f / sigma_tr_i;
            tally.add_fission_events(p.weight * fission_share, chosen->global_index);
            tally.add_fission_production(p.weight * gd.nu * fission_share, chosen->global_index);

            // Implicit capture: survive as the transport-reduced scatter share. The
            // neutron is NOT killed at fission, and sigma_c never terminates a
            // history directly (E1c, BLK-05).
            p.weight *= (gd.sigma_s - (gd.sigma_t() - sigma_tr_i)) / sigma_tr_i;
            if (p.weight <= 0.0) {
                break;
            }

            // E1e — Russian roulette. Unbiased: the survivor's weight is
            // w_surv and it survives with probability w/w_surv.
            if (p.weight < kWeightMin) {
                if (p.stream.uniform_d() > p.weight / kWeightSurv) {
                    break;
                }
                p.weight = kWeightSurv;
            }

            // Scatter: isotropic in the lab, with the group change drawn from
            // transfer[from][.]. A null transfer means the set is SIM-tagged and
            // group-preserving.
            p.dir = sample_isotropic(p.stream);
            if (!chosen->iso->transfer_is_null) {
                double xi = p.stream.uniform_d();
                int to_group = p.group;
                for (std::size_t to = 0; to < xs::kGroupsN; ++to) {
                    xi -= chosen->iso->transfer[g][to];
                    if (xi <= 0.0) {
                        to_group = static_cast<int>(to);
                        break;
                    }
                }
                p.group = to_group;
            }
        }

        tally.end_history();
    }
}

}  // namespace ns::ref

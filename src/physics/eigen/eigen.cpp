#include "physics/eigen/eigen.h"

#include "core/constants/constants.h"
#include "core/diagnostics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ns::physics {
namespace {

/// Entropy window half-width, W = 5 generations (E2b).
constexpr int kWindow = 5;

/// Delayed fraction for a species, from the generated constants (C-022…C-022e).
///
/// Falls back to zero for a species with no published beta rather than to some
/// nominal: a wrong beta silently biases k_prompt, whereas a zero is visible in
/// beta_eff and cannot be mistaken for data.
double beta_of(const std::string& species) {
    if (species == "Pu239") return consts::beta_pu239;
    if (species == "U235") return consts::beta_u235;
    if (species == "U238") return consts::beta_u238;
    if (species == "Pu240") return consts::beta_pu240;
    if (species == "Pu241") return consts::beta_pu241;
    return 0.0;
}

double mean(const std::vector<double>& v, std::size_t first, std::size_t count) {
    if (count == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = first; i < first + count && i < v.size(); ++i) {
        sum += v[i];
    }
    return sum / static_cast<double>(count);
}

/// Standard error of the mean of `v`.
double standard_error(const std::vector<double>& v) {
    const std::size_t n = v.size();
    if (n < 2) {
        return 0.0;
    }
    const double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(n);
    double ss = 0.0;
    for (const double x : v) {
        ss += (x - m) * (x - m);
    }
    return std::sqrt(ss / (static_cast<double>(n) - 1.0) / static_cast<double>(n));
}

/// Batched-means standard error over `batches` equal blocks (01 §3).
double batched_means_error(const std::vector<double>& v, std::size_t batches) {
    if (v.size() < batches * 2) {
        return 0.0;
    }
    const std::size_t per = v.size() / batches;
    std::vector<double> means;
    means.reserve(batches);
    for (std::size_t b = 0; b < batches; ++b) {
        means.push_back(mean(v, b * per, per));
    }
    return standard_error(means);
}

}  // namespace

double shannon_entropy(const std::vector<double>& mesh) {
    const double total = std::accumulate(mesh.begin(), mesh.end(), 0.0);
    if (total <= 0.0) {
        return 0.0;
    }
    double h = 0.0;
    for (const double count : mesh) {
        if (count > 0.0) {
            const double p = count / total;
            h -= p * std::log(p);
        }
    }
    return h;
}

EigenResult run_eigen(ref::RefTransport& transport, const EigenSpec& spec) {
    require(spec.batch > 0, "<EigenSpec>", "batch", "must be positive");
    require(spec.inactive >= 1, "<EigenSpec>", "inactive",
            "must be at least 1: inactive cycles are what let the fission source converge "
            "before tallying (05 §2)");
    require(spec.active >= 2, "<EigenSpec>", "active",
            "must be at least 2 for a standard error to exist");

    EigenResult result;
    auto bank = transport.initial_bank(spec.batch, spec.bad_initial_source, spec.seed);

    double k = spec.k0;
    std::vector<double> active_k;
    std::vector<double> entropy;
    ref::GenerationResult generation;

    std::vector<double> production_by_isotope;
    double time_weight_total = 0.0;
    double source_weight_total = 0.0;

    const int total_generations = spec.inactive + spec.active;
    for (int gen = 0; gen < total_generations; ++gen) {
        if (bank.empty()) {
            // A subcritical system can starve the bank. Reseeding would silently
            // change what is being measured, so stop and report instead.
            break;
        }
        transport.run_generation(bank, k, static_cast<std::uint64_t>(gen), generation);

        // E2a: k_{i+1} = F_{i+1} / (F_i / k_i). The bank was produced by
        // dividing by k_i, so its size IS F_i/k_i.
        const double previous_bank = static_cast<double>(bank.size());
        const double k_new = generation.production / previous_bank;

        entropy.push_back(shannon_entropy(generation.source.mesh));
        result.h_history.push_back(entropy.back());
        result.k_history.push_back(k_new);

        const bool inactive_phase = gen < result.inactive_used
                                    || (!result.converged && gen < spec.inactive);
        if (!result.converged && gen + 1 >= 2 * kWindow && gen + 1 >= spec.min_inactive) {
            const std::size_t n = entropy.size();
            const double recent = mean(entropy, n - kWindow, kWindow);
            const double preceding = mean(entropy, n - 2 * kWindow, kWindow);
            if (std::abs(recent - preceding) < spec.h_tol) {
                result.converged = true;
                result.inactive_used = gen + 1;
            }
        }

        if (!inactive_phase && (result.converged || gen >= spec.inactive)) {
            active_k.push_back(k_new);
            time_weight_total += generation.time_weight;
            source_weight_total += generation.source_weight;
            if (production_by_isotope.empty()) {
                production_by_isotope.assign(generation.source.by_isotope.size(), 0.0);
            }
            for (std::size_t i = 0; i < generation.source.by_isotope.size(); ++i) {
                production_by_isotope[i] += generation.source.by_isotope[i];
            }
        }

        k = k_new;
        bank = generation.source.sites;
    }

    if (!result.converged) {
        result.inactive_used = spec.inactive;
    }
    result.source = generation.source;

    if (!active_k.empty()) {
        result.k = std::accumulate(active_k.begin(), active_k.end(), 0.0)
                   / static_cast<double>(active_k.size());
        // The gate uses the LARGER of the two estimates: the active-cycle
        // standard error ignores inter-cycle correlation and is therefore
        // optimistic (MAJ-32).
        const double cycle_se = standard_error(active_k);
        const double batched_se = batched_means_error(active_k, 10);
        result.sigma_pcm = std::max(cycle_se, batched_se) * 1e5;
    }

    // beta_eff = Sum_i beta_i S_i / Sum_i S_i, source-weighted (ADR-013).
    // v1 omits the delayed-neutron importance factor — a few percent in fast
    // metal systems — which is a recorded approximation, not an oversight.
    double beta_numerator = 0.0, source_total = 0.0;
    const auto& names = transport.isotope_names();
    for (std::size_t i = 0; i < production_by_isotope.size() && i < names.size(); ++i) {
        beta_numerator += beta_of(names[i]) * production_by_isotope[i];
        source_total += production_by_isotope[i];
    }
    result.beta_eff = source_total > 0.0 ? beta_numerator / source_total : 0.0;

    // Lambda = l/k, with l the track-length-estimated mean prompt lifetime
    // (01 §3, BLK-04). Lambda scales as 1/rho, which is why it MUST come from
    // the eigen run rather than being held constant across compression.
    result.lifetime_s = source_weight_total > 0.0 ? time_weight_total / source_weight_total : 0.0;
    result.lambda_s = result.k > 0.0 ? result.lifetime_s / result.k : 0.0;
    result.alpha_per_s = result.lambda_s > 0.0 ? (result.k - 1.0) / result.lambda_s : 0.0;

    return result;
}

}  // namespace ns::physics

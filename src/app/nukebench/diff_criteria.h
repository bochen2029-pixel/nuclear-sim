// G0c cross-backend differential criteria (08 §2 b/c) — the per-shell fission-source and
// population-series equivalence tests that `nukebench diff` applies on top of the k-equivalence
// (criterion a). Pure functions over already-extracted series (a per-generation k sequence, a
// per-shell fission histogram) so they are backend-agnostic and CI-verifiable without a GPU:
// the ref and gpu eigens each produce the series, these compare them.
//
// (a) is in gate_report.cpp (run_diff). (b)/(c) added M1-T5-c-5.

#pragma once

#include <cstddef>
#include <vector>

namespace ns::nukebench {

/// Cumulative log10 population from a per-generation k sequence: log10 N(n) = Sum_{i<=n} log10 k(i),
/// relative to N(0) = 1. One entry per input generation (element n-1 is log10 N(n)). Empty in -> empty.
std::vector<double> population_series(const std::vector<double>& k_seq);

/// Result of a series/shell equivalence check: the worst |value - bound_slack| ratio observed
/// (<= 1 means every element passed), the index where it occurred, and the element count compared.
struct DiffCheck {
    bool pass = true;
    double worst_ratio = 0.0;  // max over elements of |delta| / bound; <= 1.0 iff pass
    std::size_t worst_index = 0;
    std::size_t n = 0;         // number of elements compared
};

/// G0c criterion (c) — population-series statistical equivalence (08 §2):
///   |log10 N_ref(n) - log10 N_gpu(n)| <= 3 * n * sigma_k / (k * ln10)   for all n.
/// `k_ref`/`k_gpu` are the per-active-generation k sequences (compared over their common length);
/// `k` is the mean k (the bound's dk -> d(log10 N) conversion, 1/(k*ln10)). `sigma_k` is the
/// combined PER-GENERATION spread of k (sqrt(std(k_ref)^2 + std(k_gpu)^2)) — NOT the standard error
/// of the mean. This matters: the population difference is a random walk (independent-RNG backends)
/// growing as sqrt(n)*sigma_step; the LINEAR-in-n envelope 3*n*sigma_k bounds that walk (and the
/// worst excursion) for all n only when sigma_k is the per-gen spread, while still catching a
/// SYSTEMATIC cross-backend bias Delta_k > ~3*sigma_k (which accumulates linearly). With the SE of
/// the mean (sqrt(n) smaller) the bound is tighter than 3-sigma at intermediate n and a genuinely
/// equivalent pair fails — batch-independently. Requires n >= 1, k > 0, sigma_k > 0.
DiffCheck population_series_equivalence(const std::vector<double>& k_ref,
                                        const std::vector<double>& k_gpu, double sigma_k, double k);

/// G0c criterion (b) — per-shell fission-source equivalence (08 §2):
///   |f_ref[s] - f_gpu[s]| <= max( 3 * sqrt(sig_ref[s]^2 + sig_gpu[s]^2), 0.02 * f_ref[s] )  per shell.
/// `f_ref`/`f_gpu` are the per-shell fission source (same shell count, compared over the common
/// length). `sig_ref`/`sig_gpu` are the per-shell 1-sigma (pass empty for none -> the 2% relative
/// bound governs). Shells with f_ref == 0 pass iff f_gpu is also within the (then sigma-only) bound.
DiffCheck per_shell_equivalence(const std::vector<double>& f_ref, const std::vector<double>& f_gpu,
                                const std::vector<double>& sig_ref = {},
                                const std::vector<double>& sig_gpu = {});

/// Bin fission-site radii into `n_shells` equal-width radial shells over [0, r_max], one unit of
/// weight per site -> a per-shell fission-source histogram (the input to per_shell_equivalence).
/// Sites at exactly r_max land in the last shell; sites beyond r_max are clamped into it. n_shells>=1.
std::vector<double> radial_shell_histogram(const std::vector<double>& radii, double r_max,
                                           int n_shells);

}  // namespace ns::nukebench

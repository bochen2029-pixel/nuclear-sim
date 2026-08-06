#include "app/nukebench/diff_criteria.h"

#include <algorithm>
#include <cmath>

namespace ns::nukebench {

std::vector<double> population_series(const std::vector<double>& k_seq) {
    std::vector<double> out;
    out.reserve(k_seq.size());
    double cum = 0.0;
    for (const double k : k_seq) {
        cum += std::log10(k);  // k > 0 for any real eigen; log10 N(n) = cumulative sum
        out.push_back(cum);
    }
    return out;
}

DiffCheck population_series_equivalence(const std::vector<double>& k_ref,
                                        const std::vector<double>& k_gpu, double sigma_k, double k) {
    DiffCheck r;
    const std::size_t n = std::min(k_ref.size(), k_gpu.size());
    r.n = n;
    if (n == 0 || k <= 0.0 || sigma_k <= 0.0) {
        r.pass = (n == 0);  // no data compares as trivially equal; bad args cannot pass a real series
        return r;
    }
    const std::vector<double> pop_ref = population_series(k_ref);
    const std::vector<double> pop_gpu = population_series(k_gpu);
    const double ln10 = std::log(10.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double gen = static_cast<double>(i + 1);          // n = 1 .. N
        const double bound = 3.0 * gen * sigma_k / (k * ln10);  // 08 §2 (c)
        const double delta = std::abs(pop_ref[i] - pop_gpu[i]);
        const double ratio = bound > 0.0 ? delta / bound : (delta > 0.0 ? 1.0e9 : 0.0);
        if (ratio > r.worst_ratio) {
            r.worst_ratio = ratio;
            r.worst_index = i;
        }
        if (delta > bound) r.pass = false;
    }
    return r;
}

DiffCheck per_shell_equivalence(const std::vector<double>& f_ref, const std::vector<double>& f_gpu,
                                const std::vector<double>& sig_ref,
                                const std::vector<double>& sig_gpu) {
    DiffCheck r;
    const std::size_t n = std::min(f_ref.size(), f_gpu.size());
    r.n = n;
    for (std::size_t s = 0; s < n; ++s) {
        const double sr = s < sig_ref.size() ? sig_ref[s] : 0.0;
        const double sg = s < sig_gpu.size() ? sig_gpu[s] : 0.0;
        const double sig_bound = 3.0 * std::sqrt(sr * sr + sg * sg);
        const double rel_bound = 0.02 * std::abs(f_ref[s]);      // 08 §2 (b): max(3sigma, 2% f_ref)
        const double bound = std::max(sig_bound, rel_bound);
        const double delta = std::abs(f_ref[s] - f_gpu[s]);
        const double ratio = bound > 0.0 ? delta / bound : (delta > 0.0 ? 1.0e9 : 0.0);
        if (ratio > r.worst_ratio) {
            r.worst_ratio = ratio;
            r.worst_index = s;
        }
        if (delta > bound) r.pass = false;
    }
    return r;
}

std::vector<double> radial_shell_histogram(const std::vector<double>& radii, double r_max,
                                           int n_shells) {
    if (n_shells < 1) n_shells = 1;
    std::vector<double> hist(static_cast<std::size_t>(n_shells), 0.0);
    if (r_max <= 0.0) return hist;
    const double width = r_max / static_cast<double>(n_shells);
    for (const double rr : radii) {
        int s = static_cast<int>(rr / width);
        if (s < 0) s = 0;
        if (s >= n_shells) s = n_shells - 1;  // clamp r >= r_max into the outermost shell
        hist[static_cast<std::size_t>(s)] += 1.0;
    }
    return hist;
}

}  // namespace ns::nukebench

// The built-in space-filling samplers grid / lhs / random + make_sampler
// (06 §2) — M5-T3-a.
//
// All three are Coverage samplers: they enumerate a deterministic set of points
// planned up front and ignore report() (only an adaptive sampler — MCTS, M5-T4 —
// uses the reported tally). Determinism: grid is a fixed lattice; lhs/random draw
// from an ns::rng::Philox stream seeded by a stable hash of the manifest name, so
// a given sweep.toml always yields the same points.

#include "app/nukefarm/sweep.h"

#include "core/rng/rng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ns::nukefarm {

namespace {

// A stable, reproducible per-sweep seed from the manifest name (FNV-1a 64) —
// std::hash is not portable/stable across runs, and the samplers must reproduce.
std::uint64_t name_seed(const std::string& name) {
    std::uint64_t h = 1469598103934665603ull;  // FNV offset basis
    for (unsigned char c : name) {
        h ^= c;
        h *= 1099511628211ull;  // FNV prime
    }
    return h;
}

// Map a unit coordinate in [0, 1] onto an axis range.
double axis_value(const SweepAxis& ax, double unit) {
    return ax.lo + unit * (ax.hi - ax.lo);
}

// The space-filling built-ins: hand out a precomputed list of points.
class PlannedSampler : public Sampler {
public:
    explicit PlannedSampler(std::vector<ParamSet> points) : points_(std::move(points)) {}

    std::optional<ParamSet> next() override {
        if (cursor_ >= points_.size()) return std::nullopt;
        return points_[cursor_++];
    }
    void report(const ParamSet&, const ns::physics::TallyResult&) override {}
    ScoreKind score_kind() const override { return ScoreKind::Coverage; }
    bool supports_categorical() const override { return false; }

private:
    std::vector<ParamSet> points_;
    std::size_t cursor_ = 0;
};

// grid: floor(budget_runs^(1/n)) evenly-spaced points per axis (>= 1), Cartesian
// product. Deriving the per-axis count from budget_runs keeps the total <= budget
// without adding a schema field (SYNC-M5c).
std::vector<ParamSet> grid_points(const SweepManifest& m) {
    const std::size_t n = m.space.size();
    std::int64_t per = static_cast<std::int64_t>(
        std::floor(std::pow(static_cast<double>(m.budget_runs), 1.0 / static_cast<double>(n))));
    if (per < 1) per = 1;

    std::vector<std::vector<double>> axis_vals(n);
    for (std::size_t a = 0; a < n; ++a) {
        for (std::int64_t i = 0; i < per; ++i) {
            const double unit =
                (per == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(per - 1);
            axis_vals[a].push_back(axis_value(m.space[a], unit));
        }
    }

    std::vector<ParamSet> out;
    std::vector<std::size_t> idx(n, 0);
    for (;;) {
        ParamSet ps;
        ps.reserve(n);
        for (std::size_t a = 0; a < n; ++a) ps.push_back({m.space[a].param, axis_vals[a][idx[a]]});
        out.push_back(std::move(ps));

        // mixed-radix increment across the axes
        std::size_t a = 0;
        for (; a < n; ++a) {
            if (++idx[a] < axis_vals[a].size()) break;
            idx[a] = 0;
        }
        if (a == n) break;
    }
    return out;
}

// random: budget_runs points, each axis uniform in [lo, hi].
std::vector<ParamSet> random_points(const SweepManifest& m) {
    ns::rng::Stream rng(name_seed(m.name), 0);
    std::vector<ParamSet> out;
    out.reserve(static_cast<std::size_t>(m.budget_runs));
    for (std::int64_t r = 0; r < m.budget_runs; ++r) {
        ParamSet ps;
        ps.reserve(m.space.size());
        for (const auto& ax : m.space) ps.push_back({ax.param, axis_value(ax, rng.uniform_d())});
        out.push_back(std::move(ps));
    }
    return out;
}

// lhs: Latin Hypercube — N strata per axis, one sample per stratum (jittered),
// with a seeded per-axis permutation so the axes are independently paired.
std::vector<ParamSet> lhs_points(const SweepManifest& m) {
    const std::int64_t big_n = m.budget_runs;
    const std::size_t rows = static_cast<std::size_t>(big_n);
    const std::size_t n = m.space.size();
    ns::rng::Stream rng(name_seed(m.name), 0);

    std::vector<std::vector<std::size_t>> perm(n);
    for (std::size_t a = 0; a < n; ++a) {
        perm[a].resize(rows);
        std::iota(perm[a].begin(), perm[a].end(), std::size_t{0});
        // Fisher-Yates with the seeded stream.
        for (std::size_t i = perm[a].size(); i > 1; --i) {
            std::size_t j = static_cast<std::size_t>(rng.uniform_d() * static_cast<double>(i));
            if (j >= i) j = i - 1;  // guard the [0,1) upper edge against FP surprises
            std::swap(perm[a][i - 1], perm[a][j]);
        }
    }

    std::vector<ParamSet> out;
    out.reserve(rows);
    for (std::size_t r = 0; r < rows; ++r) {
        ParamSet ps;
        ps.reserve(n);
        for (std::size_t a = 0; a < n; ++a) {
            const double stratum = static_cast<double>(perm[a][r]);
            const double unit = (stratum + rng.uniform_d()) / static_cast<double>(big_n);
            ps.push_back({m.space[a].param, axis_value(m.space[a], unit)});
        }
        out.push_back(std::move(ps));
    }
    return out;
}

// mcts: PUCT over a k-d box tree (06 §2 "continuous axes node-discretized then
// refined"). Each node is a hyperrectangle; it bisects its longest axis into two
// child half-boxes, added by progressive widening. next() descends by PUCT
// (Q + c·sqrt(N_parent)/(1+N_child)) — expanding a new child or selecting the best
// — and returns the leaf box's CENTRE (deterministic + reproducible; refinement,
// not randomness, drives it toward the optimum). report() scores yield vs the
// calibrate band centre (ScoreKind::BandCenter) and backpropagates along the last
// path. NB assumes next()→report() alternation (the sweep run loop, and the tests,
// do exactly that).
class MctsSampler : public Sampler {
public:
    MctsSampler(std::vector<SweepAxis> axes, double center, double half_width)
        : axes_(std::move(axes)), center_(center), half_width_(half_width > 0.0 ? half_width : 1.0) {
        Node root;
        root.region.reserve(axes_.size());
        for (const auto& ax : axes_) root.region.push_back({ax.lo, ax.hi});
        nodes_.push_back(std::move(root));
    }

    std::optional<ParamSet> next() override {
        last_path_.clear();
        int node = 0;
        int depth = 0;
        for (;;) {
            last_path_.push_back(node);
            const std::size_t nchildren = nodes_[node].children.size();
            if (depth < kMaxDepth && nchildren < allowed_children(nodes_[node].n) &&
                splittable(nodes_[node].region)) {
                const int child = add_child(node);  // expand
                last_path_.push_back(child);
                return center_point(nodes_[child].region);
            }
            if (nodes_[node].children.empty()) {  // leaf at max depth / unsplittable
                return center_point(nodes_[node].region);
            }
            node = best_child(node);  // select
            ++depth;
        }
    }

    void report(const ParamSet&, const ns::physics::TallyResult& tally) override {
        const double score = -std::abs(tally.yield_kt - center_) / half_width_;
        for (int idx : last_path_) {
            nodes_[idx].n += 1.0;
            nodes_[idx].w += score;
        }
    }

    ScoreKind score_kind() const override { return ScoreKind::BandCenter; }
    bool supports_categorical() const override { return false; }

private:
    struct Node {
        std::vector<std::pair<double, double>> region;  // [lo, hi] per axis
        double n = 0.0;                                 // visits
        double w = 0.0;                                 // value sum (mean = w/n)
        std::vector<int> children;
        int split_axis = -1;
    };

    static constexpr int kMaxDepth = 24;
    static constexpr double kCPuct = 1.2;
    static constexpr double kPwK = 1.0;
    static constexpr double kPwAlpha = 0.5;

    // Progressive widening: 1 + floor(k·n^alpha), capped at 2 (binary k-d split).
    std::size_t allowed_children(double n) const {
        std::size_t c = static_cast<std::size_t>(1.0 + kPwK * std::pow(n, kPwAlpha));
        return c > 2 ? std::size_t{2} : c;
    }

    static bool splittable(const std::vector<std::pair<double, double>>& region) {
        for (const auto& r : region)
            if (r.second > r.first) return true;
        return false;
    }

    static int longest_axis(const std::vector<std::pair<double, double>>& region) {
        int best = 0;
        double span = -1.0;
        for (std::size_t i = 0; i < region.size(); ++i) {
            const double s = region[i].second - region[i].first;
            if (s > span) {
                span = s;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int add_child(int node) {
        if (nodes_[node].split_axis < 0) nodes_[node].split_axis = longest_axis(nodes_[node].region);
        const int ax = nodes_[node].split_axis;
        const double lo = nodes_[node].region[static_cast<std::size_t>(ax)].first;
        const double hi = nodes_[node].region[static_cast<std::size_t>(ax)].second;
        const double mid = 0.5 * (lo + hi);
        const bool lower_first = nodes_[node].children.empty();
        Node child;
        child.region = nodes_[node].region;  // copy before any reallocation
        child.region[static_cast<std::size_t>(ax)] = lower_first ? std::make_pair(lo, mid)
                                                                 : std::make_pair(mid, hi);
        const int idx = static_cast<int>(nodes_.size());
        nodes_.push_back(std::move(child));      // may reallocate nodes_
        nodes_[node].children.push_back(idx);    // fresh index lookup after the push
        return idx;
    }

    int best_child(int node) const {
        int best = nodes_[node].children.front();
        double best_score = -1e300;
        for (int c : nodes_[node].children) {
            const Node& ch = nodes_[static_cast<std::size_t>(c)];
            const double q = ch.n > 0.0 ? ch.w / ch.n : 0.0;
            const double u = kCPuct * std::sqrt(nodes_[node].n) / (1.0 + ch.n);
            if (q + u > best_score) {
                best_score = q + u;
                best = c;
            }
        }
        return best;
    }

    ParamSet center_point(const std::vector<std::pair<double, double>>& region) const {
        ParamSet ps;
        ps.reserve(axes_.size());
        for (std::size_t i = 0; i < axes_.size(); ++i)
            ps.push_back({axes_[i].param, 0.5 * (region[i].first + region[i].second)});
        return ps;
    }

    std::vector<SweepAxis> axes_;
    double center_;
    double half_width_;
    std::vector<Node> nodes_;
    std::vector<int> last_path_;
};

}  // namespace

std::unique_ptr<Sampler> make_sampler(const SweepManifest& m) {
    std::unique_ptr<Sampler> s;
    if (m.sampler == "grid") {
        s = std::make_unique<PlannedSampler>(grid_points(m));
    } else if (m.sampler == "random") {
        s = std::make_unique<PlannedSampler>(random_points(m));
    } else if (m.sampler == "lhs") {
        s = std::make_unique<PlannedSampler>(lhs_points(m));
    } else if (m.sampler == "mcts") {
        // BandCenter needs a band centre to target -> mcts requires a calibrate
        // objective with target_yield_kt. (Pedagogical axes are already loader-
        // rejected for any non-grid sampler, so mcts never sees one.)
        if (!m.objective.target_lo.has_value() || !m.objective.target_hi.has_value()) {
            throw SweepError(
                "sampler \"mcts\" requires a calibrate objective with target_yield_kt "
                "(its BandCenter scoring needs a band centre to target)");
        }
        const double lo = *m.objective.target_lo;
        const double hi = *m.objective.target_hi;
        s = std::make_unique<MctsSampler>(m.space, 0.5 * (lo + hi), 0.5 * (hi - lo));
    } else {
        throw SweepError("unknown sampler \"" + m.sampler + "\" (06 §2: grid | lhs | random | mcts)");
    }

    // 06 §2 load rule: a sampler declaring OptimizeExtremum is rejected. The
    // built-ins are all Coverage, so this guards a future plugin, never these.
    if (s->score_kind() == ScoreKind::OptimizeExtremum) {
        throw SweepError("sampler \"" + m.sampler +
                         "\" declares OptimizeExtremum — rejected at load (03 §7 / 06 §2)");
    }
    return s;
}

}  // namespace ns::nukefarm

// CPU reference transport — the permanent correctness oracle (D1/ADR-001).
//
// History-based, double precision throughout, clarity over speed. This exists
// only to be correct; src/gpu/ exists only to be fast, and G0c binds them.
//
// Implicit capture is normative (E1c, BLK-05 / ADR-012 item 1): the neutron is
// never killed at fission, and `sigma_c` never terminates a history directly.
// The analog-with-implicit-absorption variant was explicitly rejected — it
// double-counts Sigma_f/Sigma_t and shifts k by a few hundred pcm, which is
// small enough to still look plausible.

#pragma once

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/rng/rng.h"
#include "core/xs/xs.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ns::ref {

struct Particle {
    geom::Vec3 pos;
    geom::Vec3 dir;
    int group = 0;
    double weight = 1.0;
    rng::Stream stream;
};

/// One banked fission neutron. The eigen solver's bank is a vector of these.
struct FissionSite {
    geom::Vec3 pos;
    int group = 0;
    int isotope = -1;
    int layer = -1;
};

/// The fission source of one generation: sites plus the resolved breakdowns
/// 05 §1/§2 require (per-isotope S_i drives beta_eff and nu_bar_eff; the fixed
/// 8^3 mesh drives the Shannon-entropy convergence test).
struct FissionSource {
    std::vector<FissionSite> sites;
    std::vector<double> by_isotope;
    std::vector<double> by_layer;
    std::vector<double> mesh;  // 8*8*8 = 512, C-908
};

/// What one power-iteration generation produced.
struct GenerationResult {
    FissionSource source;
    double production = 0.0;      // sum of w*nu*sigma_f/sigma_t over collisions
    double source_weight = 0.0;
    double leaked_weight = 0.0;
    /// sum of w * path_length / v, i.e. the numerator of the prompt lifetime
    /// (01 §3's track-length time estimator).
    double time_weight = 0.0;
};

/// Mirrors 03 §4's [source] block (05 §1).
struct SourceSpec {
    enum class Kind { PointIsotropic, UniformSphere, UniformShell, FissionSourceReplay };

    Kind kind = Kind::PointIsotropic;
    geom::Vec3 center{0.0, 0.0, 0.0};
    double r_inner = 0.0;
    double r_outer = 0.0;
    std::array<double, xs::kGroups> group_weights{1.0, 0.0, 0.0, 0.0};  // normalized at use
    std::int64_t histories = 100000;
};

/// Accumulated tallies plus the per-history moments needed for sigma.
///
/// Statistics come from per-HISTORY scores, not per-collision ones: collisions
/// within a history are correlated, so treating them as independent samples
/// would understate sigma — and both M1-T2 DoD checks are stated in sigma.
class TallyAcc {
public:
    void begin_history(double source_weight);
    void end_history();

    void add_leaked(double weight);
    void add_fission_production(double weight, int isotope_index);
    void add_fission_events(double weight, int isotope_index);
    void add_track_length(double weight_times_distance, int layer);

    void resize_isotopes(std::size_t count);
    void resize_layers(std::size_t count);

    std::int64_t histories() const noexcept { return histories_; }
    double source_weight() const noexcept { return source_weight_; }

    /// Leaked weight per unit source weight, and the standard error of that mean.
    double leaked_fraction() const;
    double leaked_fraction_sigma() const;

    /// Fission neutrons produced per source neutron. In a medium with no
    /// leakage this IS k_inf (01 §2).
    double k_estimate() const;
    double k_sigma() const;

    const std::vector<double>& fissions_by_isotope() const noexcept { return fissions_by_isotope_; }
    const std::vector<double>& production_by_isotope() const noexcept {
        return production_by_isotope_;
    }
    const std::vector<double>& track_length_by_layer() const noexcept { return flux_by_layer_; }

private:
    static double standard_error(double sum, double sum_sq, std::int64_t n);

    std::int64_t histories_ = 0;
    double source_weight_ = 0.0;

    double history_leaked_ = 0.0;
    double history_production_ = 0.0;

    double sum_leak_ = 0.0, sum_leak_sq_ = 0.0;
    double sum_prod_ = 0.0, sum_prod_sq_ = 0.0;

    std::vector<double> fissions_by_isotope_;
    std::vector<double> production_by_isotope_;
    std::vector<double> flux_by_layer_;
};

class RefTransport {
public:
    RefTransport(const geom::LayerStack& stack, const material::MaterialLib& materials,
                 const xs::FewGroupXS& xs_set, std::uint64_t seed);

    /// Transports `spec.histories` source neutrons and accumulates into `tally`.
    ///
    /// Fission progeny are TALLIED but not propagated: propagating them is a
    /// fission-source iteration, i.e. the eigen solver (M1-T3). What this
    /// measures is production per source neutron, which in a leakage-free
    /// medium is exactly k_inf.
    void run_fixed_source(const SourceSpec& spec, TallyAcc& tally);

    /// Transports one generation from `bank` and produces the next one.
    ///
    /// Progeny are banked as floor(w*nu_i*(sigma_f,i/sigma_t,i)/k_gen + xi)
    /// sites at the collision point (E1c). Dividing by the running k is what
    /// keeps the bank population stable across generations.
    void run_generation(const std::vector<FissionSite>& bank, double k_gen,
                        std::uint64_t generation, GenerationResult& out);

    /// An initial bank for the power iteration.
    /// `concentrated` puts every site at the origin — 05 §2's deliberately bad
    /// source, which must take measurably longer to converge than a uniform one.
    std::vector<FissionSite> initial_bank(std::int64_t count, bool concentrated,
                                          std::uint64_t seed) const;

    /// Neutron speed for a group, cm/s.
    ///
    /// Derived by scaling C-031 (1.4e9 cm/s at ~1 MeV) as v ∝ sqrt(E), with E
    /// the geometric-mean energy of the group. Anchoring on the existing
    /// constant avoids introducing a neutron mass the appendix does not carry,
    /// and makes the 1 MeV group reproduce C-031 by construction.
    double group_speed_cm_s(int group) const;

    const geom::LayerStack& stack() const noexcept { return stack_; }

    /// Analytic k_inf of a homogeneous medium: nu*Sigma_f / (Sigma_c + Sigma_f).
    /// The oracle the MC result is checked against, computed independently of
    /// the transport loop.
    static double analytic_k_inf(const material::Material& mat, const xs::FewGroupXS& xs_set,
                                 int group);

    const std::vector<std::string>& isotope_names() const noexcept { return isotope_names_; }

private:
    struct IsotopeSlot {
        const xs::IsotopeXS* iso = nullptr;
        double number_density = 0.0;  // atoms/barn-cm, so n*sigma is 1/cm
        int global_index = -1;
    };
    struct LayerData {
        std::vector<IsotopeSlot> isotopes;
        std::array<double, xs::kGroups> sigma_t{};
        std::array<double, xs::kGroups> sigma_tr{};
    };

    const geom::LayerStack& stack_;
    geom::AnalyticSphereTracker tracker_;
    const xs::FewGroupXS& xs_;
    std::uint64_t seed_;
    std::vector<LayerData> layers_;
    std::vector<std::string> isotope_names_;
};

}  // namespace ns::ref

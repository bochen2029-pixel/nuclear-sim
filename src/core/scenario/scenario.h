// Simulation scenario, schema v1 (03 §4, 04 §6).
//
// The loader performs FULL validation, including resolving the datasets the
// scenario names. "No silent physics defaults" (02 §4) means an unknown key is
// an error rather than something ignored: a typo'd `quench_epsilion` that is
// quietly dropped leaves the run using a default the author did not choose, and
// nothing downstream can tell.

#pragma once

#include "core/material/material.h"
#include "core/xs/xs.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ns {
struct LoadWarning;
}

namespace ns::scenario {

/// "td" is deliberately absent: it is a v1 VALIDATION ERROR (D3/ADR-003), not
/// an unimplemented enumerator, so the loader can say why.
enum class Mode { Alpha, EigenOnly, FixedSource };

struct LayerSpec {
    std::string id;
    double r_outer_cm = 0.0;
    std::string material;
    std::string status;
};

struct SourceSpec {
    std::string kind;       // point | shell | volume | replay
    double position_cm[3] = {0.0, 0.0, 0.0};
    double r_cm = 0.0;      // shell/volume only
    std::string spectrum;   // watt_pu239 | watt_u235 | group_chi | mono
    double mono_MeV = 0.0;
    std::string angular;    // isotropic | forward
    long long sim_particles = 0;
};

/// One [ui."<dotted.path>"] table. `range` is ALWAYS in schema units; display_*
/// is presentation only and never changes the stored value or the hash.
struct UiAnnotation {
    std::string path;
    std::string label;
    double range_lo = 0.0;
    double range_hi = 0.0;
    std::string status;
    std::optional<std::string> display_unit;
    std::optional<double> display_scale;
};

using ParamValue = std::variant<double, long long, bool, std::string, std::vector<double>>;

struct Scenario {
    int schema_version = 1;
    std::string name;
    long long seed = 0;
    Mode mode = Mode::Alpha;

    std::string xs_set;
    std::filesystem::path materials_dir;
    std::filesystem::path xs_path;

    std::vector<LayerSpec> layers;

    std::string t_zero;

    double initiator_strength_n_per_s = 0.0;
    double initiator_t_fire_s = 0.0;
    double initiator_pulse_width_s = 0.0;

    int compression_tier = 2;
    double compression_ratio = 0.0;
    std::optional<double> compression_t_c_s;  // Tier-1 only; WARN and ignored at tier 2

    double generation_time_s_initial = 0.0;
    int eigen_refresh_gens = 0;
    double eigen_refresh_dr_frac = 0.0;
    double t_max_s = 0.0;
    double quench_epsilon = 0.0;

    long long eigen_batch = 0;
    int eigen_inactive = 0;
    int eigen_active = 0;

    long long sim_neutrons = 0;
    int hydro_every_gens = 1;

    std::optional<int> lens_count;
    std::optional<double> lens_jitter_ns;

    std::optional<SourceSpec> source;

    std::vector<std::pair<std::string, ParamValue>> overrides;

    std::vector<std::string> tallies;
    int checkpoint_every_gens = 0;
    bool dump_fields = false;
    int dump_stride = 1;
    int dump_max_frames = 0;
    double dump_budget_gb = 0.0;

    std::map<std::string, UiAnnotation> ui;

    /// 04 §6. Resolves `[data]` paths relative to the repository root inferred
    /// from `toml` (…/data/scenarios/x.toml → …). Use the two-argument form
    /// when the layout differs, which is what tests do.
    static Scenario load(const std::filesystem::path& toml);
    static Scenario load(const std::filesystem::path& toml, const std::filesystem::path& data_root,
                         std::vector<LoadWarning>* warnings = nullptr);

    /// Unknown key ⇒ hard error (04 §6). Grammar: layers.<id>.<field>,
    /// layers[<int>].<field>, materials.<name>.<field>, xs.<iso>.<field>,
    /// section.key. Any other prefix is rejected.
    void apply_overrides(const std::vector<std::pair<std::string, ParamValue>>& items);

    /// True when any override touched a PUBLIC/DECLASSIFIED value. Such runs are
    /// marked non_canonical and MUST NOT be used as gate evidence (03 §4).
    bool non_canonical = false;
};

/// A scenario plus the datasets it resolves to.
struct LoadedScenario {
    Scenario scenario;
    xs::FewGroupXS xs;
    material::MaterialLib materials;
};

LoadedScenario load_scenario(const std::filesystem::path& toml,
                             const std::filesystem::path& data_root,
                             std::vector<LoadWarning>* warnings = nullptr);

}  // namespace ns::scenario

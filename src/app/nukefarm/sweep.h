// sweep.toml manifest + the axis/objective enforcement + the Sampler interface
// (03 §7, 06 §2, D8) — M5-T3-a.
//
// The store-independent PLANNING front of nukefarm: parse + FULLY validate a
// sweep.toml, then hand out sample points through the 06 §2 Sampler interface.
// The validation is the mechanical enforcement of the 00 §3.4 boundary ("explain
// a design space, do not SEARCH one") — `axis_class` + `ScoreKind` + objective
// kind, per 03 §7 / MAJ-35.
//
// This is a FRONTEND (src/app, D7): it composes nscore outputs (it scores a
// ns::physics::TallyResult) but contains no physics. The FS work-queue, the
// store-backed run loop, and the submit/worker/status/resume CLI are M5-T3-b;
// the MCTS sampler is M5-T4.

#pragma once

#include "physics/tally/tally.h"  // ns::physics::TallyResult — the sampler's `Tally`

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns::nukefarm {

/// Thrown on any 03 §7 load/validation failure (a malformed manifest, an axis
/// range outside its constant's band, an optimizing sampler on a pedagogical
/// axis, an unknown `constant_id`, ...), with a precise diagnostic.
struct SweepError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// 03 §7 axis class (MAJ-35). `numerical` = a SIM knob (unrestricted);
/// `uncertainty` = a physical parameter confined to its published band;
/// `pedagogical` = outside the published envelope (heavily restricted).
enum class AxisClass { Numerical, Uncertainty, Pedagogical };

/// 03 §7 / D8 objective kind — the ONLY two permitted (ADR-007). `calibrate`
/// scores toward a published band CENTER; `sensitivity` just covers the space.
enum class ObjectiveKind { Sensitivity, Calibrate };

/// 06 §2 sampler scoring kind. `OptimizeExtremum` exists only so the loader can
/// REJECT it — no built-in sampler declares it (that is the mechanical block on
/// "search a design space toward a performance extremum", 00 §3.4).
enum class ScoreKind { Coverage, BandCenter, OptimizeExtremum };

/// One `[[space]]` axis (03 §7). `range` is `[lo, hi]`.
struct SweepAxis {
    std::string param;        ///< override path, e.g. "compression.ratio"
    AxisClass axis_class = AxisClass::Numerical;
    std::string constant_id;  ///< REQUIRED for uncertainty (e.g. "C-060"); else empty
    double lo = 0.0;
    double hi = 0.0;
};

/// `[objective]` (03 §7).
struct Objective {
    ObjectiveKind kind = ObjectiveKind::Sensitivity;
    std::optional<double> target_lo;  ///< `target_yield_kt` band (calibrate only)
    std::optional<double> target_hi;
    std::vector<std::string> report;  ///< reported tally fields
};

/// sweep.toml (03 §7, schema v1).
struct SweepManifest {
    int schema_version = 1;
    std::string name;
    std::string base_scenario;
    std::string sampler;  ///< grid | lhs | random | mcts
    std::int64_t budget_runs = 0;
    double budget_wallclock_h = 0.0;
    std::int64_t checkpoint_every_runs = 0;
    Objective objective;
    std::vector<SweepAxis> space;

    /// Parse + FULLY validate a sweep.toml (03 §7 / MAJ-35). Throws SweepError on
    /// any violation. `load` reads a file; `parse` takes the document text.
    static SweepManifest load(const std::string& path);
    static SweepManifest parse(const std::string& toml_text);
};

/// One sampled point: an ordered set of `param -> value` assignments (a sweep
/// override set). Values are doubles — the physical/SIM axes are continuous.
struct ParamAssignment {
    std::string param;
    double value = 0.0;
};
using ParamSet = std::vector<ParamAssignment>;

/// 06 §2 sampler plugin interface (in-tree C++; no `extern "C"` — C++ types cross
/// the boundary, E7). `report` feeds an adaptive sampler (MCTS) the tally of a
/// completed point; the space-filling built-ins ignore it.
class Sampler {
public:
    virtual ~Sampler() = default;
    virtual std::optional<ParamSet> next() = 0;
    virtual void report(const ParamSet& point, const ns::physics::TallyResult& tally) = 0;
    virtual ScoreKind score_kind() const = 0;
    virtual bool supports_categorical() const = 0;
};

/// Instantiate the manifest's sampler. Enforces the 06 §2 load rule: a sampler
/// whose `score_kind()` is `OptimizeExtremum` is REJECTED (SweepError). The
/// built-ins are grid | lhs | random (all `Coverage`); "mcts" is M5-T4 and is
/// rejected here with a precise diagnostic.
std::unique_ptr<Sampler> make_sampler(const SweepManifest& m);

}  // namespace ns::nukefarm

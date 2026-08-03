// Tally result + the nine 03 §5 invariants (M3-T3-a).
//
// `TallyResult` mirrors tally.json (03 §5, schema v1). `tally_invariants()`
// asserts the nine normative invariants of 03 §5. The 03 §5 illustrative example
// is itself a fixture (QC-04 / 11 §4) and MUST satisfy all nine; `oracle.py` §6
// is the INDEPENDENT Python reference this ports (it measures I3 = 7.5e-8,
// I4 ≈ 3.8e-8 on that example).
//
// SCOPE (the M3-T3 split): the tally STRUCT + invariants + JSON (de)serialize —
// the fast4-independent half. The `TallySink` streaming interface and the burst
// that FILLS a TallyResult are the 02 §3 coupling loop (M3-T3-b); the *canonical*
// end-to-end bundle that runs those invariants on a real Trinity tally needs the
// blocked `fast4` xs (M1-T4a-2).

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ns::physics {

struct TallyKEff {
    double peak = 0.0;
    double at_initiator = 0.0;
    double at_quench = 0.0;
    double sigma_pcm = 0.0;
};

struct TallyKPrompt {
    double peak = 0.0;
    double at_quench = 0.0;
};

struct TallyBurnup {
    double pu_fraction = 0.0;
    double pu_fraction_sigma = 0.0;
    double pu_fissions = 0.0;
    double u238_tamper_fissions = 0.0;
    double tamper_yield_fraction = 0.0;
};

struct TallyYieldSplit {
    double pre_peak_kt = 0.0;
    double post_peak_kt = 0.0;
};

struct TallyPopulationSeries {
    double dt_s = 0.0;
    std::vector<double> log10_N;
};

struct TallyFissionMesh {
    std::string format;                 // "radial_shells"
    std::vector<double> shell_edges_cm;
    std::vector<double> fissions;       // len == shell_edges_cm.size() - 1
    double leaked_fissions = 0.0;
};

struct TallyTiming {
    double wall_s = 0.0;
    std::int64_t eigen_calls = 0;
    std::int64_t generations = 0;
    double max_q = 0.0;
};

/// Ordered per-isotope fission counts (a JSON object; insertion order preserved
/// for deterministic re-serialization). `get()` returns 0 for an absent isotope,
/// matching oracle.py's `by_iso.get(iso, 0.0)`.
struct IsotopeFissions {
    std::vector<std::pair<std::string, double>> entries;
    double get(const std::string& iso) const;
    double sum() const;
};

/// tally.json (03 §5, schema v1).
struct TallyResult {
    int schema_version = 1;
    std::string run_id;
    bool non_canonical = false;
    TallyKEff k_eff;
    TallyKPrompt k_prompt;
    double yield_kt = 0.0;
    double yield_kt_sigma = 0.0;
    double fissions_total = 0.0;
    IsotopeFissions fissions_by_isotope;
    TallyBurnup burnup;
    TallyYieldSplit yield_split;
    TallyPopulationSeries population_series;
    TallyFissionMesh fission_mesh;
    TallyTiming timing;
};

/// Physical constants the invariants need (01 §8 — supplied by the caller from
/// `ns::consts`, never magic numbers). `t_max_s` is a scenario field
/// (03 §4 `[kinetics]`), not a constant.
struct InvariantConstants {
    double phi_kt = 0.0;   // C-041 fissions/kt
    double n_a = 0.0;      // C-916 Avogadro
    double m_pit_g = 0.0;  // C-102 pit mass, grams (mass_kg × 1000)
    double m_pu239 = 0.0;  // C-910
    double m_pu240 = 0.0;  // C-911
    double m_pu241 = 0.0;  // C-919
    double t_max_s = 0.0;  // scenario [kinetics] t_max_s
};

/// One violated 03 §5 invariant. `measured`/`tol` are NaN for the structural
/// invariants (1, 8), which have no numeric residual.
struct TallyViolation {
    int number = 0;
    std::string label;
    double measured = 0.0;
    double tol = 0.0;
};

/// Assert the nine 03 §5 invariants. Empty result == all pass. Pure: no I/O and
/// no constant lookups — the caller supplies `c`. The tolerances (1e-6, 1e-3)
/// are the 03 §5 invariant definitions, not physics constants.
std::vector<TallyViolation> tally_invariants(const TallyResult& t, const InvariantConstants& c);

/// Serialize to the 03 §5 tally.json shape (field order per 03 §5). Idempotent
/// after one parse: `to_json(parse_tally_json(s))` re-serializes stably.
std::string to_json(const TallyResult& t, int indent = 2);

/// Parse a 03 §5 tally.json document. Throws std::runtime_error on a missing
/// REQUIRED field — in particular BOTH `burnup.pu_fraction` and
/// `burnup.tamper_yield_fraction` (03 §5 invariant 6: "ALWAYS both present and
/// never merged").
TallyResult parse_tally_json(const std::string& text);

}  // namespace ns::physics

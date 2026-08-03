// tally.json (de)serialize + the nine 03 §5 invariants (M3-T3-a).
//
// tally_invariants() ports oracle.py §6 (section_invariants) into C++ so the
// coupling loop (M3-T3-b) and the gates can assert the same nine identities the
// Python oracle already checks on the 03 §5 fixture. The two live independently
// (oracle reads the TOML + markdown; this reads a TallyResult) precisely so their
// agreement means something.

#include "physics/tally/tally.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ns::physics {

namespace {
using json = nlohmann::ordered_json;

// 03 §5 invariant tolerances (definitions, not physics constants).
constexpr double kRelTol = 1e-6;    // invariants 2, 3, 4, 6, 9
constexpr double kTamperTol = 1e-3;  // invariant 5

const json& require_field(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) {
        throw std::runtime_error(std::string("tally.json: missing required field '") + key + "'");
    }
    return *it;
}
}  // namespace

double IsotopeFissions::get(const std::string& iso) const {
    for (const auto& [name, value] : entries) {
        if (name == iso) {
            return value;
        }
    }
    return 0.0;
}

double IsotopeFissions::sum() const {
    double s = 0.0;
    for (const auto& [name, value] : entries) {
        s += value;
    }
    return s;
}

std::vector<TallyViolation> tally_invariants(const TallyResult& t, const InvariantConstants& c) {
    std::vector<TallyViolation> out;
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    const auto fail = [&out](int number, const char* label, double measured, double tol) {
        out.push_back(TallyViolation{number, label, measured, tol});
    };

    const auto& mesh = t.fission_mesh;
    const double total = t.fissions_total;

    // 1. len(fissions) == len(shell_edges_cm) - 1  (structural)
    if (mesh.shell_edges_cm.empty() || mesh.fissions.size() + 1 != mesh.shell_edges_cm.size()) {
        fail(1, "len(fissions) == len(shell_edges_cm) - 1", qnan, qnan);
    }

    // 2. |sum(shell fissions) + leaked - total| <= 1e-6 * total
    {
        double s = 0.0;
        for (const double f : mesh.fissions) {
            s += f;
        }
        const double i2 = std::abs(s + mesh.leaked_fissions - total) / total;
        if (!(i2 <= kRelTol)) {
            fail(2, "shell fissions + leaked == fissions_total", i2, kRelTol);
        }
    }

    // 3. |yield_kt * Phi_kt / fissions_total - 1| <= 1e-6
    {
        const double i3 = std::abs(t.yield_kt * c.phi_kt / total - 1.0);
        if (!(i3 <= kRelTol)) {
            fail(3, "yield_kt * Phi_kt == fissions_total", i3, kRelTol);
        }
    }

    // 4. |Sum_corePu (fissions_i * M_i) / (N_A * M_pit) - pu_fraction| <= 1e-6.
    //    Core Pu = Pu239/Pu240/Pu241, EACH with its OWN molar mass: using
    //    M(Pu-239) for all Pu injects ~4e-5 at a 1% Pu-240 share, 40x the tol
    //    (QC-05) — so the invariant is unsatisfiable rather than merely inexact.
    {
        const double pu_mass = t.fissions_by_isotope.get("Pu239") * c.m_pu239 +
                               t.fissions_by_isotope.get("Pu240") * c.m_pu240 +
                               t.fissions_by_isotope.get("Pu241") * c.m_pu241;
        const double i4 = std::abs(pu_mass / (c.n_a * c.m_pit_g) - t.burnup.pu_fraction);
        if (!(i4 <= kRelTol)) {
            fail(4, "per-isotope Pu mass / (N_A*M_pit) == pu_fraction", i4, kRelTol);
        }
    }

    // 5. |u238_tamper_fissions/total - tamper_yield_fraction| <= 1e-3
    {
        const double i5 = std::abs(t.burnup.u238_tamper_fissions / total - t.burnup.tamper_yield_fraction);
        if (!(i5 <= kTamperTol)) {
            fail(5, "u238 tamper share == tamper_yield_fraction", i5, kTamperTol);
        }
    }

    // 6. |sum(fissions_by_isotope) - total| <= 1e-6 * total. The "both burnup
    //    fields present, never merged" half is enforced at parse (parse_tally_json
    //    requires both keys) — a TallyResult always carries both fields.
    {
        const double i6 = std::abs(t.fissions_by_isotope.sum() - total) / total;
        if (!(i6 <= kRelTol)) {
            fail(6, "sum(fissions_by_isotope) == fissions_total", i6, kRelTol);
        }
    }

    // 7. generations * mean(Lambda) <= t_max_s. mean(Lambda) is the population
    //    series' own time step dt_s (the per-generation cadence, 02 §3 t += Lambda).
    //    oracle.py left this unconditional; here it is a real comparison.
    {
        const double i7 = static_cast<double>(t.timing.generations) * t.population_series.dt_s;
        if (!(i7 <= c.t_max_s)) {
            fail(7, "generations * mean(Lambda) <= t_max_s", i7, c.t_max_s);
        }
    }

    // 8. population_series is log-scale / renorm-safe: log10_N present and finite,
    //    positive step (structural — the log domain is what makes it renorm-safe,
    //    M3-T1 BurstAccumulator).
    {
        bool ok = t.population_series.dt_s > 0.0 && !t.population_series.log10_N.empty();
        for (const double x : t.population_series.log10_N) {
            ok = ok && std::isfinite(x);
        }
        if (!ok) {
            fail(8, "population_series is log-scale (log10_N present, finite)", qnan, qnan);
        }
    }

    // 9. |pre_peak + post_peak - yield_kt| <= 1e-6 * yield_kt (BLK-03: proves E6
    //    post-peak integration is wired; a k=1 termination cannot satisfy it).
    {
        const double i9 = std::abs(t.yield_split.pre_peak_kt + t.yield_split.post_peak_kt - t.yield_kt) /
                          t.yield_kt;
        if (!(i9 <= kRelTol)) {
            fail(9, "yield_split sums to yield_kt", i9, kRelTol);
        }
    }

    return out;
}

std::string to_json(const TallyResult& t, int indent) {
    json j;
    j["schema_version"] = t.schema_version;
    j["run_id"] = t.run_id;
    j["non_canonical"] = t.non_canonical;
    j["k_eff"] = {{"peak", t.k_eff.peak},
                  {"at_initiator", t.k_eff.at_initiator},
                  {"at_quench", t.k_eff.at_quench},
                  {"sigma_pcm", t.k_eff.sigma_pcm}};
    j["k_prompt"] = {{"peak", t.k_prompt.peak}, {"at_quench", t.k_prompt.at_quench}};
    j["yield_kt"] = t.yield_kt;
    j["yield_kt_sigma"] = t.yield_kt_sigma;
    j["fissions_total"] = t.fissions_total;

    json iso = json::object();
    for (const auto& [name, value] : t.fissions_by_isotope.entries) {
        iso[name] = value;
    }
    j["fissions_by_isotope"] = std::move(iso);

    j["burnup"] = {{"pu_fraction", t.burnup.pu_fraction},
                   {"pu_fraction_sigma", t.burnup.pu_fraction_sigma},
                   {"pu_fissions", t.burnup.pu_fissions},
                   {"u238_tamper_fissions", t.burnup.u238_tamper_fissions},
                   {"tamper_yield_fraction", t.burnup.tamper_yield_fraction}};
    j["yield_split"] = {{"pre_peak_kt", t.yield_split.pre_peak_kt},
                        {"post_peak_kt", t.yield_split.post_peak_kt}};
    j["population_series"] = {{"dt_s", t.population_series.dt_s},
                              {"log10_N", t.population_series.log10_N}};
    j["fission_mesh"] = {{"format", t.fission_mesh.format},
                         {"shell_edges_cm", t.fission_mesh.shell_edges_cm},
                         {"fissions", t.fission_mesh.fissions},
                         {"leaked_fissions", t.fission_mesh.leaked_fissions}};
    j["timing"] = {{"wall_s", t.timing.wall_s},
                   {"eigen_calls", t.timing.eigen_calls},
                   {"generations", t.timing.generations},
                   {"max_q", t.timing.max_q}};

    return j.dump(indent);
}

TallyResult parse_tally_json(const std::string& text) {
    const json j = json::parse(text);
    TallyResult t;

    if (const auto it = j.find("schema_version"); it != j.end()) {
        t.schema_version = it->get<int>();
    }
    if (const auto it = j.find("run_id"); it != j.end()) {
        t.run_id = it->get<std::string>();
    }
    if (const auto it = j.find("non_canonical"); it != j.end()) {
        t.non_canonical = it->get<bool>();
    }

    const json& ke = require_field(j, "k_eff");
    t.k_eff.peak = require_field(ke, "peak").get<double>();
    t.k_eff.at_initiator = require_field(ke, "at_initiator").get<double>();
    t.k_eff.at_quench = require_field(ke, "at_quench").get<double>();
    t.k_eff.sigma_pcm = require_field(ke, "sigma_pcm").get<double>();

    if (const auto it = j.find("k_prompt"); it != j.end()) {
        t.k_prompt.peak = require_field(*it, "peak").get<double>();
        t.k_prompt.at_quench = require_field(*it, "at_quench").get<double>();
    }

    t.yield_kt = require_field(j, "yield_kt").get<double>();
    if (const auto it = j.find("yield_kt_sigma"); it != j.end()) {
        t.yield_kt_sigma = it->get<double>();
    }
    t.fissions_total = require_field(j, "fissions_total").get<double>();

    const json& iso = require_field(j, "fissions_by_isotope");
    for (const auto& [name, value] : iso.items()) {
        t.fissions_by_isotope.entries.emplace_back(name, value.get<double>());
    }

    // 03 §5 invariant 6: pu_fraction and tamper_yield_fraction are ALWAYS both
    // present and never merged — a hard parse contract, not a soft default.
    const json& bu = require_field(j, "burnup");
    t.burnup.pu_fraction = require_field(bu, "pu_fraction").get<double>();
    t.burnup.tamper_yield_fraction = require_field(bu, "tamper_yield_fraction").get<double>();
    if (const auto it = bu.find("pu_fraction_sigma"); it != bu.end()) {
        t.burnup.pu_fraction_sigma = it->get<double>();
    }
    if (const auto it = bu.find("pu_fissions"); it != bu.end()) {
        t.burnup.pu_fissions = it->get<double>();
    }
    if (const auto it = bu.find("u238_tamper_fissions"); it != bu.end()) {
        t.burnup.u238_tamper_fissions = it->get<double>();
    }

    const json& ys = require_field(j, "yield_split");
    t.yield_split.pre_peak_kt = require_field(ys, "pre_peak_kt").get<double>();
    t.yield_split.post_peak_kt = require_field(ys, "post_peak_kt").get<double>();

    const json& ps = require_field(j, "population_series");
    t.population_series.dt_s = require_field(ps, "dt_s").get<double>();
    t.population_series.log10_N = require_field(ps, "log10_N").get<std::vector<double>>();

    const json& fm = require_field(j, "fission_mesh");
    if (const auto it = fm.find("format"); it != fm.end()) {
        t.fission_mesh.format = it->get<std::string>();
    }
    t.fission_mesh.shell_edges_cm = require_field(fm, "shell_edges_cm").get<std::vector<double>>();
    t.fission_mesh.fissions = require_field(fm, "fissions").get<std::vector<double>>();
    t.fission_mesh.leaked_fissions = require_field(fm, "leaked_fissions").get<double>();

    const json& tm = require_field(j, "timing");
    if (const auto it = tm.find("wall_s"); it != tm.end()) {
        t.timing.wall_s = it->get<double>();
    }
    if (const auto it = tm.find("eigen_calls"); it != tm.end()) {
        t.timing.eigen_calls = it->get<std::int64_t>();
    }
    t.timing.generations = require_field(tm, "generations").get<std::int64_t>();
    if (const auto it = tm.find("max_q"); it != tm.end()) {
        t.timing.max_q = it->get<double>();
    }

    return t;
}

}  // namespace ns::physics

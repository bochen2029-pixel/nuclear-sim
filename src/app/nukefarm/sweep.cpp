// sweep.toml loader + the 03 §7 axis/objective enforcement (MAJ-35) — M5-T3-a.

#include "app/nukefarm/sweep.h"

#include "core/constants/constants.h"

#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <set>
#include <string>
#include <utility>

namespace ns::nukefarm {

namespace {

[[noreturn]] void fail(const std::string& msg) { throw SweepError(msg); }

void require(bool cond, const std::string& msg) {
    if (!cond) fail(msg);
}

// Unknown keys hard-error: a silently ignored key leaves the sweep using a default
// the author did not choose (the 02 §4 "no silent defaults" rule, as in 03 §4).
void reject_unknown_keys(const toml::table& t, const std::set<std::string>& known,
                         const std::string& ctx) {
    for (const auto& [key, _] : t) {
        const std::string name(key.str());
        if (known.find(name) == known.end()) {
            fail(ctx + ": unknown key '" + name + "' (sweep.toml keys hard-error, 03 §7)");
        }
    }
}

std::string require_string(const toml::table& t, const std::string& key, const std::string& ctx) {
    const auto* node = t.get(key);
    require(node != nullptr, ctx + "." + key + " is REQUIRED (03 §7)");
    const auto v = node->value<std::string>();
    require(v.has_value(), ctx + "." + key + " must be a string");
    return *v;
}

std::int64_t require_integer(const toml::table& t, const std::string& key, const std::string& ctx) {
    const auto* node = t.get(key);
    require(node != nullptr, ctx + "." + key + " is REQUIRED (03 §7)");
    const auto v = node->value<std::int64_t>();
    require(v.has_value(), ctx + "." + key + " must be an integer");
    return *v;
}

// A 2-element numeric array [lo, hi] with lo <= hi.
std::pair<double, double> require_range(const toml::node* node, const std::string& ctx) {
    require(node != nullptr, ctx + " is REQUIRED (03 §7)");
    const auto* arr = node->as_array();
    require(arr != nullptr && arr->size() == 2, ctx + " must be a 2-element array [lo, hi]");
    const auto lo = arr->get(0)->value<double>();
    const auto hi = arr->get(1)->value<double>();
    require(lo.has_value() && hi.has_value(), ctx + " entries must be numbers");
    require(*lo <= *hi, ctx + " requires lo <= hi");
    return {*lo, *hi};
}

AxisClass parse_axis_class(const std::string& s, const std::string& ctx) {
    if (s == "numerical") return AxisClass::Numerical;
    if (s == "uncertainty") return AxisClass::Uncertainty;
    if (s == "pedagogical") return AxisClass::Pedagogical;
    fail(ctx + ".axis_class '" + s + "' invalid — numerical | uncertainty | pedagogical (03 §7)");
}

ObjectiveKind parse_objective_kind(const std::string& s) {
    if (s == "sensitivity") return ObjectiveKind::Sensitivity;
    if (s == "calibrate") return ObjectiveKind::Calibrate;
    fail("[objective].kind '" + s + "' invalid — sensitivity | calibrate (03 §7 / D8)");
}

const std::set<std::string> kSamplers = {"grid", "lhs", "random", "mcts"};

SweepManifest from_document(const toml::table& doc) {
    reject_unknown_keys(
        doc, {"schema_version", "name", "base_scenario", "sweep", "objective", "space"},
        "sweep.toml");

    SweepManifest m;
    if (const auto v = doc["schema_version"].value<std::int64_t>()) {
        require(*v == 1, "sweep.toml schema_version must be 1");
        m.schema_version = static_cast<int>(*v);
    }
    m.name = require_string(doc, "name", "sweep.toml");
    m.base_scenario = require_string(doc, "base_scenario", "sweep.toml");

    // [sweep]
    const auto* sweep_node = doc.get("sweep");
    require(sweep_node != nullptr && sweep_node->is_table(), "[sweep] table is REQUIRED (03 §7)");
    const toml::table& sweep = *sweep_node->as_table();
    reject_unknown_keys(
        sweep, {"sampler", "budget_runs", "budget_wallclock_h", "checkpoint_every_runs"}, "[sweep]");
    m.sampler = require_string(sweep, "sampler", "[sweep]");
    require(kSamplers.find(m.sampler) != kSamplers.end(),
            "[sweep].sampler '" + m.sampler + "' unknown — grid | lhs | random | mcts (06 §2)");
    m.budget_runs = require_integer(sweep, "budget_runs", "[sweep]");
    require(m.budget_runs > 0, "[sweep].budget_runs must be > 0");
    if (const auto v = sweep["budget_wallclock_h"].value<double>()) m.budget_wallclock_h = *v;
    if (const auto v = sweep["checkpoint_every_runs"].value<std::int64_t>())
        m.checkpoint_every_runs = *v;

    // [objective]
    const auto* obj_node = doc.get("objective");
    require(obj_node != nullptr && obj_node->is_table(), "[objective] table is REQUIRED (03 §7)");
    const toml::table& obj = *obj_node->as_table();
    reject_unknown_keys(obj, {"kind", "target_yield_kt", "report"}, "[objective]");
    m.objective.kind = parse_objective_kind(require_string(obj, "kind", "[objective]"));
    if (const auto* tgt = obj.get("target_yield_kt")) {
        auto [lo, hi] = require_range(tgt, "[objective].target_yield_kt");
        m.objective.target_lo = lo;
        m.objective.target_hi = hi;
    }
    if (const auto* rep = obj.get("report")) {
        const auto* arr = rep->as_array();
        require(arr != nullptr, "[objective].report must be an array of strings");
        for (std::size_t i = 0; i < arr->size(); ++i) {
            const auto s = arr->get(i)->value<std::string>();
            require(s.has_value(), "[objective].report entries must be strings");
            m.objective.report.push_back(*s);
        }
    }

    // [[space]]
    const auto* space_node = doc.get("space");
    require(space_node != nullptr && space_node->is_array(),
            "[[space]] must contain at least one axis (03 §7)");
    const toml::array& space = *space_node->as_array();
    require(!space.empty(), "[[space]] must contain at least one axis (03 §7)");
    for (std::size_t i = 0; i < space.size(); ++i) {
        const std::string ctx = "[[space]][" + std::to_string(i) + "]";
        const auto* entry = space.get(i)->as_table();
        require(entry != nullptr, ctx + " must be a table");
        reject_unknown_keys(*entry, {"param", "axis_class", "constant_id", "range"}, ctx);

        SweepAxis ax;
        ax.param = require_string(*entry, "param", ctx);
        ax.axis_class = parse_axis_class(require_string(*entry, "axis_class", ctx), ctx);
        if (const auto cid = (*entry)["constant_id"].value<std::string>()) ax.constant_id = *cid;
        std::tie(ax.lo, ax.hi) = require_range(entry->get("range"), ctx + ".range");

        if (ax.axis_class == AxisClass::Uncertainty) {
            require(!ax.constant_id.empty(),
                    ctx + ": an uncertainty axis REQUIRES constant_id (03 §7)");
            double blo = 0.0;
            double bhi = 0.0;
            try {
                blo = ns::consts::get_lo(ax.constant_id);
                bhi = ns::consts::get_hi(ax.constant_id);
            } catch (const std::exception& e) {
                fail(ctx + ": constant_id '" + ax.constant_id +
                     "' is not a banded constant (" + e.what() + ")");
            }
            require(ax.lo >= blo && ax.hi <= bhi,
                    ctx + ": range [" + std::to_string(ax.lo) + ", " + std::to_string(ax.hi) +
                        "] must lie inside " + ax.constant_id + "'s band [" + std::to_string(blo) +
                        ", " + std::to_string(bhi) + "] (03 §7)");
        }
        m.space.push_back(std::move(ax));
    }

    // Cross-cutting enforcement (03 §7 / MAJ-35): the mechanical "explain, don't
    // search" boundary of 00 §3.4.
    bool has_pedagogical = false;
    for (const auto& ax : m.space)
        if (ax.axis_class == AxisClass::Pedagogical) has_pedagogical = true;

    if (has_pedagogical) {
        require(m.sampler == "grid",
                "a pedagogical axis is permitted ONLY with sampler=\"grid\" (03 §7 / MAJ-35); got "
                "\"" + m.sampler + "\"");
        require(m.budget_runs <= 100,
                "a pedagogical axis requires budget_runs <= 100 (03 §7 / MAJ-35); got " +
                    std::to_string(m.budget_runs));
        require(m.objective.kind == ObjectiveKind::Sensitivity,
                "a pedagogical axis requires objective.kind=\"sensitivity\" (03 §7 / MAJ-35)");
    }
    if (m.objective.kind == ObjectiveKind::Calibrate) {
        require(!has_pedagogical,
                "calibrate requires every axis to be numerical or uncertainty; a pedagogical axis "
                "is present (03 §7)");
        require(m.objective.target_lo.has_value(),
                "calibrate requires [objective].target_yield_kt (it scores toward the band center, "
                "03 §7)");
    }

    return m;
}

}  // namespace

SweepManifest SweepManifest::parse(const std::string& toml_text) {
    toml::table doc;
    try {
        doc = toml::parse(toml_text);
    } catch (const toml::parse_error& err) {
        fail("sweep.toml parse error: " + std::string(err.description()));
    }
    return from_document(doc);
}

SweepManifest SweepManifest::load(const std::string& path) {
    toml::table doc;
    try {
        doc = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        fail("sweep.toml '" + path + "' parse error: " + std::string(err.description()));
    }
    return from_document(doc);
}

}  // namespace ns::nukefarm

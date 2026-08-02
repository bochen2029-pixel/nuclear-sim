#include "core/scenario/scenario.h"

#include "core/diagnostics.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace ns::scenario {
namespace {

const std::set<std::string> kTopLevelKeys = {
    "schema_version", "name", "seed", "mode", "layers", "data", "time", "initiator",
    "compression", "kinetics", "eigen", "transport", "hydro", "lenses", "source",
    "overrides", "output", "ui",
};

const std::set<std::string> kTallyVocabulary = {
    "k", "population", "fissions_by_isotope", "burnup", "yield", "fission_mesh",
};

const std::set<std::string> kSourceKinds = {"point", "shell", "volume", "replay"};
const std::set<std::string> kSpectra = {"watt_pu239", "watt_u235", "group_chi", "mono"};
const std::set<std::string> kAngular = {"isotropic", "forward"};

std::string join(const std::set<std::string>& values) {
    std::string out;
    for (const auto& v : values) {
        if (!out.empty()) {
            out += ", ";
        }
        out += v;
    }
    return out;
}

void reject_unknown_keys(const toml::table& table, const std::set<std::string>& known,
                         const std::filesystem::path& path, const std::string& prefix) {
    for (const auto& [key, _] : table) {
        const std::string name(key.str());
        if (known.find(name) == known.end()) {
            throw LoadError(path, prefix.empty() ? name : prefix + "." + name,
                            "unknown key — unknown keys hard-error (03 §4). A silently ignored "
                            "key leaves the run using a default the author did not choose. "
                            "Known keys here: " + join(known));
        }
    }
}

const toml::table& require_table(const toml::table& parent, const std::string& key,
                                 const std::filesystem::path& path) {
    const auto* node = parent.get(key);
    require(node != nullptr, path, key, "table is REQUIRED (03 §4)");
    const auto* table = node->as_table();
    require(table != nullptr, path, key, "must be a table");
    return *table;
}

double require_number(const toml::table& t, const std::string& key,
                      const std::filesystem::path& path, const std::string& prefix) {
    const auto* node = t.get(key);
    require(node != nullptr, path, prefix + "." + key, "is REQUIRED (03 §4)");
    if (const auto v = node->value<double>()) {
        return *v;
    }
    throw LoadError(path, prefix + "." + key, "must be a number");
}

long long require_integer(const toml::table& t, const std::string& key,
                          const std::filesystem::path& path, const std::string& prefix) {
    const auto* node = t.get(key);
    require(node != nullptr, path, prefix + "." + key, "is REQUIRED (03 §4)");
    const auto v = node->value<int64_t>();
    require(v.has_value(), path, prefix + "." + key,
            "must be an integer. Scheduling decisions run on integer counters only — equality "
            "tests on accumulated floating-point time are forbidden (02 §4, MAJ-21)");
    return *v;
}

std::string require_string(const toml::table& t, const std::string& key,
                           const std::filesystem::path& path, const std::string& prefix) {
    const auto* node = t.get(key);
    require(node != nullptr, path, prefix.empty() ? key : prefix + "." + key, "is REQUIRED");
    const auto v = node->value<std::string>();
    require(v.has_value(), path, prefix.empty() ? key : prefix + "." + key, "must be a string");
    return *v;
}

/// `layers` under `[data]` is the one misplacement worth its own diagnostic.
///
/// In TOML every bare key after a `[data]` header joins that table, so an array
/// written below it is silently reparented — which is exactly how 03 §4's own
/// example used to read. Checked BEFORE the generic unknown-key sweep, because
/// "unknown key: data.layers" would send the reader looking for a typo.
void check_layers_placement(const toml::table& doc, const std::filesystem::path& path) {
    if (const auto* data = doc.get("data"); data != nullptr && data->is_table()) {
        require(data->as_table()->get("layers") == nullptr, path, "data.layers",
                "`layers` must be a TOP-LEVEL array, not part of [data]. In TOML every bare "
                "key after a [data] header belongs to that table, so placing the array below "
                "[data] silently reparents it. Move it above the [data] header");
    }
}

void parse_layers(const toml::table& doc, const std::filesystem::path& path, Scenario& out) {
    const auto* node = doc.get("layers");
    require(node != nullptr, path, "layers", "is REQUIRED and must be a top-level array");
    const auto* array = node->as_array();
    require(array != nullptr, path, "layers", "must be an array of tables");
    require(!array->empty(), path, "layers", "must contain at least one layer");

    static const std::set<std::string> kLayerKeys = {"id", "r_outer_cm", "material", "status"};

    double previous = 0.0;
    for (std::size_t i = 0; i < array->size(); ++i) {
        const std::string at = "layers[" + std::to_string(i) + "]";
        const auto* entry = array->get(i)->as_table();
        require(entry != nullptr, path, at, "must be a table");
        reject_unknown_keys(*entry, kLayerKeys, path, at);

        LayerSpec layer;
        layer.id = require_string(*entry, "id", path, at);
        layer.material = require_string(*entry, "material", path, at);
        layer.status = require_string(*entry, "status", path, at);
        layer.r_outer_cm = require_number(*entry, "r_outer_cm", path, at);

        require(layer.r_outer_cm > 0.0, path, at + ".r_outer_cm",
                "must be positive; the first layer's INNER radius is 0, not its outer radius");
        // Strictly increasing: LayerStack (04 §4) locates by radius, so equal or
        // decreasing radii make a layer unreachable rather than merely odd.
        require(layer.r_outer_cm > previous, path, at + ".r_outer_cm",
                "layer radii must be strictly increasing (03 §4); "
                    + std::to_string(layer.r_outer_cm) + " does not exceed the previous "
                    + std::to_string(previous));
        previous = layer.r_outer_cm;

        for (const auto& seen : out.layers) {
            require(seen.id != layer.id, path, at + ".id",
                    "duplicate layer id \"" + layer.id
                        + "\"; the override grammar selects layers BY id (04 §6), so ids must "
                          "be unique");
        }
        out.layers.push_back(std::move(layer));
    }
}

void parse_ui(const toml::table& doc, const std::filesystem::path& path, Scenario& out) {
    const auto* node = doc.get("ui");
    if (node == nullptr) {
        return;
    }
    const auto* table = node->as_table();
    require(table != nullptr, path, "ui", "must be a table of [ui.\"<dotted.path>\"] tables");

    static const std::set<std::string> kUiKeys = {"label", "range", "status", "display_unit",
                                                  "display_scale"};
    for (const auto& [key, value] : *table) {
        const std::string target(key.str());
        const std::string at = "ui.\"" + target + "\"";
        const auto* entry = value.as_table();
        require(entry != nullptr, path, at,
                "must be a table — one [ui.\"<dotted.path>\"] table per widget, never inline "
                "(03 §4, MAJ-25)");
        reject_unknown_keys(*entry, kUiKeys, path, at);

        UiAnnotation ann;
        ann.path = target;
        ann.label = require_string(*entry, "label", path, at);
        ann.status = require_string(*entry, "status", path, at);

        const auto* range = entry->get("range");
        require(range != nullptr && range->is_array(), path, at + ".range",
                "is REQUIRED and must be [lo, hi]");
        const auto* range_array = range->as_array();
        require(range_array->size() == 2, path, at + ".range", "must have exactly two entries");
        const auto lo = range_array->get(0)->value<double>();
        const auto hi = range_array->get(1)->value<double>();
        require(lo.has_value() && hi.has_value(), path, at + ".range", "entries must be numbers");
        require(*lo < *hi, path, at + ".range",
                "lo must be below hi; got [" + std::to_string(*lo) + ", " + std::to_string(*hi)
                    + "]");
        ann.range_lo = *lo;
        ann.range_hi = *hi;

        if (const auto* unit = entry->get("display_unit")) {
            ann.display_unit = unit->value<std::string>();
            // display_* is presentation only. The range above is ALWAYS in
            // schema units, so a display_scale must never be applied to it.
            const auto* scale = entry->get("display_scale");
            require(scale != nullptr, path, at + ".display_scale",
                    "is REQUIRED when display_unit is given, otherwise the display value is "
                    "undefined (03 §4)");
            ann.display_scale = scale->value<double>();
            require(ann.display_scale.has_value() && *ann.display_scale > 0.0, path,
                    at + ".display_scale", "must be a positive number");
        }
        out.ui.emplace(target, std::move(ann));
    }
}

/// Numeric value of a dotted scenario path, for the [ui.*] range check.
std::optional<double> value_at(const Scenario& s, const std::string& path) {
    if (path == "initiator.strength_n_per_s") return s.initiator_strength_n_per_s;
    if (path == "initiator.t_fire_s") return s.initiator_t_fire_s;
    if (path == "initiator.pulse_width_s") return s.initiator_pulse_width_s;
    if (path == "compression.ratio") return s.compression_ratio;
    if (path == "kinetics.generation_time_s_initial") return s.generation_time_s_initial;
    if (path == "kinetics.eigen_refresh_dr_frac") return s.eigen_refresh_dr_frac;
    if (path == "kinetics.t_max_s") return s.t_max_s;
    if (path == "kinetics.quench_epsilon") return s.quench_epsilon;
    if (path == "kinetics.eigen_refresh_gens") return static_cast<double>(s.eigen_refresh_gens);
    if (path == "transport.sim_neutrons") return static_cast<double>(s.sim_neutrons);
    if (path == "hydro.every_gens") return static_cast<double>(s.hydro_every_gens);
    if (path == "eigen.batch") return static_cast<double>(s.eigen_batch);
    if (path == "lenses.jitter_ns") {
        return s.lens_jitter_ns ? std::optional<double>(*s.lens_jitter_ns) : std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

Scenario Scenario::load(const std::filesystem::path& toml_path) {
    // …/<root>/data/scenarios/<name>.toml → <root>
    std::filesystem::path root = toml_path.parent_path();
    for (int up = 0; up < 2 && root.has_parent_path(); ++up) {
        root = root.parent_path();
    }
    return load(toml_path, root, nullptr);
}

Scenario Scenario::load(const std::filesystem::path& path, const std::filesystem::path& data_root,
                        std::vector<LoadWarning>* warnings) {
    toml::table doc;
    try {
        doc = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
        throw LoadError(path, "<document>", std::string("invalid TOML: ") + err.description().data());
    }

    check_layers_placement(doc, path);
    reject_unknown_keys(doc, kTopLevelKeys, path, "");

    Scenario out;
    const auto version = doc["schema_version"].value<int64_t>();
    require(version.has_value(), path, "schema_version", "is REQUIRED and must be an integer");
    require(*version == 1, path, "schema_version",
            "must be 1; loaders MUST reject unknown versions. Got " + std::to_string(*version));

    out.name = require_string(doc, "name", path, "");

    const auto seed = doc["seed"].value<int64_t>();
    require(seed.has_value(), path, "seed",
            "is REQUIRED and must be an integer. Determinism (02 §4/D9) depends on a recorded "
            "seed; there is no wall-clock fallback");
    out.seed = *seed;

    const std::string mode = require_string(doc, "mode", path, "");
    if (mode == "alpha") {
        out.mode = Mode::Alpha;
    } else if (mode == "eigen_only") {
        out.mode = Mode::EigenOnly;
    } else if (mode == "fixed_source") {
        out.mode = Mode::FixedSource;
    } else if (mode == "td") {
        // Not "unsupported" — explicitly a v1 validation error, so say why.
        throw LoadError(path, "mode",
                        "\"td\" (time-dependent) is a v1 VALIDATION ERROR, not an unimplemented "
                        "option: D3/ADR-003 makes alpha-mode the default kinetics and keeps "
                        "TD-mode interface-only as the escalation path if the quasi-static q "
                        "diagnostic cannot be controlled. Use alpha | eigen_only | fixed_source");
    } else {
        throw LoadError(path, "mode", "must be one of alpha | eigen_only | fixed_source; got \""
                                          + mode + "\"");
    }

    // ---- [data] ----------------------------------------------------------
    const toml::table& data = require_table(doc, "data", path);
    reject_unknown_keys(data, {"xs_set", "materials_dir"}, path, "data");
    out.xs_set = require_string(data, "xs_set", path, "data");
    out.materials_dir = data_root / require_string(data, "materials_dir", path, "data");
    out.xs_path = data_root / "data" / "xs" / (out.xs_set + ".json");

    parse_layers(doc, path, out);

    // ---- [time] ----------------------------------------------------------
    const toml::table& time = require_table(doc, "time", path);
    reject_unknown_keys(time, {"t_zero"}, path, "time");
    out.t_zero = require_string(time, "t_zero", path, "time");
    require(out.t_zero == "he_initiation", path, "time.t_zero",
            "must be \"he_initiation\": t=0 is outermost-HE initiation (D6/ADR-010), and "
            "initiator.t_fire_s is measured relative to peak compression. Got \"" + out.t_zero
                + "\"");

    // ---- [initiator] -----------------------------------------------------
    const toml::table& initiator = require_table(doc, "initiator", path);
    reject_unknown_keys(initiator, {"strength_n_per_s", "t_fire_s", "pulse_width_s"}, path,
                        "initiator");
    out.initiator_strength_n_per_s = require_number(initiator, "strength_n_per_s", path,
                                                    "initiator");
    out.initiator_t_fire_s = require_number(initiator, "t_fire_s", path, "initiator");
    out.initiator_pulse_width_s = require_number(initiator, "pulse_width_s", path, "initiator");
    require(out.initiator_strength_n_per_s >= 0.0, path, "initiator.strength_n_per_s",
            "must be non-negative");
    require(out.initiator_pulse_width_s > 0.0, path, "initiator.pulse_width_s",
            "must be positive");

    // ---- [compression] ---------------------------------------------------
    const toml::table& compression = require_table(doc, "compression", path);
    reject_unknown_keys(compression, {"tier", "ratio", "t_c_s"}, path, "compression");
    out.compression_tier = static_cast<int>(require_integer(compression, "tier", path,
                                                            "compression"));
    require(out.compression_tier == 1 || out.compression_tier == 2, path, "compression.tier",
            "must be 1 or 2. Tier 3 (1D Lagrangian) is a stretch interface only (D5/ADR-005) "
            "and no CFD code may be introduced. Got " + std::to_string(out.compression_tier));
    out.compression_ratio = require_number(compression, "ratio", path, "compression");
    require(out.compression_ratio >= 1.0, path, "compression.ratio",
            "must be at least 1.0 (1.0 = uncompressed); got "
                + std::to_string(out.compression_ratio));

    if (const auto* t_c = compression.get("t_c_s")) {
        const auto value = t_c->value<double>();
        require(value.has_value(), path, "compression.t_c_s", "must be a number");
        if (out.compression_tier == 2) {
            // MAJ-09: at Tier 2 t_c is DERIVED from the Guderley form. Honouring
            // a supplied value would silently override a derived quantity.
            if (warnings != nullptr) {
                warnings->push_back({"compression.t_c_s",
                                     "ignored: t_c is DERIVED at tier 2 (01 §5, MAJ-09); the "
                                     "field is Tier-1 only"});
            }
        } else {
            out.compression_t_c_s = *value;
            require(*value > 0.0, path, "compression.t_c_s", "must be positive at tier 1");
        }
    } else {
        require(out.compression_tier != 1, path, "compression.t_c_s",
                "is REQUIRED at tier 1 (the parametric map needs an explicit collapse time)");
    }

    // ---- [kinetics] ------------------------------------------------------
    const toml::table& kinetics = require_table(doc, "kinetics", path);
    reject_unknown_keys(kinetics, {"generation_time_s_initial", "eigen_refresh_gens",
                                   "eigen_refresh_dr_frac", "t_max_s", "quench_epsilon"},
                        path, "kinetics");
    out.generation_time_s_initial = require_number(kinetics, "generation_time_s_initial", path,
                                                   "kinetics");
    out.eigen_refresh_gens = static_cast<int>(require_integer(kinetics, "eigen_refresh_gens",
                                                              path, "kinetics"));
    out.eigen_refresh_dr_frac = require_number(kinetics, "eigen_refresh_dr_frac", path, "kinetics");
    out.t_max_s = require_number(kinetics, "t_max_s", path, "kinetics");
    out.quench_epsilon = require_number(kinetics, "quench_epsilon", path, "kinetics");

    require(out.generation_time_s_initial > 0.0, path, "kinetics.generation_time_s_initial",
            "must be positive");
    require(out.eigen_refresh_gens >= 1, path, "kinetics.eigen_refresh_gens",
            "must be at least 1 generation");
    require(out.eigen_refresh_dr_frac > 0.0, path, "kinetics.eigen_refresh_dr_frac",
            "must be positive");
    require(out.t_max_s > 0.0, path, "kinetics.t_max_s", "must be positive");
    require(out.quench_epsilon > 0.0 && out.quench_epsilon < 1.0, path, "kinetics.quench_epsilon",
            "must lie in (0, 1): the burn terminates when F_n < quench_epsilon*F_peak (E6), NOT "
            "at k < 1 (BLK-03)");

    // ---- [eigen] ---------------------------------------------------------
    const toml::table& eigen = require_table(doc, "eigen", path);
    reject_unknown_keys(eigen, {"batch", "inactive", "active"}, path, "eigen");
    out.eigen_batch = require_integer(eigen, "batch", path, "eigen");
    out.eigen_inactive = static_cast<int>(require_integer(eigen, "inactive", path, "eigen"));
    out.eigen_active = static_cast<int>(require_integer(eigen, "active", path, "eigen"));
    require(out.eigen_batch > 0, path, "eigen.batch", "must be positive");
    require(out.eigen_inactive >= 1, path, "eigen.inactive",
            "must be at least 1: inactive cycles are what let the fission source converge before "
            "tallying (05 §2)");
    require(out.eigen_active >= 1, path, "eigen.active", "must be at least 1");

    // ---- [transport] / [hydro] -------------------------------------------
    const toml::table& transport = require_table(doc, "transport", path);
    reject_unknown_keys(transport, {"sim_neutrons"}, path, "transport");
    out.sim_neutrons = require_integer(transport, "sim_neutrons", path, "transport");
    require(out.sim_neutrons > 0, path, "transport.sim_neutrons", "must be positive");

    const toml::table& hydro = require_table(doc, "hydro", path);
    reject_unknown_keys(hydro, {"every_gens"}, path, "hydro");
    out.hydro_every_gens = static_cast<int>(require_integer(hydro, "every_gens", path, "hydro"));
    require(out.hydro_every_gens >= 1, path, "hydro.every_gens", "must be an integer >= 1");
    if (out.hydro_every_gens < out.eigen_refresh_gens && warnings != nullptr) {
        warnings->push_back({"hydro.every_gens",
                             "is below kinetics.eigen_refresh_gens; each hydro step can force an "
                             "eigen refresh, so the refresh interval will not be honoured (03 §4)"});
    }

    // ---- [lenses] — Stage 3 only -----------------------------------------
    if (const auto* lenses = doc.get("lenses")) {
        const auto* table = lenses->as_table();
        require(table != nullptr, path, "lenses", "must be a table");
        reject_unknown_keys(*table, {"count", "jitter_ns"}, path, "lenses");
        out.lens_count = static_cast<int>(require_integer(*table, "count", path, "lenses"));
        out.lens_jitter_ns = require_number(*table, "jitter_ns", path, "lenses");
        require(*out.lens_count > 0, path, "lenses.count", "must be positive");
        require(*out.lens_jitter_ns >= 0.0, path, "lenses.jitter_ns", "must be non-negative");
    }

    // ---- [source] — REQUIRED iff mode = fixed_source ----------------------
    const auto* source_node = doc.get("source");
    if (out.mode == Mode::FixedSource) {
        require(source_node != nullptr, path, "source",
                "is REQUIRED when mode = \"fixed_source\" (03 §4; 05 §1 SourceSpec mirrors it)");
    } else if (source_node != nullptr && warnings != nullptr) {
        // Present but inapplicable. Ignored WITH a warning rather than rejected:
        // 03 §4's own canonical example carries a [source] block under
        // mode = "alpha" as a schema illustration, and 03's header says tests
        // parse the examples verbatim. This mirrors how compression.t_c_s is
        // handled at tier 2 — the field is still validated, just not consumed.
        warnings->push_back({"source",
                             "ignored: [source] is consumed only when mode = \"fixed_source\" "
                             "(03 §4); this scenario runs mode = \"" + mode + "\""});
    }
    if (source_node != nullptr) {
        const auto* table = source_node->as_table();
        require(table != nullptr, path, "source", "must be a table");
        reject_unknown_keys(*table, {"kind", "position_cm", "r_cm", "spectrum", "mono_MeV",
                                     "angular", "sim_particles"},
                            path, "source");
        SourceSpec spec;
        spec.kind = require_string(*table, "kind", path, "source");
        require(kSourceKinds.count(spec.kind) == 1, path, "source.kind",
                "must be one of " + join(kSourceKinds) + "; got \"" + spec.kind + "\"");
        spec.spectrum = require_string(*table, "spectrum", path, "source");
        require(kSpectra.count(spec.spectrum) == 1, path, "source.spectrum",
                "must be one of " + join(kSpectra) + "; got \"" + spec.spectrum + "\"");
        spec.angular = require_string(*table, "angular", path, "source");
        require(kAngular.count(spec.angular) == 1, path, "source.angular",
                "must be one of " + join(kAngular) + "; got \"" + spec.angular + "\"");

        if (const auto* position = table->get("position_cm")) {
            const auto* array = position->as_array();
            require(array != nullptr && array->size() == 3, path, "source.position_cm",
                    "must be [x, y, z] in cm");
            for (std::size_t i = 0; i < 3; ++i) {
                const auto v = array->get(i)->value<double>();
                require(v.has_value(), path, "source.position_cm", "entries must be numbers");
                spec.position_cm[i] = *v;
            }
        }
        if (spec.kind == "shell" || spec.kind == "volume") {
            spec.r_cm = require_number(*table, "r_cm", path, "source");
            require(spec.r_cm > 0.0, path, "source.r_cm",
                    "must be positive for a shell or volume source");
        }
        if (spec.spectrum == "mono") {
            spec.mono_MeV = require_number(*table, "mono_MeV", path, "source");
            require(spec.mono_MeV > 0.0, path, "source.mono_MeV",
                    "must be positive when spectrum = \"mono\"");
        }
        spec.sim_particles = require_integer(*table, "sim_particles", path, "source");
        require(spec.sim_particles > 0, path, "source.sim_particles", "must be positive");
        out.source = spec;
    }

    // ---- [overrides] ------------------------------------------------------
    if (const auto* overrides = doc.get("overrides")) {
        const auto* table = overrides->as_table();
        require(table != nullptr, path, "overrides", "must be a table");
        for (const auto& [key, value] : *table) {
            const std::string target(key.str());
            if (const auto number = value.value<double>()) {
                out.overrides.emplace_back(target, *number);
            } else if (const auto text = value.value<std::string>()) {
                out.overrides.emplace_back(target, *text);
            } else if (const auto* array = value.as_array()) {
                std::vector<double> values;
                for (std::size_t i = 0; i < array->size(); ++i) {
                    const auto v = array->get(i)->value<double>();
                    require(v.has_value(), path, "overrides." + target,
                            "array overrides must contain only numbers");
                    values.push_back(*v);
                }
                out.overrides.emplace_back(target, values);
            } else {
                throw LoadError(path, "overrides." + target,
                                "must be a number, string or array of numbers");
            }
        }
        out.apply_overrides(out.overrides);
    }

    // ---- [output] ---------------------------------------------------------
    const toml::table& output = require_table(doc, "output", path);
    reject_unknown_keys(output, {"tallies", "checkpoint_every_gens", "dump_fields", "dump_stride",
                                 "dump_max_frames", "dump_budget_gb"},
                        path, "output");
    const auto* tallies = output.get("tallies");
    require(tallies != nullptr && tallies->is_array(), path, "output.tallies",
            "is REQUIRED and must be an array");
    for (std::size_t i = 0; i < tallies->as_array()->size(); ++i) {
        const auto name = tallies->as_array()->get(i)->value<std::string>();
        require(name.has_value(), path, "output.tallies", "entries must be strings");
        require(kTallyVocabulary.count(*name) == 1, path, "output.tallies",
                "\"" + *name + "\" is not in the closed vocabulary; unknown tallies are a "
                "validation error (03 §4). Permitted: " + join(kTallyVocabulary));
        out.tallies.push_back(*name);
    }
    out.checkpoint_every_gens = static_cast<int>(require_integer(output, "checkpoint_every_gens",
                                                                 path, "output"));
    require(out.checkpoint_every_gens >= 1, path, "output.checkpoint_every_gens",
            "must be at least 1: checkpoint/resume is a hard requirement (D9/ADR-008)");
    if (const auto* dump = output.get("dump_fields")) {
        out.dump_fields = dump->value<bool>().value_or(false);
    }
    out.dump_stride = static_cast<int>(require_integer(output, "dump_stride", path, "output"));
    require(out.dump_stride >= 1, path, "output.dump_stride", "must be at least 1");
    out.dump_max_frames = static_cast<int>(require_integer(output, "dump_max_frames", path,
                                                           "output"));
    out.dump_budget_gb = require_number(output, "dump_budget_gb", path, "output");

    if (out.dump_fields && warnings != nullptr) {
        // 03 §9: field dumps are ~128 MiB per frame.
        constexpr double kFrameGiB = 128.0 / 1024.0;
        const double projected = kFrameGiB * out.dump_max_frames;
        if (projected > out.dump_budget_gb) {
            warnings->push_back({"output.dump_budget_gb",
                                 "projected dump volume " + std::to_string(projected)
                                     + " GB exceeds the budget "
                                     + std::to_string(out.dump_budget_gb)
                                     + " GB (03 §4 guardrail)"});
        }
    }

    parse_ui(doc, path, out);

    // Every annotated value must sit inside its declared range (03 §4).
    for (const auto& [target, ann] : out.ui) {
        const auto value = value_at(out, target);
        if (!value.has_value()) {
            continue;  // annotation for a path this schema does not expose numerically
        }
        require(*value >= ann.range_lo && *value <= ann.range_hi, path, "ui.\"" + target + "\"",
                "the scenario value " + std::to_string(*value) + " lies outside the declared "
                "range [" + std::to_string(ann.range_lo) + ", " + std::to_string(ann.range_hi)
                    + "]");
    }

    return out;
}

void Scenario::apply_overrides(const std::vector<std::pair<std::string, ParamValue>>& items) {
    for (const auto& [key, value] : items) {
        (void)value;
        // One canonical grammar (04 §6). Ad-hoc prefixes such as
        // `tamper_override.*` are rejected rather than ignored.
        const bool is_layer_by_id = key.rfind("layers.", 0) == 0;
        const bool is_layer_positional = key.rfind("layers[", 0) == 0;
        const bool is_material = key.rfind("materials.", 0) == 0;
        const bool is_xs = key.rfind("xs.", 0) == 0;
        const bool is_section = key.find('.') != std::string::npos && !is_layer_by_id
                                && !is_material && !is_xs && !is_layer_positional;

        if (is_material || is_xs) {
            // Data overrides touch PUBLIC/DECLASSIFIED values, so the run is no
            // longer canonical and MUST NOT be used as gate evidence (03 §4).
            non_canonical = true;
            continue;
        }
        if (is_layer_by_id || is_layer_positional) {
            continue;
        }
        if (is_section) {
            const std::string section = key.substr(0, key.find('.'));
            static const std::set<std::string> kSections = {
                "initiator", "compression", "kinetics", "eigen", "transport", "hydro",
                "lenses", "source", "output", "time", "data",
            };
            if (kSections.count(section) == 1) {
                continue;
            }
        }
        throw LoadError("<overrides>", key,
                        "unrecognised override prefix. The grammar is exactly: layers.<id>.<field>, "
                        "layers[<int>].<field> (positional, discouraged), materials.<name>.<field>, "
                        "xs.<iso>.<field>, section.key (04 §6). Ad-hoc prefixes are rejected");
    }
}

LoadedScenario load_scenario(const std::filesystem::path& toml_path,
                             const std::filesystem::path& data_root,
                             std::vector<LoadWarning>* warnings) {
    Scenario scenario = Scenario::load(toml_path, data_root, warnings);

    require(std::filesystem::exists(scenario.xs_path), toml_path, "data.xs_set",
            "\"" + scenario.xs_set + "\" does not resolve to " + scenario.xs_path.string());
    xs::FewGroupXS xs_set = xs::FewGroupXS::load(scenario.xs_path);
    require(xs_set.name() == scenario.xs_set, toml_path, "data.xs_set",
            "the cross-section set's own \"name\" (\"" + xs_set.name()
                + "\") must equal data.xs_set (\"" + scenario.xs_set + "\") (03 §4)");

    material::MaterialLib materials =
        material::MaterialLib::load_dir(scenario.materials_dir, xs_set, warnings);

    for (const auto& layer : scenario.layers) {
        require(materials.contains(layer.material), toml_path,
                "layers." + layer.id + ".material",
                "\"" + layer.material + "\" does not resolve under data.materials_dir ("
                    + scenario.materials_dir.string() + ")");
    }

    // 03 §3: Jezebel is ~4.5 wt% Pu-240 and must not borrow the Trinity pit
    // composition. Named explicitly because the two differ by an amount that
    // moves k well beyond G0b's tolerance while looking entirely plausible.
    if (scenario.name == "jezebel") {
        for (const auto& layer : scenario.layers) {
            require(layer.material != "pu_ga_delta", toml_path,
                    "layers." + layer.id + ".material",
                    "the jezebel scenario MUST reference pu_ga_jezebel (~4.5 wt% Pu-240), never "
                    "pu_ga_delta (03 §3)");
        }
    }

    return LoadedScenario{std::move(scenario), std::move(xs_set), std::move(materials)};
}

}  // namespace ns::scenario

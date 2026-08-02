// canonical_hash (04 §6).
//
// The hash is taken over the PARSED struct, not the source text. Every
// stability property 04 §6 requires then holds by construction rather than by
// careful text handling: key order, comments, CRLF vs LF and equivalent float
// spellings all vanish at parse time, and schema defaults are materialized
// because the struct always carries them.

#include "core/hash/sha256.h"
#include "core/scenario/scenario.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ns::scenario {
namespace {

/// Shortest round-trip float text, so parse(emit(x)) == x bit for bit (04 §6).
std::string canonical_double(double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec == std::errc{}) {
        return std::string(buffer.data(), result.ptr);
    }
    // %.17g always round-trips a double, just not always shortest.
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return std::string(buffer.data(), static_cast<std::size_t>(written > 0 ? written : 0));
}

/// SHA-256 of a file with CRLF folded to LF.
///
/// 04 §6 lists CRLF/LF invariance among the required stability properties, and
/// .gitattributes normalises the tree to LF for the same reason: a checkout on
/// Windows must not produce a different scenario identity from one on Linux.
std::string hash_file_lf(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return "missing";
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string text = buffer.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return hash::sha256_hex(text);
}

}  // namespace

std::string Scenario::canonical_form() const {
    // A flat, lexicographically sorted projection rather than re-emitted TOML.
    // 04 §6's "keys sorted at every level, tables before arrays-of-tables" rule
    // exists to make the byte string unique; a flat sorted map achieves that
    // with no residual ambiguity about nesting order, and nothing ever re-parses
    // this form — it is only hashed and diffed.
    std::map<std::string, std::string> fields;

    auto put = [&](const std::string& key, const std::string& value) { fields[key] = value; };
    auto put_num = [&](const std::string& key, double value) {
        fields[key] = canonical_double(value);
    };
    auto put_int = [&](const std::string& key, long long value) {
        fields[key] = std::to_string(value);
    };

    put("schema_version", std::to_string(schema_version));
    put("name", name);
    put_int("seed", seed);
    put("mode", mode == Mode::Alpha       ? "alpha"
                : mode == Mode::EigenOnly ? "eigen_only"
                                          : "fixed_source");
    put("data.xs_set", xs_set);
    put("data.materials_dir", materials_dir.generic_string());
    put("time.t_zero", t_zero);

    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string at = "layers[" + std::to_string(i) + "].";
        put(at + "id", layers[i].id);
        put_num(at + "r_outer_cm", layers[i].r_outer_cm);
        put(at + "material", layers[i].material);
        put(at + "status", layers[i].status);
    }

    put_num("initiator.strength_n_per_s", initiator_strength_n_per_s);
    put_num("initiator.t_fire_s", initiator_t_fire_s);
    put_num("initiator.pulse_width_s", initiator_pulse_width_s);

    put_int("compression.tier", compression_tier);
    put_num("compression.ratio", compression_ratio);
    // An absent optional and a present one must produce different bytes, never
    // the same ones by omission.
    put("compression.t_c_s", compression_t_c_s ? canonical_double(*compression_t_c_s) : "none");

    put_num("kinetics.generation_time_s_initial", generation_time_s_initial);
    put_int("kinetics.eigen_refresh_gens", eigen_refresh_gens);
    put_num("kinetics.eigen_refresh_dr_frac", eigen_refresh_dr_frac);
    put_num("kinetics.t_max_s", t_max_s);
    put_num("kinetics.quench_epsilon", quench_epsilon);

    put_int("eigen.batch", eigen_batch);
    put_int("eigen.inactive", eigen_inactive);
    put_int("eigen.active", eigen_active);
    put_int("transport.sim_neutrons", sim_neutrons);
    put_int("hydro.every_gens", hydro_every_gens);

    put("lenses.count", lens_count ? std::to_string(*lens_count) : "none");
    put("lenses.jitter_ns", lens_jitter_ns ? canonical_double(*lens_jitter_ns) : "none");

    if (source) {
        put("source.kind", source->kind);
        put("source.spectrum", source->spectrum);
        put("source.angular", source->angular);
        put_num("source.r_cm", source->r_cm);
        put_num("source.mono_MeV", source->mono_MeV);
        put_int("source.sim_particles", source->sim_particles);
        for (int i = 0; i < 3; ++i) {
            put_num("source.position_cm[" + std::to_string(i) + "]", source->position_cm[i]);
        }
    } else {
        put("source", "none");
    }

    // Tallies are a set, not a sequence: ["k","yield"] and ["yield","k"] request
    // the same run and must hash identically.
    std::vector<std::string> sorted_tallies = tallies;
    std::sort(sorted_tallies.begin(), sorted_tallies.end());
    for (std::size_t i = 0; i < sorted_tallies.size(); ++i) {
        put("output.tallies[" + std::to_string(i) + "]", sorted_tallies[i]);
    }
    put_int("output.checkpoint_every_gens", checkpoint_every_gens);
    put("output.dump_fields", dump_fields ? "true" : "false");
    put_int("output.dump_stride", dump_stride);
    put_int("output.dump_max_frames", dump_max_frames);
    put_num("output.dump_budget_gb", dump_budget_gb);

    // [ui.*] is presentation metadata, but a changed range changes which values
    // a gate run will accept, so it is part of the identity.
    for (const auto& [target, ann] : ui) {
        const std::string at = "ui." + target + ".";
        put(at + "label", ann.label);
        put(at + "status", ann.status);
        put_num(at + "range.lo", ann.range_lo);
        put_num(at + "range.hi", ann.range_hi);
        put(at + "display_unit", ann.display_unit.value_or("none"));
        put(at + "display_scale",
            ann.display_scale ? canonical_double(*ann.display_scale) : "none");
    }

    std::string out;
    for (const auto& [key, value] : fields) {
        out += key;
        out += '=';
        out += value;
        out += '\n';
    }
    return out;
}

std::string Scenario::canonical_hash() const {
    // 04 §6 step 3: canonical scenario bytes + sha256(resolved xs file) +
    // sha256(each resolved material file, in order) + the overrides block.
    hash::Sha256 hasher;
    hasher.update(canonical_form());

    hasher.update("xs=");
    hasher.update(hash_file_lf(xs_path));
    hasher.update("\n");

    std::vector<std::filesystem::path> material_files;
    if (std::filesystem::is_directory(materials_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(materials_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                material_files.push_back(entry.path());
            }
        }
    }
    // Directory iteration order is unspecified; sort so two machines agree.
    std::sort(material_files.begin(), material_files.end());
    for (const auto& file : material_files) {
        hasher.update("material=");
        hasher.update(file.filename().generic_string());
        hasher.update(":");
        hasher.update(hash_file_lf(file));
        hasher.update("\n");
    }

    std::map<std::string, std::string> sorted_overrides;
    for (const auto& [key, value] : overrides) {
        std::string text;
        if (const auto* d = std::get_if<double>(&value)) {
            text = canonical_double(*d);
        } else if (const auto* i = std::get_if<long long>(&value)) {
            text = std::to_string(*i);
        } else if (const auto* b = std::get_if<bool>(&value)) {
            text = *b ? "true" : "false";
        } else if (const auto* str = std::get_if<std::string>(&value)) {
            text = *str;
        } else if (const auto* vec = std::get_if<std::vector<double>>(&value)) {
            for (const double v : *vec) {
                text += canonical_double(v);
                text += ',';
            }
        }
        sorted_overrides[key] = text;
    }
    for (const auto& [key, text] : sorted_overrides) {
        hasher.update("override=");
        hasher.update(key);
        hasher.update("=");
        hasher.update(text);
        hasher.update("\n");
    }

    return hasher.hex_digest();
}

}  // namespace ns::scenario

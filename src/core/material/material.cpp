#include "core/material/material.h"

#include "core/constants/constants.h"
#include "core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace ns::material {
namespace {

using nlohmann::json;

constexpr double kFractionSumTol = 1e-6;
constexpr double kCompositionWarnPp = 0.2;  // percentage points (03 §3)

json parse_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    require(stream.good(), path, "<file>", "cannot be opened for reading");
    try {
        return json::parse(stream);
    } catch (const json::parse_error& err) {
        throw LoadError(path, "<document>", std::string("invalid JSON: ") + err.what());
    }
}

/// Weight percent of the species whose names start with `prefix` (e.g. "Ga").
double weight_pct_prefix(const Material& mat, std::string_view prefix) {
    double numerator = 0.0;
    for (const auto& c : mat.fracs) {
        if (c.species.rfind(prefix, 0) == 0) {
            numerator += c.atom_fraction * c.molar_mass;
        }
    }
    return 100.0 * numerator / mat.mean_molar_mass;
}

void check_composition(const json& doc, const std::filesystem::path& path, const Material& mat,
                       std::vector<LoadWarning>* warnings) {
    if (!doc.contains("composition_check") || warnings == nullptr) {
        return;
    }
    const json& check = doc.at("composition_check");
    require(check.is_object(), path, "composition_check", "must be an object");

    // 03 §3: the loader recomputes the declared weight percents and WARNs above
    // 0.2 pp. A WARN, not an error — these are derived readouts of a composition
    // the isotope fractions already fix, so a mismatch means the note is stale,
    // not that the material is unusable.
    if (check.contains("Ga_wt_pct")) {
        const double declared = check.at("Ga_wt_pct").get<double>();
        const double computed = weight_pct_prefix(mat, "Ga");
        if (std::abs(computed - declared) > kCompositionWarnPp) {
            warnings->push_back({path.filename().string() + ": composition_check.Ga_wt_pct",
                                 "declared " + std::to_string(declared) + " pp, computed "
                                     + std::to_string(computed) + " pp (> 0.2 pp, 03 §3)"});
        }
    }
    if (check.contains("Pu240_wt_pct_of_Pu")) {
        const double declared = check.at("Pu240_wt_pct_of_Pu").get<double>();
        double pu_mass = 0.0, pu240_mass = 0.0;
        for (const auto& c : mat.fracs) {
            if (c.species.rfind("Pu", 0) == 0) {
                pu_mass += c.atom_fraction * c.molar_mass;
                if (c.species == "Pu240") {
                    pu240_mass += c.atom_fraction * c.molar_mass;
                }
            }
        }
        const double computed = pu_mass > 0.0 ? 100.0 * pu240_mass / pu_mass : 0.0;
        if (std::abs(computed - declared) > kCompositionWarnPp) {
            warnings->push_back({path.filename().string() + ": composition_check.Pu240_wt_pct_of_Pu",
                                 "declared " + std::to_string(declared) + " pp, computed "
                                     + std::to_string(computed) + " pp (> 0.2 pp, 03 §3)"});
        }
    }
}

}  // namespace

double Material::number_density(std::size_t index) const {
    const auto& c = fracs.at(index);
    return c.atom_fraction * density / mean_molar_mass * consts::avogadro_constant;
}

MatXS mix(const Material& mat, const xs::FewGroupXS& xs_set) {
    const auto groups = static_cast<std::size_t>(xs_set.groups());
    MatXS macro;
    macro.sigma_f.assign(groups, 0.0);
    macro.sigma_c.assign(groups, 0.0);
    macro.sigma_s.assign(groups, 0.0);
    macro.sigma_t.assign(groups, 0.0);
    macro.sigma_tr.assign(groups, 0.0);
    macro.nu_sigma_f.assign(groups, 0.0);

    constexpr double kBarnToCm2 = 1e-24;
    for (std::size_t i = 0; i < mat.fracs.size(); ++i) {
        const Constituent& c = mat.fracs[i];
        if (c.iso == nullptr) {
            // Has a molar mass but no cross sections in this set (a structural
            // element in an actinide-only dataset). It contributes mass — and
            // therefore number density — but nothing to Sigma.
            continue;
        }
        const double n = mat.number_density(i) * kBarnToCm2;
        for (std::size_t g = 0; g < groups; ++g) {
            const auto& gd = c.iso->g[g];
            macro.sigma_f[g] += n * gd.sigma_f;
            macro.sigma_c[g] += n * gd.sigma_c;
            macro.sigma_s[g] += n * gd.sigma_s;
            macro.sigma_t[g] += n * gd.sigma_t();
            macro.sigma_tr[g] += n * gd.sigma_tr();
            macro.nu_sigma_f[g] += n * gd.nu * gd.sigma_f;
        }
    }
    return macro;
}

Material MaterialLib::load_file(const std::filesystem::path& path, const xs::FewGroupXS& xs_set,
                                std::vector<LoadWarning>* warnings) {
    const json doc = parse_file(path);

    require(doc.contains("schema_version"), path, "schema_version", "is REQUIRED");
    const int version = doc.at("schema_version").get<int>();
    require(version == 1, path, "schema_version",
            "must be 1; loaders MUST reject unknown versions. Got " + std::to_string(version));

    Material mat;
    for (const char* field : {"name", "status", "cite"}) {
        require(doc.contains(field) && doc.at(field).is_string(), path, field,
                "is REQUIRED and must be a string");
    }
    mat.name = doc.at("name").get<std::string>();
    mat.status = doc.at("status").get<std::string>();
    mat.cite = doc.at("cite").get<std::string>();

    require(doc.contains("density_g_cm3") && doc.at("density_g_cm3").is_number(), path,
            "density_g_cm3", "is REQUIRED and must be a number");
    mat.density = doc.at("density_g_cm3").get<double>();
    require(mat.density > 0.0, path, "density_g_cm3",
            "must be positive; got " + std::to_string(mat.density));

    require(doc.contains("isotopes") && doc.at("isotopes").is_object(), path, "isotopes",
            "is REQUIRED and must be an object of species -> atom fraction");
    const json& isotopes = doc.at("isotopes");
    require(!isotopes.empty(), path, "isotopes", "must name at least one species");

    double sum = 0.0;
    for (const auto& [species, node] : isotopes.items()) {
        require(node.is_number(), path, "isotopes." + species, "atom fraction must be a number");
        const double fraction = node.get<double>();
        require(fraction >= 0.0 && fraction <= 1.0, path, "isotopes." + species,
                "atom fraction must lie in [0, 1]; got " + std::to_string(fraction));

        // Appendix §3 completeness rule: a HARD error, never a warning. Without
        // a molar mass the number density is silently wrong, and with it every
        // macroscopic cross section computed below (04 §5).
        require(consts::has_molar_mass(species), path, "isotopes." + species,
                "species has no molar-mass constant. 03 §3 forbids natural-abundance expansion "
                "by the loader, so every species needs its own mass; add one to "
                "spec/appendix/constants.md §3 (appendix §3 completeness rule)");

        Constituent c;
        c.species = species;
        c.atom_fraction = fraction;
        c.molar_mass = consts::molar_mass(species);
        c.iso = xs_set.has_isotope(species) ? &xs_set.isotope(species) : nullptr;
        mat.fracs.push_back(c);
        sum += fraction;
    }

    require(std::abs(sum - 1.0) <= kFractionSumTol, path, "isotopes",
            "atom fractions MUST sum to 1.0 +/- 1e-6 (03 §3); sum = " + std::to_string(sum));

    // 04 §5's sorted-name index order — the same ordering LayerStack uses, so
    // an index means the same thing in both.
    std::sort(mat.fracs.begin(), mat.fracs.end(),
              [](const Constituent& a, const Constituent& b) { return a.species < b.species; });

    for (const auto& c : mat.fracs) {
        mat.mean_molar_mass += c.atom_fraction * c.molar_mass;
    }
    require(mat.mean_molar_mass > 0.0, path, "isotopes",
            "fraction-weighted mean molar mass must be positive");

    mat.macro = mix(mat, xs_set);

    check_composition(doc, path, mat, warnings);
    return mat;
}

MaterialLib MaterialLib::load_dir(const std::filesystem::path& dir, const xs::FewGroupXS& xs_set,
                                  std::vector<LoadWarning>* warnings) {
    require(std::filesystem::is_directory(dir), dir, "<materials_dir>",
            "does not exist or is not a directory");

    MaterialLib lib;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    // Directory iteration order is unspecified; sort so the resulting index
    // order is reproducible across machines (determinism, 02 §4).
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        Material mat = load_file(file, xs_set, warnings);
        require(!lib.contains(mat.name), file, "name",
                "duplicate material name \"" + mat.name + "\" in " + dir.string());
        lib.materials_.push_back(std::move(mat));
    }

    std::sort(lib.materials_.begin(), lib.materials_.end(),
              [](const Material& a, const Material& b) { return a.name < b.name; });
    return lib;
}

bool MaterialLib::contains(std::string_view name) const { return index_of(name) >= 0; }

int MaterialLib::index_of(std::string_view name) const {
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        if (materials_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Material& MaterialLib::at(std::string_view name) const {
    const int index = index_of(name);
    if (index < 0) {
        throw LoadError("<materials>", std::string(name), "material is not loaded");
    }
    return materials_[static_cast<std::size_t>(index)];
}

}  // namespace ns::material

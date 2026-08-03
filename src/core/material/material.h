// Materials and macroscopic cross sections (03 §3, 04 §5).
//
// n_i = frac_i * rho / Mbar * N_A, with Mbar the fraction-weighted mean molar
// mass. Every species must resolve to its own molar mass — 03 §3 forbids
// natural-abundance expansion by the loader precisely so that the choice is
// explicit, and appendix §3 makes a missing mass a hard error because it would
// otherwise corrupt every macroscopic cross section silently.

#pragma once

#include "core/xs/xs.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ns {
struct LoadWarning;
}

namespace ns::material {

/// Macroscopic cross sections, per group, for one material.
struct MatXS {
    std::vector<double> sigma_f;    // 1/cm
    std::vector<double> sigma_c;
    std::vector<double> sigma_s;
    std::vector<double> sigma_t;
    std::vector<double> sigma_tr;
    std::vector<double> nu_sigma_f;  // TOTAL nu-bar weighted (ADR-013)
};

struct Constituent {
    std::string species;
    double atom_fraction = 0.0;
    double molar_mass = 0.0;
    /// Null when the species has a molar mass but no cross sections in the set
    /// (e.g. a structural element in an actinide-only test set).
    const xs::IsotopeXS* iso = nullptr;
};

struct Material {
    std::string name;
    double density = 0.0;  // g/cm^3
    std::string status;
    std::string cite;
    std::vector<Constituent> fracs;  // sorted by species name
    double mean_molar_mass = 0.0;
    MatXS macro;

    /// Number density of a constituent, atoms/cm^3.
    double number_density(std::size_t index) const;
};

/// Builds the macroscopic per-group cross sections for a material (04 §3).
///
/// Declared here rather than in core/xs because it needs a Material; 04 §3
/// names it in the cross-section section, but the dependency runs the other way.
MatXS mix(const Material& mat, const xs::FewGroupXS& xs);

class MaterialLib {
public:
    /// Loads every *.json in the directory. `xs` resolves cross sections;
    /// warnings (composition_check deviations) are appended to `warnings`.
    static MaterialLib load_dir(const std::filesystem::path& dir, const xs::FewGroupXS& xs,
                               std::vector<LoadWarning>* warnings = nullptr);

    /// Loads exactly one file. Used by tests and by scenario resolution.
    static Material load_file(const std::filesystem::path& json, const xs::FewGroupXS& xs,
                              std::vector<LoadWarning>* warnings = nullptr);

    bool contains(std::string_view name) const;
    int index_of(std::string_view name) const;  // -1 when absent
    const Material& at(std::string_view name) const;
    const std::vector<Material>& all() const noexcept { return materials_; }

    /// A copy with every material's number density scaled by `factor` — models
    /// uniform compression (factor > 1) or disassembly (factor < 1) at fixed
    /// isotopics. Σ ∝ ρ (n_i = frac_i·ρ/Mbar·N_A), so this is the reactivity
    /// handle the hydro drives; mass conservation links it to the radius scale
    /// (ρ/ρ₀ = (r₀/r)³). Scales `density` and the precomputed `macro` (∝ ρ)
    /// together so the two stay consistent, whichever a consumer reads.
    MaterialLib with_density_scale(double factor) const;

private:
    std::vector<Material> materials_;  // sorted by name — 04 §4's name->index order
};

}  // namespace ns::material

// The studio/binding API: DemonCoreAssembly + evaluate (06 §frontends) — M3-T3-g.

#include "api/studio.h"

#include "core/constants/constants.h"
#include "physics/couple/couple.h"
#include "physics/eigen/eigen.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <system_error>

namespace ns::api {

namespace {
using json = nlohmann::ordered_json;

// δ-Pu density DERIVED from the cited pit OD (C-101) + mass (C-102) — exactly the
// pu_ga_delta card's derivation (≈15.23 g/cm³): ρ = m / (4/3 π r³), r = OD/2. No
// magic number: both inputs are ns::consts.
double derived_pit_density_g_cm3() {
    const double r_cm = ns::consts::od_pu_ga_core / 2.0;               // C-101
    const double mass_g = ns::consts::od_pu_ga_core_mass_kg * 1000.0;  // C-102
    const double volume = (4.0 / 3.0) * std::numbers::pi * r_cm * r_cm * r_cm;
    return mass_g / volume;
}

// Bare-sphere radius for a mass at fixed density: r = (3 m / (4 π ρ))^(1/3).
double radius_for_mass(double mass_kg, double density_g_cm3) {
    const double volume = mass_kg * 1000.0 / density_g_cm3;
    return std::cbrt(3.0 * volume / (4.0 * std::numbers::pi));
}

// One SIM isotope's 4-group xs (schema v2). Fast-group fission/scatter, identity
// transfer, chi in group 0. status SIM — clearly a stand-in, not cited data.
json sim_isotope(double nu, double sigma_f, double sigma_c, double sigma_s) {
    const auto four = [](double v) { return json::array({v, v, v, v}); };
    const json identity = json::array({json::array({1.0, 0.0, 0.0, 0.0}),
                                       json::array({0.0, 1.0, 0.0, 0.0}),
                                       json::array({0.0, 0.0, 1.0, 0.0}),
                                       json::array({0.0, 0.0, 0.0, 1.0})});
    return {{"nu", four(nu)},
            {"chi", json::array({1.0, 0.0, 0.0, 0.0})},
            {"sigma_f", four(sigma_f)},
            {"sigma_c", four(sigma_c)},
            {"sigma_s", four(sigma_s)},
            {"sigma_n2n", four(0.0)},
            {"mu_bar", four(0.0)},
            {"beta", 0.0021},
            {"transfer", identity},
            {"cite", "synthetic test medium — not physical data (SIM, pending fast4)"},
            {"status", "SIM"}};
}

std::uint64_t next_assembly_id() {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1);
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

ns::physics::EigenSpec gauge_spec(std::uint64_t seed, std::int64_t batch) {
    ns::physics::EigenSpec spec;
    spec.batch = batch > 0 ? batch : 4000;
    spec.inactive = 8;
    spec.active = 15;
    spec.h_tol = 0.05;   // a usable gauge; sigma_pcm reports the residual MC noise
    spec.seed = seed;
    return spec;
}

}  // namespace

DemonCoreAssembly::DemonCoreAssembly(const StudioConfig& cfg) {
    seed_ = cfg.seed;
    eigen_batch_ = cfg.eigen_batch;
    compression_ratio_ = cfg.compression_ratio > 0.0 ? cfg.compression_ratio : 1.0;
    density_g_cm3_ = derived_pit_density_g_cm3();
    r0_cm_ = radius_for_mass(cfg.pit_mass_kg, density_g_cm3_);

    root_ = std::filesystem::temp_directory_path() /
            ("nukesim_studio_" + std::to_string(next_assembly_id()));
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);

    // SIM xs: Pu-239 fissile (RealSphere-like); Pu-240 a net poison (lower fast
    // fission, higher capture) so more Pu-240 lowers k — the demon-core gauge.
    const json xs = {{"schema_version", 2},
                     {"name", "pu_sim"},
                     {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3})},
                     {"isotopes",
                      {{"Pu239", sim_isotope(2.9, 1.4, 0.15, 4.0)},
                       {"Pu240", sim_isotope(2.5, 0.5, 0.80, 4.0)}}}};
    write_text(root_ / "xs" / "pu.json", xs.dump(2));

    // Pu-240 atom fraction from cfg; Pu-239 the balance (Σ = 1). Ga (3.35 at% in
    // the real card) is omitted in the bare-core SIM stand-in — noted; the real
    // 4-isotope card + cited xs arrive with fast4.
    const double x = cfg.pu240_fraction;
    const json mat = {{"schema_version", 1},
                      {"name", "pit"},
                      {"density_g_cm3", density_g_cm3_},
                      {"status", "SIM"},
                      {"cite", "bare demon-core Pu sphere; density DERIVED from C-101 OD + "
                               "C-102 mass; SIM xs pending fast4"},
                      {"isotopes", {{"Pu239", 1.0 - x}, {"Pu240", x}}}};
    write_text(root_ / "materials" / "pit.json", mat.dump(2));

    xs_ = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root_ / "xs" / "pu.json"));
    materials_ = std::make_unique<ns::material::MaterialLib>(
        ns::material::MaterialLib::load_dir(root_ / "materials", *xs_));
    geometry_ = ns::geom::LayerStack({ns::geom::Layer{"pit", r0_cm_, 0, "SIM"}});
}

DemonCoreAssembly::~DemonCoreAssembly() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
}

ns::geom::LayerStack DemonCoreAssembly::compressed_geometry() const {
    ns::geom::LayerStack g = geometry_;
    // Radius shrinks by ratio^(-1/3); ref_eigen_fn_masscons(r_ref = r0) then raises
    // ρ by exactly the compression ratio (mass-conserving) — compression ignites.
    g.scale_radii(std::cbrt(1.0 / compression_ratio_));
    return g;
}

EvaluateResult evaluate(const DemonCoreAssembly& assembly) {
    const ns::physics::EigenFn eigen = ns::physics::ref_eigen_fn_masscons(
        assembly.materials(), assembly.xs(), gauge_spec(assembly.seed(), assembly.eigen_batch()),
        assembly.seed(), assembly.r0_cm());
    const ns::physics::EigenResult er = eigen(assembly.compressed_geometry());

    EvaluateResult out;
    out.k_eff = er.k;
    out.k_prompt = er.k_prompt();
    out.sigma_pcm = er.sigma_pcm;
    out.ready = er.k_prompt() >= 1.0;   // prompt-supercritical — the burst will ignite
    return out;
}

EvaluateResult evaluate(const StudioConfig& cfg) {
    const DemonCoreAssembly assembly(cfg);
    return evaluate(assembly);
}

std::string to_json(const EvaluateResult& r, int indent) {
    json j;
    j["k_eff"] = r.k_eff;
    j["k_prompt"] = r.k_prompt;
    j["sigma_pcm"] = r.sigma_pcm;
    j["ready"] = r.ready;
    return j.dump(indent);
}

StudioConfig StudioConfig::from_json(const std::string& cfg_json) {
    const json j = json::parse(cfg_json);
    StudioConfig cfg;
    const auto num = [&j](const char* key, double fallback) {
        const auto it = j.find(key);
        return (it != j.end() && it->is_number()) ? it->get<double>() : fallback;
    };
    cfg.pit_mass_kg = num("pit.mass_kg", cfg.pit_mass_kg);
    cfg.pu240_fraction = num("materials.pu_ga_delta.Pu240", cfg.pu240_fraction);
    cfg.compression_ratio = num("compression.ratio", cfg.compression_ratio);
    cfg.initiator_strength_n_per_s = num("initiator.strength_n_per_s", cfg.initiator_strength_n_per_s);
    cfg.generation_time_s_initial =
        num("kinetics.generation_time_s_initial", cfg.generation_time_s_initial);
    cfg.tamper_scale = num("tamper.scale", cfg.tamper_scale);
    cfg.lens_jitter_ns = num("lenses.jitter_ns", cfg.lens_jitter_ns);
    if (const auto it = j.find("seed"); it != j.end() && it->is_number()) {
        cfg.seed = it->get<std::uint64_t>();
    }
    if (const auto it = j.find("eigen.batch"); it != j.end() && it->is_number()) {
        cfg.eigen_batch = it->get<std::int64_t>();
    }
    return cfg;
}

std::string evaluate_json(const std::string& cfg_json) {
    return to_json(evaluate(StudioConfig::from_json(cfg_json)));
}

}  // namespace ns::api

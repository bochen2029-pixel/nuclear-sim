// The studio/binding API: DemonCoreAssembly + evaluate (06 §frontends) — M3-T3-g.

#include "api/studio.h"

#include "core/constants/constants.h"
#include "core/hash/sha256.h"
#include "physics/couple/couple.h"
#include "physics/eigen/eigen.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

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
    const std::string xs_text = xs.dump(2);
    write_text(root_ / "xs" / "pu.json", xs_text);
    xs_sha256_ = ns::hash::sha256_hex(xs_text);

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
    const std::string mat_text = mat.dump(2);
    write_text(root_ / "materials" / "pit.json", mat_text);
    material_sha256_ = ns::hash::sha256_hex(mat_text);

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
    cfg.burst_t_max_s = num("kinetics.t_max_s", cfg.burst_t_max_s);
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

namespace {

// Compact human-readable number for `reasons`.
std::string short_num(double v, int prec = 4) {
    std::ostringstream ss;
    ss << std::setprecision(prec) << v;
    return ss.str();
}

// A stable canonical hash of the demon-core StudioConfig — its scenario identity
// (there is no scenario TOML for the bare-core cfg path; this stands in for
// Scenario::canonical_hash() so unit_id / scenario_sha256 are deterministic).
std::string cfg_canonical_hash(const StudioConfig& cfg) {
    std::ostringstream ss;
    ss << std::setprecision(17) << "demon_core"
       << ";pit_mass_kg=" << cfg.pit_mass_kg << ";pu240=" << cfg.pu240_fraction
       << ";compression=" << cfg.compression_ratio << ";initiator=" << cfg.initiator_strength_n_per_s
       << ";gen_time=" << cfg.generation_time_s_initial;
    return ns::hash::sha256_hex(ss.str());
}

// One GenerationSample → the simstub.js `samples[]` shape (the pitscope tap).
nlohmann::ordered_json sample_to_json(const ns::physics::GenerationSample& g) {
    nlohmann::ordered_json sites = nlohmann::ordered_json::array();
    for (const auto& s : g.sites) {
        sites.push_back({{"pos", nlohmann::ordered_json::array({s.pos.x, s.pos.y, s.pos.z})},
                         {"group", s.group},
                         {"isotope", s.isotope},
                         {"layer", s.layer}});
    }
    return {{"n", g.n},
            {"t_s", g.t_s},
            {"lambda_s", g.lambda_s},
            {"k_eff", g.k_eff},
            {"k_prompt", g.k_prompt},
            {"log10_population", g.log10_population},
            {"log10_fissions", g.log10_fissions},
            {"isotope_shares", g.isotope_shares},
            {"shell_shares", g.shell_shares},
            {"refreshed", g.refreshed},
            {"q", g.q},
            {"sites", std::move(sites)}};
}

// The sink assembles the 03 §5 tally AND keeps the per-generation sample/site
// stream (the viz "main course" tap) — a BurstTally that also records.
class CollectingTally : public ns::physics::BurstTally {
public:
    void on_generation(const ns::physics::GenerationSample& g) override {
        ns::physics::BurstTally::on_generation(g);
        samples_.push_back(g);
    }
    std::vector<ns::physics::GenerationSample>& samples() noexcept { return samples_; }

private:
    std::vector<ns::physics::GenerationSample> samples_;
};

}  // namespace

GenerateRunResult generate_run(const StudioConfig& cfg) {
    const DemonCoreAssembly assembly(cfg);
    const double r0 = assembly.r0_cm();
    const ns::geom::LayerStack geom0 = assembly.compressed_geometry();  // the burst starts compressed
    const double r_comp = geom0.outermost_radius();

    // Mass-conserving burst eigen with r_ref = the UNCOMPRESSED r0: the compressed
    // start reads as ρ × compression (prompt-super if compression suffices), and
    // disassembly (radius > r0) drops density below critical → the E6 quench.
    ns::physics::EigenSpec espec;
    espec.batch = assembly.eigen_batch();
    espec.inactive = 6;
    espec.active = 12;
    espec.h_tol = 0.05;
    espec.seed = cfg.seed;
    const ns::physics::EigenFn eigen = ns::physics::ref_eigen_fn_masscons(
        assembly.materials(), assembly.xs(), espec, cfg.seed, r0);

    ns::physics::CoupleConfig cc;
    cc.n0 = 1.0;
    cc.e_f_mev = ns::consts::e_f_prompt_deposited;                 // C-040
    cc.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;           // C-041
    cc.lambda_s_initial = cfg.generation_time_s_initial;           // C-030
    cc.eigen_refresh_gens = 8;
    cc.quench_epsilon = ns::consts::quench_epsilon;                // C-909
    cc.t_max_s = cfg.burst_t_max_s;
    cc.max_generations = static_cast<int>(cfg.burst_max_generations);
    cc.initiator_rate_n_per_s = cfg.initiator_strength_n_per_s;    // C-051
    cc.initiator_t_fire_s = 0.0;
    cc.disassembly = true;
    cc.core_mass_kg = cfg.pit_mass_kg;                             // the real disassembling core mass
    cc.core_radius0_cm = r_comp;                                  // shell start = the compressed pit
    cc.disassembly_gamma = 5.0 / 3.0;
    cc.e_f_joules = ns::consts::e_f_prompt_deposited * ns::consts::mev_to_joule;  // C-040·C-917

    const std::string canon = cfg_canonical_hash(cfg);
    const std::string unit_id = compute_unit_id(canon, {}, cfg.seed);

    ns::physics::BurstContext ctx;
    ctx.isotope_names = {"Pu239", "Pu240"};
    ctx.isotope_molar_mass_g = {ns::consts::molar_mass_pu239, ns::consts::molar_mass_pu240};
    ctx.core_pu_isotopes = {0, 1};
    ctx.tamper_isotope = -1;                                       // bare core, no tamper
    ctx.shell_edges_cm = {0.0, r_comp};                           // one radial shell (the pit)
    ctx.phi_kt = cc.phi_kt;
    ctx.n_a = ns::consts::avogadro_constant;
    ctx.m_pit_g = cfg.pit_mass_kg * 1000.0;
    ctx.non_canonical = false;
    ctx.run_id = "demoncore_" + unit_id.substr(0, 16);

    CollectingTally sink;
    const ns::physics::BurstReport report = run_burst(cc, ctx, geom0, eigen, sink);

    GenerateRunResult out;
    out.tally = sink.result();
    out.samples = std::move(sink.samples());
    out.yield_kt = out.tally.yield_kt;
    out.k_eff_peak = out.tally.k_eff.peak;
    out.k_prompt_peak = out.tally.k_prompt.peak;
    out.supercritical = report.supercritical_reached;
    out.quenched = report.quenched;
    out.non_canonical = ctx.non_canonical;

    // EMERGENT outcome — read OFF the real burst, never a hardcoded rule.
    out.detonate = report.supercritical_reached && report.quenched && out.yield_kt > 0.0;
    if (!report.supercritical_reached) {
        out.reasons.push_back("k_prompt never reached 1.000 at peak compression (ratio " +
                              short_num(cfg.compression_ratio) + ", pit " + short_num(cfg.pit_mass_kg) +
                              " kg) — the assembly stayed subcritical; the excursion fizzles");
    } else {
        out.reasons.push_back("prompt-supercritical excursion (k_eff peak = " +
                              short_num(out.k_eff_peak) + ") — a runaway alpha burst ignited");
        if (report.quenched) {
            out.reasons.push_back("self-terminated by disassembly (k_eff fell to " +
                                  short_num(out.tally.k_eff.at_quench) +
                                  " as the core expanded); finite yield " + short_num(out.yield_kt) + " kt");
        } else {
            out.reasons.push_back("did not quench within t_max — the window was too short to capture "
                                  "the full disassembly");
        }
    }

    // run.json (03 §6) — the physics-determinable provenance. Environment fields
    // (code/spec version, git, dirty, timestamps) are the frontend's to fill.
    RunProvenance& run = out.run;
    run.run_id = ctx.run_id;
    run.unit_id = unit_id;
    run.scenario_file = "demon_core";                             // cfg-driven; no scenario TOML
    run.scenario_sha256 = canon;
    run.data_hashes.xs = assembly.xs_sha256();
    run.data_hashes.materials = {{"pit", assembly.material_sha256()}};
    run.scenario_overrides = {};                                  // canonical bare-core run
    run.seed = cfg.seed;
    run.backend = "ref";                                          // CPU reference transport
    run.device = "";

    return out;
}

std::string to_json(const GenerateRunResult& r, int indent) {
    json samples = json::array();
    for (const auto& g : r.samples) {
        samples.push_back(sample_to_json(g));
    }
    json j;
    j["detonate"] = r.detonate;
    j["reasons"] = r.reasons;
    j["yield_kt"] = r.yield_kt;
    j["k_eff_peak"] = r.k_eff_peak;
    j["k_prompt_peak"] = r.k_prompt_peak;
    j["supercritical"] = r.supercritical;
    j["quenched"] = r.quenched;
    j["non_canonical"] = r.non_canonical;
    j["tally"] = json::parse(ns::physics::to_json(r.tally));      // 03 §5, embedded object
    j["run"] = json::parse(ns::api::to_json(r.run));              // 03 §6, embedded object
    j["samples"] = std::move(samples);
    return j.dump(indent);
}

std::string generate_run_json(const std::string& cfg_json) {
    return to_json(generate_run(StudioConfig::from_json(cfg_json)));
}

}  // namespace ns::api

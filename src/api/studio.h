// The studio/binding API: evaluate + generate_run (06 §frontends) — M3-T3-g/-h.
//
// The nscore surface viz/js/simstub.js calls. `evaluate(cfg)` is the criticality
// gauge; `generate_run(cfg,…)` (M3-T3-h) is the full burst. Both drive a
// DemonCoreAssembly built from the flat viz config. This is nscore, NOT a
// frontend (D7) — it composes physics/couple + physics/eigen; a frontend
// (src/app/) or a language binding calls in here.
//
// SCOPE (M3-T3-g): DemonCoreAssembly + StudioConfig + evaluate. generate_run is
// M3-T3-h. fast4-independent: the assembly is a bare Pu sphere on SIM cross
// sections (the RealSphere stand-in) — cited-real when fast4 (M1-T4a-2) lands.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/xs/xs.h"

namespace ns::api {

/// The demon-core knobs from viz/js/scenario.js CANONICAL. A typed subset; the
/// full-device knobs (tamper.scale, lenses.jitter_ns) are RECORDED but not
/// modelled in the bare-core path — they need per-layer density scaling (the
/// mass-cons adapter is uniform, §7.1) + fast4. The demon core reacts to mass,
/// compression, Pu-240 and the initiator.
struct StudioConfig {
    double pit_mass_kg = 6.15;                    // "pit.mass_kg" (C-102)
    double pu240_fraction = 0.010;                // "materials.pu_ga_delta.Pu240" (atom fraction)
    double compression_ratio = 2.2;               // "compression.ratio" (C-060)
    double initiator_strength_n_per_s = 1.0e8;    // "initiator.strength_n_per_s" (C-051)
    double generation_time_s_initial = 1.0e-8;    // "kinetics.generation_time_s_initial" (C-030)
    double tamper_scale = 1.0;                     // full-device knob; recorded, not modelled here
    double lens_jitter_ns = 10.0;                 // full-device knob; recorded, not modelled here
    std::uint64_t seed = 20260803;                // deterministic; the binding may override
    std::int64_t eigen_batch = 4000;              // gauge eigen batch (speed vs σ_pcm); not a viz key

    /// Parse the viz cfg JSON (scenario.js dotted keys, e.g. "pit.mass_kg").
    /// Missing keys keep the canonical default; unknown keys are ignored.
    static StudioConfig from_json(const std::string& cfg_json);
};

/// The criticality gauge (simstub.js evaluate shape).
struct EvaluateResult {
    double k_eff = 0.0;        // on TOTAL nu-bar (ADR-013)
    double k_prompt = 0.0;     // k_eff·(1 - beta_eff), derived once by the eigen
    double sigma_pcm = 0.0;    // eigen 1σ on k, pcm
    bool ready = false;        // PROMPT-supercritical (k_prompt ≥ 1): the burst will ignite
};

std::string to_json(const EvaluateResult& r, int indent = 2);

/// A demon-core assembly built from a StudioConfig: a bare Pu sphere (Pu-239 +
/// Pu-240) at the C-101/C-102-derived δ-Pu density, on SIM cross sections (the
/// RealSphere stand-in; cited-real with fast4). Owns a temp directory holding the
/// generated material/xs JSON the file-based loaders read (FewGroupXS/MaterialLib
/// have no in-memory constructor). RAII: the temp dir is removed on destruction.
/// Non-copyable.
class DemonCoreAssembly {
public:
    explicit DemonCoreAssembly(const StudioConfig& cfg);
    ~DemonCoreAssembly();
    DemonCoreAssembly(const DemonCoreAssembly&) = delete;
    DemonCoreAssembly& operator=(const DemonCoreAssembly&) = delete;

    const ns::material::MaterialLib& materials() const noexcept { return *materials_; }
    const ns::xs::FewGroupXS& xs() const noexcept { return *xs_; }
    /// The UNCOMPRESSED sphere (radius r0 from mass + the derived density).
    const ns::geom::LayerStack& geometry() const noexcept { return geometry_; }
    double r0_cm() const noexcept { return r0_cm_; }
    double density_g_cm3() const noexcept { return density_g_cm3_; }
    double compression_ratio() const noexcept { return compression_ratio_; }
    std::uint64_t seed() const noexcept { return seed_; }
    std::int64_t eigen_batch() const noexcept { return eigen_batch_; }

    /// The compressed geometry the eigen runs on: radius r0·ratio^(-1/3), so
    /// ref_eigen_fn_masscons(r_ref = r0) scales ρ by exactly the compression ratio
    /// (mass-conserving) — compression raises k, the honest reactivity handle.
    ns::geom::LayerStack compressed_geometry() const;

private:
    std::filesystem::path root_;
    std::unique_ptr<ns::xs::FewGroupXS> xs_;
    std::unique_ptr<ns::material::MaterialLib> materials_;
    ns::geom::LayerStack geometry_;
    double r0_cm_ = 0.0;
    double density_g_cm3_ = 0.0;
    double compression_ratio_ = 1.0;
    std::uint64_t seed_ = 0;
    std::int64_t eigen_batch_ = 4000;
};

/// evaluate(cfg) → the criticality gauge, but REAL: builds the demon-core assembly
/// and runs a real MC eigen (ref_eigen_fn_masscons) at the compressed geometry.
/// `ready` is prompt-supercritical — the honest burst-ignition threshold (simstub
/// used k_eff ≥ 1). fast4-independent. NOTE: this runs a Monte-Carlo solve (~1-2 s),
/// unlike simstub's instant formula — a UI must debounce it.
EvaluateResult evaluate(const StudioConfig& cfg);
EvaluateResult evaluate(const DemonCoreAssembly& assembly);

/// JSON in / JSON out — the exact `evaluate(cfg)` seam the binding calls: parse the
/// viz cfg JSON, evaluate, return the EvaluateResult JSON.
std::string evaluate_json(const std::string& cfg_json);

}  // namespace ns::api

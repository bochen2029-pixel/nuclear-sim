#include "app/nukebench/scan.h"

#include "app/nukebench/gates.h"  // GatesError
#include "core/diagnostics.h"     // LoadWarning
#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/scenario/scenario.h"
#include "core/xs/xs.h"
#include "physics/couple/couple.h"
#include "physics/eigen/eigen.h"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>

namespace ns::nukebench {

std::vector<double> linspace_1_to_2(int points) {
    if (points < 2) return {1.0};
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        out.push_back(1.0 + static_cast<double>(i) / static_cast<double>(points - 1));  // [1.0, 2.0]
    }
    return out;
}

std::vector<ScanPoint> run_compression_scan(const std::filesystem::path& scenario_file,
                                            const std::filesystem::path& repo,
                                            const std::vector<double>& rho_ratios,
                                            std::int64_t batch, int inactive, int active,
                                            std::uint64_t seed) {
    // Assembly (mirrors run_gate): the scenario's first layer gives the material + uncompressed
    // radius r0; fast4 is the cross-section set.
    const auto scenario = ns::scenario::Scenario::load(scenario_file, repo);
    if (scenario.layers.empty()) throw GatesError("compression scan: scenario has no layers");
    const auto& layer = scenario.layers.front();
    const double r0 = layer.r_outer_cm;

    const auto xs = ns::xs::FewGroupXS::load(repo / "data" / "xs" / "fast4.json");
    std::vector<ns::LoadWarning> warnings;
    const auto lib = ns::material::MaterialLib::load_dir(repo / "data" / "materials", xs, &warnings);
    const int mid = lib.index_of(layer.material);
    if (mid < 0) throw GatesError("compression scan: material '" + layer.material + "' not found");
    const ns::geom::LayerStack stack0({ns::geom::Layer{layer.id, r0, mid, layer.status}});

    ns::physics::EigenSpec spec;
    spec.batch = batch;
    spec.inactive = inactive;
    spec.active = active;
    spec.seed = seed;
    // Mass-conserving eigen with r_ref = the uncompressed r0: at a geometry compressed to
    // r0*s^(-1/3) it infers rho/rho0 = s (Sigma proportional to rho), so compression raises k.
    // `lib`, `xs`, `spec` are locals that outlive every eigen() call below (couple.h contract).
    const ns::physics::EigenFn eigen = ns::physics::ref_eigen_fn_masscons(lib, xs, spec, seed, r0);

    std::vector<ScanPoint> out;
    out.reserve(rho_ratios.size());
    for (const double s : rho_ratios) {
        ns::geom::LayerStack stack = stack0;         // copy the uncompressed assembly
        stack.scale_radii(std::cbrt(1.0 / s));       // r0 -> r0*s^(-1/3); masscons infers rho x s
        const ns::physics::EigenResult er = eigen(stack);
        ScanPoint p;
        p.rho_ratio = s;
        p.k_eff = er.k;
        p.k_prompt = er.k_prompt();
        p.sigma_pcm = er.sigma_pcm;
        out.push_back(p);
    }
    return out;
}

std::string scan_to_csv(const std::vector<ScanPoint>& points) {
    std::ostringstream ss;
    ss << "rho_ratio,k_eff,k_prompt,sigma_pcm\n";
    ss << std::setprecision(10);
    for (const auto& p : points) {
        ss << p.rho_ratio << "," << p.k_eff << "," << p.k_prompt << "," << p.sigma_pcm << "\n";
    }
    return ss.str();
}

}  // namespace ns::nukebench

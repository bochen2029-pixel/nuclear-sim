// gate_probe (M1-T4a-2a) — measure a benchmark scenario's k_eff and report the real
// pcm deviation from k=1.0. NOT the formal gate (that is nukebench, M1-T5); this is the
// measurement the owner asked for while fast4 is authored: load data/xs/fast4.json + the
// benchmark scenario, run the reference MC eigen, print k / sigma / pcm. Non-test tool
// (like gpu_perf). Iterate at a reduced --batch (seconds); confirm at the C-900 config.
//
// The reference k is on TOTAL nu-bar (never beta-corrected) — the benchmark-comparable
// quantity (ADR-013). Band = 500 pcm + benchmark_uncertainty_pcm; for the PUBLIC-DERIVED
// Godiva/Jezebel models the unverified uncertainty is taken as 0 (08 §1), so +/-500 pcm.

#include "core/diagnostics.h"
#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/scenario/scenario.h"
#include "core/xs/xs.h"
#include "physics/eigen/eigen.h"
#include "ref/ref_transport.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: gate_probe <scenario.toml> [--batch N] [--inactive N] "
                     "[--active N] [--seed N] [--repo PATH]\n");
        return 2;
    }
    const fs::path scenario_path = argv[1];
    fs::path repo = fs::current_path();
    long long batch_override = -1, seed_override = -1;
    int inactive_override = -1, active_override = -1, n_layers = -1;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--batch") batch_override = std::atoll(next());
        else if (a == "--inactive") inactive_override = std::atoi(next());
        else if (a == "--active") active_override = std::atoi(next());
        else if (a == "--seed") seed_override = std::atoll(next());
        else if (a == "--repo") repo = next();
        else if (a == "--layers") n_layers = std::atoi(next());  // limit to the first N shells (G1a probe)
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    try {
        const auto scenario = ns::scenario::Scenario::load(scenario_path, repo);
        const auto xs = ns::xs::FewGroupXS::load(repo / "data" / "xs" / "fast4.json");
        std::vector<ns::LoadWarning> warnings;
        const auto lib = ns::material::MaterialLib::load_dir(repo / "data" / "materials", xs,
                                                             &warnings);
        if (scenario.layers.empty()) {
            std::fprintf(stderr, "scenario has no layers\n");
            return 1;
        }
        // Build the FULL nested-shell stack (bare-sphere benchmarks have one layer; the canonical
        // Trinity assembly has pit/tamper/.../HE) -- the multi-layer eigen for G1a static criticality.
        std::vector<ns::geom::Layer> layers;
        for (const auto& L : scenario.layers) {
            if (n_layers > 0 && static_cast<int>(layers.size()) >= n_layers) break;  // G1a probe
            const int mid = lib.index_of(L.material);
            if (mid < 0) {
                std::fprintf(stderr, "material '%s' not found in the library\n", L.material.c_str());
                return 1;
            }
            layers.push_back(ns::geom::Layer{L.id, L.r_outer_cm, mid, L.status});
        }
        const ns::geom::LayerStack stack(layers);
        const auto& layer = scenario.layers.front();  // pit (innermost) -- for the printout
        ns::ref::RefTransport transport(stack, lib, xs, static_cast<std::uint64_t>(scenario.seed));

        ns::physics::EigenSpec spec;
        spec.batch = batch_override > 0 ? batch_override : scenario.eigen_batch;
        spec.inactive = inactive_override > 0 ? inactive_override : scenario.eigen_inactive;
        spec.active = active_override > 0 ? active_override : scenario.eigen_active;
        spec.seed = seed_override > 0 ? static_cast<std::uint64_t>(seed_override)
                                      : static_cast<std::uint64_t>(scenario.seed);

        std::printf("scenario=%s  layers=%zu  pit=%s r0=%.4f  R_outer=%.4f cm  batch=%lld inactive=%d active=%d seed=%llu\n",
                    scenario.name.c_str(), scenario.layers.size(), layer.material.c_str(),
                    layer.r_outer_cm, scenario.layers.back().r_outer_cm,
                    static_cast<long long>(spec.batch), spec.inactive, spec.active,
                    static_cast<unsigned long long>(spec.seed));
        std::fflush(stdout);

        const auto res = ns::physics::run_eigen(transport, spec);
        const double pcm = (res.k - 1.0) * 1e5;
        std::printf("k_eff = %.6f   sigma = %.1f pcm   deviation = %+.1f pcm from k=1.0   (band +/-500)\n",
                    res.k, res.sigma_pcm, pcm);
        std::printf("RESULT %s : %s  %+.1f pcm\n", scenario.name.c_str(),
                    (std::fabs(pcm) <= 500.0 ? "WITHIN-BAND" : "OUTSIDE-BAND"), pcm);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

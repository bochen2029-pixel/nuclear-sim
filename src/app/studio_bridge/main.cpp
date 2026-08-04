// studio_bridge — the fusion-binding CLI (the C++<->viz seam).
//
// The demon-core studio API as JSON-in / JSON-out: `evaluate` (the criticality
// gauge) and `generate-run` (the full emergent burst — tally + run + the
// per-generation sample/fission-site stream). This is the MECHANISM-INDEPENDENT
// DATA surface the visualizer's binding calls to replace viz/js/simstub.js's fake
// evaluate/generateRun with the REAL Monte-Carlo physics (src/api, M3-T3-g/h).
//
// It is nscore's studio surface exposed for a binding — a local dev-server (Python
// stdlib) that shells to this gets real physics into the existing browser viz with
// zero new C++ deps and no Emscripten; a WASM build or a native/Electron addon
// (the Steam path, M7) wrap the same evaluate_json/generate_run_json surface. The
// closure reconstruction (flux(t)/compression(t) from the returned data) is the JS
// binding's job (viz/, the -e track) — this side returns DATA only (viz-seam note).
//
// A frontend (src/app, D7): it composes only the nscore api surface, no physics.

#include "api/studio.h"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string read_all(std::istream& in) {
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"studio_bridge - the demon-core fusion API as JSON in/out"};
    app.require_subcommand(1);

    std::string cfg_path;
    auto* evaluate_cmd = app.add_subcommand(
        "evaluate", "cfg JSON (stdin or --cfg) -> EvaluateResult JSON (the criticality gauge)");
    evaluate_cmd->add_option("--cfg", cfg_path, "read cfg JSON from this file instead of stdin");
    auto* generate_cmd = app.add_subcommand(
        "generate-run",
        "cfg JSON (stdin or --cfg) -> the run JSON (tally + run + samples; the emergent burst)");
    generate_cmd->add_option("--cfg", cfg_path, "read cfg JSON from this file instead of stdin");

    CLI11_PARSE(app, argc, argv);

    try {
        std::string cfg;
        if (!cfg_path.empty()) {
            std::ifstream f(cfg_path);
            if (!f) {
                fmt::print(stderr, "cannot read {}\n", cfg_path);
                return 1;
            }
            cfg = read_all(f);
        } else {
            cfg = read_all(std::cin);
        }

        if (*evaluate_cmd) {
            std::cout << ns::api::evaluate_json(cfg) << "\n";
        } else if (*generate_cmd) {
            std::cout << ns::api::generate_run_json(cfg) << "\n";
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;  // 06 §5 general error (a malformed cfg surfaces here)
    }
    return 0;
}

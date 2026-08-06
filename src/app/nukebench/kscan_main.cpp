// kscan (M2-T3) — the k-vs-compression scan tool. Runs the mass-conserving ref eigen over
// rho/rho0 in [1.0, 2.0] on a fast4 bare-sphere scenario (default: Jezebel, bare Pu) and writes
// the k(rho/rho0) CSV curve — the G1b (08 §2) precursor artifact. Non-test exe (like gate_probe);
// not a `nukebench` subcommand (nukebench has no sweep subcommand, MIN-09).
//
// Shell note (this machine): pass Windows paths with forward slashes, e.g.
//   kscan --repo C:/NUCLEAR --out C:/NUCLEAR/data/benchmarks/compression_scan_jezebel.csv

#include "app/nukebench/scan.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    CLI::App app{"kscan - k-vs-compression scan (M2-T3): k(rho/rho0) over [1.0, 2.0]"};

    std::string scenario = "data/scenarios/jezebel.toml";
    std::string out = "artifacts/compression_scan.csv";
    std::string repo_str;
    std::int64_t batch = 200000;
    int inactive = 50, active = 150, points = 11;
    std::uint64_t seed = 1;

    app.add_option("--scenario", scenario, "bare-sphere scenario, relative to --repo (default Jezebel Pu)");
    app.add_option("--out", out, "CSV output path (default artifacts/compression_scan.csv)");
    app.add_option("--batch", batch, "eigen batch (default 200000)");
    app.add_option("--inactive", inactive, "inactive generations (default 50)");
    app.add_option("--active", active, "active generations (default 150)");
    app.add_option("--points", points, "number of rho/rho0 points over [1.0, 2.0] (default 11)");
    app.add_option("--seed", seed, "RNG seed (default 1)");
    app.add_option("--repo", repo_str, "repo root (default: cwd)");

    CLI11_PARSE(app, argc, argv);

    const std::filesystem::path repo =
        repo_str.empty() ? std::filesystem::current_path() : std::filesystem::path(repo_str);

    try {
        const std::vector<double> ratios = ns::nukebench::linspace_1_to_2(points);
        const auto pts = ns::nukebench::run_compression_scan(repo / scenario, repo, ratios, batch,
                                                             inactive, active, seed);
        const std::string csv = ns::nukebench::scan_to_csv(pts);

        const std::filesystem::path out_path(out);
        if (out_path.has_parent_path()) std::filesystem::create_directories(out_path.parent_path());
        std::ofstream(out_path, std::ios::binary) << csv;

        std::printf("%s", csv.c_str());
        std::printf("wrote %zu points -> %s\n", pts.size(), out_path.string().c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kscan error: %s\n", e.what());
        return 1;
    }
    return 0;
}

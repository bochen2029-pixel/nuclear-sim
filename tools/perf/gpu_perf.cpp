// M4-T4 GPU performance harness (profile-first, 02 §4).
//
// Times the event-based GPU eigen (gpu/eigen.cu) and fixed-source transport
// (gpu/transport.cu) across a batch sweep on a toy fissioning medium (SIM cross
// sections — this measures THROUGHPUT, not physics fidelity; it is not a cited
// benchmark and never gate evidence), and appends one JSONL record per
// measurement to artifacts/perf_history.jsonl (11 §1 — committed, append-only
// cross-session trend, rotated at 100 MB).
//
// It is NOT the whole of G4. The G4 budgets tied to the canonical burst
// (gen/s, eigen-calls-per-burst), the Godiva gate eigen (needs the cited fast4
// xs, blocked M1-T4a-2) and the render pipeline (M7) require workloads that do
// not exist yet and stay PENDING (C-945). This establishes the measurable dev-GPU
// baseline and the profile that motivates the M4-T4-b kernel optimisations
// (branchless-event split; event-based transport inside the eigen; device-side
// per-generation reductions).
//
// Determinism is unaffected: this only reads timings and cudaMemGetInfo; the
// kernels it calls are the same bit-identical-across-launch-configs ones the
// gpu.* tests pin. Host compiler only (no CUDA types here) — it calls the CUDA
// backend through the plain-typed bridges, exactly as tests/unit/test_gpu.cpp
// does. Built only under NUKESIM_WITH_CUDA, so CPU-only CI never sees it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/xs/xs.h"
#include "gpu/eigen.h"
#include "gpu/gpu_backend.h"
#include "gpu/transport.h"

namespace {

using nlohmann::json;

// A toy fissioning Pu-like medium built through the REAL loaders (as
// tests/unit/test_gpu.cpp's FissionWorld does), so the workload matches the one
// the differential tests validate. One-group-active, group-preserving, μ̄ = 0
// (Σ_tr = Σ_t). SIM status — synthetic, never a physical dataset.
struct ToyFuel {
    std::filesystem::path root;
    std::unique_ptr<ns::xs::FewGroupXS> xs;
    std::unique_ptr<ns::material::MaterialLib> materials;

    ToyFuel() {
        namespace fs = std::filesystem;
        root = fs::temp_directory_path() / "nukesim_perf_fuel";
        fs::remove_all(root);
        fs::create_directories(root / "xs");
        fs::create_directories(root / "materials");

        const auto four = [](double v) { return json::array({v, v, v, v}); };
        const json iso = {{"nu", four(2.9)},
                          {"chi", json::array({1.0, 0.0, 0.0, 0.0})},
                          {"sigma_f", four(1.0)},
                          {"sigma_c", four(0.5)},
                          {"sigma_s", four(3.0)},
                          {"sigma_n2n", four(0.0)},
                          {"mu_bar", four(0.0)},
                          {"beta", 0.0020},
                          {"transfer", json::array({json::array({1.0, 0.0, 0.0, 0.0}),
                                                    json::array({0.0, 1.0, 0.0, 0.0}),
                                                    json::array({0.0, 0.0, 1.0, 0.0}),
                                                    json::array({0.0, 0.0, 0.0, 1.0})})},
                          {"cite", "synthetic fissioning medium — not physical data"},
                          {"status", "SIM"}};
        const json xs_doc = {{"schema_version", 2},
                             {"name", "fis"},
                             {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3})},
                             {"isotopes", {{"Pu239", iso}}}};
        write(root / "xs" / "fis.json", xs_doc.dump(2));

        const json mat = {{"schema_version", 1},
                          {"name", "fuel"},
                          {"density_g_cm3", 15.0},
                          {"status", "SIM"},
                          {"cite", "synthetic"},
                          {"isotopes", {{"Pu239", 1.0}}}};
        write(root / "materials" / "fuel.json", mat.dump(2));

        xs = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root / "xs" / "fis.json"));
        materials = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root / "materials", *xs));
    }
    ~ToyFuel() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    ToyFuel(const ToyFuel&) = delete;
    ToyFuel& operator=(const ToyFuel&) = delete;

    static void write(const std::filesystem::path& p, const std::string& content) {
        std::ofstream f(p);
        f << content;
    }
};

std::string iso_utc_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Best (minimum) wall-clock over `reps` synchronized runs of `f`, seconds. The
// gpu bridges cudaDeviceSynchronize internally, so host wall-clock is the true
// device time; min rejects scheduler/thermal noise (we want the achievable rate).
template <typename F>
double best_seconds(int reps, F&& f) {
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const auto t1 = std::chrono::steady_clock::now();
        const double s = std::chrono::duration<double>(t1 - t0).count();
        best = std::min(best, s);
    }
    return best;
}

double mib(std::int64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

struct Args {
    std::string out = "artifacts/perf_history.jsonl";
    std::string commit = "unknown";
    std::string session = "unknown";
    std::string task = "M4-T4-a";
    int reps = 3;
    bool profile = false;  // minimal launches for `ncu` (no file writes, no sweep)
    bool golden = false;   // print eigen k/checksum/entropy for the test config
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        const auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (s == "--out") {
            a.out = next();
        } else if (s == "--commit") {
            a.commit = next();
        } else if (s == "--session") {
            a.session = next();
        } else if (s == "--task") {
            a.task = next();
        } else if (s == "--reps") {
            a.reps = std::max(1, std::atoi(next().c_str()));
        } else if (s == "--profile") {
            a.profile = true;
        } else if (s == "--golden") {
            a.golden = true;
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse(argc, argv);

    if (ns::gpu::device_count() < 1) {
        std::cerr << "gpu_perf: no CUDA device\n";
        return 1;
    }
    ns::gpu::DeviceInfo dev;
    if (!ns::gpu::device_info(dev)) {
        std::cerr << "gpu_perf: could not query device\n";
        return 1;
    }

    const ToyFuel fuel;
    const ns::geom::LayerStack eigen_sphere({ns::geom::Layer{"core", 12.0, 0, "SIM"}});
    const ns::geom::LayerStack source_sphere({ns::geom::Layer{"medium", 200.0, 0, "SIM"}});
    const std::uint64_t seed = 20260802;
    const std::array<float, 4> g0{1.0f, 0.0f, 0.0f, 0.0f};

    // --- Golden mode: print the eigen's exact deterministic outputs for the
    // differential-test config, so a rewrite can assert bit-identity. ----------
    if (args.golden) {
        ns::gpu::EigenResultGpu g;
        ns::gpu::gpu_eigen(eigen_sphere, *fuel.materials, seed, 3000, 20, 40, 64, 128, g);
        std::printf("GOLDEN k=%.17g checksum=%llu entropy=%.17g\n", g.k,
                    static_cast<unsigned long long>(g.source_checksum), g.entropy_final);
        return 0;
    }

    // --- Profile mode: a handful of kernel launches for `ncu` to sample. -------
    if (args.profile) {
        ns::gpu::EigenResultGpu e;
        ns::gpu::gpu_eigen(eigen_sphere, *fuel.materials, seed, 2000, 2, 3, 256, 128, e);
        ns::gpu::FixedSourceResult f;
        ns::gpu::gpu_fixed_source(source_sphere, *fuel.materials, seed, 50000, g0, 256, 128, f);
        std::cout << "profile run complete (eigen k=" << e.k << ", fixed leak="
                  << f.leaked_fraction << ")\n";
        return 0;
    }

    std::cout << "gpu_perf — " << dev.name << " (sm_" << dev.cc_major << dev.cc_minor << "), "
              << mib(dev.total_bytes) << " MiB, reps=" << args.reps << "\n";
    std::cout << "commit=" << args.commit << " session=" << args.session << " task=" << args.task
              << "\n";

    std::filesystem::create_directories(std::filesystem::path(args.out).parent_path());
    std::ofstream out(args.out, std::ios::app);
    const std::string ts = iso_utc_now();

    const auto stamp = [&](json& rec) {
        rec["schema"] = 1;
        rec["ts"] = ts;
        rec["commit"] = args.commit;
        rec["session"] = args.session;
        rec["task"] = args.task;
        rec["device"] = dev.name;
        rec["cc"] = std::to_string(dev.cc_major) + "." + std::to_string(dev.cc_minor);
        rec["total_vram_MiB"] = mib(dev.total_bytes);
        rec["note"] = "toy SIM medium (throughput, not a benchmark); PENDING: canonical/Godiva/render";
    };

    // Warm-up: pay CUDA context init + JIT once, untimed, on each path.
    {
        ns::gpu::EigenResultGpu e;
        ns::gpu::gpu_eigen(eigen_sphere, *fuel.materials, seed, 2000, 2, 3, 256, 128, e);
        ns::gpu::FixedSourceResult f;
        ns::gpu::gpu_fixed_source(source_sphere, *fuel.materials, seed, 50000, g0, 256, 128, f);
    }

    const int blocks = 256;
    const int threads = 128;

    // --- Eigen: generations/second vs batch (12 cm Pu sphere). ----------------
    std::cout << "\n[eigen]  12 cm sphere, inactive=10 active=20, " << blocks << "x" << threads
              << "\n";
    std::cout << "  batch      gen/s     k        peak VRAM (MiB)\n";
    const std::int64_t eigen_batches[] = {1000, 3000, 10000, 30000, 100000};
    for (const std::int64_t batch : eigen_batches) {
        ns::gpu::EigenResultGpu e;
        const double s = best_seconds(args.reps, [&]() {
            ns::gpu::gpu_eigen(eigen_sphere, *fuel.materials, seed, batch, 10, 20, blocks, threads, e);
        });
        const double gps = (s > 0.0) ? static_cast<double>(e.generations) / s : 0.0;
        std::printf("  %-9lld  %9.1f  %6.4f   %8.2f\n", static_cast<long long>(batch), gps, e.k,
                    mib(e.peak_vram_bytes));

        json rec;
        stamp(rec);
        rec["workload"] = "eigen";
        rec["world"] = "pu_sphere_12cm";
        rec["batch"] = batch;
        rec["inactive"] = 10;
        rec["active"] = 20;
        rec["generations"] = e.generations;
        rec["blocks"] = blocks;
        rec["threads"] = threads;
        rec["wall_s"] = s;
        rec["gen_per_s"] = gps;
        rec["k"] = e.k;
        rec["k_sigma"] = e.k_sigma;
        rec["peak_vram_MiB"] = mib(e.peak_vram_bytes);
        out << rec.dump() << "\n";
    }

    // --- Fixed-source: particles/second vs batch (200 cm sphere, deep loop). ---
    std::cout << "\n[fixed_source]  200 cm sphere, " << blocks << "x" << threads << "\n";
    std::cout << "  batch      Mparticles/s  supersteps  peak VRAM (MiB)\n";
    const std::int64_t source_batches[] = {10000, 30000, 100000, 300000, 1000000};
    for (const std::int64_t batch : source_batches) {
        ns::gpu::FixedSourceResult f;
        const double s = best_seconds(args.reps, [&]() {
            ns::gpu::gpu_fixed_source(source_sphere, *fuel.materials, seed, batch, g0, blocks,
                                      threads, f);
        });
        const double pps = (s > 0.0) ? static_cast<double>(batch) / s : 0.0;
        std::printf("  %-9lld  %11.2f   %9d   %8.2f\n", static_cast<long long>(batch),
                    pps / 1e6, f.supersteps, mib(f.peak_vram_bytes));

        json rec;
        stamp(rec);
        rec["workload"] = "fixed_source";
        rec["world"] = "pu_sphere_200cm";
        rec["batch"] = batch;
        rec["supersteps"] = f.supersteps;
        rec["blocks"] = blocks;
        rec["threads"] = threads;
        rec["wall_s"] = s;
        rec["particles_per_s"] = pps;
        rec["fission_bank_size"] = f.fission_bank_size;
        rec["peak_vram_MiB"] = mib(f.peak_vram_bytes);
        out << rec.dump() << "\n";
    }

    std::cout << "\nappended to " << args.out << "\n";
    return 0;
}

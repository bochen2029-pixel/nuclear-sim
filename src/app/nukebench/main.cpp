// nukebench — headless CLI (06 §1). A thin CLI11 dispatch over the tested handlers in cli.{h,cpp}:
// `run` (scenario -> the 03 §6 bundle, M1-T5-c-4), `gate` (M1-T5-b), `diff` (the G0c cross-backend
// differential, M1-T5-c-3). Exit codes per 06 §5: 0 ok, 1 general, 2 usage, 3 validation, 4
// gate-fail, 5 internal.

#include "app/nukebench/cli.h"
#include "app/nukebench/gates.h"  // GatesError

#include <CLI/CLI.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    CLI::App app{"nukebench - headless gate runner (08 sec 2)"};
    app.require_subcommand(1);

    auto* gate = app.add_subcommand("gate", "Run a gate's procedure over its normative seeds "
                                            "-> gate_report.json (03 sec 11)");
    std::string gate_id, report, backend = "ref", repo_str, git_hash, device;
    std::int64_t batch = 0, seed = 0;
    bool dirty = false;
    gate->add_option("--gate", gate_id, "gate id, e.g. G0a")->required();
    gate->add_option("--report", report, "report path (default artifacts/gate_reports/<gate>/)");
    gate->add_option("--backend", backend, "ref|gpu (default ref; gpu needs a CUDA build)");
    gate->add_option("--batch", batch, "reduced eigen batch for a quick look (default: gate C-900)");
    auto* seed_opt = gate->add_option("--seed", seed, "REJECTED with --gate (08 sec 2)");
    gate->add_flag("--dirty", dirty, "mark the tree dirty (verdict capped at conditional)");
    gate->add_option("--repo", repo_str, "repo root (default: cwd)");
    gate->add_option("--git", git_hash, "short commit hash of the code (provenance, 03 sec 11)");
    gate->add_option("--device", device, "device string (provenance, e.g. \"CPU ref\")");

    // M1-T5-c-3: the G0c cross-backend differential (ref vs gpu). Needs a CUDA build (the gpu backend).
    auto* diff = app.add_subcommand("diff", "Run a differential gate (ref vs gpu, e.g. G0c) "
                                            "-> gate_report.json (08 sec 2)");
    std::string d_gate, d_report, d_repo, d_git, d_device;
    std::int64_t d_batch = 0;
    bool d_dirty = false;
    diff->add_option("--gate", d_gate, "differential gate id, e.g. G0c")->required();
    diff->add_option("--report", d_report, "report path (default artifacts/gate_reports/<gate>/)");
    diff->add_option("--batch", d_batch, "reduced eigen batch for a quick look (default: gate C-900)");
    diff->add_flag("--dirty", d_dirty, "mark the tree dirty (verdict capped at conditional)");
    diff->add_option("--repo", d_repo, "repo root (default: cwd)");
    diff->add_option("--git", d_git, "short commit hash of the code (provenance, 03 sec 11)");
    diff->add_option("--device", d_device, "device string (provenance, e.g. \"ref + RTX 4070\")");

    // M1-T5-c-4: `run` — a demon-core scenario cfg -> the 03 sec 6 artifact bundle (run.json +
    // tally.json). fast4-independent (the src/api studio surface); backend ref only (CPU burst).
    auto* run = app.add_subcommand("run", "Run a demon-core scenario (cfg JSON) -> the 03 sec 6 "
                                          "artifact bundle {run.json, tally.json}");
    std::string r_scenario, r_backend = "ref", r_out = "artifacts", r_git, r_device;
    std::vector<std::string> r_overrides;
    bool r_dirty = false;
    run->add_option("--scenario", r_scenario, "demon-core cfg JSON (flat dotted keys)")->required();
    run->add_option("--override", r_overrides, "key=value applied to the cfg (repeatable, numeric)");
    run->add_option("--backend", r_backend, "ref (default; the demon-core burst is CPU transport)");
    run->add_option("--out", r_out, "artifact root (default: artifacts/)");
    run->add_option("--git", r_git, "short commit hash of the code (provenance, 03 sec 6)");
    run->add_option("--device", r_device, "device string (provenance)");
    run->add_flag("--dirty", r_dirty, "mark the working tree dirty (provenance)");

    CLI11_PARSE(app, argc, argv);

    try {
        if (*run) {
            std::vector<std::pair<std::string, std::string>> ovs;
            for (const auto& s : r_overrides) {
                const std::size_t eq = s.find('=');
                if (eq == std::string::npos || eq == 0) {
                    std::fprintf(stderr, "nukebench run: --override must be key=value: %s\n", s.c_str());
                    return 2;  // 06 sec 5 usage
                }
                ovs.emplace_back(s.substr(0, eq), s.substr(eq + 1));
            }
            const auto out =
                ns::nukebench::cli_run(r_scenario, ovs, r_out, r_backend, r_git, r_device, r_dirty);
            if (out.exit_code == 0) {
                std::printf("run %s: %s  yield=%.4g kt  (bundle: %s)\n", out.unit_id.substr(0, 16).c_str(),
                            out.detonate ? "detonate" : (out.fizzle ? "fizzle" : "no-detonate"),
                            out.yield_kt, out.out_dir.string().c_str());
            }
            return out.exit_code;  // 0 ok, 3 validation
        }
        if (*gate) {
            if (seed_opt->count() > 0) {  // --seed with --gate is a usage error (08 sec 2)
                std::fprintf(stderr, "nukebench gate: --seed is not accepted with --gate (08 sec 2)\n");
                return 2;
            }
            const std::filesystem::path repo =
                repo_str.empty() ? std::filesystem::current_path() : std::filesystem::path(repo_str);
            const std::filesystem::path report_path =
                report.empty() ? ns::nukebench::default_report_path(repo, gate_id)
                               : std::filesystem::path(report);
            const auto out = ns::nukebench::cli_gate(gate_id, repo, report_path, backend, batch,
                                                     dirty, git_hash, device);
            std::printf("gate %s: %s  (report: %s)\n", gate_id.c_str(), out.verdict.c_str(),
                        out.report_path.string().c_str());
            return out.exit_code;  // 0 pass, 4 gate-fail
        }
        if (*diff) {
            const std::filesystem::path repo =
                d_repo.empty() ? std::filesystem::current_path() : std::filesystem::path(d_repo);
            const std::filesystem::path report_path =
                d_report.empty() ? ns::nukebench::default_report_path(repo, d_gate)
                                 : std::filesystem::path(d_report);
            const auto out = ns::nukebench::cli_diff(d_gate, repo, report_path, d_batch, d_dirty,
                                                     d_git, d_device);
            std::printf("diff %s: %s  (report: %s)\n", d_gate.c_str(), out.verdict.c_str(),
                        out.report_path.string().c_str());
            return out.exit_code;  // 0 pass, 4 gate-fail
        }
    } catch (const ns::nukebench::GatesError& e) {
        std::fprintf(stderr, "validation error: %s\n", e.what());
        return 3;  // 06 sec 5
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

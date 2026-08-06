// k-vs-compression scan (M2-T3) — the G1b (08 §2) precursor curve.
//
// Builds a fast4 bare-sphere assembly from a scenario and scans k as a function of the
// mass-conserving density ratio rho/rho0 over [1.0, 2.0], reusing the PROVEN reactivity path
// `ref_eigen_fn_masscons` (Sigma proportional to rho): compression (rho/rho0 > 1) shrinks the
// radius AND raises the density, so k rises — never the "bigger sphere at fixed rho reads
// backwards" trap. The result is the k(rho/rho0) curve + a CSV artifact.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ns::nukebench {

struct ScanPoint {
    double rho_ratio = 1.0;   // rho/rho0 (the mass-conserving density ratio)
    double k_eff = 0.0;       // on TOTAL nu-bar (ADR-013)
    double k_prompt = 0.0;    // k_eff * (1 - beta_eff)
    double sigma_pcm = 0.0;   // eigen 1-sigma on k, pcm
};

/// Run the k-vs-compression scan (M2-T3): build the fast4 assembly from `scenario_file`'s first
/// layer (material + r0, on `data/xs/fast4.json`), then for each rho/rho0 `s` in `rho_ratios`
/// compress the geometry to r0*s^(-1/3) and run the mass-conserving ref eigen
/// (`ref_eigen_fn_masscons`, r_ref = r0 — so the mass-conserving factor it infers is exactly `s`
/// and compression RAISES k). `batch`/`inactive`/`active` set the eigen config; `seed` is the
/// fixed RNG seed (deterministic). Returns one ScanPoint per ratio, in input order. Throws
/// ns::nukebench::GatesError on a bad scenario/material.
std::vector<ScanPoint> run_compression_scan(const std::filesystem::path& scenario_file,
                                            const std::filesystem::path& repo,
                                            const std::vector<double>& rho_ratios,
                                            std::int64_t batch, int inactive, int active,
                                            std::uint64_t seed);

/// `rho_ratios` evenly spaced over [1.0, 2.0] with `points` samples (points >= 2; the G1b range).
std::vector<double> linspace_1_to_2(int points);

/// The k(rho/rho0) curve as CSV: a header row then one row per point,
/// `rho_ratio,k_eff,k_prompt,sigma_pcm` (locale-independent, 06 §5).
std::string scan_to_csv(const std::vector<ScanPoint>& points);

}  // namespace ns::nukebench

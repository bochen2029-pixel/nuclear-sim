// Generation-based transport for the power iteration (05 §1/§2, 01 §3).
//
// M1-T2's run_fixed_source deliberately tallies fission production without
// propagating it, because propagating progeny IS a fission-source iteration.
// This is that propagation.

#include "core/constants/constants.h"
#include "core/diagnostics.h"
#include "ref/ref_transport.h"

#include <algorithm>
#include <cmath>

namespace ns::ref {
namespace {

constexpr double kWeightMin = 1e-4;   // C-902 w_min
constexpr double kWeightSurv = 1e-2;  // C-902 w_surv
constexpr double kBarnToCm2 = 1e-24;
constexpr double kPi = 3.14159265358979323846;

geom::Vec3 sample_isotropic(rng::Stream& stream) {
    const double mu = 2.0 * stream.uniform_d() - 1.0;
    const double phi = 2.0 * kPi * stream.uniform_d();
    const double sin_theta = std::sqrt(std::max(0.0, 1.0 - mu * mu));
    return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), mu};
}

}  // namespace

double RefTransport::group_speed_cm_s(int group) const {
    const auto& bounds = xs_.bounds_MeV();
    const auto g = static_cast<std::size_t>(group);
    // Geometric mean of the group's bounds: the structure is log-spaced
    // (20 / 3 / 1 / 0.1 / 1e-3 MeV), so the arithmetic midpoint would sit far
    // too high in the wide low-energy group.
    const double energy = std::sqrt(bounds[g] * bounds[g + 1]);
    // Non-relativistic v ∝ sqrt(E), anchored on C-031 at 1 MeV.
    return consts::neutron_speed_1mev * std::sqrt(energy / 1.0);
}

std::vector<FissionSite> RefTransport::initial_bank(std::int64_t count, bool concentrated,
                                                    std::uint64_t seed) const {
    std::vector<FissionSite> bank;
    bank.reserve(static_cast<std::size_t>(count));
    const double outer = stack_.outermost_radius();

    for (std::int64_t i = 0; i < count; ++i) {
        rng::Stream stream(seed, consts::rng_stream_registry::source,
                           static_cast<std::uint64_t>(i));
        FissionSite site;
        site.group = 0;
        if (concentrated) {
            // 05 §2's deliberately bad source: everything at r = 0.
            site.pos = {0.0, 0.0, 0.0};
        } else {
            // Uniform in volume across the whole body.
            const double r = outer * std::cbrt(stream.uniform_d());
            site.pos = sample_isotropic(stream) * r;
        }
        site.layer = stack_.locate(site.pos);
        bank.push_back(site);
    }
    return bank;
}

void RefTransport::run_generation(const std::vector<FissionSite>& bank, double k_gen,
                                  std::uint64_t generation, GenerationResult& out) {
    require(k_gen > 0.0, "<RefTransport>", "k_gen", "must be positive");

    out = GenerationResult{};
    out.source.by_isotope.assign(isotope_names_.size(), 0.0);
    out.source.by_isotope_fissions.assign(isotope_names_.size(), 0.0);
    out.source.by_layer.assign(layers_.size(), 0.0);
    out.source.mesh.assign(512, 0.0);  // C-908: fixed 8x8x8
    out.source.sites.reserve(bank.size());

    // The entropy mesh is a FIXED Cartesian grid over the bounding box of the
    // outermost layer, deliberately decoupled from the layer geometry: a
    // layer-aligned mesh is degenerate on a single-layer benchmark (BLK-10).
    const double outer = stack_.outermost_radius();
    const double cell = 2.0 * outer / 8.0;

    for (std::size_t index = 0; index < bank.size(); ++index) {
        const FissionSite& site = bank[index];

        // Stream derived from (generation, index) — identity, never execution
        // order (BLK-11/E2, the same rule fork() enforces for progeny).
        rng::Stream stream(seed_,
                           rng::fork(consts::rng_stream_registry::fission, generation,
                                     static_cast<std::uint32_t>(index)));

        Particle p{site.pos, sample_isotropic(stream), site.group, 1.0, stream};
        out.source_weight += 1.0;

        int layer = tracker_.locate(p.pos);
        while (true) {
            if (layer == geom::kOutside) {
                out.leaked_weight += p.weight;
                break;
            }
            const auto& data = layers_[static_cast<std::size_t>(layer)];
            const auto g = static_cast<std::size_t>(p.group);
            const double sigma_tr = data.sigma_tr[g];
            const double speed = group_speed_cm_s(p.group);

            if (sigma_tr <= 0.0) {
                const double to_boundary = tracker_.distance_to_boundary(p.pos, p.dir, layer);
                if (!std::isfinite(to_boundary)) {
                    out.leaked_weight += p.weight;
                    break;
                }
                out.time_weight += p.weight * to_boundary / speed;
                p.pos = p.pos + p.dir * to_boundary;
                layer = tracker_.nudge_and_locate(p.pos, p.dir);
                continue;
            }

            const double flight = -std::log(p.stream.uniform_d()) / sigma_tr;
            const double to_boundary = tracker_.distance_to_boundary(p.pos, p.dir, layer);

            if (flight > to_boundary) {
                if (!std::isfinite(to_boundary)) {
                    out.leaked_weight += p.weight;
                    break;
                }
                // 01 §3's track-length time estimator: path length over speed,
                // accumulated from birth to progeny birth.
                out.time_weight += p.weight * to_boundary / speed;
                p.pos = p.pos + p.dir * to_boundary;
                layer = tracker_.nudge_and_locate(p.pos, p.dir);
                continue;
            }

            out.time_weight += p.weight * flight / speed;
            p.pos = p.pos + p.dir * flight;

            // Collision on the TRANSPORT-CORRECTED medium (ADR-021): the flight used
            // sigma_tr, so isotope selection AND the reaction split must too, or the
            // fission source is scaled by sigma_tr/sigma_t relative to the collision
            // density (which suppresses k as forward-peaking grows). mu_bar = 0 =>
            // sigma_tr = sigma_t, so isotropic sets are byte-identical to the analog.
            double pick = p.stream.uniform_d() * sigma_tr;
            const IsotopeSlot* chosen = &data.isotopes.front();
            for (const auto& slot : data.isotopes) {
                pick -= slot.number_density * slot.iso->g[g].sigma_tr();
                if (pick <= 0.0) {
                    chosen = &slot;
                    break;
                }
            }

            const auto& gd = chosen->iso->g[g];
            const double sigma_tr_i = gd.sigma_tr();
            if (sigma_tr_i <= 0.0) {
                break;
            }

            const double expected = p.weight * gd.nu * gd.sigma_f / sigma_tr_i;
            const double fissions = p.weight * gd.sigma_f / sigma_tr_i;  // no ν — for ν̄_eff (E3a)
            out.production += expected;
            out.source.by_isotope[static_cast<std::size_t>(chosen->global_index)] += expected;
            out.source.by_isotope_fissions[static_cast<std::size_t>(chosen->global_index)] +=
                fissions;
            out.source.by_layer[static_cast<std::size_t>(layer)] += expected;

            // E1c: bank floor(expected/k_gen + xi) progeny at the collision site.
            const int progeny = static_cast<int>(std::floor(expected / k_gen
                                                            + p.stream.uniform_d()));
            for (int n = 0; n < progeny; ++n) {
                FissionSite child;
                child.pos = p.pos;
                child.isotope = chosen->global_index;
                child.layer = layer;
                // Birth group from the fissioning isotope's chi spectrum.
                double xi = p.stream.uniform_d();
                child.group = xs::kGroups - 1;
                for (std::size_t b = 0; b < xs::kGroupsN; ++b) {
                    xi -= chosen->iso->g[b].chi;
                    if (xi <= 0.0) {
                        child.group = static_cast<int>(b);
                        break;
                    }
                }
                out.source.sites.push_back(child);

                const auto bin = [cell, outer](double v) {
                    const int b = static_cast<int>((v + outer) / cell);
                    return std::min(7, std::max(0, b));
                };
                const std::size_t mesh_index =
                    static_cast<std::size_t>(bin(child.pos.x) * 64 + bin(child.pos.y) * 8
                                             + bin(child.pos.z));
                out.source.mesh[mesh_index] += 1.0;
            }

            // implicit capture: survive as the transport-reduced scatter share
            // sigma_s,tr = sigma_s - (sigma_t - sigma_tr) = sigma_s - mu*sigma_s.
            p.weight *= (gd.sigma_s - (gd.sigma_t() - sigma_tr_i)) / sigma_tr_i;
            if (p.weight <= 0.0) {
                break;
            }
            if (p.weight < kWeightMin) {
                if (p.stream.uniform_d() > p.weight / kWeightSurv) {
                    break;
                }
                p.weight = kWeightSurv;
            }

            p.dir = sample_isotropic(p.stream);
            if (!chosen->iso->transfer_is_null) {
                double xi = p.stream.uniform_d();
                int to_group = p.group;
                for (std::size_t to = 0; to < xs::kGroupsN; ++to) {
                    xi -= chosen->iso->transfer[g][to];
                    if (xi <= 0.0) {
                        to_group = static_cast<int>(to);
                        break;
                    }
                }
                p.group = to_group;
            }
        }
    }
}

}  // namespace ns::ref

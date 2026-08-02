// Device analytic nested-sphere tracker (M4-T2-a), the float mirror of
// core/geometry's AnalyticSphereTracker (04 §4, D2/ADR-002).
//
// NOT a reuse of core/geometry: that class carries double, std::vector and
// std::string, none of which belong in a kernel. This is a POD, float
// re-implementation of the SAME normative math (04 §4). Cross-backend agreement
// is parity, not bit-identity — the CPU keeps double, the GPU uses float per
// 01 §9, and G0c compares them statistically. The `gpu.` tracker-parity test is
// the safety net against the two copies diverging, exactly as the Philox KATs
// guard the RNG reuse.
//
// Device-only header (included by .cu). Functions are host/device so a host
// unit could exercise the float math directly, but the parity test runs them on
// the device to prove the device code path.

#pragma once

#include <cmath>
#include <limits>

#include "core/hd.h"

namespace ns::gpu {

/// Float +infinity as a compile-time constant. MSVC's HUGE_VALF macro expands to
/// a form nvcc rejects under -Werror=all-warnings (a double literal narrowed to
/// float); this constant is baked in at compile time and works on both backends.
inline constexpr float kInfF = std::numeric_limits<float>::infinity();

/// Boundary tolerance in float, cm. The CPU uses 1e-9 (double); at float
/// precision near ~10 cm the ULP is ~1e-6, so 1e-9 is unresolvable and a
/// float-appropriate nudge/threshold is used instead. Documented numerical
/// detail; it only matters for a particle sitting ON a boundary (M4-T2-b), never
/// for the clean rays the parity test uses. 1e-4 cm = 1 µm, physically nil.
inline constexpr float kEpsilonF = 1e-4f;

/// Layer index sentinel for a point outside the outermost sphere.
inline constexpr int kOutsideDev = -1;

/// Minimal float vec3 — a kernel-friendly POD, no operators beyond what the
/// tracker needs.
struct DFloat3 {
    float x, y, z;
};

NUKESIM_HD inline float d_dot(DFloat3 a, DFloat3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

NUKESIM_HD inline DFloat3 d_advance(DFloat3 p, DFloat3 d, float t) noexcept {
    return {p.x + d.x * t, p.y + d.y * t, p.z + d.z * t};
}

/// Concentric layers on the device: flat float radii (innermost first, strictly
/// increasing), count. Non-owning — points into device memory.
struct DeviceLayerStack {
    const float* r_outer;
    int num;
};

/// Nearest positive root of |p + t·d|² = R², or +inf if none (04 §4 form, float).
NUKESIM_HD inline float d_ray_sphere(DFloat3 p, DFloat3 dir, float radius) noexcept {
    const float b = d_dot(p, dir);
    const float c = d_dot(p, p) - radius * radius;
    const float disc = b * b - c;  // d is unit, so the leading coefficient is 1
    if (disc < 0.0f) {
        return kInfF;  // the ray misses this sphere
    }
    const float root = sqrtf(disc);
    const float t_near = -b - root;
    const float t_far = -b + root;
    // Strictly positive: a root at t≈0 is the boundary the particle stands on;
    // returning it would loop the tracker.
    if (t_near > kEpsilonF) {
        return t_near;
    }
    if (t_far > kEpsilonF) {
        return t_far;
    }
    return kInfF;
}

/// Layer index containing p, or kOutsideDev. Half-open outward (a point exactly
/// on layer i's outer surface belongs to i) so locate and distance agree.
NUKESIM_HD inline int d_locate(DeviceLayerStack s, DFloat3 p) noexcept {
    const float r = sqrtf(d_dot(p, p));
    for (int i = 0; i < s.num; ++i) {
        if (r <= s.r_outer[i]) {
            return i;
        }
    }
    return kOutsideDev;
}

/// Inner radius of a layer: 0 for the innermost, else the previous outer.
NUKESIM_HD inline float d_inner_radius(DeviceLayerStack s, int layer) noexcept {
    return layer == 0 ? 0.0f : s.r_outer[layer - 1];
}

/// Distance to the nearer of the layer's inner/outer spheres, or +inf.
NUKESIM_HD inline float d_distance_to_boundary(DeviceLayerStack s, DFloat3 p, DFloat3 dir,
                                               int layer) noexcept {
    if (layer == kOutsideDev) {
        return d_ray_sphere(p, dir, s.r_outer[s.num - 1]);  // only the outer sphere is reachable
    }
    const float outer = d_ray_sphere(p, dir, s.r_outer[layer]);
    const float ir = d_inner_radius(s, layer);
    const float inner = ir > 0.0f ? d_ray_sphere(p, dir, ir) : kInfF;
    return fminf(inner, outer);
}

/// Move a point off a boundary ALONG the direction of travel, then locate it
/// (04 §4, MIN-14) — correct for entering and exiting alike.
NUKESIM_HD inline int d_nudge_and_locate(DeviceLayerStack s, DFloat3 p, DFloat3 dir) noexcept {
    return d_locate(s, d_advance(p, dir, kEpsilonF));
}

}  // namespace ns::gpu

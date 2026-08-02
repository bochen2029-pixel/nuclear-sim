// Nested-sphere geometry and the analytic tracker (04 §4, D2/ADR-002).
//
// Stages 0-2 are concentric spheres, so boundary crossings are closed-form.
// AnalyticSphereTracker is the PERMANENT oracle and the cloud path (H100/H200
// have no RT cores); OptiXCSGTracker arrives at M6 for the 32-lens assembly and
// must reproduce this one's answers.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ns::geom {

/// Returned by locate() for a point outside the outermost layer.
inline constexpr int kOutside = -1;

/// Boundary tolerance, cm. A point within this of a sphere is "on" it.
inline constexpr double kEpsilon = 1e-9;

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr double dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    constexpr double norm_squared() const noexcept { return dot(*this); }
    double norm() const noexcept;
    /// Unit vector. Undefined for the zero vector, which callers must not pass.
    Vec3 normalized() const noexcept;
};

struct Layer {
    std::string id;
    double r_outer = 0.0;
    int material_id = -1;
    std::string status;
};

/// Concentric layers, innermost first. The innermost layer's INNER radius is 0.
class LayerStack {
public:
    LayerStack() = default;
    explicit LayerStack(std::vector<Layer> layers);

    int size() const noexcept { return static_cast<int>(layers_.size()); }
    const Layer& layer(int index) const;
    const std::vector<Layer>& layers() const noexcept { return layers_; }

    /// Index of the layer containing `p`, or kOutside.
    int locate(Vec3 p) const noexcept;

    double radius_of(int layer) const;
    /// Inner radius of a layer: 0 for the innermost, else the previous outer.
    double inner_radius_of(int layer) const;

    /// Absolute values, innermost first, strictly increasing (MIN-15).
    void set_radii(const std::vector<double>& r_outer_cm);
    /// Uniform scale — Tier-1 hydro moves every boundary by the same factor.
    void scale_radii(double factor);

    double outermost_radius() const noexcept;

private:
    void validate() const;
    std::vector<Layer> layers_;
};

class Tracker {
public:
    virtual ~Tracker() = default;
    /// Distance along `dir` (unit) from `p` to the nearest boundary of `layer`,
    /// or +infinity when none is reachable.
    virtual double distance_to_boundary(Vec3 p, Vec3 dir, int layer) const = 0;
    virtual int locate(Vec3 p) const = 0;
    virtual void rebuild(const LayerStack& stack) = 0;
};

class AnalyticSphereTracker final : public Tracker {
public:
    AnalyticSphereTracker() = default;
    explicit AnalyticSphereTracker(const LayerStack& stack) { rebuild(stack); }

    double distance_to_boundary(Vec3 p, Vec3 dir, int layer) const override;
    int locate(Vec3 p) const override;
    void rebuild(const LayerStack& stack) override;

    /// Moves a point sitting on a boundary off it, ALONG the direction of
    /// travel, and returns the layer it then occupies (04 §4, MIN-14).
    ///
    /// Along-direction is correct for entering AND exiting crossings; nudging
    /// unconditionally inward puts an escaping neutron back inside the body it
    /// just left, which shows up as a leakage deficit rather than as a crash.
    int nudge_and_locate(Vec3 p, Vec3 dir) const;

    /// True when `p` lies within kEpsilon of any layer boundary.
    bool on_boundary(Vec3 p) const noexcept;

private:
    LayerStack stack_;
};

}  // namespace ns::geom

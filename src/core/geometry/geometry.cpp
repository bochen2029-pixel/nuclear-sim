#include "core/geometry/geometry.h"

#include "core/diagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns::geom {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// Nearest positive root of |p + t·d|² = R², or +inf if none.
///
/// 04 §4's normative form: b = p·d, c = |p|² − R², discriminant b² − c (d is a
/// unit vector, so the quadratic's leading coefficient is exactly 1).
double ray_sphere(const Vec3& p, const Vec3& d, double radius) noexcept {
    const double b = p.dot(d);
    const double c = p.norm_squared() - radius * radius;
    const double discriminant = b * b - c;
    if (discriminant < 0.0) {
        return kInfinity;  // the ray misses this sphere entirely
    }
    const double root = std::sqrt(discriminant);

    // Both roots, near one first. A tangential ray has t_near == t_far; that is
    // a real, single grazing intersection, not a miss.
    const double t_near = -b - root;
    const double t_far = -b + root;

    // Strictly positive: a root at t = 0 is the boundary the particle is
    // already standing on, and returning it would make the tracker loop.
    if (t_near > kEpsilon) {
        return t_near;
    }
    if (t_far > kEpsilon) {
        return t_far;
    }
    return kInfinity;
}

}  // namespace

double Vec3::norm() const noexcept { return std::sqrt(norm_squared()); }

Vec3 Vec3::normalized() const noexcept {
    const double n = norm();
    return n > 0.0 ? Vec3{x / n, y / n, z / n} : Vec3{};
}

LayerStack::LayerStack(std::vector<Layer> layers) : layers_(std::move(layers)) { validate(); }

void LayerStack::validate() const {
    require(!layers_.empty(), "<LayerStack>", "layers", "must contain at least one layer");
    double previous = 0.0;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const auto& layer = layers_[i];
        require(layer.r_outer > previous, "<LayerStack>",
                "layers[" + std::to_string(i) + "].r_outer",
                "radii must be strictly increasing from an innermost INNER radius of 0; "
                    + std::to_string(layer.r_outer) + " does not exceed "
                    + std::to_string(previous));
        previous = layer.r_outer;
    }
}

const Layer& LayerStack::layer(int index) const {
    require(index >= 0 && index < size(), "<LayerStack>", "layer index",
            "out of range: " + std::to_string(index) + " of " + std::to_string(size()));
    return layers_[static_cast<std::size_t>(index)];
}

int LayerStack::locate(Vec3 p) const noexcept {
    const double r = p.norm();
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        // Half-open in the outward sense: a point exactly on layer i's outer
        // surface belongs to i, so `locate` and `distance_to_boundary` agree
        // about which side of a crossing a particle is on.
        if (r <= layers_[i].r_outer) {
            return static_cast<int>(i);
        }
    }
    return kOutside;
}

double LayerStack::radius_of(int layer) const { return this->layer(layer).r_outer; }

double LayerStack::inner_radius_of(int layer) const {
    require(layer >= 0 && layer < size(), "<LayerStack>", "layer index",
            "out of range: " + std::to_string(layer));
    // The innermost layer has no inner boundary — its inner radius is 0, not
    // the radius of some notional cavity (04 §7).
    return layer == 0 ? 0.0 : layers_[static_cast<std::size_t>(layer) - 1].r_outer;
}

void LayerStack::set_radii(const std::vector<double>& r_outer_cm) {
    require(r_outer_cm.size() == layers_.size(), "<LayerStack>", "set_radii",
            "expects one ABSOLUTE radius per layer (MIN-15); got "
                + std::to_string(r_outer_cm.size()) + " for " + std::to_string(layers_.size())
                + " layers");
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        layers_[i].r_outer = r_outer_cm[i];
    }
    validate();
}

void LayerStack::scale_radii(double factor) {
    require(factor > 0.0, "<LayerStack>", "scale_radii",
            "factor must be positive; got " + std::to_string(factor));
    for (auto& layer : layers_) {
        layer.r_outer *= factor;
    }
    validate();
}

double LayerStack::outermost_radius() const noexcept {
    return layers_.empty() ? 0.0 : layers_.back().r_outer;
}

void AnalyticSphereTracker::rebuild(const LayerStack& stack) { stack_ = stack; }

int AnalyticSphereTracker::locate(Vec3 p) const { return stack_.locate(p); }

double AnalyticSphereTracker::distance_to_boundary(Vec3 p, Vec3 dir, int layer) const {
    if (layer == kOutside) {
        // From outside, the only reachable boundary is the outermost sphere.
        return ray_sphere(p, dir, stack_.outermost_radius());
    }
    require(layer >= 0 && layer < stack_.size(), "<AnalyticSphereTracker>", "layer",
            "out of range: " + std::to_string(layer));

    // Nearer of this layer's own inner and outer spheres. The inner sphere is
    // frequently unreachable (the ray points outward, or misses the cavity), in
    // which case ray_sphere returns +inf and the outer one wins.
    const double outer = ray_sphere(p, dir, stack_.radius_of(layer));
    const double inner_radius = stack_.inner_radius_of(layer);
    const double inner = inner_radius > 0.0 ? ray_sphere(p, dir, inner_radius) : kInfinity;
    return std::min(inner, outer);
}

bool AnalyticSphereTracker::on_boundary(Vec3 p) const noexcept {
    const double r = p.norm();
    for (const auto& layer : stack_.layers()) {
        if (std::abs(r - layer.r_outer) <= kEpsilon) {
            return true;
        }
    }
    return false;
}

int AnalyticSphereTracker::nudge_and_locate(Vec3 p, Vec3 dir) const {
    // ALONG the direction of travel (04 §4, MIN-14). Correct for entering and
    // exiting alike: an inbound particle lands just inside, an outbound one just
    // outside. Nudging unconditionally inward would return an escaping neutron
    // to the body it just left — a leakage deficit, not a crash, and therefore
    // the kind of defect that survives to a gate.
    return stack_.locate(p + dir * kEpsilon);
}

}  // namespace ns::geom

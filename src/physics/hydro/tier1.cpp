// Tier-1 parametric compression (E4 / 01 §5). See tier1.h.

#include "physics/hydro/tier1.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ns::physics {

double smootherstep(double u) {
    if (u <= 0.0) {
        return 0.0;
    }
    if (u >= 1.0) {
        return 1.0;
    }
    return u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
}

double Tier1Compression::u_of(double t_s) const {
    if (t_c_s <= 0.0) {
        return t_s >= t0_s ? 1.0 : 0.0;
    }
    return std::clamp((t_s - t0_s) / t_c_s, 0.0, 1.0);
}

double Tier1Compression::s_of(double t_s) const {
    return smootherstep(u_of(t_s));
}

double Tier1Compression::radius_scale(double s) const {
    return 1.0 + (std::pow(ratio, -1.0 / 3.0) - 1.0) * s;
}

double Tier1Compression::density_ratio(double s) const {
    const double rs = radius_scale(s);
    return 1.0 / (rs * rs * rs);
}

ns::geom::LayerStack compress(const ns::geom::LayerStack& stack, double scale) {
    std::vector<double> radii;
    radii.reserve(static_cast<std::size_t>(stack.size()));
    for (const auto& layer : stack.layers()) {
        radii.push_back(layer.r_outer * scale);
    }
    ns::geom::LayerStack out = stack;  // preserves names / materials / status
    out.set_radii(radii);
    return out;
}

}  // namespace ns::physics

// α-mode burst kinetics (E3, 01 §4). See kinetics.h.

#include "physics/kinetics/kinetics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns::physics {
namespace {

constexpr double kNegInf = -std::numeric_limits<double>::infinity();

// log10(10^a + 10^b), numerically stable. kNegInf is the additive identity (0).
double logsumexp10(double a, double b) {
    if (a == kNegInf) {
        return b;
    }
    if (b == kNegInf) {
        return a;
    }
    const double hi = std::max(a, b);
    const double lo = std::min(a, b);
    return hi + std::log10(1.0 + std::pow(10.0, lo - hi));
}

// Renormalize the mantissa if it grows past this (safely below double's ~1e308),
// so a long supercritical run never overflows. The choice is invisible to the
// log-domain cumulants, so it changes no physical result.
constexpr double kRenormAbove = 1e250;

}  // namespace

double nu_eff(const ref::FissionSource& source) {
    double prod = 0.0;
    double fiss = 0.0;
    for (const double p : source.by_isotope) {
        prod += p;
    }
    for (const double f : source.by_isotope_fissions) {
        fiss += f;
    }
    return fiss > 0.0 ? prod / fiss : 0.0;
}

std::vector<double> isotope_fission_shares(const ref::FissionSource& source) {
    std::vector<double> shares(source.by_isotope.size(), 0.0);
    double total = 0.0;
    for (const double p : source.by_isotope) {
        total += p;
    }
    if (total > 0.0) {
        for (std::size_t i = 0; i < shares.size(); ++i) {
            shares[i] = source.by_isotope[i] / total;
        }
    }
    return shares;
}

double rossi_alpha(double k, double lambda_s) {
    return lambda_s > 0.0 ? (k - 1.0) / lambda_s : 0.0;
}

double refresh_q(double delta_k, double k, int generations_since_refresh) {
    if (k <= 0.0 || generations_since_refresh <= 0) {
        return 0.0;
    }
    return std::abs(delta_k) / (k * static_cast<double>(generations_since_refresh));
}

BurstAccumulator::BurstAccumulator(double n0)
    : mant_(n0 > 0.0 ? n0 : 1.0),
      off_(0.0),
      log_fcum_(kNegInf),
      log_ecum_(kNegInf),
      log_flast_(kNegInf) {
    log10_n_hist_.push_back(std::log10(mant_) + off_);  // log10 N_0
}

void BurstAccumulator::step(double k_prompt, double nu_eff, double e_f, double s_next) {
    // F_n = k·N_n/ν̄_eff — its log10 carries off_, so it is an ABSOLUTE quantity
    // and renormalizing (mant_,off_) cannot change it, hence not F_cum/E_cum.
    double log_fn = kNegInf;
    if (k_prompt > 0.0 && mant_ > 0.0 && nu_eff > 0.0) {
        log_fn = std::log10(k_prompt) + std::log10(mant_) + off_ - std::log10(nu_eff);
    }
    log_flast_ = log_fn;
    log_fcum_ = logsumexp10(log_fcum_, log_fn);
    if (log_fn != kNegInf && e_f > 0.0) {
        log_ecum_ = logsumexp10(log_ecum_, log_fn + std::log10(e_f));
    }

    // N_{n+1} = k·N_n + S_next (S_next brought into the mantissa's scale).
    mant_ = (k_prompt > 0.0 ? k_prompt * mant_ : 0.0) + s_next * std::pow(10.0, -off_);
    if (mant_ < 0.0) {
        mant_ = 0.0;
    }
    ++n_;
    if (mant_ > kRenormAbove) {
        renormalize();
    }
    log10_n_hist_.push_back(mant_ > 0.0 ? std::log10(mant_) + off_ : kNegInf);
}

void BurstAccumulator::renormalize() {
    if (mant_ <= 0.0) {
        return;
    }
    const double shift = std::floor(std::log10(mant_));  // mantissa → [1, 10)
    mant_ *= std::pow(10.0, -shift);
    off_ += shift;
}

double BurstAccumulator::log10_N() const noexcept {
    return mant_ > 0.0 ? std::log10(mant_) + off_ : kNegInf;
}

double BurstAccumulator::log10_yield_kt(double phi_kt) const noexcept {
    return phi_kt > 0.0 ? log_fcum_ - std::log10(phi_kt) : kNegInf;
}

}  // namespace ns::physics

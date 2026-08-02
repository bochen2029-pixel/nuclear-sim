// Few-group cross sections, schema v2 (03 §2, 04 §3).
//
// D4/ADR-004: a curated, cited few-group dataset — no runtime ENDF parsing.
// The loader enforces every v2 semantic, because several of them are invisible
// downstream: a prompt-only `nu` produces a k_eff that looks entirely
// reasonable and is wrong by ~650 pcm (ADR-013).

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ns::xs {

/// v2 fixes four groups. 04 §3's Transfer is a 4x4 array, so the group count is
/// part of the normative API rather than a runtime value; more groups is the
/// schema-v3 path via ADR (D4, R-1).
inline constexpr int kGroups = 4;

struct GroupData {
    double nu = 0.0;         // TOTAL nu-bar (prompt + delayed) — ADR-013
    double chi = 0.0;
    double sigma_f = 0.0;
    double sigma_c = 0.0;    // radiative capture ONLY; the v0 name sigma_a is rejected
    double sigma_s = 0.0;
    double sigma_n2n = 0.0;
    double mu_bar = 0.0;     // mean lab scattering cosine, REQUIRED (01 §2)

    /// Computed, never read from file (03 §2).
    constexpr double sigma_t() const noexcept {
        return sigma_f + sigma_c + sigma_s + sigma_n2n;
    }

    /// Transport-corrected total, used for flight sampling.
    constexpr double sigma_tr() const noexcept { return sigma_t() - mu_bar * sigma_s; }
};

using Transfer = std::array<std::array<double, kGroups>, kGroups>;  // [from][to], rows sum to 1

struct IsotopeXS {
    std::string name;
    std::vector<GroupData> g;
    Transfer transfer{};
    bool transfer_is_null = false;  // permitted only for status = "SIM" sets
    /// Delayed-neutron fraction. REQUIRED in v2 (ADR-013): it is the only thing
    /// making the TOTAL-nu convention enforceable, since a prompt-only dataset
    /// is numerically undetectable.
    double beta = 0.0;
    std::string cite;
    std::string status;
};

class FewGroupXS {
public:
    /// Enforces 03 §2 in full. Throws ns::LoadError naming file/field/constraint.
    static FewGroupXS load(const std::filesystem::path& json);

    int groups() const noexcept { return kGroups; }
    const std::string& name() const noexcept { return name_; }
    const std::vector<double>& bounds_MeV() const noexcept { return bounds_; }

    bool has_isotope(std::string_view name) const;
    /// Throws ns::LoadError if absent — a missing isotope must never be a zero
    /// cross section, which would read as "transparent" rather than "absent".
    const IsotopeXS& isotope(std::string_view name) const;

    std::vector<std::string> isotope_names() const;

    /// True when any isotope has a null transfer matrix. Gate runs reject these
    /// (03 §2); nukebench checks this before accepting a set as gate evidence.
    bool has_null_transfer() const noexcept { return has_null_transfer_; }

private:
    std::string name_;
    std::vector<double> bounds_;
    std::map<std::string, IsotopeXS, std::less<>> isotopes_;
    bool has_null_transfer_ = false;
};

}  // namespace ns::xs

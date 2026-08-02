#include "core/xs/xs.h"

#include "core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

namespace ns::xs {
namespace {

using nlohmann::json;

constexpr double kRowSumTol = 1e-6;

json parse_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    require(stream.good(), path, "<file>", "cannot be opened for reading");
    try {
        return json::parse(stream);
    } catch (const json::parse_error& err) {
        throw LoadError(path, "<document>", std::string("invalid JSON: ") + err.what());
    }
}

std::vector<double> require_array(const json& obj, const std::filesystem::path& path,
                                  const std::string& key, const std::string& field,
                                  std::size_t expected) {
    require(obj.contains(key), path, field, "is REQUIRED (03 §2)");
    const json& node = obj.at(key);
    require(node.is_array(), path, field, "must be an array of numbers");
    require(node.size() == expected, path, field,
            "must have exactly " + std::to_string(expected) + " entries, one per group; got "
                + std::to_string(node.size()));
    std::vector<double> out;
    out.reserve(expected);
    for (std::size_t i = 0; i < node.size(); ++i) {
        require(node[i].is_number(), path, field + "[" + std::to_string(i) + "]",
                "must be a number");
        out.push_back(node[i].get<double>());
    }
    return out;
}

void reject_nonnegative(const std::vector<double>& values, const std::filesystem::path& path,
                        const std::string& field) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        require(values[i] >= 0.0, path, field + "[" + std::to_string(i) + "]",
                "cross sections and multiplicities must be non-negative; got "
                    + std::to_string(values[i]));
    }
}

void load_transfer(const json& iso, const std::filesystem::path& path, const std::string& name,
                   IsotopeXS& out) {
    const std::string field = "isotopes." + name + ".transfer";

    // REQUIRED when n_groups > 1; null permitted ONLY in SIM sets (03 §2).
    if (!iso.contains("transfer") || iso.at("transfer").is_null()) {
        require(out.status == "SIM", path, field,
                "is REQUIRED when n_groups > 1; null is permitted only for status = \"SIM\" "
                "test sets, and gate runs reject null-transfer sets (03 §2). This isotope's "
                "status is \"" + out.status + "\"");
        out.transfer_is_null = true;
        return;
    }

    const json& matrix = iso.at("transfer");
    require(matrix.is_array() && matrix.size() == kGroupsN, path, field,
            "must be a " + std::to_string(kGroups) + "x" + std::to_string(kGroups) + " matrix");

    for (std::size_t from = 0; from < kGroupsN; ++from) {
        const std::string row_field = field + "[" + std::to_string(from) + "]";
        require(matrix[from].is_array() && matrix[from].size() == kGroupsN, path, row_field,
                "each row must have " + std::to_string(kGroups) + " entries");

        double sum = 0.0;
        for (std::size_t to = 0; to < kGroupsN; ++to) {
            require(matrix[from][to].is_number(), path,
                    row_field + "[" + std::to_string(to) + "]", "must be a number");
            const double p = matrix[from][to].get<double>();
            require(p >= 0.0, path, row_field + "[" + std::to_string(to) + "]",
                    "transfer entries are probabilities and must be non-negative");

            // Groups are ordered highest -> lowest energy, so `to < from` is
            // upscatter. v2 forbids it outright: a fast-spectrum few-group set
            // that upscatters is either mis-collapsed or mis-ordered, and both
            // produce a plausible-looking k.
            if (to < from) {
                require(p == 0.0, path, row_field + "[" + std::to_string(to) + "]",
                        "upscatter (to < from) MUST be zero in schema v2; got "
                            + std::to_string(p));
            }
            out.transfer[from][to] = p;
            sum += p;
        }
        require(std::abs(sum - 1.0) <= kRowSumTol, path, row_field,
                "row must sum to 1.0 +/- 1e-6 (it is a probability distribution); sum = "
                    + std::to_string(sum));
    }
}

IsotopeXS load_isotope(const json& iso, const std::filesystem::path& path, const std::string& name) {
    const std::string prefix = "isotopes." + name;
    require(iso.is_object(), path, prefix, "must be an object");

    // Migration diagnostics first: they are far more useful than the generic
    // "unknown key" error a v0 file would otherwise produce.
    require(!iso.contains("sigma_a"), path, prefix + ".sigma_a",
            "REJECTED: schema v2 renamed it. sigma_a was ambiguous between capture-only and "
            "capture+fission; use sigma_c for radiative capture ONLY (03 §2)");
    require(!iso.contains("sigma_t"), path, prefix + ".sigma_t",
            "MUST NOT appear in the file: sigma_t is computed by the loader as "
            "sigma_f + sigma_c + sigma_s + sigma_n2n (03 §2)");
    require(!iso.contains("sigma_tr"), path, prefix + ".sigma_tr",
            "MUST NOT appear in the file: computed as sigma_t - mu_bar*sigma_s (03 §2)");

    IsotopeXS out;
    out.name = name;

    require(iso.contains("status") && iso.at("status").is_string(), path, prefix + ".status",
            "is REQUIRED; every value carries cite/status (03 §2)");
    out.status = iso.at("status").get<std::string>();
    require(iso.contains("cite") && iso.at("cite").is_string(), path, prefix + ".cite",
            "is REQUIRED; every value carries cite/status (03 §2)");
    out.cite = iso.at("cite").get<std::string>();

    // beta is what makes the TOTAL-nu convention enforceable (ADR-013). A
    // prompt-only dataset cannot be detected numerically, so its absence is an
    // error rather than a defaulted zero.
    require(iso.contains("beta"), path, prefix + ".beta",
            "is REQUIRED in schema v2 (ADR-013): nu is TOTAL nu-bar, and beta is what lets "
            "k_prompt be derived exactly once downstream. A prompt-only dataset is "
            "numerically undetectable, so this field is the convention's only enforcement");
    require(iso.at("beta").is_number(), path, prefix + ".beta", "must be a number");
    out.beta = iso.at("beta").get<double>();
    require(out.beta >= 0.0 && out.beta < 0.05, path, prefix + ".beta",
            "delayed-neutron fraction must lie in [0, 0.05); got " + std::to_string(out.beta));

    const auto nu = require_array(iso, path, "nu", prefix + ".nu", kGroupsN);
    const auto chi = require_array(iso, path, "chi", prefix + ".chi", kGroupsN);
    const auto sigma_f = require_array(iso, path, "sigma_f", prefix + ".sigma_f", kGroupsN);
    const auto sigma_c = require_array(iso, path, "sigma_c", prefix + ".sigma_c", kGroupsN);
    const auto sigma_s = require_array(iso, path, "sigma_s", prefix + ".sigma_s", kGroupsN);
    const auto mu_bar = require_array(iso, path, "mu_bar", prefix + ".mu_bar", kGroupsN);

    std::vector<double> sigma_n2n(kGroupsN, 0.0);  // optional, default 0 (03 §2)
    if (iso.contains("sigma_n2n") && !iso.at("sigma_n2n").is_null()) {
        sigma_n2n = require_array(iso, path, "sigma_n2n", prefix + ".sigma_n2n", kGroupsN);
    }

    reject_nonnegative(nu, path, prefix + ".nu");
    reject_nonnegative(chi, path, prefix + ".chi");
    reject_nonnegative(sigma_f, path, prefix + ".sigma_f");
    reject_nonnegative(sigma_c, path, prefix + ".sigma_c");
    reject_nonnegative(sigma_s, path, prefix + ".sigma_s");
    reject_nonnegative(sigma_n2n, path, prefix + ".sigma_n2n");

    for (int g = 0; g < kGroups; ++g) {
        const std::string at = "[" + std::to_string(g) + "]";
        require(mu_bar[static_cast<std::size_t>(g)] >= -1.0
                    && mu_bar[static_cast<std::size_t>(g)] <= 1.0,
                path, prefix + ".mu_bar" + at,
                "mean scattering cosine must lie in [-1, 1]; got "
                    + std::to_string(mu_bar[static_cast<std::size_t>(g)]));

        GroupData data;
        data.nu = nu[static_cast<std::size_t>(g)];
        data.chi = chi[static_cast<std::size_t>(g)];
        data.sigma_f = sigma_f[static_cast<std::size_t>(g)];
        data.sigma_c = sigma_c[static_cast<std::size_t>(g)];
        data.sigma_s = sigma_s[static_cast<std::size_t>(g)];
        data.sigma_n2n = sigma_n2n[static_cast<std::size_t>(g)];
        data.mu_bar = mu_bar[static_cast<std::size_t>(g)];

        // sigma_tr drives flight sampling; a non-positive value would make the
        // mean free path infinite or negative rather than merely inaccurate.
        require(data.sigma_tr() > 0.0, path, prefix + ".sigma_tr" + at,
                "transport-corrected total must be positive (it sets the mean free path); "
                "sigma_t = " + std::to_string(data.sigma_t()) + ", mu_bar*sigma_s = "
                    + std::to_string(data.mu_bar * data.sigma_s));
        out.g.push_back(data);
    }

    load_transfer(iso, path, name, out);
    return out;
}

}  // namespace

FewGroupXS FewGroupXS::load(const std::filesystem::path& path) {
    const json doc = parse_file(path);

    require(doc.contains("schema_version"), path, "schema_version", "is REQUIRED");
    require(doc.at("schema_version").is_number_integer(), path, "schema_version",
            "must be an integer");
    const int version = doc.at("schema_version").get<int>();
    require(version == 2, path, "schema_version",
            "must be 2; loaders MUST reject unknown versions (03 §preamble). Got "
                + std::to_string(version)
                + (version == 1 ? " — v1 used sigma_a; see 03 §2 for the v2 migration" : ""));

    FewGroupXS out;
    require(doc.contains("name") && doc.at("name").is_string(), path, "name",
            "is REQUIRED and must be a string");
    out.name_ = doc.at("name").get<std::string>();

    require(doc.contains("group_bounds_MeV"), path, "group_bounds_MeV", "is REQUIRED");
    const json& bounds = doc.at("group_bounds_MeV");
    require(bounds.is_array(), path, "group_bounds_MeV", "must be an array");
    require(bounds.size() == kGroupsN + 1, path, "group_bounds_MeV",
            "must have n_groups+1 = " + std::to_string(kGroups + 1)
                + " entries. Schema v2 fixes 4 groups (04 §3's Transfer is 4x4); more groups "
                  "is the schema-v3 path via ADR (D4, R-1). Got "
                + std::to_string(bounds.size()));

    for (std::size_t i = 0; i < bounds.size(); ++i) {
        require(bounds[i].is_number(), path, "group_bounds_MeV[" + std::to_string(i) + "]",
                "must be a number");
        out.bounds_.push_back(bounds[i].get<double>());
        if (i > 0) {
            // Strictly descending: groups are 0-based, highest -> lowest energy,
            // and group g spans (bounds[g+1], bounds[g]].
            require(out.bounds_[i] < out.bounds_[i - 1], path,
                    "group_bounds_MeV[" + std::to_string(i) + "]",
                    "must be strictly descending (groups run highest -> lowest energy, 03 §2); "
                        + std::to_string(out.bounds_[i])
                        + " is not below " + std::to_string(out.bounds_[i - 1]));
        }
    }
    require(out.bounds_.back() > 0.0, path, "group_bounds_MeV",
            "the lowest bound must be positive");

    require(doc.contains("isotopes") && doc.at("isotopes").is_object(), path, "isotopes",
            "is REQUIRED and must be an object");
    const json& isotopes = doc.at("isotopes");
    require(!isotopes.empty(), path, "isotopes", "must contain at least one isotope");

    for (const auto& [name, node] : isotopes.items()) {
        IsotopeXS iso = load_isotope(node, path, name);
        out.has_null_transfer_ = out.has_null_transfer_ || iso.transfer_is_null;
        out.isotopes_.emplace(name, std::move(iso));
    }
    return out;
}

bool FewGroupXS::has_isotope(std::string_view name) const {
    return isotopes_.find(name) != isotopes_.end();
}

const IsotopeXS& FewGroupXS::isotope(std::string_view name) const {
    const auto it = isotopes_.find(name);
    if (it == isotopes_.end()) {
        throw LoadError(name_ + " (xs set)", std::string(name),
                        "isotope is not present in this cross-section set; a missing isotope "
                        "must never be treated as a zero cross section");
    }
    return it->second;
}

std::vector<std::string> FewGroupXS::isotope_names() const {
    std::vector<std::string> names;
    names.reserve(isotopes_.size());
    for (const auto& [name, _] : isotopes_) {
        names.push_back(name);
    }
    return names;
}

}  // namespace ns::xs

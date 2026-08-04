#include "app/nukebench/gates.h"

#include "core/constants/constants.h"
#include "core/hash/sha256.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>

namespace ns::nukebench {

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw GatesError("cannot open " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Drop the \r of every \r\n so the hash is identical on Windows (dev) and Linux (CI),
/// matching gen_gates.py's normalization.
std::string normalize_lf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    return out;
}

std::string req_str(const toml::table& t, std::string_view key, const std::string& ctx) {
    auto v = t[key].value<std::string>();
    if (!v) throw GatesError(ctx + "." + std::string(key) + " is required (string)");
    return *v;
}

std::int64_t req_int(const toml::table& t, std::string_view key, const std::string& ctx) {
    auto v = t[key].value<std::int64_t>();
    if (!v) throw GatesError(ctx + "." + std::string(key) + " is required (integer)");
    return *v;
}

}  // namespace

GatesConfig load_gates(const std::filesystem::path& gates_toml,
                       const std::filesystem::path& spec_08_validation) {
    toml::table doc;
    try {
        doc = toml::parse_file(gates_toml.string());
    } catch (const toml::parse_error& e) {
        throw GatesError("gates.toml parse error: " + std::string(e.description()));
    }

    GatesConfig cfg;
    cfg.schema_version = static_cast<int>(req_int(doc, "schema_version", "gates"));
    if (cfg.schema_version != 1) throw GatesError("gates.toml schema_version must be 1");
    cfg.generated_from = req_str(doc, "generated_from", "gates");
    cfg.spec_sha256 = req_str(doc, "spec_sha256", "gates");

    // Guard (2): spec_sha256 must equal sha256 of the normative 08-validation.md.
    const std::string actual =
        ns::hash::sha256_hex(normalize_lf(read_file(spec_08_validation)));
    if (actual != cfg.spec_sha256) {
        throw GatesError("gates.toml spec_sha256 (" + cfg.spec_sha256.substr(0, 12) +
                         "...) != sha256(" + spec_08_validation.filename().string() + ") (" +
                         actual.substr(0, 12) + "...) -- regenerate gates.toml via gen_gates (03 sec 10)");
    }

    const auto* gates_arr = doc["gate"].as_array();
    if (!gates_arr || gates_arr->empty())
        throw GatesError("gates.toml has no [[gate]] entries");

    for (const auto& gnode : *gates_arr) {
        const auto* gt = gnode.as_table();
        if (!gt) throw GatesError("[[gate]] must be a table");
        Gate g;
        g.id = req_str(*gt, "id", "gate");
        g.title = req_str(*gt, "title", "gate " + g.id);
        g.scenario = req_str(*gt, "scenario", "gate " + g.id);

        const auto* seeds = (*gt)["seeds"].as_array();
        if (!seeds || seeds->empty())
            throw GatesError("gate " + g.id + ".seeds must be a non-empty array");
        for (const auto& s : *seeds) {
            auto sv = s.value<std::int64_t>();
            if (!sv) throw GatesError("gate " + g.id + ".seeds entries must be integers");
            g.seeds.push_back(*sv);
        }

        const auto* eig = (*gt)["eigen"].as_table();
        if (!eig) throw GatesError("gate " + g.id + " requires [gate.eigen]");
        g.eigen.batch = req_int(*eig, "batch", "gate " + g.id + ".eigen");
        g.eigen.inactive = static_cast<int>(req_int(*eig, "inactive", "gate " + g.id + ".eigen"));
        g.eigen.active = static_cast<int>(req_int(*eig, "active", "gate " + g.id + ".eigen"));

        const auto* crit = (*gt)["criterion"].as_array();
        if (!crit || crit->empty())
            throw GatesError("gate " + g.id + " requires [[gate.criterion]]");
        for (const auto& cnode : *crit) {
            const auto* ct = cnode.as_table();
            if (!ct) throw GatesError("gate " + g.id + " [[gate.criterion]] must be a table");
            Criterion c;
            c.name = req_str(*ct, "name", "gate " + g.id + ".criterion");
            c.op = req_str(*ct, "op", "gate " + g.id + ".criterion " + c.name);
            if (c.op != "abs_le" && c.op != "le")
                throw GatesError("gate " + g.id + " criterion " + c.name + ": op must be abs_le|le");
            auto val = (*ct)["value"].value<double>();
            if (!val)
                throw GatesError("gate " + g.id + " criterion " + c.name + ".value required (number)");
            c.value = *val;
            c.constant_id = req_str(*ct, "constant_id", "gate " + g.id + ".criterion " + c.name);

            // Guard (1): the threshold must equal its constant (no drifted copy, 03 sec 10).
            double kval = 0.0;
            try {
                kval = ns::consts::get(c.constant_id);
            } catch (const std::exception& e) {
                throw GatesError("gate " + g.id + " criterion " + c.name + ": constant_id " +
                                 c.constant_id + " does not resolve -- " + e.what());
            }
            if (std::abs(kval - c.value) > 1e-9 * std::max(1.0, std::abs(kval))) {
                throw GatesError("gate " + g.id + " criterion " + c.name + ": value " +
                                 std::to_string(c.value) + " != " + c.constant_id + " = " +
                                 std::to_string(kval) + " (03 sec 10 drift)");
            }
            g.criteria.push_back(std::move(c));
        }
        cfg.gates.push_back(std::move(g));
    }
    return cfg;
}

const Gate& find_gate(const GatesConfig& cfg, const std::string& id) {
    for (const auto& g : cfg.gates)
        if (g.id == id) return g;
    throw GatesError("no gate '" + id + "' in gates.toml");
}

}  // namespace ns::nukebench

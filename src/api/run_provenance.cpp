// run.json (03 §6) (de)serialize + the unit_id derivation — M3-T3-f.

#include "api/run_provenance.h"

#include "core/hash/sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns::api {

namespace {
using json = nlohmann::ordered_json;

const json& require_field(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) {
        throw std::runtime_error(std::string("run.json: missing required field '") + key + "'");
    }
    return *it;
}
}  // namespace

std::string to_json(const RunProvenance& r, int indent) {
    json j;
    j["schema_version"] = r.schema_version;
    j["run_id"] = r.run_id;
    j["unit_id"] = r.unit_id;
    j["scenario_file"] = r.scenario_file;
    j["scenario_sha256"] = r.scenario_sha256;

    json materials = json::object();
    for (const auto& [name, hash] : r.data_hashes.materials) {
        materials[name] = hash;
    }
    j["data_hashes"] = {{"xs", r.data_hashes.xs}, {"materials", std::move(materials)}};

    j["seed"] = r.seed;
    j["code_version"] = r.code_version;
    j["spec_version"] = r.spec_version;
    j["git"] = r.git;
    j["dirty"] = r.dirty;
    j["backend"] = r.backend;
    j["device"] = r.device;
    j["started"] = r.started;
    j["finished"] = r.finished;

    return j.dump(indent);
}

RunProvenance parse_run_json(const std::string& text) {
    const json j = json::parse(text);
    RunProvenance r;

    if (const auto it = j.find("schema_version"); it != j.end()) {
        r.schema_version = it->get<int>();
    }
    r.run_id = require_field(j, "run_id").get<std::string>();
    r.unit_id = require_field(j, "unit_id").get<std::string>();
    r.scenario_file = require_field(j, "scenario_file").get<std::string>();
    r.scenario_sha256 = require_field(j, "scenario_sha256").get<std::string>();

    const json& dh = require_field(j, "data_hashes");
    r.data_hashes.xs = require_field(dh, "xs").get<std::string>();
    const json& mats = require_field(dh, "materials");
    for (const auto& [name, hash] : mats.items()) {
        r.data_hashes.materials.emplace_back(name, hash.get<std::string>());
    }

    r.seed = require_field(j, "seed").get<std::uint64_t>();
    r.code_version = require_field(j, "code_version").get<std::string>();
    r.spec_version = require_field(j, "spec_version").get<std::string>();
    r.git = require_field(j, "git").get<std::string>();
    r.dirty = require_field(j, "dirty").get<bool>();
    r.backend = require_field(j, "backend").get<std::string>();
    r.device = require_field(j, "device").get<std::string>();
    r.started = require_field(j, "started").get<std::string>();
    r.finished = require_field(j, "finished").get<std::string>();

    return r;
}

std::string compute_unit_id(const std::string& scenario_canonical_hash,
                            const std::vector<std::string>& overrides, std::uint64_t seed) {
    // Overrides are a set: sort so the dedup key is order-independent, matching
    // canonical_hash's own sorted override block (canonical_hash.cpp).
    std::vector<std::string> sorted(overrides);
    std::sort(sorted.begin(), sorted.end());

    ns::hash::Sha256 hasher;
    hasher.update("canonical_hash=");
    hasher.update(scenario_canonical_hash);
    hasher.update("\n");
    for (const auto& ov : sorted) {
        hasher.update("override=");
        hasher.update(ov);
        hasher.update("\n");
    }
    hasher.update("seed=");
    hasher.update(std::to_string(seed));
    hasher.update("\n");
    return hasher.hex_digest();
}

}  // namespace ns::api

// run.json — run provenance / artifact identity (03 §6, schema v1) — M3-T3-f.
//
// The reproducibility record every run emits alongside tally.json (03 §5): which
// scenario + data + seed + code/spec version + backend/device produced this run,
// and the `unit_id` that is the idempotent dedup key and artifact directory name
// (03 §6, D6). This is the first increment of the fusion API (step 4).
//
// `src/api/` is nscore's studio-facing surface: the layer a frontend (src/app/)
// or a language binding (viz/js/simstub.js) calls to run the engine and record a
// run. It composes physics (physics/couple), so it is nscore, NOT a frontend —
// D7 keeps frontends physics-free, and reimplementing this shape in one would be
// the defect D7 forbids.
//
// SCOPE (bounded, the M3-T3-a pattern): the run.json STRUCT + JSON (de)serialize +
// the `unit_id` derivation, tested against the 03 §6 example read from the spec
// markdown. The evaluate/generate_run surface that FILLS a RunProvenance from a
// scenario + a burst is M3-T3-g.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ns::api {

/// data_hashes (03 §6): sha256 of the resolved xs set + each resolved material,
/// so a run records the exact data it ran against. `materials` preserves
/// insertion order for deterministic re-serialization (it is a JSON object).
struct RunDataHashes {
    std::string xs;
    std::vector<std::pair<std::string, std::string>> materials;  // name -> sha256
};

/// run.json (03 §6, schema v1) — provenance for one run. Field order (and the
/// to_json output) follows the 03 §6 example.
struct RunProvenance {
    int schema_version = 1;
    std::string run_id;
    std::string unit_id;         // compute_unit_id(...) — 03 §6 dedup key / artifact dir
    std::string scenario_file;
    std::string scenario_sha256;
    RunDataHashes data_hashes;
    std::vector<std::string> scenario_overrides;  // 03 §4: applied overrides, recorded verbatim
    std::uint64_t seed = 0;
    std::string code_version;    // tracks the app (03 §6)
    std::string spec_version;    // tracks this spec (03 §6)
    std::string git;             // short commit hash
    bool dirty = false;          // working tree dirty at run time
    std::string backend;         // "ref" | "gpu" | ...
    std::string device;          // GPU device name, or "" for a CPU run
    std::string started;         // UTC ISO-8601 (06 §5); the emitter sets it
    std::string finished;        // UTC ISO-8601
};

/// Serialize to the 03 §6 run.json shape (field order per the 03 §6 example).
/// Idempotent after one parse: to_json(parse_run_json(s)) re-serializes stably.
std::string to_json(const RunProvenance& r, int indent = 2);

/// Parse a 03 §6 run.json document. Throws std::runtime_error on a missing
/// REQUIRED provenance field — the identity/reproducibility fields (run_id,
/// unit_id, scenario_file, scenario_sha256, data_hashes{xs, materials}, seed,
/// code_version, spec_version, git, dirty, backend, device, started, finished) —
/// because a provenance record missing one of them cannot reproduce or dedup a
/// run. `schema_version` defaults to 1 and `scenario_overrides` to empty when
/// absent (03 §12 migration; overrides are empty for a canonical run).
RunProvenance parse_run_json(const std::string& text);

/// unit_id = sha256(scenario_canonical_hash ‖ overrides ‖ seed) — 03 §6. The
/// idempotent dedup key and the artifact directory name (rerunning a completed
/// unit is a no-op, D6). `scenario_canonical_hash` is Scenario::canonical_hash()
/// (04 §6). `overrides` are the applied override strings, recorded verbatim
/// (03 §4); they are SORTED here so the key is order-independent — overrides are a
/// set, the same run in any override order is the same unit — matching
/// canonical_hash's own sorted override block. Stable byte layout (M5-T2 relies on
/// it): "canonical_hash=" ‖ hash ‖ "\n", then per sorted override
/// "override=" ‖ ov ‖ "\n", then "seed=" ‖ decimal(seed) ‖ "\n".
std::string compute_unit_id(const std::string& scenario_canonical_hash,
                            const std::vector<std::string>& overrides, std::uint64_t seed);

}  // namespace ns::api

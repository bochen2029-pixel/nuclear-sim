// nukebench gates.toml loader (03 §10, M1-T5-a).
//
// gates.toml is GENERATED (tools/gen_gates) and never hand-edited. This loads + validates it
// for the nukebench gate runner (M1-T5-b). Two integrity guards are enforced at load:
//   (1) every criterion's `value` must equal its `constant_id`'s value (ns::consts) — gates
//       thresholds are the appendix constants, never a drifted copy (03 §10);
//   (2) `spec_sha256` must equal sha256 of the normative spec/08-validation.md — so a change
//       to the gate procedures without regenerating gates.toml is a hard failure (03 §10).

#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns::nukebench {

/// Thrown on any gates.toml schema / drift / spec_sha256 violation.
struct GatesError : std::runtime_error {
    explicit GatesError(const std::string& m) : std::runtime_error(m) {}
};

struct Criterion {
    std::string name;         // e.g. "k_deviation_pcm"
    std::string op;           // "abs_le" | "le"
    double value = 0.0;       // threshold, == ns::consts::get(constant_id)
    std::string constant_id;  // must resolve in ns::consts
};

struct GateEigen {
    std::int64_t batch = 0;
    int inactive = 0;
    int active = 0;
};

struct Gate {
    std::string id;     // "G0a"
    std::string title;
    std::string scenario;               // repo-relative scenario path
    std::vector<std::int64_t> seeds;    // normative seed set
    GateEigen eigen;
    std::vector<Criterion> criteria;
};

struct GatesConfig {
    int schema_version = 0;
    std::string generated_from;
    std::string spec_sha256;
    std::vector<Gate> gates;
};

/// Load + validate `gates_toml`. `spec_08_validation` is the path to spec/08-validation.md,
/// hashed (CRLF-normalized) to enforce the spec_sha256 guard. Throws GatesError on any
/// violation (bad schema, a criterion value ≠ its constant, or a spec_sha256 mismatch).
GatesConfig load_gates(const std::filesystem::path& gates_toml,
                       const std::filesystem::path& spec_08_validation);

/// The gate with id `id`, or throws GatesError if absent.
const Gate& find_gate(const GatesConfig& cfg, const std::string& id);

}  // namespace ns::nukebench

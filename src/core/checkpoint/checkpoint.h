// checkpoint.bin v2 — the resume container (03 §8, D9) — M5-T1-a.
//
// The versioned, CRC-protected, identity-guarded binary container a run serializes
// its state into and resumes from. THIS task builds the CONTAINER + the R-7 load
// rules (a mismatched checkpoint MUST NOT load silently) + the SimClock section.
// The live-state sections (RNG / neutron bank / geometry / hydro / eigen / tally),
// the run-loop resume wiring, and the T-resume bit-identical gate are M5-T1-b.
//
// `CheckpointBlob` is the type 05 §4 (`HydroModel::serialize`/`restore`) and
// couple.h deferred ("consumers don't exist yet") — providing it here is what
// unblocks M3-T4's HydroModel hierarchy.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns::checkpoint {

/// Thrown by read_checkpoint on any 03 §8 load-rule violation (R-7), carrying a
/// precise diagnostic (which check failed). Old / mismatched checkpoints never
/// load silently.
struct CheckpointError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Backend : std::uint8_t { Ref = 0, Gpu = 1 };
enum class BankPrecision : std::uint8_t { F32 = 0, F64 = 1 };

/// The 03 §8 header identity a resume MUST match, or reject. The two sha256 fields
/// are 64-hex (as Scenario::canonical_hash emits); they are stored 32-byte binary
/// and read back normalized to lowercase 64-hex.
struct CheckpointIdentity {
    std::uint32_t schema_version = 2;
    Backend backend = Backend::Ref;
    BankPrecision bank_precision = BankPrecision::F64;
    std::string scenario_sha256;   // 64-hex; stored 32-byte binary
    std::string data_sha256;       // 64-hex; stored 32-byte binary
    std::string code_version;      // ≤ 16 bytes stored
    std::string git_hash;          // ≤ 24 bytes stored (short form, e.g. "a1b2c3d")
};

/// SimClock section (03 §8 §1).
struct SimClockState {
    std::int32_t phase = 0;               // D6 staged-clock phase id
    double t_s = 0.0;
    std::int64_t generation = 0;
    double exponent_offset = 0.0;         // E3a log-domain renorm offset
    double f_peak = 0.0;                  // log10 F_peak
    bool supercritical_reached = false;
};

/// One checkpoint section: a numeric id + an opaque, CRC-protected payload. Section
/// ids (03 §8): 1 SimClock · 2 RNG · 3 neutron bank · 4 geometry · 5 hydro ·
/// 6 eigen · 7 tally · 8 sweep cursor. Typed accessors encode/decode the payload.
struct CheckpointSection {
    std::uint16_t id = 0;
    std::vector<std::uint8_t> data;
};

/// The in-memory checkpoint (03 §8): the header identity + ordered sections.
class CheckpointBlob {
public:
    CheckpointIdentity identity;
    std::vector<CheckpointSection> sections;

    /// Add or replace a raw section (last write wins for a given id).
    void put_section(std::uint16_t id, std::vector<std::uint8_t> data);
    /// The section with `id`, or nullptr if absent.
    const CheckpointSection* section(std::uint16_t id) const noexcept;

    /// Section 1 (03 §8 §1) typed accessors.
    void put_sim_clock(const SimClockState& s);
    /// Throws CheckpointError if section 1 is absent or malformed.
    SimClockState sim_clock() const;
};

/// Serialize to the checkpoint.bin byte stream: a fixed 128-byte header + the
/// section table (id/offset/length/CRC32 per section) + the section payloads.
/// Deterministic and little-endian (the header's endianness marker detects a
/// foreign-byte-order read).
std::vector<std::uint8_t> write_checkpoint(const CheckpointBlob& blob);

/// Parse + VALIDATE (03 §8 load rules, R-7). Throws CheckpointError with a precise
/// diagnostic on a bad magic, an unsupported schema_version, an endianness
/// mismatch, any section-CRC failure, or — when `expect` is non-null — a
/// backend / bank_precision / scenario_sha256 / data_sha256 / git_hash mismatch.
/// A mismatched checkpoint MUST NOT load silently.
CheckpointBlob read_checkpoint(const std::vector<std::uint8_t>& bytes,
                               const CheckpointIdentity* expect = nullptr);

}  // namespace ns::checkpoint

// M5-T1-a: the checkpoint.bin v2 container (03 §8) + the R-7 load rules.
//
// DoD: an in-tree CRC32 (checked against the IEEE reference vector); a versioned,
// identity-guarded binary container that round-trips its header + SimClock section
// + arbitrary raw sections; and the 03 §8 load rules — a checkpoint with a bad
// magic / version / endianness, a corrupted section, or a mismatched identity MUST
// NOT load silently (R-7).

#include <catch2/catch_test_macros.hpp>

#include "core/checkpoint/checkpoint.h"
#include "core/hash/crc32.h"

#include <cstdint>
#include <string>
#include <vector>

using ns::checkpoint::Backend;
using ns::checkpoint::BankPrecision;
using ns::checkpoint::CheckpointBlob;
using ns::checkpoint::CheckpointError;
using ns::checkpoint::CheckpointIdentity;
using ns::checkpoint::read_checkpoint;
using ns::checkpoint::SimClockState;
using ns::checkpoint::write_checkpoint;

namespace {

// A fully-populated checkpoint (valid 64-hex identity, a SimClock, an extra raw
// section) for round-trip and rejection tests.
CheckpointBlob sample_blob() {
    CheckpointBlob blob;
    blob.identity.schema_version = 2;
    blob.identity.backend = Backend::Ref;
    blob.identity.bank_precision = BankPrecision::F64;
    blob.identity.scenario_sha256 = std::string(64, 'a');
    blob.identity.data_sha256 = std::string(64, 'b');
    blob.identity.code_version = "0.1.0";
    blob.identity.git_hash = "a1b2c3d";

    SimClockState clk;
    clk.phase = 2;
    clk.t_s = 3.14159e-6;
    clk.generation = 494;
    clk.exponent_offset = 30.0;
    clk.f_peak = 23.7;
    clk.supercritical_reached = true;
    blob.put_sim_clock(clk);

    blob.put_section(7, std::vector<std::uint8_t>{1, 2, 3, 4, 5});  // an opaque "tally" section
    return blob;
}

}  // namespace

TEST_CASE("crc32 reproduces the IEEE 802.3 reference vector", "[checkpoint]") {
    REQUIRE(ns::hash::crc32(std::string_view("123456789")) == 0xCBF43926u);
    REQUIRE(ns::hash::crc32(std::string_view("")) == 0x00000000u);  // CRC of nothing
    // Sensitivity: a one-bit change moves the digest.
    REQUIRE(ns::hash::crc32(std::string_view("123456789")) != ns::hash::crc32(std::string_view("123456780")));
}

TEST_CASE("the checkpoint container round-trips header identity + sections", "[checkpoint]") {
    const CheckpointBlob blob = sample_blob();
    const std::vector<std::uint8_t> bytes = write_checkpoint(blob);

    const CheckpointBlob rd = read_checkpoint(bytes);  // no identity guard
    REQUIRE(rd.identity.schema_version == 2);
    REQUIRE(rd.identity.backend == Backend::Ref);
    REQUIRE(rd.identity.bank_precision == BankPrecision::F64);
    REQUIRE(rd.identity.scenario_sha256 == std::string(64, 'a'));
    REQUIRE(rd.identity.data_sha256 == std::string(64, 'b'));
    REQUIRE(rd.identity.code_version == "0.1.0");
    REQUIRE(rd.identity.git_hash == "a1b2c3d");

    // The SimClock (section 1, 03 §8 §1) round-trips exactly (doubles bit-for-bit).
    const SimClockState c = rd.sim_clock();
    REQUIRE(c.phase == 2);
    REQUIRE(c.t_s == 3.14159e-6);
    REQUIRE(c.generation == 494);
    REQUIRE(c.exponent_offset == 30.0);
    REQUIRE(c.f_peak == 23.7);
    REQUIRE(c.supercritical_reached);

    // The opaque raw section round-trips.
    const auto* s7 = rd.section(7);
    REQUIRE(s7 != nullptr);
    REQUIRE(s7->data == std::vector<std::uint8_t>{1, 2, 3, 4, 5});
    REQUIRE(rd.section(3) == nullptr);  // an absent section

    // Serialization is deterministic (re-writing the parsed blob is byte-identical).
    REQUIRE(write_checkpoint(rd) == bytes);

    // A MATCHING identity guard loads cleanly.
    CheckpointIdentity expect = blob.identity;
    REQUIRE_NOTHROW(read_checkpoint(bytes, &expect));
}

TEST_CASE("read_checkpoint enforces the 03 section 8 load rules (R-7)", "[checkpoint]") {
    const std::vector<std::uint8_t> good = write_checkpoint(sample_blob());

    SECTION("a bad magic is rejected") {
        std::vector<std::uint8_t> bad = good;
        bad[0] = 'X';
        REQUIRE_THROWS_AS(read_checkpoint(bad), CheckpointError);
    }
    SECTION("an unsupported schema_version is rejected") {
        std::vector<std::uint8_t> bad = good;
        bad[8] = 99;  // schema_version LSB
        REQUIRE_THROWS_AS(read_checkpoint(bad), CheckpointError);
    }
    SECTION("a foreign endianness marker is rejected") {
        std::vector<std::uint8_t> bad = good;
        bad[12] = static_cast<std::uint8_t>(bad[12] ^ 0xFF);
        REQUIRE_THROWS_AS(read_checkpoint(bad), CheckpointError);
    }
    SECTION("a truncated header is rejected") {
        const std::vector<std::uint8_t> bad(good.begin(), good.begin() + 64);
        REQUIRE_THROWS_AS(read_checkpoint(bad), CheckpointError);
    }
    SECTION("a corrupted section payload fails its CRC") {
        std::vector<std::uint8_t> bad = good;
        bad.back() = static_cast<std::uint8_t>(bad.back() ^ 0xFF);
        REQUIRE_THROWS_AS(read_checkpoint(bad), CheckpointError);
    }
    SECTION("a scenario_sha256 mismatch is rejected") {
        CheckpointIdentity expect = sample_blob().identity;
        expect.scenario_sha256 = std::string(64, 'c');
        REQUIRE_THROWS_AS(read_checkpoint(good, &expect), CheckpointError);
    }
    SECTION("a data_sha256 mismatch is rejected") {
        CheckpointIdentity expect = sample_blob().identity;
        expect.data_sha256 = std::string(64, 'd');
        REQUIRE_THROWS_AS(read_checkpoint(good, &expect), CheckpointError);
    }
    SECTION("a git_hash mismatch is rejected") {
        CheckpointIdentity expect = sample_blob().identity;
        expect.git_hash = "deadbee";
        REQUIRE_THROWS_AS(read_checkpoint(good, &expect), CheckpointError);
    }
    SECTION("a backend mismatch is rejected") {
        CheckpointIdentity expect = sample_blob().identity;
        expect.backend = Backend::Gpu;
        REQUIRE_THROWS_AS(read_checkpoint(good, &expect), CheckpointError);
    }
}

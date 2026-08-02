// M0-T4: counter-based RNG (04 §2).
//
// Three layers of evidence, in order of authority:
//   (a) the three PUBLISHED Random123 vectors — external ground truth, and the
//       only thing here that cannot be wrong in the same direction as our code;
//   (b) the project-local vector in rng_kat.inl, emitted by an independent
//       Python Philox that had to pass (a) first — evidence about the wrapper;
//   (c) structural properties (state, fork identity, resume) that no fixed
//       vector can express.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/constants/constants.h"
#include "core/rng/rng.h"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "rng_kat.inl"

using ns::rng::Counter;
using ns::rng::Key;
using ns::rng::Stream;

TEST_CASE("published Random123 Philox4x32-10 vectors reproduce exactly", "[rng]") {
    // 04 §2(a). This is the portability check: if these pass, the bijection is
    // the real Philox and not a plausible near-miss. Checked at compile time
    // too — philox4x32_10 is constexpr, so a regression cannot even link.
    static_assert(ns::rng::philox4x32_10(Counter{0, 0, 0, 0}, Key{0, 0})
                  == Counter{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});

    REQUIRE(ns::rng::philox4x32_10(Counter{0, 0, 0, 0}, Key{0, 0})
            == Counter{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});

    REQUIRE(ns::rng::philox4x32_10(
                Counter{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
                Key{0xFFFFFFFFu, 0xFFFFFFFFu})
            == Counter{0x408F276Du, 0x41C83B0Eu, 0xA20BC7C6u, 0x6D5451FDu});

    // The pi-digit pattern.
    REQUIRE(ns::rng::philox4x32_10(
                Counter{0x243F6A88u, 0x85A308D3u, 0x13198A2Eu, 0x03707344u},
                Key{0xA4093822u, 0x299F31D0u})
            == Counter{0xD16CFE09u, 0x94FDCCEBu, 0x5001E420u, 0x24126EA1u});
}

TEST_CASE("project-local uniform_f vector is frozen", "[rng]") {
    // 04 §2(b). Guards the wrapper: counter packing, sub-word order and the
    // 24-bit float conversion. A change here requires an ADR.
    Stream stream(0, 0);
    for (std::size_t i = 0; i < std::size(kRngKatUniformF); ++i) {
        INFO("draw " << i);
        REQUIRE(stream.uniform_f() == kRngKatUniformF[i]);
    }
}

TEST_CASE("fork known-answer value is frozen", "[rng]") {
    // 04 §2(c). M4-T1 must reproduce this exact value on the device.
    REQUIRE(ns::rng::fork(42, 1000, 3) == kRngKatFork42_1000_3);
    static_assert(ns::rng::fork(42, 1000, 3) != 0);
}

TEST_CASE("fork derives from parent identity, not buffer position", "[rng]") {
    // BLK-11/E2. Every component of the parent's identity must move the child
    // stream; if any were ignored, siblings or unrelated particles would share
    // a stream and correlate silently.
    const std::uint64_t base = ns::rng::fork(7, 100, 0);
    REQUIRE(ns::rng::fork(8, 100, 0) != base);   // parent stream matters
    REQUIRE(ns::rng::fork(7, 101, 0) != base);   // parent counter matters
    REQUIRE(ns::rng::fork(7, 100, 1) != base);   // progeny ordinal matters

    // A parent spawning many progeny must not collide across them.
    std::set<std::uint64_t> children;
    for (std::uint32_t ordinal = 0; ordinal < 4096; ++ordinal) {
        children.insert(ns::rng::fork(12345, 6789, ordinal));
    }
    REQUIRE(children.size() == 4096);

    // Nor may two different parents at the same ordinal collide.
    std::set<std::uint64_t> across;
    for (std::uint64_t parent = 0; parent < 4096; ++parent) {
        across.insert(ns::rng::fork(parent, 0, 0));
    }
    REQUIRE(across.size() == 4096);
}

TEST_CASE("(ctr, sub) round-trips and is the entire resumable state", "[rng]") {
    // 03 §8 §2 serialises exactly (ctr, sub); D9 requires that resuming from
    // them reproduces the stream bit-identically. Cut a stream at every sub
    // position, including across a block boundary, and resume.
    for (int cut = 1; cut <= 9; ++cut) {
        INFO("cut after " << cut << " draws");

        Stream original(0xDEADBEEFCAFEBABEull, 3);
        for (int i = 0; i < cut; ++i) {
            (void)original.uniform_f();
        }
        const auto [ctr, sub] = original.state();

        // sub must stay inside a block, and ctr must have advanced once we
        // crossed one — a wrapper that forgot to carry would show up here.
        REQUIRE(sub < 4);
        REQUIRE(ctr == static_cast<std::uint64_t>(cut / 4));

        Stream resumed(0xDEADBEEFCAFEBABEull, 3, ctr, sub);
        for (int i = 0; i < 8; ++i) {
            REQUIRE(resumed.uniform_f() == original.uniform_f());
        }
    }
}

TEST_CASE("normal_f keeps no state beyond (ctr, sub)", "[rng]") {
    // The Box-Muller partner is deliberately discarded rather than cached: a
    // cached deviate would be invisible to a checkpoint and would make D9's
    // kill-resume test fail only sometimes. Resuming mid-sequence must
    // reproduce exactly, which it cannot if a hidden value is carried.
    Stream original(99, 2);
    (void)original.normal_f();
    const auto [ctr, sub] = original.state();

    Stream resumed(99, 2, ctr, sub);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(resumed.normal_f() == original.normal_f());
    }
}

TEST_CASE("uniform_d consumes two words and stays in range", "[rng]") {
    Stream stream(1, ns::consts::rng_stream_registry::flight);
    const auto [ctr0, sub0] = stream.state();
    REQUIRE(ctr0 == 0);
    REQUIRE(sub0 == 0);

    const double value = stream.uniform_d();
    REQUIRE(value >= 0.0);
    REQUIRE(value < 1.0);

    const auto [ctr1, sub1] = stream.state();
    REQUIRE(ctr1 == 0);
    REQUIRE(sub1 == 2);  // 53-bit draw costs two 32-bit words
}

TEST_CASE("distinct streams from one seed do not coincide", "[rng]") {
    // Stream ids are C-907; using the generated names rather than literals is
    // what keeps this test honest if the registry ever changes.
    const std::uint64_t ids[] = {
        ns::consts::rng_stream_registry::source, ns::consts::rng_stream_registry::flight,
        ns::consts::rng_stream_registry::collision, ns::consts::rng_stream_registry::fission,
        ns::consts::rng_stream_registry::scatter, ns::consts::rng_stream_registry::hydro,
        ns::consts::rng_stream_registry::render,
    };

    std::set<float> firsts;
    for (const std::uint64_t id : ids) {
        firsts.insert(Stream(20260802, id).uniform_f());
    }
    REQUIRE(firsts.size() == std::size(ids));
}

TEST_CASE("uniform_f is in range and roughly uniform", "[rng]") {
    // Not a statistical test — a mean check this loose only catches a broken
    // conversion (wrong shift, signed cast). The real distribution evidence is
    // the frozen vector plus the published bijection.
    Stream stream(20260802, ns::consts::rng_stream_registry::collision);
    constexpr int kDraws = 200000;
    double sum = 0.0;
    std::array<int, 4> quartiles{};

    for (int i = 0; i < kDraws; ++i) {
        const float value = stream.uniform_f();
        REQUIRE(value >= 0.0f);
        REQUIRE(value < 1.0f);
        sum += value;
        ++quartiles[static_cast<std::size_t>(value * 4.0f)];
    }

    REQUIRE_THAT(sum / kDraws, Catch::Matchers::WithinAbs(0.5, 0.01));
    for (const int count : quartiles) {
        REQUIRE(count > kDraws / 4 - kDraws / 100);
        REQUIRE(count < kDraws / 4 + kDraws / 100);
    }
}

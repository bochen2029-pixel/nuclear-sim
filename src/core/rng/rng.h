// Counter-based random streams (04 §2, normative layout).
//
//   key     = { lo32(seed), hi32(seed) }
//   counter = { lo32(ctr), hi32(ctr), lo32(stream), hi32(stream) }   ctr = block index
//
// Stream ids come from C-907 in the generated constants header — do not
// re-declare them here. Two physical processes sharing a stream id would be
// silently correlated, which is the kind of defect that shows up as a
// plausible-but-wrong physics result rather than as a crash.

#pragma once

#include <cmath>
#include <cstdint>
#include <utility>

#include "core/rng/philox.h"

namespace ns::rng {

/// A resumable random stream.
///
/// The ENTIRE resumable state is (ctr, sub) — 04 §2 and 03 §8 §2 both say so,
/// and D9's kill-resume-bit-identical gate depends on it. Nothing in this class
/// may cache a value that outlives a call; see normal_f().
class Stream {
public:
    constexpr Stream(std::uint64_t seed, std::uint64_t stream,
                     std::uint64_t ctr = 0, std::uint8_t sub = 0) noexcept
        : seed_(seed), stream_(stream), ctr_(ctr), sub_(sub) {
        refill();
    }

    /// Uniform in [0, 1). 24-bit mantissa, the standard float conversion.
    constexpr float uniform_f() noexcept {
        return static_cast<float>(next_u32() >> 8) * 0x1.0p-24f;
    }

    /// Uniform in [0, 1) with a full 53-bit mantissa. src/ref/ uses this (01 §9).
    constexpr double uniform_d() noexcept {
        // Two words, high first, so the value is a deterministic function of
        // draw order rather than of word packing.
        const std::uint64_t hi = next_u32();
        const std::uint64_t lo = next_u32();
        return static_cast<double>(((hi << 32) | lo) >> 11) * 0x1.0p-53;
    }

    /// Standard normal, Box-Muller.
    ///
    /// Deliberately generates two uniforms and returns ONE deviate, discarding
    /// the sine partner rather than caching it. A cached deviate would be state
    /// beyond (ctr, sub): a checkpoint taken between two calls would resume
    /// having lost it, and the resume would differ from the un-killed run only
    /// sometimes. Intermittent D9 failures are far more expensive than one cos.
    float normal_f() noexcept {
        // uniform_f() is [0, 1) and log(0) is -inf, so pull u1 into (0, 1].
        const float u1 = 1.0f - uniform_f();
        const float u2 = uniform_f();
        constexpr float kTwoPi = 6.283185307179586f;
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(kTwoPi * u2);
    }

    /// (ctr, sub) — BOTH must be serialized (03 §8 §2).
    constexpr std::pair<std::uint64_t, std::uint8_t> state() const noexcept {
        return {ctr_, sub_};
    }

    constexpr std::uint64_t stream_id() const noexcept { return stream_; }

private:
    constexpr void refill() noexcept {
        block_ = philox4x32_10(
            Counter{static_cast<std::uint32_t>(ctr_),
                    static_cast<std::uint32_t>(ctr_ >> 32),
                    static_cast<std::uint32_t>(stream_),
                    static_cast<std::uint32_t>(stream_ >> 32)},
            Key{static_cast<std::uint32_t>(seed_),
                static_cast<std::uint32_t>(seed_ >> 32)});
    }

    constexpr std::uint32_t next_u32() noexcept {
        const std::uint32_t value = block_[sub_];
        if (++sub_ == 4) {
            sub_ = 0;
            ++ctr_;
            refill();
        }
        return value;
    }

    std::uint64_t seed_;
    std::uint64_t stream_;
    std::uint64_t ctr_;
    std::uint8_t sub_;
    Counter block_{};
};

namespace detail {

constexpr std::uint64_t rotl64(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
}

/// SplitMix64, full standard form: the golden-ratio increment followed by the
/// two xor-multiply rounds and the final xorshift.
///
/// 04 §2 writes `splitmix64(...)` without pinning which variant it means, and
/// some references omit the increment. This one is normative for the project
/// because fork(42, 1000, 3) is frozen as a known-answer test; changing the
/// variant changes that value and therefore requires an ADR.
constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

}  // namespace detail

/// Derive a child stream id from PARENT IDENTITY — never from buffer position.
///
/// This is the whole of BLK-11/E2: if a child stream were derived from where
/// its parent happened to sit in a buffer, results would depend on thread
/// count, block size and launch order, and the same-backend bit-identity
/// requirement (D1 determinism addendum, 05 §6) would be unachievable on GPU.
/// Derived from (parent_stream, parent_ctr, progeny_ordinal), all three of
/// which are properties of the parent particle itself.
constexpr std::uint64_t fork(std::uint64_t parent_stream, std::uint64_t parent_ctr,
                             std::uint32_t progeny_ordinal) noexcept {
    return detail::splitmix64(
        parent_stream
        ^ detail::rotl64(parent_ctr, 17)
        ^ (0x9E3779B97F4A7C15ull * (static_cast<std::uint64_t>(progeny_ordinal) + 1)));
}

}  // namespace ns::rng

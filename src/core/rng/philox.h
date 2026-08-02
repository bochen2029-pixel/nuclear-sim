// Philox4x32-10 counter-based bijection (04 §2).
//
// This file is ONLY the bijection: 4x32 counter + 2x32 key -> 4x32 output. The
// project's stream layout, buffering and float conversion live in rng.h.
// Keeping them apart is what lets the three published Random123 known-answer
// vectors test this file directly, against external ground truth rather than
// against our own conventions.
//
// Counter-based, not cursor-based, is a hard requirement (D9/ADR-008): resume
// must reproduce a stream exactly from a stored counter, and every value is a
// pure function of (counter, key) with no hidden carry between draws.

#pragma once

#include <array>
#include <cstdint>

#include "core/hd.h"

namespace ns::rng {

using Counter = std::array<std::uint32_t, 4>;
using Key = std::array<std::uint32_t, 2>;

namespace detail {

// Random123's multipliers and Weyl-sequence key bumps. These are part of the
// algorithm, not tunable — changing one changes every random number the project
// will ever produce, so they are not in constants.data.toml (which is for
// physical and simulation constants, 01 §8).
inline constexpr std::uint32_t kPhiloxM0 = 0xD2511F53u;
inline constexpr std::uint32_t kPhiloxM1 = 0xCD9E8D57u;
inline constexpr std::uint32_t kPhiloxW0 = 0x9E3779B9u;  // golden ratio
inline constexpr std::uint32_t kPhiloxW1 = 0xBB67AE85u;  // sqrt(3) - 1

inline constexpr int kPhiloxRounds = 10;

// 32x32 -> 64 widening multiply, split. Done in uint64_t so it is exact and
// portable; the GPU port (M4-T1) substitutes __umulhi without changing results.
NUKESIM_HD constexpr void mulhilo32(std::uint32_t a, std::uint32_t b,
                         std::uint32_t& hi, std::uint32_t& lo) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    hi = static_cast<std::uint32_t>(product >> 32);
    lo = static_cast<std::uint32_t>(product);
}

NUKESIM_HD constexpr Counter round(const Counter& ctr, const Key& key) noexcept {
    std::uint32_t hi0 = 0, lo0 = 0, hi1 = 0, lo1 = 0;
    mulhilo32(kPhiloxM0, ctr[0], hi0, lo0);
    mulhilo32(kPhiloxM1, ctr[2], hi1, lo1);
    return Counter{
        static_cast<std::uint32_t>(hi1 ^ ctr[1] ^ key[0]),
        lo1,
        static_cast<std::uint32_t>(hi0 ^ ctr[3] ^ key[1]),
        lo0,
    };
}

}  // namespace detail

/// One Philox4x32-10 block: four uint32 from a counter and a key.
///
/// constexpr so known-answer vectors can be checked at compile time and so the
/// same source compiles for device code unchanged.
NUKESIM_HD constexpr Counter philox4x32_10(Counter ctr, Key key) noexcept {
    for (int r = 0; r < detail::kPhiloxRounds; ++r) {
        if (r > 0) {
            // Key bump happens BETWEEN rounds, so round 0 uses the unmodified
            // key. Bumping before every round instead is a classic off-by-one
            // that still produces plausible-looking noise and fails the
            // published vectors — which is why those vectors are a DoD item.
            key[0] += detail::kPhiloxW0;
            key[1] += detail::kPhiloxW1;
        }
        ctr = detail::round(ctr, key);
    }
    return ctr;
}

}  // namespace ns::rng

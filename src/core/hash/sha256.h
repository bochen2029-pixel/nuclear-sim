// SHA-256 (FIPS 180-4), in-tree.
//
// canonical_hash() (04 §6) needs it, and no vcpkg baseline dependency provides
// one. 02 §4 makes a new dependency an ADR, so the choice was: log an ADR to
// pull in a crypto library, or implement ~120 lines of a fully specified,
// externally verifiable algorithm. In-tree wins on the same grounds the Philox
// KATs rest on — correctness here is checkable against published NIST vectors,
// so this is not a case where "roll your own crypto" applies. It is used for
// identity and change detection, never for security.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ns::hash {

/// Lowercase hex SHA-256 of `data`.
std::string sha256_hex(std::string_view data);

/// Streaming form, for hashing several inputs without concatenating them.
class Sha256 {
public:
    Sha256();
    void update(std::string_view data);
    /// Finalizes and returns the lowercase hex digest. Not resumable after.
    std::string hex_digest();

private:
    void compress(const std::uint8_t block[64]);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t length_bits_ = 0;
    std::size_t buffered_ = 0;
};

}  // namespace ns::hash

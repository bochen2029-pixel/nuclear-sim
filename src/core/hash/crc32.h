// CRC-32 (IEEE 802.3 / zlib, reflected polynomial 0xEDB88320), in-tree.
//
// 03 §8 protects each checkpoint section with a CRC32. No vcpkg baseline port
// provides one and 02 §4 makes a new dependency an ADR, so — exactly as with the
// in-tree SHA-256 — a tiny, externally-checkable implementation is preferred over
// pulling in zlib. Verified against the canonical vector crc32("123456789") =
// 0xCBF43926. Used for integrity/change detection only, never security.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ns::hash {

std::uint32_t crc32(const void* data, std::size_t len) noexcept;

inline std::uint32_t crc32(std::string_view s) noexcept {
    return crc32(s.data(), s.size());
}

}  // namespace ns::hash

// CRC-32 (IEEE 802.3), in-tree — M5-T1-a.

#include "core/hash/crc32.h"

namespace ns::hash {

namespace {

// Table for the reflected polynomial 0xEDB88320, built at compile time.
struct Crc32Table {
    std::uint32_t entry[256] = {};
    constexpr Crc32Table() {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            entry[i] = c;
        }
    }
};

constexpr Crc32Table kTable{};

}  // namespace

std::uint32_t crc32(const void* data, std::size_t len) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = kTable.entry[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace ns::hash

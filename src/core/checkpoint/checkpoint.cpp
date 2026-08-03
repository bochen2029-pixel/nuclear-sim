// checkpoint.bin v2 (de)serialize + the R-7 load rules (03 §8) — M5-T1-a.

#include "core/checkpoint/checkpoint.h"

#include "core/hash/crc32.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace ns::checkpoint {

namespace {

constexpr char kMagic[8] = {'N', 'S', 'C', 'K', 'P', 'T', '0', '2'};
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::size_t kHeaderBytes = 128;
constexpr std::size_t kSectionEntryBytes = 2 + 8 + 8 + 4;  // id + offset + length + crc32
constexpr std::size_t kCodeVersionBytes = 16;
constexpr std::size_t kGitHashBytes = 24;

// --- little-endian appenders ---
void put_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put_u64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put_i32(std::vector<std::uint8_t>& v, std::int32_t x) {
    put_u32(v, static_cast<std::uint32_t>(x));
}
void put_i64(std::vector<std::uint8_t>& v, std::int64_t x) {
    put_u64(v, static_cast<std::uint64_t>(x));
}
void put_f64(std::vector<std::uint8_t>& v, double x) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &x, 8);
    put_u64(v, bits);
}

// --- little-endian readers (bounds-checked by the caller) ---
std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return x;
}
std::int32_t read_i32(const std::uint8_t* p) { return static_cast<std::int32_t>(read_u32(p)); }
std::int64_t read_i64(const std::uint8_t* p) { return static_cast<std::int64_t>(read_u64(p)); }
double read_f64(const std::uint8_t* p) {
    const std::uint64_t bits = read_u64(p);
    double x = 0.0;
    std::memcpy(&x, &bits, 8);
    return x;
}

// --- hex <-> 32-byte binary (sha256) ---
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
void hex_to_bin32(const std::string& hex, std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    for (std::size_t i = 0; i < 32; ++i) {
        if (2 * i + 1 >= hex.size()) break;
        out[i] = static_cast<std::uint8_t>((hexval(hex[2 * i]) << 4) | hexval(hex[2 * i + 1]));
    }
}
std::string bin32_to_hex(const std::uint8_t* p) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[2 * i] = kDigits[(p[i] >> 4) & 0xF];
        out[2 * i + 1] = kDigits[p[i] & 0xF];
    }
    return out;
}
// Normalize a hex string through the stored 32-byte form (lowercase, exactly 64).
std::string norm_hash(const std::string& hex) {
    std::uint8_t bin[32];
    hex_to_bin32(hex, bin);
    return bin32_to_hex(bin);
}

// A fixed-width, null-padded string field (truncated if too long).
void put_fixed_str(std::uint8_t* dst, std::size_t width, const std::string& s) {
    std::memset(dst, 0, width);
    std::memcpy(dst, s.data(), std::min(width, s.size()));
}
std::string read_fixed_str(const std::uint8_t* src, std::size_t width) {
    std::size_t n = 0;
    while (n < width && src[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(src), n);
}

}  // namespace

void CheckpointBlob::put_section(std::uint16_t id, std::vector<std::uint8_t> data) {
    for (auto& s : sections) {
        if (s.id == id) {
            s.data = std::move(data);
            return;
        }
    }
    sections.push_back(CheckpointSection{id, std::move(data)});
}

const CheckpointSection* CheckpointBlob::section(std::uint16_t id) const noexcept {
    for (const auto& s : sections) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

void CheckpointBlob::put_sim_clock(const SimClockState& s) {
    std::vector<std::uint8_t> d;
    put_i32(d, s.phase);
    put_f64(d, s.t_s);
    put_i64(d, s.generation);
    put_f64(d, s.exponent_offset);
    put_f64(d, s.f_peak);
    d.push_back(s.supercritical_reached ? 1 : 0);
    put_section(1, std::move(d));
}

SimClockState CheckpointBlob::sim_clock() const {
    const CheckpointSection* s = section(1);
    if (s == nullptr) {
        throw CheckpointError("checkpoint.bin: SimClock (section 1) absent");
    }
    const auto& d = s->data;
    if (d.size() < 4 + 8 + 8 + 8 + 8 + 1) {
        throw CheckpointError("checkpoint.bin: SimClock (section 1) malformed");
    }
    SimClockState out;
    out.phase = read_i32(&d[0]);
    out.t_s = read_f64(&d[4]);
    out.generation = read_i64(&d[12]);
    out.exponent_offset = read_f64(&d[20]);
    out.f_peak = read_f64(&d[28]);
    out.supercritical_reached = d[36] != 0;
    return out;
}

std::vector<std::uint8_t> write_checkpoint(const CheckpointBlob& blob) {
    const std::uint16_t count = static_cast<std::uint16_t>(blob.sections.size());
    const std::size_t payload_start = kHeaderBytes + static_cast<std::size_t>(count) * kSectionEntryBytes;

    std::vector<std::uint8_t> out(kHeaderBytes, 0);
    std::memcpy(out.data(), kMagic, 8);
    // schema_version @8, endianness @12, backend @16, precision @17, count @18.
    {
        std::vector<std::uint8_t> h;
        put_u32(h, blob.identity.schema_version);
        put_u32(h, kEndianMarker);
        std::memcpy(&out[8], h.data(), 8);
    }
    out[16] = static_cast<std::uint8_t>(blob.identity.backend);
    out[17] = static_cast<std::uint8_t>(blob.identity.bank_precision);
    {
        std::vector<std::uint8_t> c;
        put_u16(c, count);
        std::memcpy(&out[18], c.data(), 2);
    }
    // 20..24 reserved (zero). Hashes @24 / @56 (32 B binary), strings @88 / @104.
    std::uint8_t bin[32];
    hex_to_bin32(blob.identity.scenario_sha256, bin);
    std::memcpy(&out[24], bin, 32);
    hex_to_bin32(blob.identity.data_sha256, bin);
    std::memcpy(&out[56], bin, 32);
    put_fixed_str(&out[88], kCodeVersionBytes, blob.identity.code_version);
    put_fixed_str(&out[104], kGitHashBytes, blob.identity.git_hash);

    // Section table + payloads.
    std::vector<std::uint8_t> table;
    std::vector<std::uint8_t> payloads;
    std::size_t offset = payload_start;
    for (const auto& s : blob.sections) {
        put_u16(table, s.id);
        put_u64(table, static_cast<std::uint64_t>(offset));
        put_u64(table, static_cast<std::uint64_t>(s.data.size()));
        put_u32(table, ns::hash::crc32(s.data.data(), s.data.size()));
        payloads.insert(payloads.end(), s.data.begin(), s.data.end());
        offset += s.data.size();
    }
    out.insert(out.end(), table.begin(), table.end());
    out.insert(out.end(), payloads.begin(), payloads.end());
    return out;
}

CheckpointBlob read_checkpoint(const std::vector<std::uint8_t>& b, const CheckpointIdentity* expect) {
    if (b.size() < kHeaderBytes) {
        throw CheckpointError("checkpoint.bin: truncated header (< 128 bytes)");
    }
    if (std::memcmp(b.data(), kMagic, 8) != 0) {
        throw CheckpointError("checkpoint.bin: bad magic (not NSCKPT02)");
    }
    const std::uint32_t version = read_u32(&b[8]);
    if (version != 2) {
        throw CheckpointError("checkpoint.bin: unsupported schema_version " + std::to_string(version) +
                              " (this build reads 2)");
    }
    if (read_u32(&b[12]) != kEndianMarker) {
        throw CheckpointError("checkpoint.bin: endianness marker mismatch (foreign byte order)");
    }

    CheckpointBlob blob;
    blob.identity.schema_version = version;
    blob.identity.backend = static_cast<Backend>(b[16]);
    blob.identity.bank_precision = static_cast<BankPrecision>(b[17]);
    const std::uint16_t count = read_u16(&b[18]);
    blob.identity.scenario_sha256 = bin32_to_hex(&b[24]);
    blob.identity.data_sha256 = bin32_to_hex(&b[56]);
    blob.identity.code_version = read_fixed_str(&b[88], kCodeVersionBytes);
    blob.identity.git_hash = read_fixed_str(&b[104], kGitHashBytes);

    // Identity guard (R-7): a mismatched checkpoint MUST NOT load.
    if (expect != nullptr) {
        if (blob.identity.backend != expect->backend) {
            throw CheckpointError("checkpoint.bin: backend mismatch");
        }
        if (blob.identity.bank_precision != expect->bank_precision) {
            throw CheckpointError("checkpoint.bin: bank_precision mismatch");
        }
        if (blob.identity.scenario_sha256 != norm_hash(expect->scenario_sha256)) {
            throw CheckpointError("checkpoint.bin: scenario_sha256 mismatch");
        }
        if (blob.identity.data_sha256 != norm_hash(expect->data_sha256)) {
            throw CheckpointError("checkpoint.bin: data_sha256 mismatch");
        }
        if (blob.identity.git_hash != expect->git_hash.substr(0, std::min<std::size_t>(kGitHashBytes,
                                                                                       expect->git_hash.size()))) {
            throw CheckpointError("checkpoint.bin: git_hash mismatch");
        }
    }

    const std::size_t table_bytes = static_cast<std::size_t>(count) * kSectionEntryBytes;
    if (b.size() < kHeaderBytes + table_bytes) {
        throw CheckpointError("checkpoint.bin: truncated section table");
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint8_t* e = &b[kHeaderBytes + static_cast<std::size_t>(i) * kSectionEntryBytes];
        const std::uint16_t id = read_u16(e);
        const std::uint64_t off = read_u64(e + 2);
        const std::uint64_t len = read_u64(e + 10);
        const std::uint32_t crc = read_u32(e + 18);
        if (off > b.size() || len > b.size() - off) {
            throw CheckpointError("checkpoint.bin: section " + std::to_string(id) + " out of bounds");
        }
        std::vector<std::uint8_t> data(b.begin() + static_cast<std::ptrdiff_t>(off),
                                       b.begin() + static_cast<std::ptrdiff_t>(off + len));
        if (ns::hash::crc32(data.data(), data.size()) != crc) {
            throw CheckpointError("checkpoint.bin: section " + std::to_string(id) +
                                  " CRC32 mismatch (corrupt)");
        }
        blob.sections.push_back(CheckpointSection{id, std::move(data)});
    }
    return blob;
}

}  // namespace ns::checkpoint

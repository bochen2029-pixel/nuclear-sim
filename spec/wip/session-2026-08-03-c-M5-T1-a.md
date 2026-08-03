# WIP — session-2026-08-03-c — M5-T1-a (checkpoint.bin v2 container)

Append-only, one line per non-obvious finding BEFORE acting (README §5.4b).

## Task
The `checkpoint.bin` v2 CONTAINER (03 §8): header + section table + in-tree CRC32 + the
R-7 load-rejection rules + `CheckpointBlob`/`SimClockState`. The type `05 §4`/`couple.h`
deferred (unblocks M3-T4). M5-T1-b = live-state sections + resume + T-resume gate.

## Design

- **In-tree CRC32** (IEEE 802.3), like the in-tree SHA-256 (no new dep, 02 §4). Test vector:
  crc32("123456789") == 0xCBF43926.

- **Fixed 128-B header** (exact): [0..8) magic "NSCKPT02"; [8..12) schema_version u32;
  [12..16) endianness marker u32 = 0x01020304; [16] backend u8 (0=ref,1=gpu); [17]
  bank_precision u8 (0=f32,1=f64); [18..20) section_count u16; [20..24) reserved;
  [24..56) scenario_sha256 (32-B BINARY); [56..88) data_sha256 (32-B binary); [88..104)
  code_version (16-B, null-padded/truncated); [104..128) git_hash (24-B, short form).
  Then the section table (section_count × {id u16, offset u64, length u64, crc32 u32}),
  then section payloads at their offsets. Hashes stored binary (32 B) not hex (64) to fit;
  a hex↔binary helper converts to/from Scenario::canonical_hash's hex.

- **R-7 load rules (the whole point):** `read_checkpoint(bytes, expect?)` throws
  `ns::checkpoint::CheckpointError` with a precise diagnostic on: bad magic; unsupported
  schema_version; endianness mismatch; (when `expect` given) scenario_sha256 / data_sha256 /
  git_hash mismatch; any section-CRC failure. "Old checkpoints never required to load; a
  mismatched one MUST NOT load silently."

- **SimClockState** (03 §8 §1): phase (i32), t_s (f64), generation (i64), exponent_offset
  (f64, the E3a log-domain renorm offset), f_peak (f64, log10), supercritical_reached (bool).
  Section id 1. A concrete typed section demonstrating the pattern; RNG/bank/etc. are M5-T1-b.

- **Endianness:** write LE explicitly (byte-by-byte), so the file is portable and the marker
  detects a foreign-endian read. Determinism/portability (canonical_hash folds CRLF for the
  same reason).

## Tests (checkpoint. prefix)
- crc32 reproduces the IEEE vector.
- container round-trips: blob → write → read → identical (header identity + SimClockState +
  arbitrary extra section bytes).
- each R-7 rejection class throws with a diagnostic (bad magic / version / endianness /
  scenario / data / git / corrupted-section CRC).
- Anchored `^checkpoint\.`; update the PROGRESS sum.

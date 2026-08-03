# WIP — session-2026-08-03-c — M5-T1-b (BurstAccumulator checkpoint serialize/restore)

Append-only, one line per non-obvious finding BEFORE acting (README §5.4b).

## Task
`BurstAccumulator` serialize/restore + a bit-identical resume test. The accumulator's
~24-decade log-domain state is the crux of the eventual T-resume; prove it survives a
checkpoint bit-for-bit. M5-T1-c = the full couple-loop resume + T-resume gate.

## Design
- **`BurstAccumulator::State`** (public, in kinetics.h): the full private state —
  `mant, off, log_fcum, log_ecum, log_flast, n, hist` (hist = log10_N_history vector).
- `State state() const` (snapshot) + `static BurstAccumulator from_state(const State&)`
  (or a ctor). Restore reconstructs an accumulator identical to the snapshot.
- **Codec** (in checkpoint.h/cpp): `put_accumulator(const BurstAccumulator::State&)` →
  section 7 (03 §8 §7 tally/kinetics accumulators) + `accumulator()` → State. Serializes
  the 5 doubles + int n + the hist vector (length-prefixed). Reuses the M5-T1-a container.
  BUT: checkpoint.h is core/, kinetics.h is physics/ — core must not depend on physics.
  So the accumulator↔bytes codec lives with `BurstAccumulator` (kinetics), producing a
  `std::vector<uint8_t>` that goes into a generic `put_section(7, …)`. Keeps the layering
  clean (kinetics knows its own state; checkpoint stays a generic container).

- **Bit-identical resume test** (test_kinetics or test_checkpoint): build acc A, step it
  K times; snapshot → serialize → deserialize → build acc B from the state; step BOTH A and
  B M more times with the SAME (k_prompt, ν, e_f, s) sequence; assert every readout
  (log10_N, log10_fissions_last, log10_fissions_cumulative, log10_energy_cumulative,
  generations) is EXACTLY equal (==, not approx) at each step. That is the resume proof.

## Findings
- Accumulator private state = `{mant_, off_, log_fcum_, log_ecum_, log_flast_, n_,
  log10_n_hist_}` (kinetics.h). All needed; the two-part mantissa/offset population form
  must round-trip exactly (a lossy restore would break k^n growth).
- Layering: checkpoint (core) MUST NOT include kinetics (physics). So the State↔bytes codec
  is a `BurstAccumulator` method (serialize_state/deserialize → vector<uint8_t>); the test
  wraps it in a CheckpointBlob section to exercise the M5-T1-a container end-to-end.
- Determinism: doubles serialized via memcpy (bit-exact); the mantissa/offset/cumulants are
  raw doubles → exact round-trip, so resume is bit-identical BY CONSTRUCTION.
- M5-T1-a CI green expected before merge (layers on the container).

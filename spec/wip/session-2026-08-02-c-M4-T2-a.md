# WIP journal — session-2026-08-02-c — M4-T2-a (device geometry + material/xs)

Append one line per non-obvious finding/dead-end/rejected approach, BEFORE acting on it.
Fold durable items into SESSIONS.md at END, then delete this file.

## Claim
- 2026-08-02: SPLIT M4-T2 → -a (device geometry + material/xs, this task) + -b (event
  transport + fission bank + T-diff). Event-based GPU transport plus the device data it
  reads is too big for one clean task; the static-data half is independently parity-testable.
  Continuing as session-2026-08-02-c (same session, next task — the "(cont.)" pattern).

## Plan
- `src/gpu/geometry.cuh`: `DeviceLayerStack` (float radii array, count) + `__host__ __device__`
  ray-sphere `distance_to_boundary` / `locate` / `nudge_and_locate`, mirroring
  core/geometry.h in FLOAT (01 §9: gpu positions/dirs are float). Not a reuse of
  core/geometry (that uses double + std::vector + std::string — not device-friendly); a
  parity test vs the CPU double tracker is the safety net (cf. how the Philox KATs guard the
  RNG reuse). Cross-backend is statistical/parity only anyway (G0c, 01 §9).
- `src/gpu/materials.cuh`: compact per-layer device material/xs — macro Σ_t/Σ_tr per group +
  per-isotope {number_density, per-group nu/chi/sigma_f/sigma_c/sigma_s/mu_bar, transfer} —
  mirroring what ref/'s RefTransport::LayerData precomputes for collision sampling. Flattened
  arrays (no per-layer std::vector on device).
- Host builders: CPU LayerStack/MaterialLib/FewGroupXS → device representation (upload).
- Parity tests (gpu.*): device(float) tracker vs CPU(double) tracker on axial/tangential/
  miss/inside-out rays within float tol; device macro Σ vs CPU mix() within tol; and the
  pure-function device results are config-independent.

## Findings (append as they happen)

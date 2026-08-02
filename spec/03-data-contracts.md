# 03 — Data Contracts

All formats are normative. Every format has `schema_version` (starts at 1; pre-implementation amendments evolve v1 in place — once M0-T5's loaders land, any breaking change requires a version bump + migration note per §12). Loaders MUST reject unknown versions, MUST validate required fields and ranges, and MUST emit diagnostics naming file/field/constraint. Unknown OPTIONAL fields: warn, ignore. **Examples are illustrative, NOT expected results** (§5 header rule, MAJ-45); tests parse them verbatim and check the stated invariants.

## 1. `data/constants.toml` (schema v1)

Single source of truth for all constants (`01-physics.md` §8). **Authored as `spec/appendix/constants.data.toml`** (strict-typed sibling of the human appendix; M0-T3), from which `tools/gen_constants` emits `data/constants.toml` + `constants.h/.cuh`.

```toml
schema_version = 1
[[constant]]
id     = "C-060"                # matches appendix/constants.md
name   = "compression_ratio"
value  = 2.2                    # required unless status = "PENDING"
lo     = 2.0                    # optional band (public range)
hi     = 2.5
unit   = "ratio"
status = "DECLASSIFIED"         # PUBLIC | DECLASSIFIED | RECONSTRUCTED | SIM | PENDING
use    = "compute"              # compute | crosscheck  (crosscheck may not enter tallies/gates)
cite   = "Wellerstein-2015, NWFAQ"
note   = "public range ~2–2.5x; value is the canonical working point"
```

**Three array types (ADR-015).** `[[constant]]` above is the scalar form. `[[band]]` carries `lo`/`hi` and **no `value`** — a gate band has no physical centre, and supplying a nominal is a hard generator error; it emits only `_lo`/`_hi`, so there is no bare identifier a consumer can compare against by mistake. `[[registry]]` is a named table of numbers or `[lo, hi]` pairs, for tuples (C-900 batch/inactive/active, C-901, C-908) and stream tables (C-907). Every entry of every type MUST carry `id`, `name`, `status`, `cite` and `appendix_text` — the last being the appendix Value cell verbatim, which `constants_roundtrip` compares exactly so an un-propagated appendix edit fails ctest instead of drifting. A threshold with a genuine nominal *plus* a tolerance stays a `[[constant]]` (C-930's 500 pcm is a tolerance, not a band).

**Geometry mass fields (M0-T3-b).** Appendix §2 rows carry a Mass column beside the OD. It is tabular data, not prose, so `[[constant]]` entries for geometry additionally carry `appendix_mass_text` (verbatim, drift-checked like `appendix_text`) plus `mass_kg`, or `mass_lo_kg`/`mass_hi_kg` where the appendix gives a range. The pit's 6.15 kg is a DECLASSIFIED authoritative value that `03 §5` invariant 4 and `11 §5`'s oracle both need; leaving it reachable only by parsing prose would let it drift.

Rules: `status = "PENDING"` entries MAY omit `value`, MUST carry `resolved_by = "<task-id>"`, and MUST raise if read at runtime (the generated header emits a deleted function, so reading one is a compile error). If `lo`/`hi` present, the generator emits `_lo`/`_hi` companions **and a `static_assert(lo ≤ value ≤ hi)` in the header** — stronger than the unit test this originally specified, because it cannot be forgotten for a newly added constant and it fails at compile time. Multi-entry registries (e.g. RNG streams) use `[[registry]]` arrays, not a single constant. Derived constants (e.g. C-041) carry `derived = "= C-918/(C-040*C-917)"` and the generator computes them. Human-readable ranges/notes stay in the appendix; the strict file is what the generator reads. `tools/verify/constants_roundtrip` asserts bijection between appendix rows and strict-file entries (id/value/unit/status/cite); runs under ctest.

## 2. `data/xs/<set>.json` — few-group cross sections (schema v2)

```json
{
  "schema_version": 2,
  "name": "fast4",
  "group_bounds_MeV": [20.0, 3.0, 1.0, 0.1, 1e-3],
  "isotopes": {
    "Pu239": {
      "nu":        [2.98, 2.92, 2.89, 2.89],
      "chi":       [0.55, 0.30, 0.12, 0.03],
      "sigma_f":   [1.95, 1.80, 1.80, 2.10],
      "sigma_c":   [0.12, 0.10, 0.10, 0.15],
      "sigma_s":   [4.10, 4.00, 3.90, 3.80],
      "sigma_n2n": [0.00, 0.00, 0.00, 0.00],
      "mu_bar":    [0.28, 0.24, 0.20, 0.15],
      "beta":      0.0020,
      "transfer":  [[0.70,0.20,0.08,0.02],[0.0,0.70,0.20,0.10],[0.0,0.0,0.80,0.20],[0.0,0.0,0.0,1.0]],
      "cite":      "ENDF-B-VIII-NNDC",
      "status":    "PUBLIC"
    }
  }
}
```

**Semantics (normative):**
- Groups are **0-based, highest→lowest energy**; `group_bounds_MeV` has `n_groups+1` strictly descending entries; group `g` spans `(bounds[g+1], bounds[g]]` MeV. v1 default is 4 groups with the lowest bound at **1e-3 MeV** — thermal is treated as unresolved-by-design (recorded limitation; adding thermal/epithermal groups is the v3 path, R-1).
- `sigma_f` = fission; `sigma_c` = **radiative capture only** (the v0 name `sigma_a` is REJECTED with a migration diagnostic — it was ambiguous with capture+fission); `sigma_s` = total scattering; `sigma_n2n` = (n,2n), optional, default 0.
- **`sigma_t = sigma_f + sigma_c + sigma_s + sigma_n2n` is computed by the loader; it MUST NOT appear in the file.**
- `nu` is **TOTAL** ν̄ (prompt + delayed) — ADR-013. The eigenvalue computed from it is `k_eff`, the benchmark-comparable quantity. `beta` (scalar, **REQUIRED**) is the isotope's delayed-neutron fraction (C-022…C-022e); `k_prompt = k_eff·(1−β_eff)` is derived downstream and consumed only by E3a kinetics. A set whose `nu` is prompt-only is a **loader error** — there is no way to detect it numerically, so the convention is enforced by requiring `beta` and by the M1-T4a data card stating which evaluation the `nu` came from.
- `mu_bar` = mean lab-frame scattering cosine per group (**REQUIRED**; transport-corrected P0, `01 §2`). Transport-corrected total `sigma_tr = sigma_t − mu_bar·sigma_s` is used for flight sampling.
- `transfer[from][to]` = probability that scattering in group `from` places the neutron in group `to`; each row sums to 1.0 ± 1e-6; **upscatter (`to < from`) MUST be zero in v2** (loader rejects). `transfer` is REQUIRED when `n_groups > 1`; `null` is permitted only in `status = "SIM"` test sets, and gate runs reject null-transfer sets.
- All cross sections in barns. Every value carries `cite`/`status`.

Values above are illustrative placeholders — M1-T4a replaces them with cited values and records provenance + collapse method in `data/benchmarks/`.

## 3. `data/materials/<name>.json` (schema v1)

```json
{
  "schema_version": 1,
  "name": "pu_ga_delta",
  "density_g_cm3": 15.23,
  "status": "DECLASSIFIED",
  "cite": "LA-3067/NWFAQ-8.1.1; density derived (appendix C-102 note)",
  "isotopes": { "Pu239": 0.95683, "Pu240": 0.00967, "Ga69": 0.02014, "Ga71": 0.01336 },
  "composition_check": { "Ga_wt_pct": 1.00, "Pu240_wt_pct_of_Pu": 1.0 }
}
```

Rules: `isotopes` are **atom fractions**, MUST sum to 1.0 ± 1e-6; each stable isotope listed separately (no natural-abundance expansion by the loader — deliberate, forces explicit choice). `composition_check` declares derived weight percents; the loader recomputes and WARNs on deviation > 0.2 pp from the material's cited note. Macroscopic Σ computed at load from number densities (`04 §5`; molar masses C-910+). Where a layer's mass is publicly known (appendix layer table), the loader computes mass from geometry×density and WARNs above `mass_tolerance_pct` (default 3.0, SIM) — never an error (reconstructed masses carry real spread; the pit density discrepancy is recorded, not resolved — appendix C-102 note). `jezebel.toml` MUST reference `pu_ga_jezebel` (≈4.5 wt% Pu-240) and MUST NOT reference `pu_ga_delta`; the loader errors on a scenario named `jezebel` using `pu_ga_delta`.

## 4. `scenarios/<name>.toml` — simulation scenario (schema v1)

```toml
schema_version = 1
name = "trinity_canonical"
seed = 12345                    # required; effective seed recorded in run.json
mode = "alpha"                  # alpha | eigen_only | fixed_source   ("td" is a v1 VALIDATION ERROR: stretch scope, ADR-003)

layers = [                      # TOP-LEVEL (M0-T5): in TOML a bare key after a table header
                                # joins that table, so this array must precede [data] or it
                                # silently becomes data.layers. Center outward; first layer
                                # inner radius = 0
  { id = "initiator", r_outer_cm = 1.00,   material = "be_po_urchin", status = "DECLASSIFIED" },
  { id = "pit",       r_outer_cm = 4.585,  material = "pu_ga_delta",  status = "DECLASSIFIED" },
  { id = "tamper",    r_outer_cm = 11.43,  material = "u_natural",    status = "RECONSTRUCTED" },
  { id = "boron",     r_outer_cm = 11.75,  material = "b10_acrylic",  status = "DECLASSIFIED" },
  { id = "pusher",    r_outer_cm = 23.495, material = "aluminum",     status = "RECONSTRUCTED" },
]
# Layers up to and INCLUDING pusher (r_outer = 23.495 cm) are the M2 canonical assembly.
# HE/lens layers are added at M6 (Stage 3).

[data]                          # required: which datasets to load
xs_set        = "fast4"         # resolves to data/xs/fast4.json; its "name" MUST equal xs_set
materials_dir = "data/materials"

[time]
t_zero = "he_initiation"        # normative: t=0 is outermost-HE initiation; initiator.t_fire_s is
                                # measured relative to HydroModel::at_peak_compression()

[initiator]
strength_n_per_s   = 1.0e8      # C-051: ~1 n per 5–10 ns at firing ⇒ 1.0–2.0e8 n/s
t_fire_s           = 0.0
pulse_width_s      = 1.0e-8

[compression]
tier = 2                        # 1 | 2  (3 = stretch interface only)
ratio = 2.2                     # C-060 (public ~2–2.5×)
t_c_s  = 1.0e-6                 # TIER-1 ONLY — ignored with WARN when tier = 2 (t_c is derived, 01 §5)

[kinetics]
generation_time_s_initial = 1.0e-8   # C-030; initial value ONLY — after the first eigen refresh the
                                     # solver-returned Λ (E3b) is used; this field MUST NOT be read then
eigen_refresh_gens      = 10
eigen_refresh_dr_frac   = 0.005 # refresh eigen on the first generation after any layer radius moved > this
t_max_s                 = 5.0e-6
quench_epsilon          = 1.0e-4  # burn terminates when F_n < quench_epsilon·F_peak (E6), NOT at k<1

[eigen]                         # INTERACTIVE defaults — gate runs use gates.toml values (C-900/C-900b)
batch = 100000
inactive = 10
active = 30

[transport]
sim_neutrons = 100000           # simulated particle count (gate/burst runs); weights carry physical scale

[hydro]
every_gens = 10                 # integer ≥ 1; WARN if < eigen_refresh_gens (each hydro step can force a refresh)

[lenses]                        # Stage 3 only (M6); absent earlier
count = 32
jitter_ns = 10.0
# [[lenses.detonators]]        # OPTIONAL per-detonator selection (ADR-018, M7-T5); absent = all
# enable = true                #   fire on time (the canonical form). When present, exactly `count`
# delay_s = 0.0                #   entries, one per detonator; delay_s in [0.0, 1.0e-6] seconds
                               #   (schema units — the same envelope as the 0–1000 ns jitter axis).
                               #   Any non-default detonator config marks the run non_canonical.

[source]                        # REQUIRED iff mode = "fixed_source" (05 §1 SourceSpec mirrors this)
kind = "point"                  # point | shell | volume | replay
position_cm = [0.0, 0.0, 0.0]
r_cm = 5.0                      # shell/volume only
spectrum = "watt_pu239"         # watt_pu239 | watt_u235 | group_chi | mono
mono_MeV = 1.0
angular = "isotropic"           # isotropic | forward
sim_particles = 100000

[overrides]                     # optional; applied to LOADED data objects, not the scenario tree
# "materials.pu_ga_delta.Pu240" = 0.010
# "xs.Pu239.nu" = [2.98, 2.92, 2.89, 2.89]
# Permitted prefixes: materials.<name>.<isotope> | xs.<isotope>.<field>. Recorded verbatim in run.json,
# included in canonical_hash(), and any override of a PUBLIC/DECLASSIFIED value marks the run
# non_canonical: true in tally.json — non-canonical runs MUST NOT be used as gate evidence.

[output]
tallies = ["k", "population", "fissions_by_isotope", "burnup", "yield", "fission_mesh"]  # closed vocabulary; unknown ⇒ validation error
checkpoint_every_gens = 100
dump_fields = false
dump_stride = 10                # field dumps are 128 MiB/frame (§9): stride + budget guardrails
dump_max_frames = 100
dump_budget_gb = 20             # loader warns when projected dump volume exceeds this

# UI annotations (normative, MAJ-25): one [ui."<dotted.path>"] table per widget, never inline.
[ui."initiator.strength_n_per_s"]
label = "Initiator strength"
range = [0.0, 5.0e8]            # ALWAYS schema units (n/s), never display units
status = "DECLASSIFIED"
[ui."kinetics.generation_time_s_initial"]
label = "Generation time (initial)"
range = [1.0e-9, 1.0e-7]
display_unit = "ns"             # presentation only; never changes stored value or hash
display_scale = 1.0e9
status = "DECLASSIFIED"
[ui."lenses.detonators"]        # ADR-018 (M7-T5): custom per-lens toggle/delay grid, not a scalar
label = "Detonators"            #   slider — the range bounds each entry's delay_s (schema units,
range = [0.0, 1.0e-6]           #   same envelope as the jitter axis); enable has no range.
display_unit = "ns"
display_scale = 1.0e9
status = "SIM"
```

Validation rules: `layers` is TOP-LEVEL and the loader rejects it under `[data]` with a diagnostic (M0-T5 — the array previously sat below the `[data]` header, which TOML reparents); layers strictly increasing radii; every `material` resolves under `[data].materials_dir`; `xs_set` resolves and names match; every value within its `[ui.*].range` for gate runs; `seed` required; `mode = "td"` rejected in v1; unknown keys hard-error; `layers.<id>.field` selects **by `id`** (override grammar: `layers.<id>.field`, `materials.<name>.<field>`, `xs.<iso>.<field>`, `section.key`; positional `layers[<int>]` accepted but not recommended); `lenses.detonators`, when present, MUST have exactly `lenses.count` entries and each `delay_s ∈ [0, 1e-6]` (absent = all fire on time — ADR-018).

## 5. `tally.json` — run output (schema v1)

> **ILLUSTRATIVE VALUES ONLY.** Numbers below demonstrate field relationships and the invariants; they are NOT expected results and MUST NOT be treated as targets. Peak k_eff for this device is classified (`00 §3.2`); `peak` shown is an arbitrary synthetic placeholder chosen to satisfy the invariants.

```json
{
  "schema_version": 1,
  "run_id": "2026-08-02T10-31-07Z_trinity_canonical_12345",
  "non_canonical": false,
  "k_eff": { "peak": 1.5, "at_initiator": 1.45, "at_quench": 0.94, "sigma_pcm": 19 },
  "k_prompt": { "peak": 1.497, "at_quench": 0.938 },
  "yield_kt": 20.0, "yield_kt_sigma": 0.6,
  "fissions_total": 2.9016080e24,
  "fissions_by_isotope": { "Pu239": 2.3012864e24, "Pu240": 2.0000000e22, "U238": 5.8032160e23 },
  "burnup": { "pu_fraction": 0.1498343, "pu_fraction_sigma": 0.004,
              "pu_fissions": 2.3212864e24, "u238_tamper_fissions": 5.8032160e23,
              "tamper_yield_fraction": 0.20 },
  "yield_split": { "pre_peak_kt": 10.6, "post_peak_kt": 9.4 },
  "population_series": { "dt_s": 1e-8, "log10_N": [ 0.0 ] },
  "fission_mesh": { "format": "radial_shells",
                    "shell_edges_cm": [0.0, 4.585, 11.43, 11.75, 23.495],
                    "fissions": [2.3212864e24, 5.8032160e23, 0.0, 0.0],
                    "leaked_fissions": 0.0 },
  "timing": { "wall_s": 38.7, "eigen_calls": 49, "generations": 494, "max_q": 0.012 }
}
```

**Invariants (normative; `tally_invariants` unit test asserts all, M3-T3):**
1. `len(fissions) == len(shell_edges_cm) − 1`
2. `|sum(fissions) + leaked_fissions − fissions_total| ≤ 1e-6 · fissions_total`
3. `|yield_kt · Φ_kt / fissions_total − 1| ≤ 1e-6` (Φ_kt = C-041)
4. `|Σ_i (fissions_by_isotope[i] · M_i) / (N_A · M_pit) − pu_fraction| ≤ 1e-6`, summed over the **core Pu isotopes only** (Pu-239, Pu-240, Pu-241 if present), **each with its own molar mass** (C-910/C-911/C-919, C-916). Using M_Pu239 for all Pu fissions is NOT permitted: at a 1% Pu-240 fission share it injects ~4e-5 relative error, 40× the tolerance (QC-05).
5. `|u238_tamper_fissions/fissions_total − tamper_yield_fraction| ≤ 1e-3`
6. `sum(fissions_by_isotope) == fissions_total` (±1e-6 rel); `burnup.pu_fraction` and `burnup.tamper_yield_fraction` are ALWAYS both present and never merged (`00 §3.5`)
7. `generations · mean(Λ) ≤ t_max_s`
8. `population_series` is log-scale and renormalization-safe (`01 §4`)
9. `|yield_split.pre_peak_kt + yield_split.post_peak_kt − yield_kt| ≤ 1e-6 · yield_kt` — this is the invariant that proves the E6 post-peak integration is actually wired (BLK-03); a run that terminates at the k=1 crossing cannot satisfy it with a non-trivial `post_peak_kt`.

`σ` on yield/burn-up is propagated from per-eigen-call σ on k through E3a (first-order, summed in quadrature across refreshes); where impractical, estimated from the gate's seed set sample standard deviation. `max_q` is the quasi-static validity diagnostic (R-13).

## 6. `run.json` — provenance (schema v1)

```json
{ "schema_version": 1, "run_id": "...", "unit_id": "…", "scenario_file": "scenarios/trinity_canonical.toml",
  "scenario_sha256": "…", "data_hashes": { "xs": "…", "materials": { "pu_ga_delta": "…" } },
  "seed": 12345, "code_version": "0.1.0", "spec_version": "0.2", "git": "a1b2c3d", "dirty": false,
  "backend": "gpu", "device": "NVIDIA GeForce RTX 4070 Ti SUPER", "started": "…", "finished": "…" }
```

`unit_id` = sha256(scenario canonical hash ‖ overrides ‖ seed) — the idempotent dedup key and artifact directory name (sweeps). Every run emits `run.json` + `tally.json` (+ `checkpoint.bin`) into `artifacts/<unit_id>/`. `code_version` tracks the app; `spec_version` tracks this spec; both bump independently (who bumps: the milestone-closing session).

## 7. `sweep.toml` — batch manifest (schema v1)

```toml
schema_version = 1
name = "jitter_sensitivity"
base_scenario = "scenarios/trinity_canonical.toml"
[sweep]
sampler = "mcts"                # grid | lhs | random | mcts (in-tree C++ plugin, 06 §2)
budget_runs = 5000
budget_wallclock_h = 8
checkpoint_every_runs = 50
[objective]
kind = "calibrate"              # sensitivity | calibrate
target_yield_kt = [16.6, 26.8]  # C-940 public envelope; calibrate scores -|observed - band_center|
report = ["burnup.pu_fraction", "burnup.tamper_yield_fraction", "k_eff.peak"]
[[space]]
param = "lenses.jitter_ns"
axis_class = "uncertainty"      # numerical | uncertainty | pedagogical
constant_id = "C-071"           # REQUIRED for uncertainty axes; range MUST lie inside the constant's [lo, hi]
range = [0.0, 20.0]
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
constant_id = "C-060"
range = [2.0, 2.5]
```

**Sweep space restriction (normative, MAJ-35):** `numerical` = SIM knobs only (unrestricted). `uncertainty` = physical parameter within its published band (`constant_id` + range inside [lo, hi], e.g. tamper mass 108–111 kg over C-103). `pedagogical` = outside the published envelope, permitted ONLY with `sampler = "grid"`, `budget_runs ≤ 100`, `kind = "sensitivity"`; optimizing samplers and `calibrate` are loader-rejected when any axis is pedagogical. `calibrate` requires every axis to be `numerical`/`uncertainty` and scores toward band CENTER. Sampler scoring kinds (`ScoreKind::Coverage | BandCenter`) are enforced by the plugin interface (06 §2) — `OptimizeExtremum` is rejected at load. This is the mechanical enforcement of `00 §3.4`.

Results: `sweep.db` (SQLite: `runs(unit_id PK, params_json, tally_json, wall_s, status)`, `cursor(state_json)`) + per-run artifact bundles named by `unit_id` (rerunning a completed unit is a no-op).

## 8. `checkpoint.bin` — resume state (schema v2)

**Header (128 B):** magic `NSCKPT02`, schema_version, endianness marker, `backend` (ref|gpu), `bank_precision` (f32|f64), `scenario_sha256`, `data_sha256`, `code_version`, `git_hash`, section table (offset, length, CRC32 per section).
**Load rules:** reject with precise diagnostic on any mismatch of magic/version/endianness/scenario/data/git, or any section CRC failure. Old checkpoints are never required to load (R-7); a mismatched one MUST NOT load silently.
**Sections:** 1 `SimClock` (phase, t, generation n, exponent offset, F_peak, supercritical_reached) · 2 RNG (Philox key + `(ctr, sub)` per stream, MAJ-56) · 3 Neutron bank **at backend native precision** (SoA: pos ×3, dir as (θ,φ) radians θ∈[0,π] φ∈[0,2π), group u8, weight; `bank_precision` declares) · 4 Geometry · 5 Hydro (versioned per tier) · 6 Latest EigenResult (k, Λ, σ, k/H history window, fission source) · 7 Tally accumulators (f64 or fixed-point per `01 §9`) · 8 Sweep cursor (nukefarm only).
Resume gate (T-resume, `11-testing.md`): kill at defined kill points (phase boundaries and generation boundaries; mid-eigen kills resume at the last completed generation boundary — eigen state section covers in-flight power iteration) → resume → final `tally.json` bit-identical **on the same backend**.

## 9. `fields.f16` — render field dump (schema v1)

Grid `N=256`³ **half-float** (extension renamed from `.f32`), bbox = 1.5 × outer radius. Channels: `T_K` (visualization mapping per `09 §1`, not a physics tally), `rho_g_cm3`, `fission_rate`, `shock_mask`. Header: magic `NSFLD001`, schema_version, N, bbox, channel count/types, timestamp. 128 MiB/frame ⇒ `dump_stride`/`dump_max_frames`/`dump_budget_gb` guardrails (§4).

## 10. `data/benchmarks/gates.toml` (schema v1)

Generated from `08-validation.md` by M1-T5's generator tool; never hand-edited. `nukebench` MUST fail if `spec_sha256` ≠ current `08-validation.md` hash.

```toml
schema_version = 1
generated_from = "spec/08-validation.md"
spec_sha256    = "…"
[[gate]]
id = "G0a"
title = "Godiva bare-sphere k_eff"
scenario = "data/scenarios/godiva.toml"
seeds = [1, 2, 3, 4, 5]         # normative seed set (MAJ-22); changing it requires an ADR
[gate.eigen]
batch = 1000000
inactive = 50
active = 200
[[gate.criterion]]
name = "k_deviation_pcm"
op = "abs_le"
value = 500
constant_id = "C-930"           # every criterion value MUST resolve to appendix/constants.md
[[gate.criterion]]
name = "sigma_pcm"
op = "le"
value = 25
constant_id = "C-931"
```

## 11. `gate_report.json` (schema v1)

```json
{ "schema_version": 1, "gate": "G0a", "verdict": "pass",
  "gates_toml_sha256": "…", "spec_sha256": "…",
  "code_version": "0.1.0", "git": "a1b2c3d", "dirty": false,
  "backend": "gpu", "device": "…", "started": "…", "finished": "…",
  "attempts": [ { "attempt": 1, "seed": 1, "verdict": "pass",
                  "measurements": { "k": 0.99981, "sigma_pcm": 19, "k_deviation_pcm": -19 },
                  "criteria": [ { "name": "k_deviation_pcm", "value": -19, "op": "abs_le", "threshold": 500, "pass": true } ],
                  "run_dir": "artifacts/<unit_id>" } ],
  "notes": "required when verdict = conditional" }
```

Rules: `verdict` ∈ pass|fail|conditional; pass iff every normative seed passes; `attempts` is **append-only across re-runs** (anti-seed-shopping by construction, MAJ-22); `dirty: true` caps verdict at `conditional`.

**Location and retention (QC-07):** reports are written to `artifacts/gate_reports/<gate>/gate_report.json` and are **committed to the repository** (`02 §2` .gitignore un-ignore). They are the evidence `PROGRESS.md`'s gate table cites and the only cross-session proof a gate was met; CI archives are an additional copy, never the primary one. `run_dir` points into the (untracked) run bundle — a missing bundle degrades the report's reproducibility but not its validity, and the report MUST carry enough measurements to stand alone.

## 12. Versioning & migration policy

Pre-M0-T5: schemas evolve in place at v1 (nothing to migrate). After loaders land: breaking change ⇒ `schema_version` bump + loader accepts both versions for one milestone + migration note in CHANGELOG. `checkpoint.bin` already at v2 (v1 never shipped).

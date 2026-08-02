# 04 — Module Spec: Core (`src/core/`)

APIs are normative signatures (C++20). Tests per `11-testing.md`.

## 1. `core/constants`

- Generated, never hand-written: `tools/gen_constants` reads `spec/appendix/constants.data.toml` (strict sibling, 03 §1) → `data/constants.toml` + `constants.h` / `constants.cuh` (namespace `ns::consts`).
- Generator MUST fail on missing id/value(when required)/unit/status/cite; emits `_lo`/`_hi` companions for banded entries; computes `derived =` expressions; `PENDING` entries generate accessors that raise.
- API: `double ns::consts::get(std::string_view id)` (runtime lookup, tests) + typed constexpr accessors for hot paths. `use = "crosscheck"` constants are in a separate namespace `ns::consts::crosscheck` so the static misuse check (`11 §4`) can grep for it.

## 2. `core/rng`

Philox4x32-10, counter-based. **Counter/key layout (normative, MAJ-56/C3):**

```
key     = { lo32(seed), hi32(seed) }
counter = { lo32(ctr), hi32(ctr), lo32(stream), hi32(stream) }    # ctr = per-stream block index
```

```cpp
namespace ns::rng {
struct Stream {                                  // resumable state is (ctr, sub)
  Stream(uint64_t seed, uint64_t stream, uint64_t ctr = 0, uint8_t sub = 0);
  float  uniform_f();      // (block[sub] >> 8) * 0x1.0p-24f
  double uniform_d();      // two u32s >> 11 * 0x1.0p-53d — ref/ uses this (01 §9)
  float  normal_f();       // Box–Muller
  std::pair<uint64_t,uint8_t> state() const;     // (ctr, sub) — BOTH serialized (03 §8 §2)
};
uint64_t fork(uint64_t parent_stream, uint64_t parent_ctr, uint32_t progeny_ordinal);
// fork = splitmix64(parent_stream ^ rotl(parent_ctr,17) ^ (0x9E3779B97F4A7C15ull * (progeny_ordinal+1)))
// — normative; child streams derive from PARENT IDENTITY, never buffer position (BLK-11/E2).
}
```

- Stream registry in `constants.data.toml` (`[[registry]]`, SIM): source=1, flight=2, collision=3, fission=4, scatter=5, hydro=6, render=7.
- **Known-answer tests (normative):** (a) the three published Random123 Philox4x32-10 single-block vectors (counter/key all-zero; all-0xffffffff; π pattern) MUST reproduce exactly — portability check. (b) Project-local vector: first 16 `uniform_f()` of `Stream(seed=0, stream=0)` recorded in `tests/unit/rng_kat.inl` when M0-T4 lands, frozen thereafter (change requires ADR) — regression check. (c) `fork(42, 1000, 3)` value recorded identically in ref/ and gpu/ KATs.

## 3. `core/xs`

```cpp
struct GroupData { double nu, chi, sigma_f, sigma_c, sigma_s, sigma_n2n, mu_bar; };
using Transfer = std::array<std::array<double,4>,4>;   // [from][to], probability, rows sum to 1
struct IsotopeXS { std::string name; std::vector<GroupData> g; Transfer transfer;
                   double beta;              // REQUIRED per isotope (ADR-013) — 03 §2
                   std::string cite, status; };  // every value carries cite/status — 03 §2
class FewGroupXS {
public:
  static FewGroupXS load(const std::filesystem::path& json);   // 03 §2 semantics enforced
  int  groups() const;
  const IsotopeXS& isotope(std::string_view name) const;
  const std::vector<double>& bounds_MeV() const;               // descending, 0-based groups
};
```

Loader enforces 03 §2: rejects `sigma_a` (migration diagnostic), `sigma_t` present in file, null transfer for non-SIM sets, upscatter, missing `mu_bar`, non-descending bounds. Computes `sigma_t`, `sigma_tr = sigma_t − mu_bar·sigma_s`. `MatXS mix(const Material&, const FewGroupXS&)` builds macroscopic per-group Σ + ν̄Σ_f per material.

## 4. `core/geometry`

```cpp
inline constexpr int kOutside = -1;
struct Layer { std::string id; double r_outer; int material_id; std::string status; };
class LayerStack {
public:
  static LayerStack from_scenario(const Scenario&, const MaterialLib&);  // name→index: sorted-name order
  int  locate(Vec3 p) const;                          // layer index or kOutside
  double radius_of(int layer) const;
  void set_radii(const std::vector<double>& r_outer_cm);   // absolute values (MIN-15)
  void scale_radii(double factor);                           // uniform scale (hydro Tier-1)
};

class Tracker {
public:
  virtual ~Tracker() = default;
  virtual double distance_to_boundary(Vec3 p, Vec3 dir, int layer) const = 0; // nearer of inner/outer, +inf if none
  virtual int    locate(Vec3 p) const = 0;
  virtual void   rebuild(const LayerStack&) = 0;
};
class AnalyticSphereTracker final : public Tracker { /* closed-form ray–sphere */ };
// M6: class OptiXCSGTracker final : public Tracker { /* NUKESIM_WITH_OPTIX-guarded */ };
```

Analytic math (normative): ray `p + t·d`, |d|=1; `b = p·d`, `c = |p|² − R²`; nearest positive root among the layer's inner/outer spheres. Degeneracies (|discriminant| or |p|≈boundary within ε=1e-9 cm): nudge **along direction of travel** (`p += ε·d`), then re-`locate` — correct for entering and exiting crossings (MIN-14); never nudge unconditionally inward.

## 5. `core/material`

```cpp
struct Material { std::string name; double density;
                  std::vector<std::pair<const IsotopeXS*, double>> fracs;  // atom fractions, sorted-name index
                  MatXS macro; };
class MaterialLib { public:
  static MaterialLib load_dir(const std::filesystem::path&, const FewGroupXS&);
  int index_of(std::string_view name) const; };
```

`n_i = frac_i · ρ / M̄ · N_A`, M̄ = fraction-weighted mean molar mass (C-910…C-918). Composition check per 03 §3 (wt% recompute, WARN > 0.2 pp). Mass-from-geometry WARN per 03 §3 (`mass_tolerance_pct`).

## 6. `core/scenario`

```cpp
struct Scenario {
  static Scenario load(const std::filesystem::path& toml);   // full 03 §4 validation incl. [ui.*] ranges
  void apply_overrides(const std::vector<std::pair<std::string,ParamValue>>&); // unknown key ⇒ hard error
  std::string canonical_hash() const;                        // normative below
};
```

- Override grammar (one canonical form): `layers.<id>.<field>`, `layers[<int>].<field>` (positional, discouraged), `materials.<name>.<field>` and `xs.<iso>.<field>` (data overrides — only via `[overrides]`, 03 §4), `section.key`. `tamper_override.*` and any other ad-hoc prefix are rejected.
- **`canonical_hash()` (normative, MAJ-44):** (1) load + materialize ALL schema defaults; (2) canonical serialization: keys sorted lexicographically at every level; tables before arrays-of-tables; no comments; LF; UTF-8 no BOM; integers decimal; floats shortest round-trip (`std::to_chars`, `%.17g` fallback) so parse(emit(x)) == x; (3) concat canonical scenario bytes + sha256(resolved xs file) + sha256(each resolved material file, id order) + overrides block; (4) sha256 the concatenation. Round-trip test asserts hash stability across key reordering, comment changes, CRLF↔LF, equivalent float spellings.

## 7. Tests required (DoD for core modules)

- RNG: three Random123 KATs + project-local vector + fork KAT + stream-independence + (ctr,sub) round-trip.
- XS/material loaders: parse canonical examples; reject each 03 §2/§3 violation class (negative test per rule).
- Geometry: ray–sphere known answers (axial, tangential, miss, inside-out); ε-degeneracy nudge direction both ways; `locate` across all layers; `set_radii`/`scale_radii`; scenario round-trip + `canonical_hash` stability matrix.
- `tools/verify/decision_index`: every Dn ↔ exactly one ADR.
- `tools/verify/constants_roundtrip`: appendix ↔ strict-file bijection.

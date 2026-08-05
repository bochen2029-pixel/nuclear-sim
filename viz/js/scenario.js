// scenario.js — JS mirror of data/scenarios/trinity_canonical.toml (spec 03 §4),
// the [ui.*] widget annotations (03 §4, MAJ-25), and the canonical layer radii.
// Every key, range, unit, and status matches the spec so the real nscore binding
// drops in later without UI changes. Values marked SIM/pedagogical are flagged.
export const LAYERS = [            // center outward; radii in cm (03 §4 public values)
  { id: "initiator", r_outer_cm: 1.00,   material: "be_po_urchin", status: "DECLASSIFIED" },
  { id: "pit",       r_outer_cm: 4.585,  material: "pu_ga_delta",  status: "DECLASSIFIED" },
  { id: "tamper",    r_outer_cm: 11.43,  material: "u_natural",    status: "RECONSTRUCTED" },
  { id: "boron",     r_outer_cm: 11.75,  material: "b10_acrylic",  status: "DECLASSIFIED" },
  { id: "pusher",    r_outer_cm: 23.495, material: "aluminum",     status: "RECONSTRUCTED" },
];
// fission_mesh.shell_edges_cm (03 §5) — the tally's radial shells are exactly these.
export const SHELL_EDGES_CM = [0, ...LAYERS.map(l => l.r_outer_cm)];

// world units per cm (device model scale: pusher outer surface = 0.48 world units).
// The microscope converts spatial FissionSite positions (cm, ref_transport.h) → world.
export const WORLD_PER_CM = 0.48 / 23.495;

// Canonical defaults (03 §4 + C-102 pit mass + material card atom fractions).
export const CANONICAL = {
  "pit.mass_kg": 6.15,                        // C-102 DECLASSIFIED (03 §1 geometry mass fields)
  "materials.pu_ga_delta.Pu240": 0.010,       // atom fraction; [overrides] grammar path (03 §4)
  "tamper.scale": 1.0,                        // pedagogical thickness scale (03 §7 axis_class)
  "compression.tier": 2,                      // 1 | 2 (not a widget; fixed at 2)
  "compression.ratio": 2.2,                   // C-060, public band [2.0, 2.5]
  "lenses.count": 32,                         // Stage-3 lens array (M6); ADR-018 per-detonator grid
  "lenses.jitter_ns": 10.0,                   // C-071, uncertainty band [0, 20]
  "initiator.strength_n_per_s": 1.0e8,        // C-051, band [0, 5e8]
  "kinetics.generation_time_s_initial": 1.0e-8, // C-030; initial only, solver Λ thereafter
};

// [ui."<dotted.path>"] annotations (03 §4) — the parameter panel is BUILT from
// these at boot; adding a widget here adds it to the UI. Ranges are schema units.
export const UI_WIDGETS = [
  { path: "pit.mass_kg", label: "pit mass", range: [4.0, 9.0], step: 0.01,
    unit: " kg", digits: 2, status: "DECLASSIFIED", axis_class: "pedagogical" },
  { path: "materials.pu_ga_delta.Pu240", label: "Pu-240 fraction", range: [0.005, 0.03], step: 0.0005,
    unit: "%", display_scale: 100, digits: 2, status: "DECLASSIFIED", axis_class: "override" },
  { path: "tamper.scale", label: "tamper thickness", range: [0.5, 1.5], step: 0.01,
    unit: "×", digits: 2, status: "SIM", axis_class: "pedagogical" },
  { path: "compression.ratio", label: "compression ratio", range: [2.0, 2.5], step: 0.01,
    unit: "", digits: 2, status: "DECLASSIFIED", axis_class: "uncertainty", constant_id: "C-060" },
  { path: "lenses.jitter_ns", label: "HE timing jitter", range: [0.0, 20.0], step: 0.1,
    unit: " ns", digits: 1, status: "RECONSTRUCTED", axis_class: "uncertainty", constant_id: "C-071" },
  { path: "initiator.strength_n_per_s", label: "initiator strength", range: [0.0, 5.0e8], step: 1.0e6,
    unit: "e6 n/s", display_scale: 1e-6, digits: 0, status: "DECLASSIFIED", axis_class: "uncertainty",
    constant_id: "C-051" },
  { path: "kinetics.generation_time_s_initial", label: "generation time (initial)", range: [1.0e-9, 1.0e-7], step: 1.0e-9,
    unit: " ns", display_scale: 1e9, digits: 0, status: "DECLASSIFIED", display_unit: "ns" },
];

// Spec rule (03 §4 [overrides] · 03 §7 sweep restriction): overriding a
// PUBLIC/DECLASSIFIED value, or moving a pedagogical axis off canonical, marks
// the run non_canonical — shown as a persistent badge (00 §2 / 06 §3).
export function nonCanonicalFlags(cfg) {
  const flags = [];
  for (const w of UI_WIDGETS) {
    const v = cfg[w.path], d = CANONICAL[w.path];
    if (v === undefined || d === undefined) continue;
    if (Math.abs(v - d) > (w.step || 1e-9) / 2) {
      if (w.axis_class === "pedagogical") flags.push(`${w.path} (pedagogical)`);
      else if (w.axis_class === "override") flags.push(`${w.path} (override)`);
      // uncertainty axes inside their constant band stay canonical
    }
  }
  return flags;
}

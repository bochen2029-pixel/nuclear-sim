# WIP — session-2026-08-03-b — M3-T3-a (tally + tally_invariants)

Task: SPLIT from M3-T3. `src/physics/tally/` = TallyResult (03 §5 schema v1) +
all 9 `tally_invariants` (03 §5) + JSON parse/serialize. fast4-INDEPENDENT.

## Findings / decisions (append BEFORE acting)
- fast4 xs data still ABSENT (data/xs/ = .gitkeep only); owner-gated (M1-T4a-2).
  Surfaced to owner. Proceeding with the fast4-independent frontier.
- oracle.py §6 (section_invariants) already checks all 9 on the 03 §5 fixture and
  is the PORT REFERENCE. Exact recipe pinned:
    phi = C-918/(C-040·C-917)  (= ns::consts::phi_kt_fissions_per_kiloton = 1.4508041093079906e23)
    n_a = C-916 (avogadro_constant)
    m_pit_g = C-102.mass_kg × 1000 = od_pu_ga_core_mass_kg×1000 = 6150 g
    core Pu molar = Pu239 C-910 / Pu240 C-911 / Pu241 C-919 ; U238 = C-913
    i4 core_pu = (Pu239,Pu240,Pu241), each its OWN molar mass (QC-05: one-mass = ~4e-5 > 1e-6 tol)
- Fixture i3 = 7.5e-8, i4 ≈ 3.8e-8 (both « 1e-6). Hand-checked.
- Invariant 7 `generations·mean(Λ) ≤ t_max_s`: oracle.py left it UNCONDITIONAL.
  C++ will actually compare. mean(Λ) taken as population_series.dt_s (= 1e-8 in the
  fixture, and = oracle's C-030×1e-9 exactly, since C-030=10 ns). t_max_s: the
  fixture pairs with the 03 §4 scenario EXAMPLE (t_max_s=5.0e-6), NOT
  trinity_canonical.toml (1.0e-6): 494·1e-8 = 4.94e-6 ≤ 5.0e-6 ✓ (fails vs 1e-6).
  So t_max_s parsed from the 03 §4 example (drift-proof), not trinity.
- Fixture parsed drift-free via spec_examples::fenced_block(contracts(), "## 5. `tally.json`", "json").
- nlohmann-json already a dep (src/core/xs/xs.cpp uses it). Include <nlohmann/json.hpp>.
- Invariant 6 "both burnup fields present, never merged": enforce at PARSE (require
  both keys) + negative test; checker asserts the numeric sum==total clause.

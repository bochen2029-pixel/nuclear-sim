"""Documented FAST-metal collapse weight phi(E) for the fast4 group constants.

M1-T4a-2 (owner-authorized ENDF/B-VIII.0 collapse). See tools/xs/README.md and
data/xs/PROVENANCE-fast4.md for the full rationale. In one line: Godiva and Jezebel
are bare, unmoderated fast-metal critical assemblies, so the group-averaging weight is
a representative HARD fast spectrum -- a fission (Watt) source at high energy joined to
a 1/E slowing-down tail, with NO thermal peak -- NOT a generic 1/E or thermal weight.

This module is route-independent: the same phi(E) tabulation feeds either a hand-rolled
group average (openmc.data route) or an NJOY GROUPR TAB1 weight card. It defines the
spectrum ONLY; it never touches cross sections, so it cannot be a channel for fitting
xs to a benchmark.

phi(E), E in MeV:
  E >= E_JOIN : Watt fission spectrum  chi(E) = exp(-E/a) * sinh(sqrt(b*E))
  E <  E_JOIN : 1/E slowing-down tail, scaled to be continuous with chi at E_JOIN.

Watt parameters a = 0.988 MeV, b = 2.249 /MeV are the standard U-235 thermal-fission
Watt spectrum (a citable, conventional fast source). Pu-239 is marginally harder; if the
measured G0b pcm misses, the weighting-iteration step (README) hardens phi -- the xs are
never touched.
"""

import math

# Watt fission-spectrum parameters (U-235 thermal fission; standard fast source).
WATT_A_MEV = 0.988
WATT_B_PER_MEV = 2.249

# Fission-source / slowing-down crossover. ~0.82 MeV is the conventional join in the
# standard fast weight (fission spectrum above, 1/E below).
E_JOIN_MEV = 0.82

# Thermal Maxwellian join (ADR-024, schema v3 thermal group). Below E_TH_JOIN_MEV the
# collapse weight is a 293.6 K Maxwellian NEUTRON-FLUX spectrum (phi ~ E*exp(-E/kT)); above
# it the 1/E slowing-down tail continues up to E_JOIN. 5*kT is the conventional
# thermal/epithermal join (the Maxwellian flux tail blends into 1/E there). This branch is
# ACTIVE ONLY for the v3 thermal group (bounds below 1e-3 MeV); every fast group [0.1, 20]
# MeV lies above E_TH_JOIN, so its weight -- hence its collapsed cross sections -- is
# byte-identical to the v2 fast-metal weight (gate-safety by construction).
T_THERMAL_K = 293.6
KT_MEV = 8.617333262e-11 * T_THERMAL_K   # Boltzmann k = 8.617e-5 eV/K -> kT in MeV (~2.53e-8)
E_TH_JOIN_MEV = 5.0 * KT_MEV             # ~1.27e-7 MeV (0.127 eV): thermal/epithermal join


def watt(E_MeV, a=WATT_A_MEV, b=WATT_B_PER_MEV):
    """Unnormalized Watt fission spectrum chi(E), E in MeV."""
    if E_MeV <= 0.0:
        return 0.0
    return math.exp(-E_MeV / a) * math.sinh(math.sqrt(b * E_MeV))


def maxwell_flux(E_MeV, kT=KT_MEV):
    """Unnormalized Maxwellian NEUTRON-FLUX spectrum phi(E) = E*exp(-E/kT) (the flux form;
    the number-density form is sqrt(E)*exp(-E/kT)). Peaks at E = kT and -> 0 as E -> 0, so it
    replaces the divergent 1/E tail in the thermal group with a physical thermal peak."""
    if E_MeV <= 0.0:
        return 0.0
    return E_MeV * math.exp(-E_MeV / kT)


def weight(E_MeV):
    """The collapse weight phi(E), E in MeV. Unnormalized (only ratios matter in a group
    average). Piecewise, continuous at BOTH joins by construction: Watt fission source
    (E >= E_JOIN) -> 1/E slowing-down (E_TH_JOIN <= E < E_JOIN) -> 293.6 K Maxwellian
    (E < E_TH_JOIN; the v3 thermal group, ADR-024). Fast groups only ever see the first two
    branches, so they are byte-identical to the v2 fast-metal weight."""
    if E_MeV <= 0.0:
        return 0.0
    if E_MeV >= E_JOIN_MEV:
        return watt(E_MeV)
    if E_MeV >= E_TH_JOIN_MEV:
        # 1/E slowing-down tail, scaled so phi(E_JOIN-) == phi(E_JOIN+).
        return watt(E_JOIN_MEV) * (E_JOIN_MEV / E_MeV)
    # thermal Maxwellian, scaled so phi is continuous with the 1/E tail at E_TH_JOIN.
    slowing_at_join = watt(E_JOIN_MEV) * (E_JOIN_MEV / E_TH_JOIN_MEV)
    return slowing_at_join * (maxwell_flux(E_MeV) / maxwell_flux(E_TH_JOIN_MEV))


def tabulate(energies_MeV):
    """phi on a caller-supplied energy grid (MeV) -> list of phi values."""
    return [weight(E) for E in energies_MeV]


if __name__ == "__main__":
    # Self-check: positivity + continuity at the join + a readable table over the
    # 4-group range [1e-3, 20] MeV.
    lo = weight(E_JOIN_MEV * (1.0 - 1e-9))
    hi = weight(E_JOIN_MEV * (1.0 + 1e-9))
    assert abs(lo - hi) / hi < 1e-6, f"discontinuity at E_JOIN: {lo} vs {hi}"
    print(f"continuity at E_JOIN={E_JOIN_MEV} MeV: phi-={lo:.6e} phi+={hi:.6e}  OK")
    print("  E (MeV)     phi(E)")
    for E in [1e-3, 1e-2, 0.1, 0.3, 0.82, 1.0, 2.0, 5.0, 14.0, 20.0]:
        w = weight(E)
        assert w > 0.0
        print(f"  {E:8.3f}   {w:.6e}")
    # The fission-source (Watt) component should peak near ~0.7-0.9 MeV. (phi itself
    # rises as 1/E into the slowing-down tail, so its global max is at the low-E edge --
    # that is the intended weight shape, not the fission peak.)
    peak_E = max((E * 0.001 for E in range(1, 20001)), key=watt)
    print(f"Watt (fission-source) peak near E ~ {peak_E:.3f} MeV (expected ~0.7-0.9)")

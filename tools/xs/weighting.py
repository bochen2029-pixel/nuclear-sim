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


def watt(E_MeV, a=WATT_A_MEV, b=WATT_B_PER_MEV):
    """Unnormalized Watt fission spectrum chi(E), E in MeV."""
    if E_MeV <= 0.0:
        return 0.0
    return math.exp(-E_MeV / a) * math.sinh(math.sqrt(b * E_MeV))


def weight(E_MeV):
    """The fast-metal collapse weight phi(E), E in MeV. Unnormalized (only ratios matter
    in a group average). Continuous at E_JOIN by construction."""
    if E_MeV <= 0.0:
        return 0.0
    if E_MeV >= E_JOIN_MEV:
        return watt(E_MeV)
    # 1/E tail, scaled so phi(E_JOIN-) == phi(E_JOIN+).
    return watt(E_JOIN_MEV) * (E_JOIN_MEV / E_MeV)


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

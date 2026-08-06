#!/usr/bin/env python3
"""Collapse ENDF/B-VIII.0 (pre-reconstructed HDF5, 293.6 K) to the fast4 4-group set.

M1-T4a-2a, owner-authorized Path B. Run in WSL (conda env `omc`). The numbers in
data/xs/fast4.json come ONLY from this documented collapse -- never tuned to a benchmark
(same discipline as c_a). The weight is the documented fast-metal spectrum phi(E) from
tools/xs/weighting.py (NOT 1/E or thermal). See tools/xs/README.md + PROVENANCE-fast4.md.

Group averaging:  sigma_g = integral_g sigma(E) phi(E) dE / integral_g phi(E) dE,
trapezoidal on each nuclide's own (resonance-resolving) energy grid.

Usage:  python build_fast4.py --hdf5 <dir> --out <fast4.json> [--isotopes A,B,...]
"""
import argparse
import json
import os
import sys

import numpy as np
import openmc.data

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weighting  # noqa: E402  -- phi(E_MeV), the documented fast weight

# numpy>=2.0 renamed trapz -> trapezoid; support both.
_trapz = getattr(np, "trapezoid", None) or getattr(np, "trapz")

# 03 §2 fixed structure: bounds high->low in MeV -> eV. 4 groups, 0-based high->low.
BOUNDS_MEV = [20.0, 3.0, 1.0, 0.1, 1.0e-3]
BOUNDS_EV = [b * 1.0e6 for b in BOUNDS_MEV]
NG = 4
TEMP = "294K"  # 293.6 K room temperature (the assemblies are at room temperature)
# Below this mass number, fast elastic scattering is treated as isotropic-CM downscatter
# ([alpha*E, E]); at/above it, forward-peaking dominates and elastic stays within-group (M1-T4a-2b).
kElasticDownscatterA = 30

# fast4 a-set nuclide -> openmc HDF5 filename.
ASET = ["U234", "U235", "U238", "Pu239", "Pu240", "Pu241", "Ga69", "Ga71"]


def phi_ev(E_ev):
    """Documented fast weight on an eV grid (weighting.weight takes MeV)."""
    E_ev = np.atleast_1d(np.asarray(E_ev, dtype=float))
    return np.array([weighting.weight(e / 1.0e6) for e in E_ev])


def group_nodes(iso, glo, ghi):
    """Integration nodes over [glo, ghi] eV: the nuclide's own grid (resolves resonances)
    plus the group boundaries."""
    Eg = np.asarray(iso.energy[TEMP], dtype=float)
    inside = Eg[(Eg > glo) & (Eg < ghi)]
    return np.unique(np.concatenate(([glo], inside, [ghi])))


def gavg(iso, xs, glo, ghi):
    """Flux-weighted group average of a callable xs(E) over [glo, ghi] eV (barns)."""
    E = group_nodes(iso, glo, ghi)
    s = np.asarray(xs(E), dtype=float)
    w = phi_ev(E)
    den = _trapz(w, E)
    return float(_trapz(s * w, E) / den) if den > 0 else 0.0


def xs_of(iso, mt):
    """Callable pointwise xs for reaction mt at TEMP, or a zero function if absent."""
    if mt in iso.reactions and TEMP in iso[mt].xs:
        return iso[mt].xs[TEMP]
    return lambda E: np.zeros_like(np.atleast_1d(np.asarray(E, float)))


def inelastic_xs(iso):
    """Total inelastic xs callable: MT=4 if present, else the sum of the discrete/continuum
    levels MT=51..91."""
    if 4 in iso.reactions:
        return xs_of(iso, 4)
    levels = [mt for mt in range(51, 92) if mt in iso.reactions]
    if not levels:
        return lambda E: np.zeros_like(np.atleast_1d(np.asarray(E, float)))

    def total(E):
        E = np.atleast_1d(np.asarray(E, float))
        return sum(np.asarray(iso[mt].xs[TEMP](E), float) for mt in levels)

    return total


def disappearance_xs(iso):
    """Total neutron-DISAPPEARANCE (absorption-minus-fission) xs -> sigma_c: radiative capture
    (n,gamma) MT=102 PLUS the charged-particle-out channels (n,p) 103, (n,d) 104, (n,t) 105,
    (n,3He) 106, (n,alpha) 107. For the a-set actinides these charged-particle channels are ~0
    (sigma_c is unchanged to <1e-6 b, so the a-set + G0a/G0b are essentially untouched); for the
    b-set they are the DOMINANT absorber and MUST be counted -- e.g. B-10, whose absorber role is
    entirely (n,alpha) (~0.22 b @1 MeV, ~2 b @100 keV), NOT (n,gamma) (~6e-5 b). Fission (its own
    channel) and (n,2n) (a multiplication channel) are deliberately excluded (M1-T4a-2b)."""
    fns = [xs_of(iso, mt) for mt in (102, 103, 104, 105, 106, 107)]

    def total(E):
        E = np.atleast_1d(np.asarray(E, float))
        return sum(np.asarray(f(E), float) for f in fns)

    return total


def total_nu(iso):
    """Total nu-bar(E) callable (prompt + delayed), summing the fission neutron products'
    yields. ENDF total nu is MT=452 (ADR-013: fast4 stores TOTAL nu)."""
    prods = [p for p in iso[18].products if p.particle == "neutron"]
    tot = [p for p in prods if p.emission_mode == "total"]
    use = tot if tot else [p for p in prods if p.emission_mode in ("prompt", "delayed")]

    def nu(E):
        E = np.atleast_1d(np.asarray(E, float))
        return sum(np.asarray(p.yield_(E), float) for p in use)

    return nu


def delayed_nu(iso):
    """Delayed nu-bar(E) callable (0 if no delayed data)."""
    dl = [p for p in iso[18].products if p.particle == "neutron" and p.emission_mode == "delayed"]
    if not dl:
        return lambda E: np.zeros_like(np.atleast_1d(np.asarray(E, float)))

    def nud(E):
        E = np.atleast_1d(np.asarray(E, float))
        return sum(np.asarray(p.yield_(E), float) for p in dl)

    return nud


def elastic_mubar_fn(iso):
    """mu_bar_el(E): the mean lab-frame elastic scattering cosine, from the ENDF elastic
    angular distributions (Tabular p(mu)). Forward-peaking rises steeply through the fast
    range (mu_bar ~ 0 at keV -> ~0.9 at 15 MeV) and drives the transport correction, which
    sets leakage in a bare sphere -- the dominant reactivity effect (verified: swapping the
    isotropic-CM 2/(3A) placeholder for this moves Godiva k by ~17000 pcm)."""
    ang = None
    for p in iso[2].products:
        if p.particle == "neutron" and p.distribution:
            ang = getattr(p.distribution[0], "angle", None)
            break
    if ang is None:
        A = iso.mass_number
        return lambda E: np.full_like(np.atleast_1d(np.asarray(E, float)), 2.0 / (3.0 * A))
    Eg = np.asarray(ang.energy, float)
    mub = []
    for m in ang.mu:
        x = np.asarray(m.x, float)
        p = np.asarray(m.p, float)
        d = _trapz(p, x)
        mub.append(float(_trapz(x * p, x) / d) if d > 0 else 0.0)
    mub = np.array(mub)

    def fn(E):
        E = np.atleast_1d(np.asarray(E, float))
        return np.interp(E, Eg, mub, left=mub[0], right=mub[-1])

    return fn


def fission_weighted(iso, f, glo, ghi):
    """integral_g f(E) sigma_f(E) phi(E) / integral_g sigma_f(E) phi(E) -- the fission-rate
    weighting used for nu-bar and the delayed fraction (both are fission observables)."""
    E = group_nodes(iso, glo, ghi)
    sf = np.asarray(xs_of(iso, 18)(E), float)
    w = phi_ev(E) * sf
    den = _trapz(w, E)
    return float(_trapz(np.asarray(f(E), float) * w, E) / den) if den > 0 else 0.0


def _round_sum1(vals, fix):
    """Round each entry to 6 dp but absorb the rounding residual into index `fix`, so the
    list sums to 1.0 within float epsilon (the loader requires rows/chi to sum to 1+/-1e-6)."""
    r = [round(float(x), 6) for x in vals]
    r[fix] = round(1.0 - sum(r[i] for i in range(len(r)) if i != fix), 9)
    return r


def collapse_isotope(iso):
    """Return the fast4 per-isotope dict for one nuclide (all group arrays high->low)."""
    A = iso.mass_number
    fissile = 18 in iso.reactions
    nu_fn = total_nu(iso) if fissile else None
    nud_fn = delayed_nu(iso) if fissile else None

    nu = [0.0] * NG
    sigma_f = [0.0] * NG
    sigma_c = [0.0] * NG
    sigma_s = [0.0] * NG
    sigma_n2n = [0.0] * NG
    mu_bar = [0.0] * NG
    beta_num = 0.0
    beta_den = 0.0

    el = xs_of(iso, 2)
    inel = inelastic_xs(iso)
    cap = disappearance_xs(iso)  # (n,g) + (n,p)/(n,a)/... -- the b-set's (n,a) absorbers (M1-T4a-2b)
    n2n = xs_of(iso, 16)
    fis = xs_of(iso, 18)
    mub_el = elastic_mubar_fn(iso)

    for g in range(NG):
        ghi, glo = BOUNDS_EV[g], BOUNDS_EV[g + 1]
        sigma_f[g] = gavg(iso, fis, glo, ghi) if fissile else 0.0
        sigma_c[g] = gavg(iso, cap, glo, ghi)
        sigma_s[g] = gavg(iso, el, glo, ghi) + gavg(iso, inel, glo, ghi)
        sigma_n2n[g] = gavg(iso, n2n, glo, ghi)
        # nu-bar: fission-rate weighted total nu (0 where non-fissile / no fission rate).
        nu[g] = fission_weighted(iso, nu_fn, glo, ghi) if fissile else 0.0
        # mu_bar for the transport correction sigma_tr = sigma_t - mu_bar*sigma_s. The
        # anisotropy is the ELASTIC one; inelastic is ~isotropic, so it only dilutes:
        # mu_bar_g = <mu_el * sigma_el * phi> / <(sigma_el+sigma_inel) * phi>, giving
        # mu_bar*sigma_s = mu_el*sigma_el (the correct transport-xs reduction).
        E = group_nodes(iso, glo, ghi)
        w = phi_ev(E)
        sel = np.asarray(el(E), float)
        den = _trapz((sel + np.asarray(inel(E), float)) * w, E)
        mu_bar[g] = float(_trapz(mub_el(E) * sel * w, E) / den) if den > 0 else 0.0

    # delayed fraction beta: fission-rate-weighted delayed/total over the whole fast range.
    if fissile:
        Eall = group_nodes(iso, BOUNDS_EV[-1], BOUNDS_EV[0])
        w = phi_ev(Eall) * np.asarray(fis(Eall), float)
        den = _trapz(w, Eall)
        if den > 0:
            beta_num = float(_trapz(np.asarray(nud_fn(Eall), float) * w, Eall))
            beta_den = float(_trapz(np.asarray(nu_fn(Eall), float) * w, Eall))
    beta = (beta_num / beta_den) if beta_den > 0 else 0.0

    # chi (fission emission spectrum) + transfer matrix: distribution-derived, filled by
    # complete_distributions() after the from_hdf5 secondary-distribution API is probed.
    chi = fission_chi(iso) if fissile else [0.0] * NG
    transfer = scatter_transfer(iso)

    chi_out = _round_sum1(chi, int(np.argmax(chi))) if sum(chi) > 0.5 else [round(x, 6) for x in chi]
    return {
        "nu": [round(x, 6) for x in nu],
        "chi": chi_out,
        "sigma_f": [round(x, 6) for x in sigma_f],
        "sigma_c": [round(x, 6) for x in sigma_c],
        "sigma_s": [round(x, 6) for x in sigma_s],
        "sigma_n2n": [round(x, 6) for x in sigma_n2n],
        "mu_bar": [round(x, 6) for x in mu_bar],
        "beta": round(beta, 6),
        "transfer": [_round_sum1(transfer[g], g) for g in range(NG)],
        "cite": f"ENDF/B-VIII.0 (MAT {iso.name}); 4-group collapse, fast-metal weight (tools/xs)",
        "status": "PUBLIC",
    }


# ---- distribution-derived fields: chi + the scattering transfer matrix ----
#
# No openmc.data distribution object exposes .sample(), so secondary spectra are evaluated
# analytically per type. The types seen in ENDF/B-VIII.0: fission chi = MaxwellEnergy /
# WattEnergy / Evaporation / (Continuous)Tabular; discrete inelastic = LevelInelastic
# (deterministic E'); continuum inelastic / (n,2n) = tabulated pdf.

def _group_of(E_ev):
    """Index of the group containing outgoing energy E_ev (0=highest). Clamps to [0,NG-1]."""
    for g in range(NG):
        if E_ev > BOUNDS_EV[g + 1]:
            return g
    return NG - 1


def _emission_group_fracs(edist, Ein):
    """Fraction of secondary neutrons emitted into each group for incident energy Ein,
    from an openmc.data energy distribution. Returns a length-NG array summing to ~1."""
    name = type(edist).__name__
    lo, hi = BOUNDS_EV[-1], BOUNDS_EV[0]
    Eo = np.logspace(np.log10(lo), np.log10(hi), 4000)

    if name == "LevelInelastic":  # deterministic discrete-level outgoing energy
        E_out = float(edist.mass_ratio) * (Ein - float(edist.threshold))
        f = np.zeros(NG)
        if E_out > 0:
            f[_group_of(E_out)] = 1.0
        return f

    if name in ("MaxwellEnergy", "Evaporation"):
        theta = float(edist.theta(Ein))
        u = float(getattr(edist, "u", 0.0))
        emax = max(Ein - u, 0.0)
        shape = np.sqrt(Eo) if name == "MaxwellEnergy" else Eo
        p = np.where((Eo > 0) & (Eo <= emax), shape * np.exp(-Eo / theta), 0.0)
    elif name == "WattEnergy":
        a = float(edist.a(Ein))
        b = float(edist.b(Ein))
        u = float(getattr(edist, "u", 0.0))
        emax = max(Ein - u, 0.0)
        p = np.where((Eo > 0) & (Eo <= emax), np.exp(-Eo / a) * np.sinh(np.sqrt(b * Eo)), 0.0)
    else:
        # tabulated: ContinuousTabular / CorrelatedAngleEnergy (per-incident Tabular).
        p = _tabulated_pdf(edist, Ein, Eo)
        if p is None:
            return np.zeros(NG)

    tot = _trapz(p, Eo)
    if tot <= 0:
        return np.zeros(NG)
    f = np.zeros(NG)
    for g in range(NG):
        ghi, glo = BOUNDS_EV[g], BOUNDS_EV[g + 1]
        m = (Eo >= glo) & (Eo <= ghi)
        if m.sum() >= 2:
            f[g] = _trapz(p[m], Eo[m]) / tot
    s = f.sum()
    return f / s if s > 0 else f


def _energy_dist(angle_energy):
    """The energy-distribution to evaluate for an AngleEnergy product.
    UncorrelatedAngleEnergy carries .energy = an EnergyDistribution (Maxwell / Continuous
    Tabular / LevelInelastic). CorrelatedAngleEnergy carries .energy = the incident grid and
    .energy_out = per-incident outgoing dists -- so the object ITSELF is what _tabulated_pdf
    consumes (via .energy + .energy_out)."""
    d = angle_energy
    # Correlated forms (CorrelatedAngleEnergy, KalbachMann) carry the outgoing spectrum on
    # the object (.energy = incident grid, .energy_out = per-incident Tabular).
    if type(d).__name__ in ("CorrelatedAngleEnergy", "KalbachMann"):
        return d
    return getattr(d, "energy", None)


def _tabulated_pdf(edist, Ein, Eo):
    """p(E'|Ein) sampled onto Eo for tabulated outgoing distributions; None if unhandled."""
    # ContinuousTabular: .energy (incident grid) + .energy_out (list of 1-D distributions)
    if hasattr(edist, "energy") and hasattr(edist, "energy_out"):
        Ein_grid = np.asarray(edist.energy, float)
        i = int(np.clip(np.searchsorted(Ein_grid, Ein) - 1, 0, len(edist.energy_out) - 1))
        d = edist.energy_out[i]
        x = np.asarray(getattr(d, "x", getattr(d, "_x", [])), float)
        p = np.asarray(getattr(d, "p", getattr(d, "_p", [])), float)
        if len(x) >= 2:
            return np.interp(Eo, x, p, left=0.0, right=0.0)
    # a flat Tabular / ArbitraryTabulated
    x = np.asarray(getattr(edist, "x", getattr(edist, "energy", [])), float)
    p = np.asarray(getattr(edist, "p", getattr(edist, "pdf", [])), float)
    if len(x) >= 2 and len(p) == len(x):
        return np.interp(Eo, x, p, left=0.0, right=0.0)
    return None


def _fission_neutron_edist(iso):
    """The prompt (else total) fission neutron product's energy distribution, or None."""
    prods = [p for p in iso[18].products if p.particle == "neutron"]
    order = [p for p in prods if p.emission_mode == "prompt"] or \
            [p for p in prods if p.emission_mode == "total"] or prods
    for p in order:
        for d in p.distribution:
            e = _energy_dist(d)
            if e is not None:
                return e
    return None


def fission_chi(iso):
    """chi_g: fraction of fission neutrons born into group g, incident-averaged over the
    fast fission rate sigma_f(E)phi(E)."""
    edist = _fission_neutron_edist(iso)
    if edist is None:
        return [0.0] * NG
    # fission-rate-weighted incident energies across the fast range
    Ein = group_nodes(iso, BOUNDS_EV[-1], BOUNDS_EV[0])
    wr = phi_ev(Ein) * np.asarray(xs_of(iso, 18)(Ein), float)
    acc = np.zeros(NG)
    wsum = 0.0
    for e, w in zip(Ein, wr):
        if w <= 0:
            continue
        f = _emission_group_fracs(edist, float(e))
        if f is not None:
            acc += w * f
            wsum += w
    chi = acc / wsum if wsum > 0 else acc
    s = chi.sum()
    return list(chi / s) if s > 0 else list(chi)


def scatter_transfer(iso):
    """transfer[from][to]: group-to-group scatter probability (rows sum to 1, no upscatter).
    Elastic (MT2) downscatters by FINITE-A kinematics: an s-wave (isotropic-CM) collision sends the
    outgoing energy uniformly into [alpha*E, E], alpha = ((A-1)/(A+1))^2. Heavy A -> alpha ~ 1 -> it
    is ~within-group (the a-set actinides, unchanged to rounding); light A moderates -- H-1 (alpha=0)
    is pure downscatter, which the old heavy-A within-group shortcut dropped (M1-T4a-2b). Inelastic
    discrete (MT51-90, LevelInelastic) + continuum (MT91) also downscatter. (n,2n) is excluded -- it
    is the separate sigma_n2n channel."""
    inel = [mt for mt in list(range(51, 91)) + [91] if mt in iso.reactions]
    inel_edist = {}
    for mt in inel:
        for p in iso[mt].products:
            if p.particle == "neutron" and p.distribution:
                e = _energy_dist(p.distribution[0])
                if e is not None:
                    inel_edist[mt] = e
                break

    A = iso.mass_number
    alpha = ((A - 1.0) / (A + 1.0)) ** 2  # min fraction of E retained in one elastic collision
    # The isotropic-CM downscatter [alpha*E, E] is accurate only where fast elastic is ~isotropic,
    # i.e. LIGHT nuclei (A < kElasticDownscatterA). Heavy nuclei are forward-peaked (mu_bar -> 1), so
    # elastic loses almost no energy and the within-group treatment is the better approximation --
    # applying isotropic downscatter there would OVERESTIMATE it. Keeping the split also leaves the
    # heavy a-set (U/Pu/Ga) elastic byte-identical to the committed set, so G0a/G0b/G0c are untouched.
    # The exact anisotropic elastic-downscatter kernel (from the ENDF p(mu)) is a documented follow-up.
    light = A < kElasticDownscatterA
    span = None
    T = np.zeros((NG, NG))
    for g in range(NG):
        ghi, glo = BOUNDS_EV[g], BOUNDS_EV[g + 1]
        Ef = group_nodes(iso, glo, ghi)
        el_rate = np.asarray(xs_of(iso, 2)(Ef), float) * phi_ev(Ef)
        if not light:
            # heavy: forward-peaked elastic stays within-group (reproduces the a-set exactly).
            T[g, g] += float(_trapz(el_rate, Ef))
        else:
            # light: elastic downscatter, outgoing E' ~ uniform in [alpha*E, E] (isotropic CM).
            span = Ef * (1.0 - alpha)
            for gp in range(g, NG):
                b2, a2 = BOUNDS_EV[gp], BOUNDS_EV[gp + 1]  # group gp spans (a2, b2] eV, hi->lo
                ov = np.clip(np.minimum(Ef, b2) - np.maximum(alpha * Ef, a2), 0.0, None)
                frac = np.where(span > 0.0, ov / np.where(span > 0.0, span, 1.0), 0.0)
                T[g, gp] += float(_trapz(el_rate * frac, Ef))
        # inelastic downscatter: coarse grid is enough (inelastic xs is smooth, no
        # resonances). Same trapz convention as elastic so the ratio is consistent.
        Ec = np.logspace(np.log10(glo), np.log10(ghi), 80)
        wc = phi_ev(Ec)
        for mt, edist in inel_edist.items():
            rate = np.asarray(xs_of(iso, mt)(Ec), float) * wc
            if rate.max() <= 0:
                continue
            fmat = np.array([_emission_group_fracs(edist, float(e)) if r > 0 else np.zeros(NG)
                             for e, r in zip(Ec, rate)])
            for gp in range(NG):
                T[g, gp] += float(_trapz(rate * fmat[:, gp], Ec))
        # no upscatter, then row-normalize to a probability
        T[g, :g] = 0.0
        s = T[g].sum()
        T[g] = T[g] / s if s > 0 else np.eye(NG)[g]
    return T.tolist()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hdf5", required=True, help="dir with <Nuclide>.h5 files")
    ap.add_argument("--out", required=True, help="output fast4.json")
    ap.add_argument("--isotopes", default=",".join(ASET))
    args = ap.parse_args()

    isotopes = args.isotopes.split(",")
    out = {
        "schema_version": 2,
        "name": "fast4",
        "group_bounds_MeV": BOUNDS_MEV,
        "isotopes": {},
    }
    for name in isotopes:
        path = os.path.join(args.hdf5, f"{name}.h5")
        iso = openmc.data.IncidentNeutron.from_hdf5(path)
        print(f"-- collapsing {name} (A={iso.mass_number}, MTs incl fission={18 in iso.reactions})")
        out["isotopes"][name] = collapse_isotope(iso)

    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"wrote {args.out} with {len(out['isotopes'])} isotopes")


if __name__ == "__main__":
    main()

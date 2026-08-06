#!/usr/bin/env python3
"""De-risk 'finer fast groups close the ADR-022 residual' BEFORE the engine arc. Reuses
build_fast4's collapse (monkeypatched bounds) to build Godiva few-group data at 4/8/16/32
fast groups, then a multigroup infinite-medium + buckling-leakage k. The geometric buckling B^2
is tuned once so the CURRENT 4-group keff == the MEASURED 1.0256, then held fixed (D_g adapts per
structure) so the ONLY thing changing is the group STRUCTURE. If keff drops toward 1.000 with
more groups -> finer structure closes the residual (do the arc). If it plateaus near 1.0256 ->
the residual is weighting, not structure (finer groups won't help; reconsider)."""
import sys, json
sys.path.insert(0, "tools/xs")
import numpy as np
import openmc.data
import build_fast4 as bf
from scipy.optimize import brentq

HDF5 = "/root/nukesim_hdf5/endfb80-lowtemp/neutron"
ISOS = ["U235", "U238", "U234"]
M = {"U235": 235.0439, "U238": 238.0508, "U234": 234.0410}

mat = json.load(open("data/materials/u_godiva.json"))
af = mat["isotopes"]
Mavg = sum(af[i] * M[i] for i in af)
Ntot = mat["density_g_cm3"] * 6.02214076e23 / Mavg * 1e-24
nd = {i: Ntot * af[i] for i in af}
print("Godiva N (a/b-cm):", {i: round(nd[i], 6) for i in nd}, " Ntot=%.5f" % Ntot)


def collapse_at(bounds_mev):
    bf.BOUNDS_MEV = list(bounds_mev)
    bf.BOUNDS_EV = [b * 1e6 for b in bounds_mev]
    bf.NG = len(bounds_mev) - 1
    data = {}
    for n in ISOS:
        iso = openmc.data.IncidentNeutron.from_hdf5(f"{HDF5}/{n}.h5")
        data[n] = bf.collapse_isotope(iso)
    return data, bf.NG


def kcalc(data, NG, B2):
    St = np.zeros(NG); Str = np.zeros(NG)
    Smat = np.zeros((NG, NG)); Fmat = np.zeros((NG, NG))
    for n, d in data.items():
        Ni = nd[n]
        sf = np.array(d["sigma_f"]); sc = np.array(d["sigma_c"])
        ss = np.array(d["sigma_s"]); s2 = np.array(d["sigma_n2n"])
        mub = np.array(d["mu_bar"]); nu = np.array(d["nu"]); chi = np.array(d["chi"])
        tr = np.array(d["transfer"])
        st = sf + sc + ss + s2
        St += Ni * st
        Str += Ni * (st - mub * ss)
        Smat += Ni * ss[:, None] * tr      # [from][to]
        Fmat += np.outer(chi, Ni * nu * sf)  # [to][from'']
    D = 1.0 / (3.0 * np.maximum(Str, 1e-9))
    kinf = max(np.linalg.eigvals(np.linalg.solve(np.diag(St) - Smat.T, Fmat)).real)
    keff = max(np.linalg.eigvals(np.linalg.solve(np.diag(St + D * B2) - Smat.T, Fmat)).real)
    return float(kinf), float(keff)


print("collapsing CURRENT 4-group [20,3,1,0.1,1e-3] + tuning B2 to the measured keff=1.0256 ...")
d4, NG4 = collapse_at([20.0, 3.0, 1.0, 0.1, 1e-3])
kinf4, _ = kcalc(d4, NG4, 0.0)
B2 = brentq(lambda b2: kcalc(d4, NG4, b2)[1] - 1.0256, 1e-6, 1.0)
print("  4-group(current): kinf=%.4f  tuned B2=%.5f (keff=1.0256, matches measured)\n" % (kinf4, B2))
print("%-28s %8s %10s %10s" % ("structure (fast groups)", "kinf", "keff@B2", "vs bench"))
print("%-28s %8.4f %10.4f %+9.0f pcm" % ("current-4 [20,3,1,.1,1e-3]", kinf4, 1.0256, 2560))
for N in [4, 8, 16, 32]:
    bounds = list(np.geomspace(20.0, 1e-3, N + 1))
    dN, NGN = collapse_at(bounds)
    kinf, keff = kcalc(dN, NGN, B2)
    print("%-28s %8.4f %10.4f %+9.0f pcm" % ("log-%d [1e-3,20]MeV" % N, kinf, keff, (keff - 1.0) * 1e5))
print("\nREAD: if keff@B2 falls toward 1.000 as N grows -> finer STRUCTURE closes the residual.")
print("If it plateaus near 1.026 -> the residual is WEIGHTING, and finer groups alone won't fix it.")

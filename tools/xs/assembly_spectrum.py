#!/usr/bin/env python3
"""Compute each bare critical assembly's SELF-CONSISTENT fundamental-mode flux
spectrum phi(E) via openmc continuous-energy k-eigenvalue transport (M1-T4a-5,
ADR-025). This phi(E) is the physically-correct multigroup collapse weight for the
isotopes that assembly benchmarks -- ADR-022 path (a) / tools/xs/README's iteration
rule: "compute the assembly flux, re-weight". openmc is used ONLY as a high-fidelity
flux-SHAPE calculator; it never touches a cross section, and our own multigroup
engine (ref/gpu) still computes k -- so this is NOT gate-tuning (the weight is the
assembly's real flux, whatever it is; openmc's own k comes out ~1.000, confirming
the geometry/composition, but that k is never copied).

Writes data/xs/weights/<assembly>.spectrum.json = the committed method-of-record
weight (edges + flux-per-eV + provenance). SEEDED for reproducibility within the
WSL omc env (openmc 0.15.3). Run in that env:

  python tools/xs/assembly_spectrum.py --out data/xs/weights [--which godiva,jezebel]
"""
import argparse
import json
import os

import numpy as np
import openmc

XS = "/root/nukesim_hdf5/endfb80-lowtemp/cross_sections.xml"
REPO = "/mnt/c/NUCLEAR"

# radius = the benchmark critical radius from data/scenarios/<name>.toml (cited there).
ASSEMBLIES = {
    "godiva":  {"mat": "u_godiva",      "r_cm": 8.741},   # CSEWG F5 (JEFF Report 16 Annex 3)
    "jezebel": {"mat": "pu_ga_jezebel", "r_cm": 6.385},   # CSEWG F1
}


def build_material(mat_name):
    with open(f"{REPO}/data/materials/{mat_name}.json") as fh:
        m = json.load(fh)
    mat = openmc.Material(name=mat_name)
    mat.set_density("g/cm3", m["density_g_cm3"])
    for iso, frac in m["isotopes"].items():
        mat.add_nuclide(iso, float(frac), "ao")  # OUR OWN composition (atom fractions)
    return mat, m


def run_assembly(name, cfg, workdir, particles, inactive, active, nbins, seed):
    openmc.config["cross_sections"] = XS
    mat, mjson = build_material(cfg["mat"])
    materials = openmc.Materials([mat])

    sph = openmc.Sphere(r=cfg["r_cm"], boundary_type="vacuum")
    cell = openmc.Cell(fill=mat, region=-sph)
    geom = openmc.Geometry([cell])

    edges = np.logspace(np.log10(1.0e-4), np.log10(20.0e6), nbins + 1)  # eV, fine log grid
    efilter = openmc.EnergyFilter(edges)
    tally = openmc.Tally(name="flux")
    tally.filters = [efilter]
    tally.scores = ["flux"]
    tallies = openmc.Tallies([tally])

    settings = openmc.Settings()
    settings.run_mode = "eigenvalue"
    settings.particles = particles
    settings.batches = inactive + active
    settings.inactive = inactive
    settings.seed = seed                       # reproducible within this openmc version/env
    settings.temperature = {"default": 293.6}  # the assemblies are at room temperature
    settings.source = openmc.IndependentSource(space=openmc.stats.Point((0.0, 0.0, 0.0)))
    settings.output = {"tallies": False, "summary": False}
    settings.verbosity = 4

    model = openmc.Model(geometry=geom, materials=materials, settings=settings, tallies=tallies)
    os.makedirs(workdir, exist_ok=True)
    sp_path = model.run(cwd=workdir)
    with openmc.StatePoint(sp_path) as sp:
        flux = np.asarray(sp.get_tally(name="flux").mean).ravel()  # integrated flux per bin
        keff = sp.keff

    de = np.diff(edges)
    phi_per_ev = flux / de                       # flux per unit energy (the collapse weight)
    emid = np.sqrt(edges[:-1] * edges[1:])
    tot = flux.sum()
    return {
        "schema": "assembly-spectrum-1",
        "assembly": name, "material": cfg["mat"], "r_cm": cfg["r_cm"],
        "isotopes": mjson["isotopes"],
        "openmc_version": openmc.__version__,
        "library": "ENDF/B-VIII.0 (endfb80-lowtemp, 293.6 K)",
        "particles": particles, "inactive": inactive, "active": active, "seed": seed,
        "k_openmc": float(keff.n), "k_openmc_std": float(keff.s),
        "flux_frac_above_1MeV": float(flux[emid >= 1.0e6].sum() / tot),
        "median_flux_energy_eV": float(np.interp(0.5, np.cumsum(flux) / tot, emid)),
        "edges_ev": [float(x) for x in edges],
        "phi_per_ev": [float(x) for x in phi_per_ev],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="data/xs/weights")
    ap.add_argument("--which", default="godiva,jezebel")
    ap.add_argument("--particles", type=int, default=80000)
    ap.add_argument("--inactive", type=int, default=50)
    ap.add_argument("--active", type=int, default=200)
    ap.add_argument("--nbins", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=20260806)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for name in args.which.split(","):
        cfg = ASSEMBLIES[name]
        print(f"== {name}: bare {cfg['mat']} sphere r={cfg['r_cm']} cm (seed {args.seed}) ==")
        res = run_assembly(name, cfg, workdir=f"/tmp/asm_{name}",
                           particles=args.particles, inactive=args.inactive,
                           active=args.active, nbins=args.nbins, seed=args.seed)
        print(f"  k_openmc={res['k_openmc']:.5f}+/-{res['k_openmc_std']:.5f}  "
              f"frac>1MeV={res['flux_frac_above_1MeV']*100:.1f}%  "
              f"median={res['median_flux_energy_eV']/1e6:.3f} MeV")
        outp = os.path.join(args.out, f"{name}.spectrum.json")
        with open(outp, "w") as fh:
            json.dump(res, fh)
        print(f"  wrote {outp}")


if __name__ == "__main__":
    main()

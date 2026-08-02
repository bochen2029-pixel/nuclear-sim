# The Trinity "Gadget" & "Fat Man" Implosion Device: A Technical & Historical Reference for a 3D GPU Criticality/Fission Simulator

## TL;DR
- Every layer of the 1945 plutonium implosion device is documented in public/declassified sources with enough numerical precision (core 9.17 cm / 6.15 kg Pu-Ga, ~22.9 cm U-238 tamper, ~47 cm Al pusher, 32-lens truncated-icosahedron HE assembly, ~145 cm duralumin sphere) to build a physically faithful concentric-sphere 3D model — but several key numbers are reconstructed (Coster-Mullen) rather than officially released, and the exact peak k-effective remains classified.
- The physics you need is fully public: fast-fission cross-sections (Pu-239 ~1.8 b), ν≈2.9, the ~10 ns "shake" generation time, ~80 generations to full yield, exponential neutron growth e^((k−1)t/g), ~2× compression to 3–4 critical masses, ~15–17% Pu burn-up, and ~17.74 kt/kg (U-235) to 18.29 kt/kg (Pu-239) fully fissioned — all traceable to the Los Alamos Primer, the Nuclear Weapon Archive, and Wellerstein.
- Your architecture is directly precedented: the UC Berkeley **WARP** code already does 3D continuous-energy Monte Carlo neutron transport on GPUs using **NVIDIA OptiX** ray-tracing for BVH geometry + delta/Woodcock tracking; combine a CUDA event-based neutron kernel with an OptiX render pass for the fireball, and expose Wellerstein-style parameters in true 3D.

---

## Key Findings

1. **The device is a set of nested spheres plus a firing system.** From the center outward: polonium-beryllium "Urchin" initiator → solid Pu-239/Ga δ-phase core → natural-uranium tamper → boron-plastic shell → aluminum "pusher" → HE booster shell → 32 explosive lenses → duralumin case → (for Fat Man) the Y-1561 ballistic bomb casing. [PUBLIC HISTORICAL FACT / RECONSTRUCTED]

2. **The solid "Christy" core was a deliberate conservative choice.** Facing the Pu-240 predetonation crisis and implosion-asymmetry risk, Robert Christy proposed (Sept 1944) a solid rather than hollow pit; less efficient but far more reliable in the available time. [PUBLIC HISTORICAL FACT]

3. **Implosion was chosen for plutonium because of Pu-240 spontaneous fission.** Reactor-bred plutonium contains Pu-240, whose high spontaneous-fission neutron rate would cause a gun-type weapon to "fizzle" via predetonation; only fast implosion assembly (~microseconds) avoids this. [PUBLIC HISTORICAL FACT]

4. **The math and physics are undergraduate-accessible and declassified.** The Los Alamos Primer (LA-1, Serber) lays out critical mass, multiplication, efficiency; the Bethe-Feynman efficiency formula's basic form (yield ∝ R²·excess-criticality) is public though implementation coefficients remain classified. [DECLASSIFIED TECHNICAL]

5. **A GPU/RTX architecture is proven.** WARP (Bergmann/Vujić, UC Berkeley) demonstrated OptiX-based BVH ray tracing for neutron transport at run times 11–82× lower than CPU MCNP/Serpent in its thesis benchmark; Shift, OpenMC-GPU, PRAGMA, GUARDYAN, and Mercury are other GPU Monte Carlo precedents. [DECLASSIFIED TECHNICAL / PUBLIC]

---

## Details

### 1. Historical Context and Timeline [PUBLIC HISTORICAL FACT]

- **Los Alamos founded:** Project Y established as a secret laboratory at Los Alamos, New Mexico, spring 1943; J. Robert Oppenheimer was its scientific director. Serber delivered the indoctrination lectures that became the Los Alamos Primer (LA-1) in April 1943.
- **The Primer's stated object:** "to produce a practical military weapon in the form of a bomb in which the energy is released by a fast neutron chain reaction in one or more of the materials known to show nuclear fission."
- **Implosion origin:** Seth Neddermeyer instigated small-scale implosion experiments during Los Alamos' first year, working in relative obscurity while the gun method had priority under William Parsons.
- **Von Neumann's intervention (Sept/late 1943):** John von Neumann, invited by Oppenheimer, examined Neddermeyer's tests and argued that high-explosive implosion could assemble a subcritical mass faster and with less material than the gun method, also compressing the metal to higher density (the compression insight came from Edward Teller). This galvanized the lab.
- **Kistiakowsky:** George Kistiakowsky (Harvard physical chemist) was brought in as a consultant in October 1943 and by June 15, 1944 replaced Neddermeyer as head of the explosives effort; James Tuck contributed the explosive-lens "focusing" concept. Kistiakowsky later insisted "the real invention should be given full credit to [Seth] Neddermeyer."
- **Christy core (Sept 1944):** Robert Christy, in the theoretical implosion group, proposed the solid-pit design that was adopted, tested at Trinity, and used at Nagasaki. The device and pit were nicknamed the "Christy Gadget"/"Christy pit."
- **Espionage:** Klaus Fuchs (a key developer) and Theodore Hall independently passed implosion design information to the Soviets; David Greenglass passed lens information. This is why RDS-1 (Joe-1) closely copied the design, and why so much emerged publicly post-1991.
- **Trinity test:** July 16, 1945, 5:29:45 a.m. Mountain War Time, at the Trinity site in the Jornada del Muerto desert on the Alamogordo Bombing Range, atop a 100-foot steel tower. ~425 people present that weekend; observers included Fermi, Bethe, Bush, Chadwick.
- **Yield:** T-Division predicted 5–10 kt; Fermi's on-the-spot estimate ~10 kt; radiochemical analysis gave ~18.6 kt; the modern DOE official figure is **21 kt**. A recent peer-reviewed reassessment (Selby et al., "A New Yield Assessment for the Trinity Nuclear Test, 75 Years Later," *Nuclear Technology* 207, S321–S325, Dec 2021) reached "a final yield determination of 24.8 ± 2 kt TNT equivalent, substantially higher than the previous U.S. Department of Energy released value of 21 kt." Fireball temperature reached ~8,430 K (hotter than the Sun's surface). The Gadget was hauled up the tower by electric winch over a truckload of mattresses.
- **Nagasaki:** Fat Man (Y-1561, unit F-31) dropped from B-29 *Bockscar* on August 9, 1945; ~21 kt.

### 2. Physical Layout — Concentric Layers

Primary authoritative public source: Carey Sublette's **Nuclear Weapon Archive** (NWFAQ §8.1.1), which draws on the declassified LA-3067 (Paxton, "Los Alamos Critical Mass Data," 1964) and John Coster-Mullen's reconstruction. Numbers below are outside-diameter (OD) unless noted. [DECLASSIFIED TECHNICAL where from LA-3067; otherwise RECONSTRUCTED/INFERRED FROM OPEN SOURCES]

| Component | Dimension (OD) | Mass | Notes |
|---|---|---|---|
| Neutron initiator ("Urchin") | 2.0 cm | ~7 g | Po-210 + Be, hollow Be shell + inner pellet, 15 grooves |
| Initiator cavity | 2.1 cm | — | in center of pit |
| Plutonium core | 9.17 cm (3.61 in) | 6.15 kg | δ-phase Pu-Ga, solid, two hemispheres |
| Uranium tamper shell | 22.86 cm (9.0 in) | ~108–111 kg | natural U (U-238) |
| Boron-plastic shell | 23.50 cm (9.25 in) | — | ~0.32 cm (1/8 in) B-10 in acrylic |
| Aluminum pusher shell | 46.99 cm (18.5 in) | ~128–130 kg | ~12 cm thick |
| Inner HE booster shell | 92.075 cm (36.25 in) | ~608 kg | Comp B |
| Explosive lens layer | 137.8 cm (54.25 in) | ~1,800–1,900 kg | 32 lenses |
| Cork liner | 140.3 cm (55.25 in) | — | |
| Duralumin case | 145.4 cm (57.25 in) | — | "1561" 2-cap + 5-segment design, 90 bolts |
| Fat Man ballistic case | 152–153 cm ID/OD (~59.875–60.25 in) | — | full bomb ~10,300 lb, 12 ft long |

**Modulated neutron initiator ("Urchin"):** ~2 cm Be sphere with 50 curies (11 mg) of Po-210 deposited in 15 grooves; gold+nickel plating separates the Po from the Be. Arrival of the implosion shock collapses the grooves, creating Munroe-effect jets that mix Po and Be; the α-particles from Po-210 strike Be nuclei via the (α,n) reaction, producing a neutron burst (~one every 5–10 ns) timed to peak compression. [DECLASSIFIED TECHNICAL]

**Plutonium pit:** 6.15 kg δ-phase Pu alloyed with 3.35% gallium (molar; ~1% by weight), density ~15.6 g/cm³, stabilized against phase change and corrosion. Two hemispheres (HS-1, HS-2), originally silver-plated then gold-leafed for Gadget (nickel-plated after Trinity). A ~0.1 mm gold gasket between hemispheres blocked shock-wave jets that could prematurely fire the initiator. Super-grade plutonium: only 1.0% Pu-240 due to short (~150-day) Hanford irradiation. Pit was ~78% of a critical mass before implosion (with tamper reflection); a cadmium wire added safety margin. Emitted ~15 W of decay heat, warm to the touch. [DECLASSIFIED TECHNICAL / RECONSTRUCTED]

**U-238 tamper:** natural-uranium shell ~6.56 cm thick, ~108–111 kg. Functions: (a) inertial confinement (delays disassembly), (b) neutron reflection back into core, (c) contributes ~20% of total yield via fast fission of U-238. Constructed as a solid sphere with a central cylindrical "tamper plug" (two halves) so the pit could be inserted as a final assembly step at the McDonald Ranch House on July 13, 1945. [DECLASSIFIED TECHNICAL / RECONSTRUCTED]

**Aluminum pusher:** ~12 cm thick, ~128–130 kg, density ratio ~1.64 to the explosive. Slows and partially reflects the shock, reducing Rayleigh-Taylor/Taylor-instability irregularities and sharpening the implosion wave. [RECONSTRUCTED]

**Boron-plastic shell:** thin (~0.32 cm) B-10-loaded acrylic that captures slow neutrons moderated by the surrounding HE, reducing predetonation-causing background in the pit. [DECLASSIFIED TECHNICAL]

**High-explosive lens assembly:** 32 lenses (20 hexagonal + 12 pentagonal) in a truncated-icosahedron ("soccer ball") pattern. Each lens pairs fast **Composition B** (60% RDX / 39% TNT / 1% wax; detonation velocity ~7.9 km/s) with slow **Baratol** (barium nitrate + TNT, ~4.9 km/s). The slow-explosive insert shapes the initially convex (diverging) detonation wave into a concave (converging) spherical wave; the 32 waves merge into one smooth spherical implosive wave. Fit tolerance ~1/32 in (0.8 mm); HE detonation symmetry tolerance ~5%. Full HE assembly ~2,400 kg. [DECLASSIFIED TECHNICAL / RECONSTRUCTED]

**Detonators and X-Unit:** 32 exploding-bridgewire (EBW) detonators (they used redundant detonators/cabling — 64 total), fired within ±10 ns simultaneity by the **X-Unit**, a ~180 kg (400 lb) capacitor-discharge firing set using a cascade of spark-gap switches. Developed under Luis Alvarez / Donald Hornig. [DECLASSIFIED TECHNICAL]

**Casing / ballistics:** The simpler "1561" spherical duralumin shell (two polar caps + five equatorial segments, 90 bolts, ~1 in aluminum) replaced the bolt-heavy "1222" icosahedral-steel design. Fat Man's Y-1561 ballistic case: 60 in diameter, ~12 ft long, ~10,300 lb, fuzed by four "Archie" tail radars set to ~1,850 ft (±100 ft) with a barometric fail-safe below 7,000 ft; the "California Parachute" boxed tail fixed a wobble. Delivered only by Silverplate B-29s (~7,200 lb lighter than standard, no armor, single-point release). [PUBLIC HISTORICAL FACT]

### 3. The Physics [DECLASSIFIED TECHNICAL]

- **Fast-fission cross-sections (fission-spectrum, ~1 MeV region):** Pu-239 ~1.7–1.8 b (ENDF/B-VII.1 gives ~1.72 b above 10 keV; a 1943 Los Alamos measurement gave 2.18 b at 650 keV); U-235 ~1.2 b; U-238 (fast) ~0.5 b with a threshold near ~1 MeV. Thermal values (not used in a fast bomb) are far larger (Pu-239 ~750 b). Public data: ENDF/B, JEFF-3.x via NNDC/JANIS.
- **Neutrons per fission (ν):** Pu-239 ≈ 2.9 (thermal ν̄ ≈ 2.89; fast is slightly higher); U-235 ≈ 2.4.
- **Energy per fission:** ~200 MeV total, ~180 MeV prompt (~170 MeV appears in some Los Alamos-derived summaries as prompt-deposited).
- **Prompt neutron generation time ("shake"):** ~10 ns; neutron speed ~1.4×10⁹ cm/s at ~1 MeV; fission mean free path ~13 cm at normal density (Pu-239 ~12.7 cm), scattering MFP ~2.5 cm (≈5 scatters before fission).
- **Chain-reaction growth:** neutron population and cumulative fissions grow as **e^((k−1)·t/g)**, where g is generation time. ~80 generations (doublings if ν-driven) yield ~6×10²³ fissions (a mole); with k≈2 a single neutron reaches ~2×10²⁴ fissions in ~56 shakes (~560 ns) ≈ 20 kt.
- **Critical mass:** per Wellerstein ("Critical mass," Restricted Data, Apr 10, 2015): "The bare sphere critical mass of plutonium-239 is 10 kg. The Nagasaki bomb contained 6.2 kg of plutonium… Increase its density by 2.5X through the careful application of high explosives, however, and suddenly that is at least one critical mass." With a good tamper/reflector the bare figure drops to ~5–6 kg. (Serber's Primer values were deliberately uncertain and later refined by the Godiva/Jezebel bare-metal criticality benchmarks.)
- **Multiplication factor:** k_eff = f − (l_c + l_e) (production minus capture and leakage losses). Implosion drives the assembly from sub-critical (~0.9; pit ~78% critical) to well above prompt-critical.
- **Rossi alpha:** α = (k−1)/τ — the exponential growth rate constant; a standard public criticality diagnostic (measured on Godiva/Jezebel).
- **Compression & criticality:** implosion compresses the pit to **over twice normal density**, converting a ~78%-critical solid into **3–4 critical masses** ("A two-fold compression will boost a slightly sub-critical solid mass to nearly four critical masses" — Sublette, NWFAQ §2).
- **Efficiency (Trinity/Fat Man):** Alex Wellerstein ("Kilotons per kilogram," Restricted Data, Dec 23, 2013): "Pu-239 releases around 19 kilotons per kilogram that completely fissions, so that means that around 15% of the Fat Man core (a little under 1 kg of plutonium) underwent fission." Elsewhere Wellerstein states "Fat Man had an efficiency of 17% or so." So ~15–17% of the plutonium fissioned — roughly 1 kg of the ~6.2 kg core, with ~1 gram of mass converted to energy for the ~21 kt yield. Separately, ~20% of the *total* yield came from fast fission of the U-238 tamper (a distinct quantity, frequently conflated with the burn-up fraction).
- **Kilotons per kilogram (rule of thumb):** Per R. Sher & C. Beck, "Fission Energy Release for 16 Fissioning Nuclides" (NP-1771, Stanford, March 1981), as tabulated in NWFAQ §12: "Fission of U-235: 17.74 kt/kg… Fission of Pu-239: 18.29 kt/kg." Theoretical maxima (Theodore B. Taylor, *Scientific American*, April 1987): ~17 kt/kg for fission, ~50 kt/kg for fusion.
- **Bethe-Feynman efficiency formula:** basic public form efficiency ∝ (bc)² and yield ∝ R²·(excess criticality); Serber's Primer version f ~ (1/6)(v′²/ετ²)R_c²Δ with Δ=2(R₂−R₀)/R_c. The scaling insight (double the radius → ~10× efficiency) is public; exact numerical coefficients remain classified.
- **Detonation velocities:** Comp B ~7.9 km/s; Baratol ~4.9 km/s. Implosion inward velocity of the metal is on the order of a few km/s.

### 4. The Mathematics of Implosion [DECLASSIFIED TECHNICAL / public applied math]

- **Spherical convergence & compression:** density scales as (r₀/r)³ for spherical volume compression; the converging shock concentrates energy as radius shrinks.
- **Guderley self-similar solution (1942):** the canonical self-similar converging-shock solution to the spherical Euler equations, with shock position R_s(t) ∝ |t|^α (α from numerical integration of the Euler equations); velocity and temperature diverge at focus while density stays bounded. This is the analytic backbone for modeling the imploding shock.
- **Rayleigh-Taylor instability:** grows at interfaces when a lighter medium accelerates a heavier one (e.g., HE→dense tamper). This is *why* the aluminum pusher and precise lens tolerances matter — to suppress perturbation growth that would spoil symmetry. Related: Richtmyer-Meshkov (shock-driven) instability.
- **Chapman-Jouguet detonation theory:** governs the steady detonation-wave state in the HE; the CJ condition sets detonation velocity and pressure for Comp B / Baratol.
- **Lens geometry:** the truncated icosahedron (32 faces, 60 vertices, 90 edges, icosahedral symmetry I_h) converts 32 point initiations into a near-spherical converging front; the slow-explosive lens element does the wavefront "refraction."
- **Timing tolerance:** sub-microsecond; EBW/X-Unit simultaneity ~±10 ns; overall implosion symmetry tolerance ~5%.

### 5. Simulation & Computational History [PUBLIC / DECLASSIFIED TECHNICAL]

- **Historical computing:** Los Alamos T-Division used human "computers" (T-5) and IBM punched-card accounting machines (PCAM; T-6), organized by Stanley Frankel and Eldred Nelson; Richard Feynman and Nicholas Metropolis ran the famous human-vs-machine race; Naomi Livesay programmed and supervised the IBM implosion runs (three shifts, 24 h/day, 6 days/week). Eight implosion problems were done by end of 1944, 17 in 1945. The Harvard Mark I and (postwar) ENIAC — on which von Neumann ran the million-punch-card thermonuclear feasibility problem in Dec 1945 — and later MANIAC followed.
- **Modern open Monte Carlo transport:** OpenMC (Argonne, open-source), Serpent (VTT, delta-tracking), MCNP (LANL). Public criticality benchmarks: **Godiva** (bare U-235 sphere) and **Jezebel** (bare Pu-239 sphere) — ideal validation cases for a bare-sphere mode. ICSBEP handbook provides evaluated benchmarks.
- **Tracking algorithms:** surface (ray) tracking vs **Woodcock/delta-tracking** (rejection sampling with a majorant cross-section and "virtual collisions"), which avoids stopping particles at every material boundary — ideal for the many concentric shells here.

### 6. GPU / CUDA / Ray-Tracing Guidance [DECLASSIFIED TECHNICAL / PUBLIC]

- **WARP (Bergmann & Vujić, UC Berkeley, 2014–2017):** the first continuous-energy 3D Monte Carlo neutron transport code built for GPUs; uses **NVIDIA OptiX** to build BVH acceleration structures around CSG geometry and perform ray-triangle intersection / "where am I" material queries; uses CUDPP for parallel sort/reduce, CURAND for RNG; event-based ("weaving all the random particles" — sorting neutrons into coherent bundles to avoid warp divergence). Per Bergmann's PhD thesis (eScholarship), "WARP is capable of delivering results that are anywhere from 4 to 800 pcm away from MCNP 6.1 and Serpent 2.1.18, but with run times that are 11-82 times lower, depending on problem geometry and materials." (Note: the later peer-reviewed 2017 benchmark, Bergmann/Rowland/Radnović/Slaybaugh/Vujić in *Annals of Nuclear Energy*, reports the more conservative figure that GPUs running WARP were "between 0.8 and 7.6 times as fast as CPU platforms running production codes" — treat the 11–82× as thesis-stage, ~1–8× as the vetted result.) It ran as well on a consumer Titan Black as on a Tesla K80. Open-source at github.com/weft/warp.
- **Shift (ORNL):** CUDA/HIP continuous-energy MC, event-based, k-eigenvalue and fixed-source, ~7.7× node speedup for Godiva-in-water on Sierra-class systems.
- **OpenMC-GPU** (OpenMP-offload and a CUDA C++ port), **PRAGMA** (GPU-only, CUDA), **GUARDYAN** (time-dependent GPU MC), **Mercury/MonteRay** (LLNL) — all public precedents.
- **Event-based algorithm** (sort particles by next event, launch branchless kernels per event type — OpenMC uses ~8 event types) is the key to GPU efficiency; history-based (one particle per thread) suffers warp divergence.
- **Delta-tracking on RTX:** WARP-style — represent each spherical shell as OptiX geometry, cast the neutron's flight as a ray, use the majorant cross-section to sample flight distance, and use OptiX's BVH to resolve which material the collision point lies in (point-in-region query). Rejection-sample real vs virtual collisions. Note WARP found pure delta-tracking can be *slower* than surface tracking for some materials (strong-absorber majorant); a hybrid surface/delta scheme (as in Serpent2/MCATK) is often best.
- **Memory patterns:** coalesced global-memory access for cross-section lookups (use `__ldg` / read-only cache), shared memory for per-block reductions, single vs double precision trade-offs (WARP was single-precision only).
- **Rendering:** a second OptiX/RTX pass for physically-based volumetric rendering — ray-march emissive/absorptive media for the plasma fireball and shock, with blackbody color mapped from local temperature.

### 7. Visual / Aesthetic Reference [PUBLIC HISTORICAL FACT]

- **Trinity photography:** Julian Mack & Berlyn Brixner led ~50 cameras; Fastax cameras at ~10,000 fps captured the fireball. Iconic frames at 0.016 s and 0.090 s show the toroidal fireball with the "rope trick" spikes down the guy wires; mushroom cloud forms over 2–12 s and reached ~38,000 ft. Fireball ~2,000 ft diameter.
- **Rapatronic camera (Edgerton/Wyckoff/EG&G):** magneto-optical Kerr-cell shutter, ~10 ns exposure, ~10 million fps effective, from ~7 miles with a ~10-ft lens; revealed fireball "mottling" (density variations in the casing), the "rope trick," and early radiative-transport-driven growth. First used at Operation Greenhouse (1951), heavily in Tumbler-Snapper (1952) — so these are *later* tests, not Trinity itself, but the definitive reference for the first-microseconds look.
- **Fireball stages/color:** initial blue-white flash (ionized air, near-8,000+ K), then a brilliant white/yellow luminous fireball, orange as it cools, then the rising red-brown mushroom with condensation cap.
- **Nolan's *Oppenheimer* (2023):** VFX supervisor Andrew Jackson and SFX supervisor Scott R. Fisher used **no CGI** — practical forced-perspective "big-atures" and combustion (gasoline, propane, black powder, aluminum powder, magnesium flares); Nolan wanted it "beautiful and threatening in equal measure," rejecting CGI as feeling "safe." Aesthetic takeaway for the simulator: high-contrast, physically-grounded, slightly chaotic/organic light rather than clean synthetic gradients.

### 8. Key Public Sources
- Robert Serber, *The Los Alamos Primer* (LA-1; UC Press 1992 annotated edition) — declassified.
- Carey Sublette, *Nuclear Weapon Archive* / NWFAQ (nuclearweaponarchive.org) §2, §8, §12 — the most authoritative public FAQ; cites LA-3067 and Sher & Beck NP-1771.
- John Coster-Mullen, *Atom Bombs: The Top Secret Inside Story of Little Boy and Fat Man* — the reconstructed dimensional canon.
- Richard Rhodes, *The Making of the Atomic Bomb*.
- Alex Wellerstein, blog.nuclearsecrecy.com ("Kilotons per kilogram," "Critical mass," "The Fat Man's uranium") and *Restricted Data* (2021); his 2D Critical Assembly Simulator.
- Chuck Hansen, *Swords of Armageddon* / *U.S. Nuclear Weapons*.
- Selby et al., "A New Yield Assessment for the Trinity Nuclear Test, 75 Years Later," *Nuclear Technology* 207 (2021).
- FAS nuclear archive; Manhattan Project National Historical Park; Bradbury Science Museum / National Museum of Nuclear Science & History; atomicarchive.com; Atomic Heritage Foundation (nuclearmuseum.org).
- LA-3067 (Paxton, "Los Alamos Critical Mass Data," 1964); Sher & Beck NP-1771 (1981); arXiv *Nuclear Technology* special-issue papers on the Trinity computing effort and HE implosion system.
- WARP: Bergmann PhD thesis (eScholarship) + Bergmann & Vujić 2015 / Bergmann et al. 2017 (*Annals of Nuclear Energy*); Shift: Hamilton & Evans 2019.

---

## Recommendations (Staged Simulator Build Plan)

**Stage 0 — Validate the neutronics in a bare sphere.** Before any bomb geometry, implement a single-material sphere and reproduce the public **Jezebel** (bare Pu-239) and **Godiva** (bare U-235) k_eff≈1 benchmarks. Benchmark/threshold: match published critical radius/mass within a few percent. This proves your cross-section handling and tracking before adding complexity.

**Stage 1 — Static layered geometry + k_eff solver.** Build the concentric-sphere CSG (Table above) in OptiX; run a k-eigenvalue Monte Carlo with delta-tracking. Threshold to advance: reproduce "pit ~78% critical uncompressed, 3–4 critical masses at 2× compression."

**Stage 2 — Time-dependent supercritical kinetics.** Add exponential population tracking e^((k−1)t/g) with g≈10 ns, initiator neutron injection at peak compression, and a simple hydrodynamic disassembly feedback (expansion → density drop → k falls below 1 → burn quenches at ~15–17% burn-up). Threshold: total yield lands near ~21 kt for canonical inputs (≈1 kg Pu fissioned × ~18.29 kt/kg).

**Stage 3 — Coupled implosion hydro (optional fidelity).** Add a Guderley-style converging-shock model and a lens-timing/asymmetry model; expose Rayleigh-Taylor perturbation growth so that lens jitter visibly degrades symmetry and yield.

**Stage 4 — Rendering.** Second OptiX pass: volumetric blackbody fireball, neutron-population heat map, per-shell fission-event density, shock front. Adopt the Nolan palette (organic, high-contrast, no "clean CGI").

**Decision thresholds that change the plan:** if real-time (≥30 fps) can't be met with full continuous-energy CE data, drop to multi-group cross-sections (a few fast groups) and/or reduce neutron count — this is the standard fidelity/speed lever. If OptiX delta-tracking underperforms on the strong-absorber boron/U layers (WARP's caution), switch to hybrid surface+delta tracking.

### Suggested Physics Kernel Design
1. **Persistent neutron buffer** (SoA layout: position xyz, direction, energy, weight, alive flag) in GPU global memory.
2. **Event-based loop:** each step, sort/partition neutrons by next event (cross-shell surface, collision, fission, leak, absorb); launch a branchless CUDA kernel per event type (à la OpenMC/WARP/Shift).
3. **Geometry via OptiX BVH:** ray = neutron flight; use majorant Σ_M(E) for delta-tracking flight sampling; OptiX resolves collision-point material; rejection-sample real vs virtual collision.
4. **Fission kernel:** sample ν (≈2.9 Pu / ≈2.4 U-235) new neutrons, deposit ~180 MeV, append progeny to buffer; accumulate cumulative fissions for yield.
5. **Hydro feedback:** every N shakes, update per-shell density/radius from energy deposited; recompute k; stop when sub-critical.
6. **Tallies:** neutron population vs time (log plot), spatial fission heat map, k_eff estimator.

### Suggested Rendering Pipeline
`CUDA neutron kernel (physics state) → shared buffers (temperature/density/fission-density fields) → OptiX/RTX ray-march pass (volumetric emission-absorption, blackbody color from T) → denoise (OptiX AI denoiser) → tone-map → UI overlay (population graph, parameter sliders).`

### Suggested UI Parameter Table (Wellerstein-style, in 3D)

| Parameter | Symbol | Public canonical value | Suggested UI range | Units | Source status |
|---|---|---|---|---|---|
| Number of source/simulated neutrons | N | initiator ~10⁶/s burst | 10³–10⁷ | count | SIM REC |
| Initiator strength | — | ~1 neutron / 5–10 ns | 0–10⁷ n/s | n/s | DECLASSIFIED |
| Pit radius | r_pit | 4.585 cm (9.17 cm dia) | 3–6 cm | cm | DECLASSIFIED (LA-3067) |
| Pit mass | M_pit | 6.15 kg | 3–10 kg | kg | DECLASSIFIED |
| Pu-240 fraction | — | 1.0% | 0–7% | % | DECLASSIFIED |
| Gallium content | — | 3.35% molar | 0–5% | % molar | DECLASSIFIED |
| Tamper thickness | t_tamp | 6.56 cm | 0–12 cm | cm | RECONSTRUCTED |
| Tamper material | — | natural U-238 | U/W/Be/none | — | DECLASSIFIED |
| Pusher thickness | t_push | ~12 cm Al | 0–15 cm | cm | RECONSTRUCTED |
| HE lens count | — | 32 (20 hex+12 pent) | 12–92 | count | PUBLIC |
| Comp B / Baratol velocity | D | 7.9 / 4.9 | 4–9 | km/s | PUBLIC |
| Detonation timing jitter | Δt | ±10 ns | 0–1000 ns | ns | DECLASSIFIED |
| Compression ratio | ρ/ρ₀ | ~2–2.5× | 1–3× | ratio | DECLASSIFIED |
| Generation time | g | 10 ns | 1–100 ns | ns | DECLASSIFIED |
| ν (Pu-239) | ν | 2.9 | 2.4–3.0 | n/fission | PUBLIC |
| Fission cross-section (Pu, fast) | σ_f | ~1.8 b | 0.5–2.5 b | barns | PUBLIC (ENDF) |
| Energy per fission | E_f | 180–200 MeV | fixed | MeV | PUBLIC |

**Visualizing criticality progression:** (1) log-scale neutron population vs time showing the e^((k−1)t/g) blow-up; (2) a per-nucleus/per-voxel fission-event heat map igniting from the initiator outward; (3) the converging shock front then the disassembly expansion; (4) a live k_eff / critical-mass readout crossing 1.0 and peaking at 3–4, then collapsing.

---

## Caveats

- **Public canon vs reconstruction.** The core numbers traceable to declassified LA-3067 (6.15 kg δ-Pu-Ga core, 9.17 cm) are firm. Most *layer thicknesses/masses* (tamper 108 vs 111 vs 120 kg; pusher 128 vs 130 kg; lens masses) come from Coster-Mullen's reconstruction and Sublette's synthesis and carry minor internal inconsistencies (Sublette himself lists the tamper as 108 kg in text, 111 kg in his table). Treat these as best-available open estimates, not official specs.
- **Distinct efficiencies conflated.** The ~15–17% *plutonium burn-up fraction* (≈1 kg fissioned) is different from the ~20% of *total yield* contributed by U-238 tamper fast fission. Popular sources routinely mix these; keep them separate in your model.
- **Yield is a range, not a point.** 18.6 kt (1945 radiochemistry) → 21 kt (modern DOE official) → 24.8 ± 2 kt (Selby et al. 2021 reassessment). Expose it, don't hard-code it.
- **Classified boundaries — stay out.** Exact peak k-effective, the precise Bethe-Feynman numerical coefficients, exact lens internal contours, precise implosion velocity/timing profiles, and modern pit details remain classified. This document deliberately uses only published/declassified values; do not attempt to "fill gaps" with inferred weapons-design specifics. A simulator built on Primer-level physics + Sublette/Coster-Mullen geometry is educational and public, which is the correct scope.
- **Rapatronic imagery is post-Trinity.** Those ultra-sharp first-microsecond stills are from 1951–1957 Nevada/Pacific tests, not Trinity; use them for the *look* of the earliest fireball, but Trinity's own record is the Brixner/Mack Fastax footage.
- **Tertiary AI/fan-wiki sources** (Grokipedia, fan wikis) appeared in research; where any claim originated there it was cross-checked against Sublette, Wellerstein, or primary Los Alamos/arXiv documents, which are the authorities to cite. When in doubt, cite the Nuclear Weapon Archive, the Los Alamos Primer, LA-3067, or the peer-reviewed *Nuclear Technology* papers directly.
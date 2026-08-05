// pitscope.js — the volumetric chain-reaction microscope ("main course").
// Zoom into the pit and watch a representative fission cascade: isotope-colored
// nuclei (fractions from data/materials/pu_ga_delta.json, 03 §3), neutrons on
// exponential-flight clocks, capture → fission → ν new neutrons, leakage at the
// boundary. Population tracks the same driver as everything else: the run's
// flux curve / tally.population_series (03 §5) — synthetic rates, spec shapes.
//
// Representation honesty: N markers stand for the pit's ~1.5e25 real atoms
// (each marker ≙ ~1e21). This is a *sampling*, exactly like the Monte-Carlo
// core's sim_neutrons carrying weights (03 §4 [transport]).
//
// ADR-023 reconciliation (branch viz/reconcile-true3d): TRUE-3D render base from
// the viz/atom-proto branch — nuclei/fragments are InstancedMesh spheres, neutrons
// are 3D LineSegments streaks (NEVER THREE.Points / sprites, ADR-023 cl.2). It
// consumes the SAME real data contract as before: run.samples[].sites[] =
// { pos:[x,y,z] cm, group, isotope, layer } (05 §5 GenerationSample / couple.h),
// so it works with studio-adapter.js (real nscore) OR simstub.js (synthetic).
// The zoom is CAMERA FOCUS, not a mode (ADR-023 cl.3): the whole device STAYS
// VISIBLE and the camera dollies within the one shared scene — see toggle()/tick().
//
// Optional arg `cutawayDevice` (default true): on focus-enter, cut the near
// hemisphere of the device (09 §2 clip plane, ADR-018) so the core is revealed
// IN-CONTEXT rather than by hiding the device; the prior device clip is restored
// on exit. Pass `cutawayDevice: false` if the host main.js manages the clip itself.
import { WORLD_PER_CM as WCM } from './scenario.js?v=2';

export function createPitScope({ THREE, scene, camera, controls, dev, getRun, getCfg, cutawayDevice = true }) {
  const group = new THREE.Group();
  group.visible = false;
  scene.add(group);

  const PIT_R = 0.0937;                 // world units (scenario.js: 4.585 cm × CM)
  const PIT_ATOMS = 1.55e25;            // ≈ 6.15 kg of Pu-239 (display only)
  const N_NUCLEI = 24000;               // representative nuclei (user: 10–50k)
  const MAXN = 40000;                   // max live neutrons
  const SPEED = 0.16;                   // neutron speed, world/s at 1× (display)

  // isotope palette from the material card (atom fractions, 03 §3)
  const ISO = [
    { name: 'Pu-239', frac: 0.95683, col: new THREE.Color(0x7f96b8) },
    { name: 'Pu-240', frac: 0.00967, col: new THREE.Color(0xb07fb8) },
    { name: 'Ga',     frac: 0.03350, col: new THREE.Color(0x8fb8a0) },
  ];

  // ---------------- nuclei (static cloud + spatial hash) ----------------
  const nucPos = new Float32Array(N_NUCLEI * 3);
  const nucCol = new Float32Array(N_NUCLEI * 3);
  const nucBase = new Float32Array(N_NUCLEI * 3);   // isotope base colors
  const nucState = new Uint8Array(N_NUCLEI);        // 0 idle · 1 flash · 2 spent
  const nucAge = new Float32Array(N_NUCLEI);        // seconds since flash
  {
    const tmp = new THREE.Color();
    for (let i = 0; i < N_NUCLEI; i++) {
      const v = new THREE.Vector3().randomDirection()
        .multiplyScalar(PIT_R * 0.97 * Math.cbrt(Math.random()));
      nucPos.set([v.x, v.y, v.z], i * 3);
      const r = Math.random(); let acc = 0, iso = ISO[0];
      for (const s of ISO) { acc += s.frac; if (r <= acc) { iso = s; break; } }
      tmp.copy(iso.col).multiplyScalar(0.32 + Math.random() * 0.28);
      nucBase.set([tmp.r, tmp.g, tmp.b], i * 3);
      nucCol.set([tmp.r, tmp.g, tmp.b], i * 3);
      nucAge[i] = -1;
    }
  }
  // nuclei — real 3D InstancedMesh (low-poly spheres) with per-instance isotope
  // color; a one-line onBeforeCompile patch adds a per-instance emissive glow so
  // each nucleus reads as a lumpy glowing thing (not a flat billboard).
  const NUC_R = 0.0019;                                  // world radius of a nucleus
  const nucGeoI = new THREE.IcosahedronGeometry(1, 1);   // ~80 tris, scaled per instance
  const nucMatI = new THREE.MeshStandardMaterial({ roughness: 0.5, metalness: 0.0 });
  // cutaway: clip the hemisphere facing the camera so the interior cascade shows.
  // constant huge (10) = no cut; 0 = cut at the centre. Toggled via setCutaway().
  const microClip = new THREE.Plane(new THREE.Vector3(0, 0, -1), 10.0);
  nucMatI.clippingPlanes = [microClip];
  nucMatI.onBeforeCompile = (sh) => {
    sh.fragmentShader = sh.fragmentShader.replace(
      '#include <emissivemap_fragment>',
      '#include <emissivemap_fragment>\n\ttotalEmissiveRadiance += vColor * 0.14;');
  };
  const nuclei = new THREE.InstancedMesh(nucGeoI, nucMatI, N_NUCLEI);
  nuclei.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  const nucScale = new Float32Array(N_NUCLEI);
  const _cc = new THREE.Color();
  const _m4 = new THREE.Matrix4(), _qid = new THREE.Quaternion(),
        _pv = new THREE.Vector3(), _sv = new THREE.Vector3();
  for (let i = 0; i < N_NUCLEI; i++) {
    nucScale[i] = NUC_R * (0.75 + Math.random() * 0.6);
    _pv.set(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2]);
    _m4.compose(_pv, _qid, _sv.setScalar(nucScale[i]));
    nuclei.setMatrixAt(i, _m4);
    nuclei.setColorAt(i, _cc.setRGB(nucBase[i * 3], nucBase[i * 3 + 1], nucBase[i * 3 + 2]));
  }
  nuclei.instanceMatrix.needsUpdate = true;
  nuclei.instanceColor.needsUpdate = true;
  group.add(nuclei);
  let nucColDirty = false;
  function setNucColor(i, r, g, b) {
    nucCol[i * 3] = r; nucCol[i * 3 + 1] = g; nucCol[i * 3 + 2] = b;
    nuclei.setColorAt(i, _cc.setRGB(r, g, b));
    nucColDirty = true;
  }
  function setNucScale(i, s) {
    _pv.set(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2]);
    _m4.compose(_pv, _qid, _sv.setScalar(s));
    nuclei.setMatrixAt(i, _m4);
    nuclei.instanceMatrix.needsUpdate = true;
  }

  // in-group lighting so the nuclei read as lit 3D spheres at pit zoom
  const coreGlow = new THREE.PointLight(0xfff0e0, 0.18, 0, 1.0);
  group.add(coreGlow);
  const microFill = new THREE.PointLight(0x8fc8ff, 0.5, 0, 0.9);
  microFill.position.set(PIT_R * 1.6, PIT_R * 1.2, PIT_R * 1.6);
  group.add(microFill);

  // ---- fission fragments — the visible "the atom splits in two" ----
  // On a fission the parent nucleus heats + scales up, then shrinks away while two
  // fragment instances fly apart (asymmetric heavy/light masses) and cool to ash.
  const FRAG = 1600;                                     // live-fragment cap (~800 splits)
  const fragGeo = new THREE.IcosahedronGeometry(1, 0);  // ~20 tris, scaled per instance
  const fragMat = new THREE.MeshStandardMaterial({ roughness: 0.62, metalness: 0.0 });
  fragMat.clippingPlanes = [microClip];
  fragMat.onBeforeCompile = (sh) => {
    sh.fragmentShader = sh.fragmentShader.replace(
      '#include <emissivemap_fragment>',
      '#include <emissivemap_fragment>\n\ttotalEmissiveRadiance += vColor * 0.6;');
  };
  const frags = new THREE.InstancedMesh(fragGeo, fragMat, FRAG);
  frags.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  const fPos = new Float32Array(FRAG * 3), fVel = new Float32Array(FRAG * 3), fCol = new Float32Array(FRAG * 3);
  const fAge = new Float32Array(FRAG), fLife = new Float32Array(FRAG), fSz = new Float32Array(FRAG);
  for (let i = 0; i < FRAG; i++) { fAge[i] = -1; frags.setMatrixAt(i, _m4.makeScale(0, 0, 0)); frags.setColorAt(i, _cc.setRGB(0, 0, 0)); }
  frags.instanceMatrix.needsUpdate = true; frags.instanceColor.needsUpdate = true;
  group.add(frags);
  let fCursor = 0;
  const FRAG_LIFE = 0.6, _ax = new THREE.Vector3();
  function spawnFrag(x, y, z, size, r, g, b, vx, vy, vz) {
    const i = fCursor; fCursor = (fCursor + 1) % FRAG;
    fPos[i * 3] = x; fPos[i * 3 + 1] = y; fPos[i * 3 + 2] = z;
    fVel[i * 3] = vx; fVel[i * 3 + 1] = vy; fVel[i * 3 + 2] = vz;
    fAge[i] = 0; fLife[i] = FRAG_LIFE * (0.7 + Math.random() * 0.7); fSz[i] = size;
    fCol[i * 3] = r; fCol[i * 3 + 1] = g; fCol[i * 3 + 2] = b;
  }
  // Start a fission split at idle nucleus j (heat flash now; the shrink animates in tick).
  function fission(j) {
    if (nucState[j] !== 0) return false;
    nucState[j] = 1; nucAge[j] = 0;
    setNucColor(j, 1.0, 0.97, 0.82);
    fissionCount++;
    const x = nucPos[j * 3], y = nucPos[j * 3 + 1], z = nucPos[j * 3 + 2], b = nucScale[j];
    _ax.randomDirection();
    const sp = 0.09;                                     // fragment recoil speed (world/s, display)
    spawnFrag(x, y, z, b * 0.95, 1.0, 0.82, 0.45, _ax.x * sp * 0.72, _ax.y * sp * 0.72, _ax.z * sp * 0.72);  // heavy
    spawnFrag(x, y, z, b * 0.70, 1.0, 0.62, 0.30, -_ax.x * sp, -_ax.y * sp, -_ax.z * sp);                    // light
    const f = flashPool[flashIdx++ % FLASHES];
    f.position.set(x, y, z); f.userData.age = 0;
    return true;
  }

  // pit ghost-shell for orientation
  const shell = new THREE.Mesh(new THREE.SphereGeometry(PIT_R, 48, 32),
    new THREE.MeshBasicMaterial({ color: 0x46d6ff, transparent: true, opacity: 0.05,
      depthWrite: false, side: THREE.DoubleSide }));
  group.add(shell);

  // spatial hash over the sphere (nuclei are static → build once)
  const GRID = 10, CELL = (2 * PIT_R) / GRID;
  const hash = new Map();
  const cellOf = (x, y, z) => {
    const cx = Math.floor((x + PIT_R) / CELL), cy = Math.floor((y + PIT_R) / CELL), cz = Math.floor((z + PIT_R) / CELL);
    return cx + cy * GRID + cz * GRID * GRID;
  };
  for (let i = 0; i < N_NUCLEI; i++) {
    const c = cellOf(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2]);
    if (!hash.has(c)) hash.set(c, []);
    hash.get(c).push(i);
  }
  function nearestIdle(x, y, z) {
    let best = -1, bestD = CELL * CELL * 4;
    const cx = Math.floor((x + PIT_R) / CELL), cy = Math.floor((y + PIT_R) / CELL), cz = Math.floor((z + PIT_R) / CELL);
    for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) for (let dz = -1; dz <= 1; dz++) {
      const arr = hash.get((cx + dx) + (cy + dy) * GRID + (cz + dz) * GRID * GRID);
      if (!arr) continue;
      for (const i of arr) {
        if (nucState[i] !== 0) continue;
        const ddx = nucPos[i * 3] - x, ddy = nucPos[i * 3 + 1] - y, ddz = nucPos[i * 3 + 2] - z;
        const d = ddx * ddx + ddy * ddy + ddz * ddz;
        if (d < bestD) { bestD = d; best = i; }
      }
    }
    return best;
  }

  // ---------------- neutrons (dynamic pool) ----------------
  const nPos = new Float32Array(MAXN * 3);
  const nVel = new Float32Array(MAXN * 3);
  const nClock = new Float32Array(MAXN);
  const nGen = new Uint16Array(MAXN);
  let nAlive = 0;
  // neutrons as additive STREAKS (LineSegments): tail trails behind along velocity,
  // head = current position, colour fades tail(dim)→head(bright cyan-white).
  const nLinePos = new Float32Array(MAXN * 6), nLineCol = new Float32Array(MAXN * 6);
  const nGeo = new THREE.BufferGeometry();
  nGeo.setAttribute('position', new THREE.BufferAttribute(nLinePos, 3));
  nGeo.setAttribute('color', new THREE.BufferAttribute(nLineCol, 3));
  const nMat = new THREE.LineBasicMaterial({ vertexColors: true, transparent: true,
    opacity: 0.92, blending: THREE.AdditiveBlending, depthWrite: false });
  const nPts = new THREE.LineSegments(nGeo, nMat);
  group.add(nPts);
  const STREAK = 0.014;                                 // streak length (world units)

  const mfp = () => -Math.log(1 - Math.random()) * 0.05;   // synthetic mean free clock
  function spawn(x, y, z, gen) {
    if (nAlive >= MAXN) return;
    const i = nAlive++;
    nPos.set([x, y, z], i * 3);
    const v = new THREE.Vector3().randomDirection();
    nVel.set([v.x * SPEED, v.y * SPEED, v.z * SPEED], i * 3);
    nClock[i] = mfp();
    nGen[i] = gen;
  }
  function kill(i) {
    const l = --nAlive;
    if (i !== l) {
      nPos.copyWithin(i * 3, l * 3, l * 3 + 3);
      nVel.copyWithin(i * 3, l * 3, l * 3 + 3);
      nClock[i] = nClock[l]; nGen[i] = nGen[l];
    }
    nPos.set([0, -999, 0], l * 3);
  }

  // fission flash pool
  const FLASHES = 64;
  const flashPool = [];
  for (let i = 0; i < FLASHES; i++) {
    const m = new THREE.Mesh(new THREE.SphereGeometry(1, 12, 8),
      new THREE.MeshBasicMaterial({ color: 0xffe9b0, transparent: true, opacity: 0,
        blending: THREE.AdditiveBlending, depthWrite: false }));
    m.userData.age = -1;
    group.add(m); flashPool.push(m);
  }
  let flashIdx = 0, fissionCount = 0, wave = 0, spentCount = 0, lastSiteGen = -1, lastSample = null;

  // ---------------- state / camera (ADR-023 cl.3: zoom = camera FOCUS, not a mode) ----------------
  // `active` stays true from focus-enter until the pull-OUT glide finishes, so the
  // host's `if (micro.active) micro.tick()` keeps driving the exit animation. The
  // DEVICE IS NEVER HIDDEN — the camera merely dollies within the one shared scene;
  // the moment a glide lands, animDir clears and OrbitControls has full control again
  // (orbit / pan / dolly in further, or out to the whole device in context).
  let active = false;
  let animDir = null;                          // 'in' | 'out' | null — current glide
  let animT = 1;                               // 0..1 progress of the current glide
  let savedDevClip = null;                     // device clip plane to restore on exit
  const DOLLY_SPEED = 1.2;                      // glide rate (≈0.8 s to enter/exit)
  const FOCUS_DIST = PIT_R * 3.3;              // close pit-framing distance (≈ the old fixed 0.31)
  const camFrom = new THREE.Vector3(), camTo = new THREE.Vector3();
  const tgtFrom = new THREE.Vector3(), tgtTo = new THREE.Vector3(0, 0, 0);   // pit centre = world origin
  const camSave = new THREE.Vector3(), tgtSave = new THREE.Vector3();        // pre-focus wide view (restored on exit)
  const _dir = new THREE.Vector3();
  const ease = (t) => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;

  function toggle() {
    if (animDir === 'out') return;             // ignore re-entry while pulling out
    if (!active) {
      // ---- ENTER focus: keep the whole device visible (NOT a mode switch). Remember
      //      the current wide view, then dolly straight in on the pit along the current
      //      sightline to a default close framing.
      active = true;
      group.visible = true;
      document.getElementById('microHud')?.classList.remove('hidden');
      camSave.copy(camera.position); tgtSave.copy(controls.target);       // context view to return to
      _dir.copy(camera.position).sub(controls.target);                    // current sightline
      if (_dir.lengthSq() < 1e-9) _dir.set(0.19, 0.07, 0.24);            // degenerate → a sensible bearing
      camTo.copy(tgtTo).add(_dir.setLength(FOCUS_DIST));                  // close pose, same bearing, on the pit
      camFrom.copy(camera.position); tgtFrom.copy(controls.target);
      animDir = 'in'; animT = 0; lastSample = null;                       // fresh burst → fresh frozen readout
      if (cutawayDevice) enterDeviceCutaway();                            // reveal the core in-context (09 §2 clip)
    } else {
      // ---- EXIT focus: dolly back OUT to the saved wide view; the device was never
      //      hidden. The group stays visible through the pull-out, hidden when it lands.
      document.getElementById('microHud')?.classList.add('hidden');
      camFrom.copy(camera.position); tgtFrom.copy(controls.target);
      animDir = 'out'; animT = 0;
    }
  }

  // ---------------- per-frame ----------------
  function tick(dt, runT) {
    if (!active) return;
    // keep any active cutaway plane facing the camera as the user orbits, so the near
    // hemisphere (microscope nuclei AND the device shells) is always the one cut away.
    if (cutaway || devClipOn) {
      _cn.copy(camera.position).normalize();
      const nx = -_cn.x, ny = -_cn.y, nz = -_cn.z;
      if (cutaway)   { microClip.normal.set(nx, ny, nz); microClip.constant = 0.0; }
      if (devClipOn) { devClip.normal.set(nx, ny, nz);   devClip.constant = 0.0; }
    }
    // ---- camera glide: a DOLLY within the ONE scene (ADR-023 cl.3), not a mode switch.
    //      'in' eases to a close pit framing; 'out' eases back to the saved wide view.
    //      While gliding we drive the camera; the instant it lands, animDir clears and
    //      OrbitControls owns the camera again. The device is visible the whole time.
    if (animDir) {
      animT = Math.min(1, animT + dt * DOLLY_SPEED);
      const e = ease(animT);
      const destP = animDir === 'in' ? camTo : camSave;
      const destT = animDir === 'in' ? tgtTo : tgtSave;
      camera.position.lerpVectors(camFrom, destP, e);
      controls.target.lerpVectors(tgtFrom, destT, e);
      if (animT >= 1) {
        const wasOut = animDir === 'out';
        animDir = null;                        // hand the camera back to OrbitControls
        if (wasOut) {                          // fully exited → tear down focus, restore the whole-device scene
          exitDeviceCutaway();
          group.visible = false;
          active = false;
          return;
        }
      }
    }

    const run = getRun();
    const cfg = getCfg();
    let k = run ? run.kEff : (window.__gaugeK ?? 1.0);
    let curLambda = null, curSample = null;

    // run-sync: consume the 05 §5 GenerationSample stream (couple.h) during the
    // burst window — target population from log10_population; spatial fission
    // sites (FissionSite{pos cm, group, isotope, layer}) applied once per sample.
    if (run && runT >= run.phases[3].t0 && runT <= run.phases[3].t1) {
      const samples = run.samples;
      if (samples && samples.length) {
        const span = run.phases[3].t1 - run.phases[3].t0;
        const gi = Math.min(samples.length - 1, Math.max(0,
          Math.floor(((runT - run.phases[3].t0) / span) * samples.length)));
        const s = samples[gi];
        curSample = s; lastSample = s; curLambda = s.lambda_s; k = s.k_eff;
        const target = Math.min(MAXN * 0.75, Math.pow(10, s.log10_population) * 7.5e-8);
        let guard = 600;
        while (nAlive < target && guard-- > 0)
          spawn((Math.random() - 0.5) * 0.01, (Math.random() - 0.5) * 0.01, (Math.random() - 0.5) * 0.01, 1);
        if (s.sites && gi !== lastSiteGen) {          // apply the spatial sites once
          lastSiteGen = gi;
          for (const site of s.sites) {
            const px = site.pos[0] * WCM, py = site.pos[1] * WCM, pz = site.pos[2] * WCM;
            if (px * px + py * py + pz * pz > PIT_R * PIT_R) continue;   // tamper sites: outside the pit volume
            const j = nearestIdle(px, py, pz);
            if (j < 0) continue;
            if (!fission(j)) continue;
            const nu = Math.random() < 0.25 ? 3 : 2;
            for (let s2 = 0; s2 < nu; s2++) spawn(nucPos[j * 3], nucPos[j * 3 + 1], nucPos[j * 3 + 2], 1);
          }
        }
      }
    }

    // neutron transport (bounded work per frame)
    const pCap = Math.min(0.92, Math.max(0.04, k / 2.4));     // synthetic capture prob
    const nuBar = 2.0;                                         // synthetic ν per fission
    for (let i = nAlive - 1; i >= 0; i--) {
      nPos[i * 3] += nVel[i * 3] * dt;
      nPos[i * 3 + 1] += nVel[i * 3 + 1] * dt;
      nPos[i * 3 + 2] += nVel[i * 3 + 2] * dt;
      nClock[i] -= dt;
      if (nClock[i] > 0) continue;
      const x = nPos[i * 3], y = nPos[i * 3 + 1], z = nPos[i * 3 + 2];
      if (x * x + y * y + z * z > PIT_R * PIT_R) { kill(i); continue; }     // leakage
      if (Math.random() < pCap) {
        const j = nearestIdle(x, y, z);
        if (j >= 0 && fission(j)) {
          spentCount++;
          wave = Math.max(wave, nGen[i] + 1);
          const nu = Math.random() < 0.25 ? 3 : 2;                          // ν ≈ 2–3
          for (let s = 0; s < nu; s++) spawn(nucPos[j * 3], nucPos[j * 3 + 1], nucPos[j * 3 + 2], nGen[i] + 1);
          kill(i);
          continue;
        }
      }
      // scatter
      const v = new THREE.Vector3().randomDirection();
      nVel.set([v.x * SPEED, v.y * SPEED, v.z * SPEED], i * 3);
      nClock[i] = mfp();
    }
    for (let i = 0; i < nAlive; i++) {
      const px = nPos[i * 3], py = nPos[i * 3 + 1], pz = nPos[i * 3 + 2];
      let vx = nVel[i * 3], vy = nVel[i * 3 + 1], vz = nVel[i * 3 + 2];
      const vl = Math.hypot(vx, vy, vz) || 1; vx /= vl; vy /= vl; vz /= vl;
      const k = i * 6;
      nLinePos[k] = px - vx * STREAK; nLinePos[k + 1] = py - vy * STREAK; nLinePos[k + 2] = pz - vz * STREAK;
      nLinePos[k + 3] = px; nLinePos[k + 4] = py; nLinePos[k + 5] = pz;
      nLineCol[k] = 0.02; nLineCol[k + 1] = 0.09; nLineCol[k + 2] = 0.15;      // tail (dim)
      nLineCol[k + 3] = 0.6; nLineCol[k + 4] = 0.9; nLineCol[k + 5] = 1.0;     // head (bright)
    }
    nGeo.attributes.position.needsUpdate = true;
    nGeo.attributes.color.needsUpdate = true;
    nGeo.setDrawRange(0, nAlive * 2);

    // fissioning nuclei: heat-up swell → shrink away (the parent tears apart)
    for (let i = 0; i < N_NUCLEI; i++) {
      if (nucState[i] !== 1) continue;
      nucAge[i] += dt;
      const a = nucAge[i];
      if (a < 0.09) setNucScale(i, nucScale[i] * (1 + (a / 0.09) * 0.45));
      else setNucScale(i, nucScale[i] * 1.45 * Math.max(0, 1 - (a - 0.09) / 0.24));
      if (a > 0.33) {
        nucState[i] = 2; setNucScale(i, 1e-5);
        setNucColor(i, nucBase[i * 3] * 0.16, nucBase[i * 3 + 1] * 0.16, nucBase[i * 3 + 2] * 0.16);
      } else {
        const d = Math.max(0.25, 1 - a * 2.2);
        setNucColor(i, d, d * 0.9, d * 0.72);
      }
    }
    if (nucColDirty) { nuclei.instanceColor.needsUpdate = true; nucColDirty = false; }

    // fragments fly apart, decelerate, cool hot→ash, recycle
    let fragDirty = false;
    for (let i = 0; i < FRAG; i++) {
      if (fAge[i] < 0) continue;
      fAge[i] += dt;
      const life = fLife[i];
      if (fAge[i] >= life) { fAge[i] = -1; frags.setMatrixAt(i, _m4.makeScale(0, 0, 0)); fragDirty = true; continue; }
      const u = fAge[i] / life;
      fPos[i * 3] += fVel[i * 3] * dt; fPos[i * 3 + 1] += fVel[i * 3 + 1] * dt; fPos[i * 3 + 2] += fVel[i * 3 + 2] * dt;
      fVel[i * 3] *= 0.93; fVel[i * 3 + 1] *= 0.93; fVel[i * 3 + 2] *= 0.93;
      _pv.set(fPos[i * 3], fPos[i * 3 + 1], fPos[i * 3 + 2]);
      _m4.compose(_pv, _qid, _sv.setScalar(Math.max(1e-5, fSz[i] * (1 - u * 0.45))));
      frags.setMatrixAt(i, _m4);
      frags.setColorAt(i, _cc.setRGB(fCol[i * 3] * (1 - u) + 0.30 * u, fCol[i * 3 + 1] * (1 - u) + 0.16 * u, fCol[i * 3 + 2] * (1 - u) + 0.11 * u));
      fragDirty = true;
    }
    if (fragDirty) { frags.instanceMatrix.needsUpdate = true; frags.instanceColor.needsUpdate = true; }

    // flashes
    for (const f of flashPool) {
      if (f.userData.age < 0) continue;
      f.userData.age += dt;
      const a = f.userData.age;
      if (a > 0.4) { f.userData.age = -1; f.material.opacity = 0; continue; }
      f.scale.setScalar(0.0015 + a * 0.012);
      f.material.opacity = 0.4 * (1 - a / 0.4);
    }

    // HUD — during the burst the CURRENT sample rules; after it, the last
    // sample is retained (frozen readout) so the burst state stays inspectable.
    const logN = nAlive > 0 ? Math.log10(Math.max(nAlive, 1)) : 0;
    const disp = curSample || lastSample;
    document.getElementById('mAlive').textContent = nAlive.toLocaleString();
    document.getElementById('mFissions').textContent = fissionCount.toLocaleString();
    document.getElementById('mWave').textContent = wave;
    document.getElementById('mLogN').textContent = disp ? disp.log10_population.toFixed(2) : logN.toFixed(2);
    document.getElementById('mK').textContent = disp ? disp.k_eff.toFixed(3) : k.toFixed(3);
    document.getElementById('mLambda').textContent = disp ? `${(disp.lambda_s * 1e9).toFixed(1)} ns` : '—';
    document.getElementById('mSample').textContent = disp ? `${disp.n}${disp.refreshed ? ' ⚡' : ''}` : '—';
  }

  function inject(n = 200) {
    for (let i = 0; i < n; i++) {
      const v = new THREE.Vector3().randomDirection().multiplyScalar(PIT_R * 0.92 * Math.cbrt(Math.random()));
      spawn(v.x, v.y, v.z, 1);   // seed throughout the pit volume, not a central pile
    }
  }
  function reset() {
    nAlive = 0; fissionCount = 0; wave = 0; spentCount = 0;
    for (let i = 0; i < N_NUCLEI; i++) {
      nucState[i] = 0; nucAge[i] = -1;
      setNucScale(i, nucScale[i]);
      setNucColor(i, nucBase[i * 3], nucBase[i * 3 + 1], nucBase[i * 3 + 2]);
    }
    if (nucColDirty) { nuclei.instanceColor.needsUpdate = true; nucColDirty = false; }
    for (let i = 0; i < FRAG; i++) { fAge[i] = -1; frags.setMatrixAt(i, _m4.makeScale(0, 0, 0)); }
    frags.instanceMatrix.needsUpdate = true;
    nGeo.setDrawRange(0, 0);
  }

  const _cn = new THREE.Vector3();
  let cutaway = false;
  function setCutaway(on) {
    cutaway = on;
    if (on) { _cn.copy(camera.position).normalize(); microClip.normal.set(-_cn.x, -_cn.y, -_cn.z); microClip.constant = 0.0; }
    else microClip.constant = 10.0;
  }

  // ---- device cutaway (09 §2 clip plane, ADR-018): reveal the core WITHOUT hiding
  //      the device. On focus-enter we cut the near hemisphere of the whole assembly
  //      (kept camera-facing each frame in tick) so the cascade shows in-context; on
  //      exit the device's prior clip state is restored. Feature-detected + optional,
  //      so it degrades to a no-op on a device that exposes no setClipPlane.
  const devClip = new THREE.Plane(new THREE.Vector3(0, 0, -1), 0);
  let devClipOn = false;
  function enterDeviceCutaway() {
    setCutaway(true);                                  // cut the microscope nuclei to match
    if (dev && typeof dev.setClipPlane === 'function') {
      savedDevClip = (dev.mats && dev.mats[0] && dev.mats[0].clippingPlanes && dev.mats[0].clippingPlanes[0]) || null;
      _cn.copy(camera.position).normalize();
      devClip.normal.set(-_cn.x, -_cn.y, -_cn.z); devClip.constant = 0.0;
      dev.setClipPlane(devClip);
      devClipOn = true;
    }
  }
  function exitDeviceCutaway() {
    setCutaway(false);
    if (devClipOn) {
      if (dev && typeof dev.setClipPlane === 'function') dev.setClipPlane(savedDevClip);   // restore prior clip
      devClipOn = false;
    }
  }

  return {
    get active() { return active; },
    toggle, tick, inject, reset, setCutaway,
    markerRatio: `1 marker ≙ ${(PIT_ATOMS / N_NUCLEI).toExponential(1)} atoms`,
    iso: ISO,
  };
}

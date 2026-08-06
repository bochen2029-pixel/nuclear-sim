// core-view.js — cinematic demon-core chain-reaction renderer (ADR-023).
//
// A self-contained AAA-leaning view: TRUE 3D volumetric particles (InstancedMesh,
// never sprites), PBR + per-instance emissive fission flash, streaking neutrons, a
// living burst core, ACES tone-mapping + bloom + a custom cinematic grade pass, and
// auto time-dilation through the burst. Physics is SYNTHETIC but spec-shaped
// (representative sampling; population/generations/leakage) — the seam the real
// nscore GenerationSample stream will later feed. Touches no other viz/ file.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { BokehPass } from 'three/addons/postprocessing/BokehPass.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

// ------------------------------------------------------------------ constants
const PIT_R = 1.6;                 // pit radius, world units (the view's hero scale)
const N_NUCLEI = 24000;            // representative nuclei (each ≙ ~6e20 atoms)
const MAXN = 8000;                 // max live neutrons (streaked → keep bounded)
const SPEED = 2.1;                 // neutron speed (world/s at 1×, display)
const ISO = [
  { name: 'Pu-239', frac: 0.95683, col: new THREE.Color(0x6f88b8) },
  { name: 'Pu-240', frac: 0.00967, col: new THREE.Color(0xa878c0) },
  { name: 'Ga',     frac: 0.03350, col: new THREE.Color(0x74c49a) },
];
const FLASH = new THREE.Color(0xfff0d0);
const SPENT = 0.18;                // spent-nucleus dim factor

// ------------------------------------------------------------------ renderer
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance', preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x04060a, 0.026);
// graded backdrop — a subtle radial pool of light so the core sits in space, not flat void
{
  const c = document.createElement('canvas'); c.width = c.height = 512;
  const g = c.getContext('2d');
  const rad = g.createRadialGradient(256, 232, 30, 256, 256, 400);
  rad.addColorStop(0, '#141f2e'); rad.addColorStop(0.45, '#0a121d'); rad.addColorStop(1, '#03050a');
  g.fillStyle = rad; g.fillRect(0, 0, 512, 512);
  // faint nebula wisps → atmospheric depth (the DOF softens them into a dreamy backdrop)
  for (let i = 0; i < 16; i++) {
    const x = Math.random() * 512, y = Math.random() * 512, r = 40 + Math.random() * 150;
    const bg = g.createRadialGradient(x, y, 0, x, y, r);
    bg.addColorStop(0, ['#1c3242', '#182a3c', '#213045', '#12222e'][i % 4]);
    bg.addColorStop(1, 'rgba(0,0,0,0)');
    g.globalAlpha = 0.10 + Math.random() * 0.12; g.fillStyle = bg;
    g.beginPath(); g.arc(x, y, r, 0, 7); g.fill();
  }
  g.globalAlpha = 1;
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace;
  scene.background = tex;
}
// faint drifting dust (atmosphere + scale cue; decorative, not physics particles)
{
  const ND = 1500, dp = new Float32Array(ND * 3), dv = new THREE.Vector3();
  for (let i = 0; i < ND; i++) { dv.randomDirection().multiplyScalar(PIT_R * (2.2 + Math.random() * 9)); dp.set([dv.x, dv.y * 0.55, dv.z], i * 3); }
  const dg = new THREE.BufferGeometry(); dg.setAttribute('position', new THREE.BufferAttribute(dp, 3));
  scene.add(new THREE.Points(dg, new THREE.PointsMaterial({ color: 0x44607e, size: PIT_R * 0.011, transparent: true, opacity: 0.45, sizeAttenuation: true, depthWrite: false })));
}

const camera = new THREE.PerspectiveCamera(42, innerWidth / innerHeight, 0.1, 400);
camera.position.set(5.2, 2.4, 6.6);

const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.minDistance = 2.0;
controls.maxDistance = 40;
controls.autoRotate = true;
controls.autoRotateSpeed = 0.35;
controls.target.set(0, 0, 0);

// subtle IBL for PBR sheen (kept dim; explicit lights carry the mood)
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.03).texture;

// ------------------------------------------------------------------ lighting
scene.add(new THREE.AmbientLight(0x18222f, 0.3));
const key = new THREE.DirectionalLight(0xffe6cc, 1.75);      // warm dramatic key
key.position.set(6, 9, 4); key.castShadow = true;
key.shadow.mapSize.set(2048, 2048); key.shadow.camera.near = 1; key.shadow.camera.far = 40;
key.shadow.bias = -0.0006; key.shadow.radius = 3; scene.add(key);
const rim = new THREE.PointLight(0x4aa8ff, 70, 32, 1.7); rim.position.set(-7, 3, -6); scene.add(rim);   // cool back-rim
const fill = new THREE.PointLight(0xff8a3a, 14, 24, 1.8); fill.position.set(5, -3, 5); scene.add(fill);
// the burst core light — intensity is driven by the reaction population
const coreLight = new THREE.PointLight(0xffd28a, 0, 30, 1.5); scene.add(coreLight);

// faint ground plane to catch shadow + reflection
const ground = new THREE.Mesh(
  new THREE.CircleGeometry(26, 64),
  new THREE.MeshStandardMaterial({ color: 0x070b12, metalness: 0.6, roughness: 0.55 }));
ground.rotation.x = -Math.PI / 2; ground.position.y = -PIT_R * 2.1; ground.receiveShadow = true;
scene.add(ground);

// ------------------------------------------------------------------ nuclei (instanced, true-3D)
const nucGeo = new THREE.IcosahedronGeometry(1, 2);   // detail 2 → smoother spheres in the sharp focal plane
// per-instance emissive glow (fission flash) injected into the standard shader
nucGeo.setAttribute('aGlow', new THREE.InstancedBufferAttribute(new Float32Array(N_NUCLEI), 1));
const nucMat = new THREE.MeshStandardMaterial({ metalness: 0.42, roughness: 0.36, envMapIntensity: 1.2 });
nucMat.onBeforeCompile = (sh) => {
  sh.vertexShader = 'attribute float aGlow;\nvarying float vGlow;\n' +
    sh.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n  vGlow = aGlow;');
  sh.fragmentShader = 'varying float vGlow;\n' +
    sh.fragmentShader.replace('#include <emissivemap_fragment>',
      '#include <emissivemap_fragment>\n  totalEmissiveRadiance += vGlow * vGlow * vec3(2.4, 1.35, 0.45);');
};
const nuclei = new THREE.InstancedMesh(nucGeo, nucMat, N_NUCLEI);
nuclei.instanceMatrix.setUsage(THREE.StaticDrawUsage);
nuclei.castShadow = false; nuclei.receiveShadow = false;
scene.add(nuclei);

const nucPos = new Float32Array(N_NUCLEI * 3);
const nucBase = new Float32Array(N_NUCLEI * 3);       // isotope base color
const nucState = new Uint8Array(N_NUCLEI);            // 0 idle · 1 flashing · 2 spent
const nucAge = new Float32Array(N_NUCLEI);
const aGlow = nucGeo.getAttribute('aGlow');
const NUC_SIZE = PIT_R * 0.0135;
{
  const dummy = new THREE.Object3D();
  const dir = new THREE.Vector3();
  const c = new THREE.Color();
  for (let i = 0; i < N_NUCLEI; i++) {
    dir.randomDirection().multiplyScalar(PIT_R * 0.985 * Math.cbrt(Math.random()));
    nucPos.set([dir.x, dir.y, dir.z], i * 3);
    dummy.position.copy(dir);
    dummy.scale.setScalar(NUC_SIZE * (0.7 + Math.random() * 0.5));
    dummy.updateMatrix();
    nuclei.setMatrixAt(i, dummy.matrix);
    let r = Math.random(), acc = 0, iso = ISO[0];
    for (const s of ISO) { acc += s.frac; if (r <= acc) { iso = s; break; } }
    c.copy(iso.col).multiplyScalar(0.6 + Math.random() * 0.4);
    nucBase.set([c.r, c.g, c.b], i * 3);
    nuclei.setColorAt(i, c);
  }
  nuclei.instanceMatrix.needsUpdate = true;
  nuclei.instanceColor.needsUpdate = true;
}

// spatial hash for nearest-idle nucleus lookup
const GRID = 12, CELL = (2 * PIT_R) / GRID;
const hash = new Map();
const cidx = (x, y, z) =>
  (Math.floor((x + PIT_R) / CELL)) + (Math.floor((y + PIT_R) / CELL)) * GRID +
  (Math.floor((z + PIT_R) / CELL)) * GRID * GRID;
for (let i = 0; i < N_NUCLEI; i++)
  (hash.get(cidx(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2])) ||
    hash.set(cidx(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2]), []).get(cidx(nucPos[i * 3], nucPos[i * 3 + 1], nucPos[i * 3 + 2]))).push(i);
function nearestIdle(x, y, z) {
  const cx = Math.floor((x + PIT_R) / CELL), cy = Math.floor((y + PIT_R) / CELL), cz = Math.floor((z + PIT_R) / CELL);
  let best = -1, bestD = CELL * CELL * 5;
  for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) for (let dz = -1; dz <= 1; dz++) {
    const arr = hash.get((cx + dx) + (cy + dy) * GRID + (cz + dz) * GRID * GRID);
    if (!arr) continue;
    for (const i of arr) {
      if (nucState[i] !== 0) continue;
      const ax = nucPos[i * 3] - x, ay = nucPos[i * 3 + 1] - y, az = nucPos[i * 3 + 2] - z;
      const d = ax * ax + ay * ay + az * az;
      if (d < bestD) { bestD = d; best = i; }
    }
  }
  return best;
}

// ------------------------------------------------------------------ neutrons (instanced, streaked)
const nGeo = new THREE.IcosahedronGeometry(1, 1);
const nMat = new THREE.MeshBasicMaterial({ color: 0xcaffff });   // self-lit + bright → bloom streaks it
const neutrons = new THREE.InstancedMesh(nGeo, nMat, MAXN);
neutrons.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
neutrons.frustumCulled = false;
scene.add(neutrons);
const nPos = new Float32Array(MAXN * 3);
const nVel = new Float32Array(MAXN * 3);
const nClock = new Float32Array(MAXN);
const nGen = new Uint16Array(MAXN);
let nAlive = 0;
const NSIZE = PIT_R * 0.007;
const UP = new THREE.Vector3(0, 1, 0);
const _q = new THREE.Quaternion(), _v = new THREE.Vector3(), _d = new THREE.Object3D(), _hide = new THREE.Matrix4().makeScale(0, 0, 0);

const mfp = () => -Math.log(1 - Math.random()) * 0.11;
function spawn(x, y, z, gen) {
  if (nAlive >= MAXN) return;
  const i = nAlive++;
  nPos.set([x, y, z], i * 3);
  _v.randomDirection();
  nVel.set([_v.x * SPEED, _v.y * SPEED, _v.z * SPEED], i * 3);
  nClock[i] = mfp(); nGen[i] = gen;
}
function kill(i) {
  const l = --nAlive;
  if (i !== l) {
    nPos.copyWithin(i * 3, l * 3, l * 3 + 3);
    nVel.copyWithin(i * 3, l * 3, l * 3 + 3);
    nClock[i] = nClock[l]; nGen[i] = nGen[l];
  }
}
function fission(j, gen) {
  nucState[j] = 1; nucAge[j] = 0; aGlow.array[j] = 1.0;
  fissionCount++; wave = Math.max(wave, gen);
  const nu = Math.random() < 0.28 ? 3 : 2;
  for (let s = 0; s < nu; s++) spawn(nucPos[j * 3], nucPos[j * 3 + 1], nucPos[j * 3 + 2], gen);
}

// ------------------------------------------------------------------ burst core (emissive + glow shells)
const core = new THREE.Mesh(
  new THREE.IcosahedronGeometry(PIT_R * 0.13, 4),
  new THREE.MeshBasicMaterial({ color: 0xffdca8, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false }));
scene.add(core);
const glowShells = [];
for (let i = 0; i < 3; i++) {
  const g = new THREE.Mesh(
    new THREE.SphereGeometry(PIT_R * (0.2 + i * 0.16), 32, 24),
    new THREE.MeshBasicMaterial({ color: 0xffcf8a, transparent: true, opacity: 0,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.BackSide }));
  scene.add(g); glowShells.push(g);
}
// pit boundary ghost (orientation)
const boundary = new THREE.Mesh(
  new THREE.SphereGeometry(PIT_R, 64, 48),
  new THREE.MeshBasicMaterial({ color: 0x2f6f9f, transparent: true, opacity: 0.04,
    depthWrite: false, side: THREE.BackSide }));
scene.add(boundary);

// ------------------------------------------------------------------ post
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
// custom rack-focus DOF: center-sharp, rim melts to bokeh. A pure HDR-safe post-filter
// (composer targets are half-float) placed BEFORE bloom so out-of-focus highlights bloom
// into soft glowing discs — the cinematic UE5/MW tell, without BokehPass's HDR clipping.
const DofShader = {
  uniforms: { tDiffuse: { value: null }, uRes: { value: new THREE.Vector2() }, uAmount: { value: 0.022 } },
  vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform vec2 uRes; uniform float uAmount;
    varying vec2 vUv;
    void main(){
      vec2 c = vUv - 0.5;
      float coc = smoothstep(0.09, 0.62, length(vec2(c.x * uRes.x / uRes.y, c.y)) * 1.6) * uAmount;
      float ar = uRes.y / uRes.x;
      vec3 col = texture2D(tDiffuse, vUv).rgb; float w = 1.0;
      for (int i = 1; i <= 30; i++){
        float a = float(i) * 2.3999632;                       // golden-angle disc
        float rad = sqrt(float(i) / 30.0) * coc;
        vec2 off = vec2(cos(a) * ar, sin(a)) * rad;
        col += texture2D(tDiffuse, vUv + off).rgb; w += 1.0;
      }
      gl_FragColor = vec4(col / w, 1.0);
    }`,
};
const dof = new ShaderPass(DofShader);
dof.uniforms.uRes.value.set(innerWidth, innerHeight);
composer.addPass(dof);
// volumetric god-rays: radial crepuscular shafts from the bright core (screen-space
// light scattering toward center) — the UE5/MW atmospheric signature, driven by the burst.
const GodRaysShader = {
  uniforms: { tDiffuse: { value: null }, uStrength: { value: 0.1 } },
  vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float uStrength;
    varying vec2 vUv;
    void main(){
      vec3 col = texture2D(tDiffuse, vUv).rgb;
      vec2 dir = (vec2(0.5) - vUv) / 28.0;
      vec2 uv = vUv; float decay = 1.0; vec3 shaft = vec3(0.0);
      for (int i = 0; i < 28; i++){
        uv += dir;
        vec3 s = texture2D(tDiffuse, uv).rgb;
        float b = max(0.0, dot(s, vec3(0.333)) - 0.72);   // only bright emissive seeds shafts
        shaft += s * b * decay;
        decay *= 0.93;
      }
      gl_FragColor = vec4(col + shaft * uStrength, 1.0);
    }`,
};
const godrays = new ShaderPass(GodRaysShader);
composer.addPass(godrays);
const bloom = new UnrealBloomPass(new THREE.Vector2(innerWidth, innerHeight), 0.52, 0.7, 0.9);
composer.addPass(bloom);
composer.addPass(new OutputPass());
const GradeShader = {
  uniforms: { tDiffuse: { value: null }, uTime: { value: 0 }, uRes: { value: new THREE.Vector2() }, uBurst: { value: 0 } },
  vertexShader: 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }',
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float uTime; uniform vec2 uRes; uniform float uBurst;
    varying vec2 vUv;
    void main(){
      vec2 uv = vUv; vec2 c = uv - 0.5; float d = dot(c, c);
      // radial chromatic aberration (subtle lens)
      float ca = (0.0006 + d * 0.0024) * (1.0 + uBurst * 0.8);
      vec3 col;
      col.r = texture2D(tDiffuse, uv + c * ca).r;
      col.g = texture2D(tDiffuse, uv).g;
      col.b = texture2D(tDiffuse, uv - c * ca).b;
      // unsharp-mask sharpen (crisp AAA micro-detail)
      vec2 px = 1.0 / uRes;
      vec3 bl = texture2D(tDiffuse, uv + vec2(px.x, 0.0)).rgb + texture2D(tDiffuse, uv - vec2(px.x, 0.0)).rgb
              + texture2D(tDiffuse, uv + vec2(0.0, px.y)).rgb + texture2D(tDiffuse, uv - vec2(0.0, px.y)).rgb;
      col += (col - bl * 0.25) * 0.20;
      // cinematic teal-orange grade: teal in the shadows, warm in the highlights
      float l = dot(col, vec3(0.299, 0.587, 0.114));
      col += mix(vec3(-0.012, 0.010, 0.030), vec3(0.050, 0.016, -0.030), smoothstep(0.12, 0.85, l));
      col = clamp(col, 0.0, 1.0);
      col = mix(vec3(l), col, 1.12);              // +12% saturation
      // vignette
      float vig = smoothstep(1.08, 0.20, d * 1.5);
      col *= mix(0.52, 1.0, vig);
      // fine film grain
      float g = fract(sin(dot(uv * uRes + uTime, vec2(12.9898, 78.233))) * 43758.5453);
      col += (g - 0.5) * 0.017;
      gl_FragColor = vec4(max(col, 0.0), 1.0);
    }`,
};
const grade = new ShaderPass(GradeShader);
grade.uniforms.uRes.value.set(innerWidth, innerHeight);
composer.addPass(grade);

// ------------------------------------------------------------------ reaction driver (spec-shaped)
let fissionCount = 0, wave = 0;
let phase = 'idle', tSim = 0, kEff = 1.0, logN = 0, yieldRel = 0, timeScale = 1, slowAuto = true;
const PHASES = ['CRITICAL ASSEMBLY — IDLE', 'INITIATION', 'PROMPT CHAIN — SUPERCRITICAL', 'PEAK BURST', 'DISASSEMBLY — QUENCH', 'AFTERGLOW'];

function ignite() {
  if (phase !== 'idle' && phase !== 'done') return;
  reset();
  phase = 'init'; tSim = 0;
  for (let i = 0; i < 90; i++) spawn((Math.random() - 0.5) * 0.06, (Math.random() - 0.5) * 0.06, (Math.random() - 0.5) * 0.06, 1);
}
function reset() {
  nAlive = 0; fissionCount = 0; wave = 0; tSim = 0; yieldRel = 0; kEff = 1.0; phase = 'idle';
  const c = new THREE.Color();
  for (let i = 0; i < N_NUCLEI; i++) {
    nucState[i] = 0; nucAge[i] = 0; aGlow.array[i] = 0;
    c.setRGB(nucBase[i * 3], nucBase[i * 3 + 1], nucBase[i * 3 + 2]);
    nuclei.setColorAt(i, c);
  }
  aGlow.needsUpdate = true; nuclei.instanceColor.needsUpdate = true;
}

// k(t): idle → prompt-supercritical spike → disassembly decay. Drives the capture
// probability so the population follows a physical burst while the SPATIAL cascade
// stays emergent from the transport (the ADR-023 "graphics track the math" spirit).
function updatePhase(dtSim) {
  if (phase === 'idle' || phase === 'done') { kEff = 1.0; return; }
  tSim += dtSim;
  if (phase === 'init') { kEff = 1.28; if (tSim > 0.35) { phase = 'growth'; tSim = 0; } }
  else if (phase === 'growth') { kEff = 1.66; if (nAlive > MAXN * 0.72) { phase = 'peak'; tSim = 0; } }
  else if (phase === 'peak') { kEff = 1.04 - tSim * 0.6; if (tSim > 0.4) { phase = 'quench'; tSim = 0; } }
  else if (phase === 'quench') { kEff = Math.max(0.45, 0.95 - tSim * 0.9); if (nAlive < 40) { phase = 'afterglow'; tSim = 0; } }
  else if (phase === 'afterglow') { kEff = 0.5; if (tSim > 2.0) phase = 'done'; }
  yieldRel = Math.min(999, fissionCount / (N_NUCLEI * 0.045));
}

// ------------------------------------------------------------------ per-frame
const clock = new THREE.Clock();
let acc = 0;
function frame(dtReal, doRender = true) {
  // auto time-dilation: slow way down through the prompt chain / peak (ADR-023)
  let target = 1.0;
  if (slowAuto) {
    if (phase === 'growth' || phase === 'peak') target = 0.06;
    else if (phase === 'init' || phase === 'quench') target = 0.22;
    else if (phase === 'afterglow') target = 0.5;
  }
  timeScale += (target - timeScale) * Math.min(1, dtReal * 4);
  const dt = dtReal * timeScale;

  updatePhase(dt);

  // ---- neutron transport (bounded) ----
  const k = kEff;
  const pCap = Math.min(0.94, Math.max(0.02, k / 2.35));
  for (let i = nAlive - 1; i >= 0; i--) {
    nPos[i * 3] += nVel[i * 3] * dt; nPos[i * 3 + 1] += nVel[i * 3 + 1] * dt; nPos[i * 3 + 2] += nVel[i * 3 + 2] * dt;
    nClock[i] -= dt;
    if (nClock[i] > 0) continue;
    const x = nPos[i * 3], y = nPos[i * 3 + 1], z = nPos[i * 3 + 2];
    if (x * x + y * y + z * z > PIT_R * PIT_R) { kill(i); continue; }     // leakage
    if (Math.random() < pCap) {
      const j = nearestIdle(x, y, z);
      if (j >= 0) { fission(j, nGen[i] + 1); kill(i); continue; }
    }
    _v.randomDirection();
    nVel.set([_v.x * SPEED, _v.y * SPEED, _v.z * SPEED], i * 3);
    nClock[i] = mfp();
  }
  logN = nAlive > 0 ? Math.log10(nAlive) : 0;

  // ---- write neutron instances (position + velocity-stretch streak) ----
  for (let i = 0; i < nAlive; i++) {
    _v.set(nVel[i * 3], nVel[i * 3 + 1], nVel[i * 3 + 2]);
    const sp = _v.length() || 1;
    _q.setFromUnitVectors(UP, _v.divideScalar(sp));
    _d.position.set(nPos[i * 3], nPos[i * 3 + 1], nPos[i * 3 + 2]);
    _d.quaternion.copy(_q);
    _d.scale.set(NSIZE, NSIZE * 5.4, NSIZE);         // stretched along travel
    _d.updateMatrix();
    neutrons.setMatrixAt(i, _d.matrix);
  }
  for (let i = nAlive; i < neutrons.count; i++) neutrons.setMatrixAt(i, _hide);
  neutrons.instanceMatrix.needsUpdate = true;

  // ---- nucleus flash decay ----
  let glowDirty = false, colorDirty = false;
  const c = new THREE.Color();
  for (let i = 0; i < N_NUCLEI; i++) {
    if (nucState[i] !== 1) continue;
    nucAge[i] += dt; glowDirty = true;
    const a = nucAge[i];
    aGlow.array[i] = Math.max(0, 1 - a * 5.5);
    if (a > 0.26) {
      nucState[i] = 2; aGlow.array[i] = 0;
      c.setRGB(nucBase[i * 3] * SPENT, nucBase[i * 3 + 1] * SPENT, nucBase[i * 3 + 2] * SPENT);
      nuclei.setColorAt(i, c); colorDirty = true;
    }
  }
  if (glowDirty) aGlow.needsUpdate = true;
  if (colorDirty) nuclei.instanceColor.needsUpdate = true;

  // ---- burst core + lights driven by population ----
  const inten = Math.min(1, nAlive / MAXN);   // linear fill fraction → clean growth→peak ramp
  const burst = inten * inten;
  core.material.opacity = burst * 0.38;
  core.scale.setScalar(0.35 + burst * 1.5);
  for (let i = 0; i < glowShells.length; i++) {
    glowShells[i].material.opacity = burst * (0.22 - i * 0.05);
    glowShells[i].scale.setScalar(0.6 + burst * (1.4 + i * 0.5));
  }
  coreLight.intensity = burst * 20;
  fill.intensity = 14 + burst * 14;
  grade.uniforms.uBurst.value = burst;
  godrays.uniforms.uStrength.value = 0.03 + burst * 0.3;   // shafts intensify with the burst
  boundary.material.opacity = 0.04 + burst * 0.05;

  // camera: gently ease closer as the chain builds (zoom = focus, ADR-023)
  controls.autoRotateSpeed = 0.35 + burst * 0.5;
  controls.update();

  grade.uniforms.uTime.value = (acc += dtReal);
  if (doRender) composer.render();
  updateHud();
}
let paused = false;
function tick() { if (!paused) frame(Math.min(0.05, clock.getDelta())); requestAnimationFrame(tick); }

// ------------------------------------------------------------------ HUD
const el = (id) => document.getElementById(id);
function updateHud() {
  el('rK').textContent = kEff.toFixed(3);
  el('rKfill').style.width = `${Math.min(100, (kEff / 2.0) * 100)}%`;
  el('rN').textContent = nAlive.toLocaleString();
  el('rGen').textContent = wave;
  el('rFiss').textContent = fissionCount.toLocaleString();
  el('rLogN').textContent = logN.toFixed(2);
  el('rYield').textContent = yieldRel.toFixed(1);
  const pi = { idle: 0, init: 1, growth: 2, peak: 3, quench: 4, afterglow: 5, done: 5 }[phase] ?? 0;
  el('phaseLabel').textContent = PHASES[pi];
  el('clock').textContent = `t = ${tSim.toFixed(2)} · ${timeScale.toFixed(2)}×`;
}

// ------------------------------------------------------------------ UI + resize
el('ignite').onclick = ignite;
el('reset').onclick = reset;
el('slowmo').onclick = () => { slowAuto = !slowAuto; el('slowmo').textContent = `◐ SLOW-MO: ${slowAuto ? 'AUTO' : 'OFF'}`; };
addEventListener('keydown', (e) => { if (e.code === 'Space') { e.preventDefault(); ignite(); } });
addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight); composer.setSize(innerWidth, innerHeight);
  bloom.setSize(innerWidth, innerHeight);
  grade.uniforms.uRes.value.set(innerWidth, innerHeight); dof.uniforms.uRes.value.set(innerWidth, innerHeight);
});

// debug/introspection + agent-eyes hook (autonomous screenshot loop, per the
// Booster-Lander insight: capture the composited canvas in-page → downscaled JPEG
// data URL, works with the pane hidden; move the camera to any pose to inspect).
window.__cv = {
  THREE, renderer, scene, camera, nuclei, neutrons,
  get state() { return { phase, nAlive, fissionCount, wave, kEff: +kEff.toFixed(3), logN: +logN.toFixed(2), timeScale: +timeScale.toFixed(3) }; },
  // capture the post-processed canvas → small JPEG data URL (preserveDrawingBuffer=true)
  shot(maxW = 720, q = 0.55) {
    const cv = renderer.domElement;
    const s = Math.min(1, maxW / cv.width);
    const w = Math.round(cv.width * s), h = Math.round(cv.height * s);
    const t = document.createElement('canvas'); t.width = w; t.height = h;
    t.getContext('2d').drawImage(cv, 0, 0, w, h);
    return t.toDataURL('image/jpeg', q);
  },
  // reposition the camera (disables auto-orbit so the pose holds for a clean capture)
  pose(eye, target = [0, 0, 0]) {
    controls.autoRotate = false;
    camera.position.set(eye[0], eye[1], eye[2]);
    controls.target.set(target[0], target[1], target[2]);
    controls.update();
  },
  set slowmoAuto(v) { slowAuto = v; },
  step: (dt = 0.02, render = true) => frame(dt, render),   // deterministic fixed-dt advance
  pause: (v = true) => { paused = v; },
  ignite, reset,
};

updateHud();
tick();

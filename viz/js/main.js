// main.js — scene, post chain, UI wiring, and the run sequencer.
// All run visuals are pure functions of display-time t, so scrubbing is exact.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { buildDevice } from './device.js';
import { createSimStub } from './simstub.js';

const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = (t) => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
const easeOut = (t) => 1 - Math.pow(1 - clamp(t, 0, 1), 3);

// ---------------------------------------------------------------- renderer
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.15;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.localClippingEnabled = true;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x05070c);
scene.fog = new THREE.FogExp2(0x05070c, 0.045);

const camera = new THREE.PerspectiveCamera(45, innerWidth / innerHeight, 0.1, 120);
camera.position.set(4.8, 1.9, 6.4);
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, -0.15, 0);
controls.enableDamping = true;
controls.maxDistance = 18; controls.minDistance = 1.2;

const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const bloom = new UnrealBloomPass(new THREE.Vector2(innerWidth, innerHeight), 0.85, 0.55, 0.85);
composer.addPass(bloom);
composer.addPass(new OutputPass());

// ---------------------------------------------------------------- environment
{
  const floor = new THREE.Mesh(
    new THREE.CircleGeometry(16, 64),
    new THREE.MeshStandardMaterial({ color: 0x0b0f16, metalness: 0.45, roughness: 0.65 }));
  floor.rotation.x = -Math.PI / 2; floor.position.y = -2.16; floor.receiveShadow = true;
  scene.add(floor);
  const grid = new THREE.GridHelper(24, 48, 0x14283a, 0x0d1622);
  grid.position.y = -2.15; scene.add(grid);

  scene.add(new THREE.AmbientLight(0x2a3a52, 0.7));
  const key = new THREE.SpotLight(0xfff1dc, 320, 40, 0.55, 0.5, 1.6);
  key.position.set(6, 9, 4); key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048); key.shadow.bias = -0.0004;
  scene.add(key);
  const rim = new THREE.PointLight(0x46d6ff, 60, 30, 1.8); rim.position.set(-6.5, 2.5, -5); scene.add(rim);
  const fill = new THREE.PointLight(0xffb347, 25, 25, 1.8); fill.position.set(4.5, -1, 5); scene.add(fill);
}
const coreLight = new THREE.PointLight(0xff7a2a, 0, 12, 1.6); scene.add(coreLight);
const blastLight = new THREE.PointLight(0xfff3d0, 0, 60, 1.4); scene.add(blastLight);

// ---------------------------------------------------------------- device
const dev = buildDevice();
scene.add(dev.root);
const { blocks, parts } = dev;

// shock shell
const shock = new THREE.Mesh(
  new THREE.SphereGeometry(1, 48, 32),
  new THREE.MeshBasicMaterial({ color: 0xffcf8a, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
scene.add(shock);

// fireball
const fireball = new THREE.Mesh(
  new THREE.IcosahedronGeometry(1, 3),
  new THREE.MeshBasicMaterial({ color: 0xfff1cf, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false }));
const fbBase = fireball.geometry.getAttribute('position').array.slice();
const fbJit = new Float32Array(fbBase.length);
for (let i = 0; i < fbJit.length; i++) fbJit[i] = (Math.random() - 0.5) * 0.22;
scene.add(fireball);

// neutron tracer particles
const P = 3000;
const pGeo = new THREE.BufferGeometry();
const pPos = new Float32Array(P * 3), pCol = new Float32Array(P * 3);
const pDir = new Float32Array(P * 3), pSpd = new Float32Array(P), pOff = new Float32Array(P);
{
  const c1 = new THREE.Color(0xffb347), c2 = new THREE.Color(0xfff6e0), tmp = new THREE.Color();
  for (let i = 0; i < P; i++) {
    const v = new THREE.Vector3().randomDirection();
    pDir.set([v.x, v.y, v.z], i * 3);
    pSpd[i] = 0.4 + Math.random() * 2.4;
    pOff[i] = Math.random() * 0.55;
    tmp.lerpColors(c1, c2, Math.random());
    pCol.set([tmp.r, tmp.g, tmp.b], i * 3);
    pPos.set([0, -999, 0], i * 3);
  }
}
pGeo.setAttribute('position', new THREE.BufferAttribute(pPos, 3));
pGeo.setAttribute('color', new THREE.BufferAttribute(pCol, 3));
const pMat = new THREE.PointsMaterial({ size: 0.022, vertexColors: true, transparent: true,
  opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, sizeAttenuation: true });
const neutrons = new THREE.Points(pGeo, pMat); scene.add(neutrons);

// smoke (fizzle)
const S = 240;
const sGeo = new THREE.BufferGeometry();
const sPos = new Float32Array(S * 3), sDir = new Float32Array(S * 3), sSpd = new Float32Array(S);
for (let i = 0; i < S; i++) {
  const v = new THREE.Vector3().randomDirection();
  sDir.set([v.x, Math.abs(v.y) * 0.6 + 0.2, v.z], i * 3);
  sSpd[i] = 0.15 + Math.random() * 0.5;
  sPos.set([0, -999, 0], i * 3);
}
sGeo.setAttribute('position', new THREE.BufferAttribute(sPos, 3));
const sMat = new THREE.PointsMaterial({ size: 0.3, color: 0x555c66, transparent: true,
  opacity: 0, depthWrite: false });
const smoke = new THREE.Points(sGeo, sMat); scene.add(smoke);

// ---------------------------------------------------------------- labels
const labelBox = document.getElementById('labels');
const labelEls = dev.anchors.map(a => {
  const el = document.createElement('div');
  el.className = 'lbl'; el.textContent = a.name;
  labelBox.appendChild(el);
  return { a, el };
});
const _v = new THREE.Vector3();
function updateLabels(explodeVal) {
  for (const { a, el } of labelEls) {
    const vis = a.layer ? dev.root && true : true;
    const layerOn = document.querySelector(`input[data-layer="${a.layer}"]`)?.checked ?? true;
    if (!layerOn || explodeVal > 0.85) { el.style.opacity = 0; continue; }
    a.obj.getWorldPosition(_v).add(a.off);
    _v.project(camera);
    if (_v.z > 1) { el.style.opacity = 0; continue; }
    el.style.opacity = 0.92;
    el.style.left = `${(_v.x * 0.5 + 0.5) * innerWidth}px`;
    el.style.top = `${(-_v.y * 0.5 + 0.5) * innerHeight}px`;
  }
}

// ---------------------------------------------------------------- UI state
const $ = (id) => document.getElementById(id);
const sim = createSimStub();
const params = { mass: 0.62, enrich: 0.70, refl: 0.55, comp: 0.60, jitter: 0.15, init: 0.65 };
const bind = (id, key) => {
  const el = $(id), out = el.parentElement.querySelector('span');
  const show = () => out.textContent = Number(el.value).toFixed(2);
  el.addEventListener('input', () => { params[key] = +el.value; show(); refreshGauge(); });
  show();
};
bind('p_mass', 'mass'); bind('p_enrich', 'enrich'); bind('p_refl', 'refl');
bind('p_comp', 'comp'); bind('p_jitter', 'jitter'); bind('p_init', 'init');

const failedSet = new Set();
function faultCount() { return failedSet.size; }
function refreshGauge() {
  const { k, ready } = sim.evaluate(params, faultCount());
  $('kval').textContent = k.toFixed(2);
  $('kfill').style.width = `${(k / 1.6) * 100}%`;
  const lamp = $('lamp');
  lamp.className = 'lamp ' + (ready ? 'ready' : 'sub');
  lamp.textContent = ready ? 'READY — SUPERCRITICAL' : 'SUBCRITICAL';
}
refreshGauge();

// layer toggles
document.querySelectorAll('input[data-layer]').forEach(cb =>
  cb.addEventListener('change', () => dev.setLayerVisible(cb.dataset.layer, cb.checked)));

// explode + clip
let explodeVal = 0;
$('explode').addEventListener('input', e => {
  if (run && playing) return;              // run owns the shells while playing
  explodeVal = +e.target.value;
  dev.setExplode(explodeVal);
});
const clipPlane = new THREE.Plane(new THREE.Vector3(0, -1, 0), 99);
$('clip').addEventListener('input', e => {
  const v = +e.target.value;               // 1 = no slice (plane parked above)
  if (v >= 0.999) { dev.setClipPlane(null); return; }
  clipPlane.constant = lerp(-1.75, 1.75, (v + 1) / 2);
  dev.setClipPlane(clipPlane);
});

// block picking (click, not drag) — listen to both pointer and mouse flavors
const ray = new THREE.Raycaster(); let downAt = null;
const onDown = e => downAt = [e.clientX, e.clientY];
const onUp = e => {
  if (!downAt) return;
  const moved = Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]); downAt = null;
  if (moved > 5 || (run && playing)) return;   // pickable when idle, paused, or post-run
  const m = new THREE.Vector2((e.clientX / innerWidth) * 2 - 1, -(e.clientY / innerHeight) * 2 + 1);
  ray.setFromCamera(m, camera);
  const hit = ray.intersectObjects(blocks, false)[0];
  if (hit) {
    const b = hit.object, i = b.userData.idx;
    const failed = !failedSet.has(i);
    failed ? failedSet.add(i) : failedSet.delete(i);
    dev.setBlockFailed(b, failed);
    $('faultCount').textContent = faultCount();
    refreshGauge();
  }
};
for (const ev of ['pointerdown', 'mousedown']) renderer.domElement.addEventListener(ev, onDown);
for (const ev of ['pointerup', 'mouseup']) renderer.domElement.addEventListener(ev, onUp);
$('clearFaults').addEventListener('click', () => {
  failedSet.clear();
  for (const b of blocks) dev.setBlockFailed(b, false);
  $('faultCount').textContent = 0; refreshGauge();
});

// ---------------------------------------------------------------- sequencer
let run = null, runT = 0, playing = false, cardShown = false;

$('commit').addEventListener('click', () => {
  explodeVal = 0; $('explode').value = 0; dev.setExplode(0);
  dev.setClipPlane(null); $('clip').value = 1;
  const dirs = blocks.map(b => b.userData.dir);
  run = sim.generateRun(params, [...failedSet], dirs);
  runT = 0; playing = true; cardShown = false;
  $('outcome').classList.add('hidden');
  $('play').disabled = false; $('restart').disabled = false;
  $('play').textContent = '⏸';
  // phase marks
  const marks = $('phaseMarks'); marks.innerHTML = '';
  for (const ph of run.phases.slice(1)) {
    const i = document.createElement('i');
    i.style.left = `${(ph.t0 / run.duration) * 100}%`;
    marks.appendChild(i);
  }
});
$('play').addEventListener('click', () => {
  if (!run) return;
  playing = !playing;
  $('play').textContent = playing ? '⏸' : '▶';
});
$('restart').addEventListener('click', () => {
  if (!run) return;
  runT = 0; playing = true; cardShown = false;
  $('outcome').classList.add('hidden'); $('play').textContent = '⏸';
});
document.querySelector('#transport .track').addEventListener('pointerdown', e => {
  if (!run) return;
  const r = e.currentTarget.getBoundingClientRect();
  runT = clamp((e.clientX - r.left) / r.width, 0, 1) * run.duration;
  cardShown = false; $('outcome').classList.add('hidden');
});
$('speed').addEventListener('input', e => {
  $('speedVal').textContent = `${Math.pow(10, +e.target.value).toFixed(2).replace(/\.?0+$/, '')}×`;
});
addEventListener('keydown', e => {
  if (e.code === 'Space' && run) { e.preventDefault(); $('play').click(); }
  if (e.code === 'KeyR' && run) $('restart').click();
});
$('outcomeClose').addEventListener('click', () => {
  $('outcome').classList.add('hidden');
  runT = 0; playing = false; $('play').textContent = '▶';
});

function showOutcome() {
  const t = $('outcomeTitle');
  t.textContent = run.detonate ? 'DETONATION' : 'FIZZLE';
  t.className = run.detonate ? 'det' : 'fiz';
  $('outcomeReasons').innerHTML = run.reasons.map(r => `<li>${r}</li>`).join('');
  $('outcomeYield').innerHTML = run.detonate
    ? `estimated yield <b>${run.yieldKt} kt</b> · <em>synthetic stub — not a real calculation</em>`
    : `nuclear yield ≈ <b>0</b> · chemical scatter only · <em>synthetic stub</em>`;
  $('outcome').classList.remove('hidden');
}

// ------------------------------------------------- pure f(t) visual application
function applyRun(t) {
  const [idle, he, comp, excur, out] = run.phases;
  const inPhase = (ph) => t >= ph.t0 && t < ph.t1;

  // HE fire: per-block emissive pulse at its fire time (failed blocks stay dark)
  for (let i = 0; i < blocks.length; i++) {
    const b = blocks[i];
    if (failedSet.has(i)) continue;
    const ft = run.fireTimes[i];
    const x = (t - ft) / 0.22;
    const on = x > 0 && x < 1;
    b.material.emissive.setHex(0xffca7a);
    b.material.emissiveIntensity = on ? Math.sin(x * Math.PI) * 2.4 : 0;
  }

  // shock shell during he + compression
  if (t >= he.t0 && t < comp.t1) {
    const x = t < he.t1 ? 1 : 1 - smooth((t - comp.t0) / (comp.t1 - comp.t0)) * 0.75;
    shock.scale.setScalar(0.85 * x);
    shock.material.opacity = 0.10 + 0.22 * Math.abs(Math.sin(t * 14));
  } else shock.material.opacity = 0;

  // compression: shells squeeze (skewed if faults / jitter)
  const c = run.compression(t);
  const skew = run.asymAmt * (1 - c);
  for (const [mesh, skewK] of [[parts.pusher, 1.0], [parts.reflector, 0.8], [parts.core, 0.5]]) {
    mesh.scale.setScalar(c);
    mesh.scale.x *= 1 + skew * 0.5 * skewK;
    mesh.scale.z *= 1 - skew * 0.3 * skewK;
    mesh.position.copy(run.asym).multiplyScalar(skew * 0.22 * skewK);
  }

  // excursion: core glow + tracer burst
  if (t >= excur.t0) {
    const x = clamp((t - excur.t0) / (excur.t1 - excur.t0), 0, 1);
    const f = run.flux(t), fMax = run.flux(excur.t1);
    parts.core.material.emissiveIntensity = clamp((f / fMax) * 4, 0, 4);
    coreLight.intensity = run.detonate ? 220 * x : 25 * x * (1 - x);
    const active = t < out.t0 + 0.5;
    pMat.opacity = active ? smooth((t - excur.t0) / 0.25) * (run.detonate ? 0.95 : 0.5) : 0;
    if (active) {
      for (let i = 0; i < P; i++) {
        const st = excur.t0 + pOff[i] * (run.detonate ? 0.6 : 1.0);
        const age = t - st;
        if (age <= 0) { pPos.set([0, -999, 0], i * 3); continue; }
        const r = pSpd[i] * age * (run.detonate ? 2.2 : 0.8);
        pPos[i * 3] = pDir[i * 3] * r;
        pPos[i * 3 + 1] = pDir[i * 3 + 1] * r;
        pPos[i * 3 + 2] = pDir[i * 3 + 2] * r;
      }
      pGeo.attributes.position.needsUpdate = true;
    }
  } else { pMat.opacity = 0; coreLight.intensity = 0; parts.core.material.emissiveIntensity = 0; }

  // outcome
  if (t >= out.t0) {
    const x = (t - out.t0) / (out.t1 - out.t0);
    if (run.detonate) {
      // white flash
      $('flash').style.opacity = Math.max(0, 1 - x * 3.2);
      // fireball
      const s = 0.2 + easeOut(x) * 4.0;
      fireball.scale.setScalar(s);
      fireball.material.opacity = clamp(1.2 - x * 0.55, 0, 1);
      const posA = fireball.geometry.getAttribute('position');
      for (let i = 0; i < posA.count; i++) {
        posA.array[i * 3]     = fbBase[i * 3]     + fbJit[i * 3]     * Math.sin(t * 11 + i);
        posA.array[i * 3 + 1] = fbBase[i * 3 + 1] + fbJit[i * 3 + 1] * Math.sin(t * 13 + i * 2);
        posA.array[i * 3 + 2] = fbBase[i * 3 + 2] + fbJit[i * 3 + 2] * Math.sin(t * 9 + i * 3);
      }
      posA.needsUpdate = true;
      blastLight.intensity = lerp(1600, 220, easeOut(x));
      bloom.strength = lerp(1.6, 0.9, easeOut(x));
      // camera shake, decaying
      const sh = (1 - easeOut(x)) * 0.05;
      controls.target.set((Math.random() - 0.5) * sh, -0.15 + (Math.random() - 0.5) * sh, (Math.random() - 0.5) * sh);
    } else {
      // fizzle: blocks + shells drift apart, smoke, no flash
      const drift = easeOut(Math.min(x * 1.4, 1));
      for (let i = 0; i < blocks.length; i++) {
        const b = blocks[i];
        const k = 0.25 + (i % 7) * 0.05;
        b.position.copy(b.userData.dir).multiplyScalar(b.userData.r0 + drift * k);
        b.rotation.x += 0.002 * (i % 3); b.rotation.z += 0.002 * ((i + 1) % 3);
      }
      for (const [mesh, k] of [[parts.pusher, 0.35], [parts.reflector, 0.5], [parts.core, 0.6]]) {
        mesh.position.y = drift * k * 0.5;
      }
      sMat.opacity = 0.35 * smooth(x / 0.3) * (1 - smooth((x - 0.6) / 0.4));
      for (let i = 0; i < S; i++) {
        const r = sSpd[i] * (t - out.t0);
        sPos[i * 3] = sDir[i * 3] * r; sPos[i * 3 + 1] = -0.4 + sDir[i * 3 + 1] * r; sPos[i * 3 + 2] = sDir[i * 3 + 2] * r;
      }
      sGeo.attributes.position.needsUpdate = true;
    }
  } else {
    $('flash').style.opacity = 0;
    fireball.material.opacity = 0; blastLight.intensity = 0; bloom.strength = 0.85;
    sMat.opacity = 0;
    if (!playing || t < he.t0) {                       // restore resting layout pre-run
      for (const b of blocks) b.position.copy(b.userData.dir).multiplyScalar(b.userData.r0);
      for (const mesh of [parts.pusher, parts.reflector, parts.core]) mesh.position.set(0, 0, 0);
    }
  }

  // phase label + end-of-run card
  const ph = run.phases.find(p => t >= p.t0 && t < p.t1) || run.phases[run.phases.length - 1];
  $('phaseLabel').textContent = ph.name;
  if (t >= run.duration - 0.01 && !cardShown) { cardShown = true; playing = false; $('play').textContent = '▶'; showOutcome(); }
}

// ---------------------------------------------------------------- main loop
const clock = new THREE.Clock();
function frame() {
  requestAnimationFrame(frame);
  const dt = Math.min(clock.getDelta(), 0.05);
  if (run && playing) runT = Math.min(runT + dt * Math.pow(10, +$('speed').value), run.duration);
  if (run) {
    applyRun(runT);
    $('scrub').style.width = `${(runT / run.duration) * 100}%`;
    $('clock').textContent = `t ${runT.toFixed(2)} / ${run.duration.toFixed(1)}`;
  }
  controls.update();
  updateLabels(explodeVal);
  composer.render();
}

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
  composer.setSize(innerWidth, innerHeight);
});
renderer.setSize(innerWidth, innerHeight);
composer.setSize(innerWidth, innerHeight);
frame();

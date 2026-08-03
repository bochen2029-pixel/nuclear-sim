// simstub.js — SYNTHETIC physics stub. *** This is the seam. ***
// Swap this module for a WebSocket/HTTP adapter to your CUDA core later.
// The UI only ever calls evaluate() and generateRun() — keep those two
// signatures and nothing else in the front-end changes.
//
// Everything below is invented math for visualization only.

function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const seedOf = (params, faults) => {
  const s = JSON.stringify([params, faults]);
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  return h >>> 0;
};
const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const smooth = (t) => t * t * (3 - 2 * t);

export function createSimStub() {

  // live "k_eff-ish" gauge from the parameter panel — pure UI candy.
  function evaluate(p, faultCount) {
    const k = clamp(
      0.35 + 0.45 * p.mass + 0.18 * p.enrich + 0.14 * p.refl +
      0.22 * p.comp + 0.08 * p.init - 0.03 * faultCount, 0, 1.6);
    return { k, ready: k >= 1.0 };
  }

  // pre-computes one full deterministic run; all visuals are pure f(t),
  // which is what makes timeline scrubbing trivially correct.
  function generateRun(p, failedIdx, blockDirs) {
    const rand = mulberry32(seedOf(p, failedIdx));
    const faults = failedIdx.length;
    const { k } = evaluate(p, faults);

    // --- outcome rule (synthetic, illustrative) ---
    const symmetry = 1 - faults / 32 - 0.5 * p.jitter;
    const supercritical = k >= 1.0;
    const detonate = supercritical && faults <= 1 && symmetry > 0.55;
    const reasons = [];
    if (!supercritical) reasons.push(`subcritical configuration — k̂ ${k.toFixed(2)} < 1.00 (synthetic)`);
    if (faults > 1) reasons.push(`${faults} lens blocks inert — implosion wave breaks symmetry`);
    if (symmetry <= 0.55 && faults <= 1) reasons.push(`HE timing jitter σ too high — convergent wave torn (σ̂ ${(p.jitter * 100) | 0}%)`);
    if (detonate) reasons.push('convergent compression achieved — runaway excursion (synthetic)');

    // --- phase timeline (display seconds at 1× warp) ---
    const phases = [
      { id: 'idle',  name: 'armed idle',        t0: 0.0, t1: 0.6 },
      { id: 'he',    name: 'HE array fire',     t0: 0.6, t1: 1.6 },
      { id: 'comp',  name: 'implosion / compression', t0: 1.6, t1: 3.0 },
      { id: 'excur', name: detonate ? 'supercritical excursion' : 'aborted excursion', t0: 3.0, t1: 4.2 },
      { id: 'out',   name: detonate ? 'detonation' : 'fizzle — disassembly', t0: 4.2, t1: 6.5 },
    ];
    const duration = 6.5;

    // per-block fire times within the HE window, widened by jitter
    const fireTimes = blockDirs.map((_, i) =>
      phases[1].t0 + (i / blockDirs.length) * 0.25 + rand() * (0.15 + 0.55 * p.jitter));

    // asymmetry vector from failed blocks (drives skewed compression)
    // plain {x,y,z} math — this module is engine-free (no THREE import);
    // Vector3.copy()/multiplyScalar() on the main.js side accept plain objects.
    const asym = { x: 0, y: 0, z: 0 };
    for (let i = 0; i < blockDirs.length; i++) {
      if (failedIdx.includes(i)) { asym.x += blockDirs[i].x; asym.y += blockDirs[i].y; asym.z += blockDirs[i].z; }
    }
    const asymLen = Math.hypot(asym.x, asym.y, asym.z);
    if (asymLen > 0) { asym.x /= asymLen; asym.y /= asymLen; asym.z /= asymLen; }
    const asymAmt = clamp(faults / 4 + p.jitter * 0.35, 0, 1);

    // compression target: 1 → ~0.34 symmetric; worse with asymmetry
    const compTarget = 0.34 + 0.30 * asymAmt * (detonate ? 0.5 : 1.0);

    const flux = (t) => {                    // synthetic neutron population curve
      if (t < phases[3].t0) return 0;
      const x = (t - phases[3].t0) / (phases[3].t1 - phases[3].t0);
      return detonate ? Math.exp(9 * x) : Math.exp(3.5 * x) * (1 - smooth(x));
    };
    const compression = (t) => {
      const { t0, t1 } = phases[2];
      if (t <= t0) return 1;
      if (t >= t1) return compTarget;
      return 1 - (1 - compTarget) * smooth((t - t0) / (t1 - t0));
    };
    const yieldKt = detonate ? (4 + 26 * p.mass * p.comp * (1 - p.jitter * 0.6)).toFixed(1) : '0.00';

    return { phases, duration, detonate, reasons, k, fireTimes, asym, asymAmt,
             compTarget, flux, compression, yieldKt };
  }

  return { evaluate, generateRun };
}

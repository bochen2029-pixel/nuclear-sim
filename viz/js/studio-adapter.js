// studio-adapter.js — the REAL fusion binding for the visualizer (M-track start).
//
// A drop-in replacement for viz/js/simstub.js's createSimStub(), but backed by the
// real nscore Monte-Carlo engine instead of synthetic formulae: it fetches
// `tools/studio_server.py` (which shells to the `studio_bridge` exe over
// src/api/evaluate_json / generate_run_json) and returns the SAME shape simstub
// does — so main.js is unchanged EXCEPT that evaluate/generateRun become async
// (they run a real eigen/burst — seconds — so the call sites must `await` + the UI
// must debounce; see docs/viz-adapter-worklog.md for the exact main.js recipe).
//
//   const sim = createStudioAdapter('http://127.0.0.1:8100');
//   const gauge = await sim.evaluate(cfg);                       // {k_eff,k_prompt,sigma_pcm,ready}
//   const run   = await sim.generateRun(cfg, detonators, dirs);  // same shape as simstub
//
// REAL from nscore: run.tally (03 §5), run.run (03 §6), run.samples[] (the real
// per-generation fission SITES — the pitscope "main course"), detonate, reasons,
// yieldKt, kEff, non_canonical. RECONSTRUCTED presentation: phases/duration (render
// timeline), the detonator geometry (fireTimes/asym — the bare demon core doesn't
// model lenses), compression(t) = 1 -> ratio^(-1/3) (the REAL radius fraction), and
// flux(t) = the REAL samples' log10_population interpolated over the excursion phase.

const BETA_EFF = 0.0065;

const _clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, x));
const _smooth = (x) => { x = _clamp(x, 0, 1); return x * x * (3 - 2 * x); };

function mulberry32(a) {
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// A stable seed for the (presentation-only) detonator reconstruction.
function seedOf(cfg, disabledIdx) {
  let h = 2166136261 >>> 0;
  const s = JSON.stringify([cfg["compression.ratio"], cfg["pit.mass_kg"],
                            cfg["lenses.jitter_ns"], disabledIdx]);
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  return h;
}

async function _post(url, body, timeoutMs = 60000) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const res = await fetch(url, {
      method: 'POST', body: JSON.stringify(body),
      headers: { 'Content-Type': 'application/json' }, signal: ctl.signal,
    });
    if (!res.ok) throw new Error(`${url} -> HTTP ${res.status}: ${(await res.text()).slice(0, 300)}`);
    return await res.json();
  } finally {
    clearTimeout(timer);
  }
}

export function createStudioAdapter(baseUrl = 'http://127.0.0.1:8100') {
  baseUrl = baseUrl.replace(/\/$/, '');

  // Real criticality gauge — a genuine MC eigen (seconds). Same shape as simstub.
  async function evaluate(cfg) {
    const g = await _post(`${baseUrl}/evaluate`, cfg);
    return { k_eff: g.k_eff, k_prompt: g.k_prompt, sigma_pcm: g.sigma_pcm, ready: g.ready };
  }

  // Real emergent burst; reconstruct simstub's presentation layer from the data.
  async function generateRun(cfg, detonators = [], blockDirs = []) {
    const data = await _post(`${baseUrl}/generate-run`, cfg);
    const detonate = !!data.detonate;

    // --- render timeline (D6 staged clock); names driven by the REAL outcome ---
    const phases = [
      { id: 'idle',  name: 'armed — awaiting he_initiation',           t0: 0.0, t1: 0.6 },
      { id: 'he',    name: 'he_initiation — lens array fire',          t0: 0.6, t1: 1.6 },
      { id: 'comp',  name: 'compression (tier-2 hydro)',               t0: 1.6, t1: 3.0 },
      { id: 'excur', name: detonate ? 'burst — α excursion' : 'aborted excursion', t0: 3.0, t1: 4.2 },
      { id: 'out',   name: detonate ? 'detonation' : 'quench — disassembly (fizzle)', t0: 4.2, t1: 6.5 },
    ];
    const duration = 6.5;

    // --- detonator geometry: PRESENTATION (the bare demon core doesn't model lenses,
    //     step 5 + fast4) — reconstructed deterministically like simstub ---
    const jitterN = (cfg["lenses.jitter_ns"] ?? 10) / 20.0;
    const disabledIdx = detonators.map((d, i) => (d && d.enable ? -1 : i)).filter(i => i >= 0);
    const faults = disabledIdx.length;
    const rand = mulberry32(seedOf(cfg, disabledIdx));
    const fireTimes = blockDirs.map((_, i) =>
      phases[1].t0 + (i / Math.max(1, blockDirs.length)) * 0.25 + rand() * (0.15 + 0.55 * jitterN));
    const asym = { x: 0, y: 0, z: 0 };
    for (const i of disabledIdx) { const d = blockDirs[i]; if (d) { asym.x += d.x; asym.y += d.y; asym.z += d.z; } }
    const aLen = Math.hypot(asym.x, asym.y, asym.z);
    if (aLen > 0) { asym.x /= aLen; asym.y /= aLen; asym.z /= aLen; }
    const asymAmt = _clamp(faults / 4 + jitterN * 0.35, 0, 1);

    // --- compression(t): the REAL radius fraction ratio^(-1/3) (mass-conserving) ---
    const ratio = cfg["compression.ratio"] ?? 2.2;
    const compTarget = Math.pow(ratio, -1 / 3);
    const compression = (t) => {
      const { t0, t1 } = phases[2];
      if (t <= t0) return 1;
      if (t >= t1) return compTarget;
      return 1 - (1 - compTarget) * _smooth((t - t0) / (t1 - t0));
    };

    // --- flux(t): the REAL burst population trajectory (from data.samples) mapped
    //     onto the excursion phase, normalized to a brightness curve ---
    const samples = data.samples || [];
    const pops = samples.map(s => s.log10_population);
    const pmin = pops.length ? Math.min(...pops) : 0;
    const pmax = pops.length ? Math.max(...pops) : 1;
    const ex0 = phases[3].t0, ex1 = phases[3].t1;
    const flux = (t) => {
      if (t < ex0 || samples.length === 0) return 0;
      const x = _clamp((t - ex0) / (ex1 - ex0), 0, 1);
      const idx = x * (samples.length - 1);
      const i0 = Math.floor(idx), i1 = Math.min(samples.length - 1, i0 + 1);
      const lp = pops[i0] + (pops[i1] - pops[i0]) * (idx - i0);
      const norm = (lp - pmin) / Math.max(1e-6, pmax - pmin);   // 0..1 along the real trajectory
      return detonate ? Math.exp(9 * norm) : Math.exp(3.5 * norm) * (1 - _smooth(norm));
    };

    // --- REAL tally/run/samples pass through; report/context from the real data ---
    const tally = data.tally, run = data.run;
    const shellEdges = (tally.fission_mesh && tally.fission_mesh.shell_edges_cm) || [];
    const report = {
      yield_kt: data.yield_kt, fissions_total: tally.fissions_total,
      supercritical_reached: !!data.supercritical, quenched: !!data.quenched,
      generations: tally.timing.generations, eigen_calls: tally.timing.eigen_calls,
      max_q: tally.timing.max_q,
    };
    const context = {
      isotope_names: Object.keys(tally.fissions_by_isotope || { Pu239: 0, Pu240: 0 }),
      core_pu_isotopes: [0, 1], shell_edges_cm: shellEdges,
      m_pit_g: (cfg["pit.mass_kg"] ?? 6.15) * 1000,
      run_id: run.run_id, non_canonical: !!data.non_canonical,
      backend: run.backend || 'ref', engine: 'nscore (real Monte-Carlo)',
    };

    return {
      phases, duration, detonate, reasons: data.reasons || [], fireTimes, asym, asymAmt,
      compTarget, flux, compression, tally, run, samples, context, report,
      non_canonical: !!data.non_canonical, kEff: data.k_eff_peak, yieldKt: data.yield_kt,
    };
  }

  return { evaluate, generateRun, _backend: 'studio-adapter (real nscore)' };
}

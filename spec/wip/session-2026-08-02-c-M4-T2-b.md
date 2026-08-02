# WIP journal — session-2026-08-02-c — M4-T2-b (event-based GPU fixed-source transport + T-diff)

Append one line per non-obvious finding/dead-end/rejected approach, BEFORE acting on it.
Fold durable items into SESSIONS.md at END, then delete this file.

## Claim
- 2026-08-02: continuing as session-2026-08-02-c (3rd task this session). Reuses everything
  from M4-T1 (buffers, scan, fixed-point reduction, fork) and M4-T2-a (device tracker, device
  materials). Builds on the ToyWorld loader-toy in test_gpu.cpp; ref side is test_ref's World.

## Plan (incremental, each green-committed to the branch)
- Increment A: **event-based fixed-source transport, PURE CAPTURER → T-diff leakage vs ref.**
  The pure capturer (Σ_s=Σ_f=0) terminates in one superstep per history (leak, or collide→die),
  so it exercises the FULL event machinery (SoA init, tentative-event sample, prefix-sum
  partition, branchless leak/collide kernels, fixed-point tally) with a known answer
  exp(−Σ_c·R) and ref parity. G0c criterion: |leak_gpu − leak_ref| ≤ 3√(σ²+σ²).
- Increment B: **add scattering (multi-superstep) + fission production tally → T-diff k_inf.**
  Infinite-medium k_inf = νΣ_f/(Σ_c+Σ_f); GPU production/source vs ref within 3σ. Needs the
  per-isotope transfer + nu/chi/global_index M4-T2-a deferred — add to the device build here.
- Increment C: **deterministic fission bank** — bank ⌊w·ν_i·(Σ_f,i/Σ_t,i)/k + ξ⌋ progeny at
  the exclusive-prefix-sum slots (M4-T1 scan), stream = fork(parent). Test determinism (bank
  bit-identical across thread counts) + count parity vs ref's tallied production.

## Key design constraints (from spec)
- Determinism (01 §9/BLK-11): particle stream keyed by SOURCE INDEX, not thread/buffer pos;
  store (ctr,sub) in SoA and resume each superstep. Tallies via fixed-point int64 (M4-T1),
  never FP atomicAdd. Progeny at prefix-sum slots, fork streams. Bit-identity across configs.
- float per-event arithmetic (01 §9); the tracker eps is 1e-4f (particles landing ON a
  boundary use nudge-along-direction — M4-T2-a's d_nudge_and_locate).
- Implicit capture (E1c): never kill at fission; w ← w·Σ_s,i/Σ_t,i; sample isotope ∝ n_iΣ_t,i;
  scatter isotropic-in-lab, group from transfer[from][·]. Roulette E1e at w_min=1e-4/w_surv=1e-2.
- Flight on Σ_tr; collision/weight on Σ_t (the intentional asymmetry, M1-T2 note).
- Cross-backend is STATISTICAL only (G0c) — never assert ref==gpu bit-identity.

## Findings (append as they happen)
- ref keys each history's stream as `Stream(seed, source_id, ctr=history)` — history index goes
  in the COUNTER slot. Multi-block histories can overlap (history h draws blocks h,h+1,… while
  h+1 starts at h+1) → mild inter-history correlation. NOT my task to fix ref (M1-T2 done/green),
  and cross-backend is statistical, so the GPU uses the BETTER keying: particle p's stream =
  `fork(source_base, 0, p)` (splitmix-distributed unique stream id, non-overlapping). Store
  (ctr,sub) in SoA and resume each superstep; stream id recomputable from the particle's
  orig_index. This is exactly what `fork` is for (BLK-11 streams-from-identity).
- GPU uses `uniform_f` (float, 01 §9); ref uses `uniform_d`. Streams therefore differ →
  histories differ → compared STATISTICALLY only (G0c, within 3σ). Never assert ref==gpu bits.
- sigma computed like ref: per-HISTORY scores (per-particle score_leak/score_prod arrays), then
  mean + standard_error on the host — identical estimator to TallyAcc, and deterministic since
  each particle's score depends only on its index-keyed stream (bit-identity across configs).
- Scope call: deliver a correct, deterministic multi-superstep transport (dead-skip per
  superstep) + T-diff FIRST; prefix-sum COMPACTION of alive particles (reusing M4-T1 scan) is
  the "prefix-sum partition" the DoD names — add once the physics T-diff is green.

#include "app/nukebench/gate_report.h"

#include "app/nukebench/diff_criteria.h"  // G0c (b)/(c) comparisons (M1-T5-c-5)
#include "core/diagnostics.h"
#include "core/geometry/geometry.h"
#include "core/material/material.h"
#include "core/scenario/scenario.h"
#include "core/xs/xs.h"
#include "physics/eigen/eigen.h"
#include "ref/ref_transport.h"
#ifdef NUKESIM_WITH_CUDA
#include "gpu/eigen.h"  // host-callable gpu_eigen (M1-T5-c-2); CUDA-guarded
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ns::nukebench {

namespace {
using json = nlohmann::ordered_json;

/// Evaluate one criterion against a measured value. abs_le: |value| <= threshold; le:
/// value <= threshold. (03 §10 defines only these two ops for M1-T5-a's gates.)
bool passes(const std::string& op, double value, double threshold) {
    if (op == "abs_le") return std::abs(value) <= threshold;
    if (op == "le") return value <= threshold;
    return false;  // load_gates already rejected unknown ops
}

/// The measured value a criterion tests, from an attempt's measurements.
double measured_for(const std::string& name, const Attempt& a) {
    if (name == "k_deviation_pcm") return a.k_deviation_pcm;
    if (name == "sigma_pcm") return a.sigma_pcm;
    return std::nan("");  // criterion the runner doesn't measure -> never passes
}

// --- G0c (b)/(c) detailed per-seed eigen (M1-T5-c-5) ---

// Radial-shell granularity for criterion (b). MUST match gpu_eigen's kShells (src/gpu/eigen.cu):
// both bin over [0, r_outer] into this many equal-width shells so ref and gpu shells align.
constexpr int kDiffRadialShells = 8;

// What criteria (a)/(b)/(c) need from one seed on one backend.
struct SeedDetail {
    double k = 0.0;
    double sigma_pcm = 0.0;
    std::vector<double> active_k;    // per-gen k over the active window [inactive, inactive+active)
    std::vector<double> per_shell;   // final fission source binned into kDiffRadialShells shells
};

// The (b) comparison is of the NORMALIZED radial fission-source distribution (fractions), so it
// tests the spatial SHAPE, not the two backends' slightly-different total final-bank counts.
std::vector<double> shell_fractions(const std::vector<double>& counts) {
    double tot = 0.0;
    for (const double c : counts) tot += c;
    std::vector<double> f(counts.size(), 0.0);
    if (tot > 0.0) {
        for (std::size_t i = 0; i < counts.size(); ++i) f[i] = counts[i] / tot;
    }
    return f;
}

// Poisson 1-sigma of each shell FRACTION: sqrt(count) / total (the count has Poisson sigma sqrt(N)).
std::vector<double> shell_fraction_sigma(const std::vector<double>& counts) {
    double tot = 0.0;
    for (const double c : counts) tot += c;
    std::vector<double> s(counts.size(), 0.0);
    if (tot > 0.0) {
        for (std::size_t i = 0; i < counts.size(); ++i) s[i] = std::sqrt(std::max(0.0, counts[i])) / tot;
    }
    return s;
}

// Sample standard deviation of a k sequence — the per-GENERATION spread of k (criterion c's sigma_k,
// NOT the standard error of the mean; see diff_criteria.h).
double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double mean = 0.0;
    for (const double x : v) mean += x;
    mean /= static_cast<double>(v.size());
    double ss = 0.0;
    for (const double x : v) ss += (x - mean) * (x - mean);
    return std::sqrt(ss / static_cast<double>(v.size() - 1));
}

// Run one seed's eigen on `backend` and extract the (b)/(c) series. `r_max` = the sphere radius.
SeedDetail run_seed_detail(const std::string& backend, const ns::geom::LayerStack& stack,
                           const ns::material::MaterialLib& lib, const ns::xs::FewGroupXS& xs,
                           const Gate& gate, std::int64_t seed, std::int64_t batch, double r_max) {
    SeedDetail out;
    if (backend == "ref") {
        ns::ref::RefTransport transport(stack, lib, xs, static_cast<std::uint64_t>(seed));
        ns::physics::EigenSpec spec;
        spec.batch = batch;
        spec.inactive = gate.eigen.inactive;
        spec.active = gate.eigen.active;
        spec.seed = static_cast<std::uint64_t>(seed);
        const ns::physics::EigenResult er = ns::physics::run_eigen(transport, spec);
        out.k = er.k;
        out.sigma_pcm = er.sigma_pcm;
        // The last `active` k's = the settled window [inactive, inactive+active) — the SAME window
        // the gpu collects (gen >= inactive), so the two population series compare gen-for-gen.
        const std::size_t act = static_cast<std::size_t>(gate.eigen.active);
        out.active_k =
            er.k_history.size() >= act
                ? std::vector<double>(er.k_history.end() - static_cast<std::ptrdiff_t>(act),
                                      er.k_history.end())
                : er.k_history;
        std::vector<double> radii;
        radii.reserve(er.source.sites.size());
        for (const auto& s : er.source.sites) {
            radii.push_back(std::sqrt(s.pos.x * s.pos.x + s.pos.y * s.pos.y + s.pos.z * s.pos.z));
        }
        out.per_shell = radial_shell_histogram(radii, r_max, kDiffRadialShells);
        return out;
    }
    if (backend == "gpu") {
#ifdef NUKESIM_WITH_CUDA
        ns::gpu::EigenResultGpu g;
        if (!ns::gpu::gpu_eigen(stack, lib, static_cast<std::uint64_t>(seed), batch,
                                gate.eigen.inactive, gate.eigen.active, 256, 128, g)) {
            throw GatesError("gate " + gate.id + ": gpu_eigen failed at seed " +
                             std::to_string(seed));
        }
        out.k = g.k;
        out.sigma_pcm = g.k_sigma * 1e5;    // k_sigma = SE of k -> pcm
        out.active_k = g.k_history;          // already the active window, in gen order
        out.per_shell = g.per_shell_source;  // kShells shells over [0, radius]
        return out;
#else
        (void)stack;
        (void)lib;
        (void)xs;
        (void)r_max;
        throw GatesError("run_diff: gpu backend requires a CUDA build (NUKESIM_WITH_CUDA is OFF)");
#endif
    }
    throw GatesError("run_diff: backend '" + backend + "' not supported (expected ref|gpu)");
}
}  // namespace

GateReport run_gate(const Gate& gate, const std::filesystem::path& repo,
                    const std::string& backend, std::int64_t eigen_batch_override) {
    if (backend != "ref" && backend != "gpu") {
        throw GatesError("run_gate: backend '" + backend + "' not supported (expected ref|gpu)");
    }
    // Assembly (mirrors the M1-T4a-2a gate_probe): the gate's scenario gives the material +
    // radius; the gate's own seeds + [gate.eigen] config drive the run (not the scenario's).
    const auto scenario = ns::scenario::Scenario::load(repo / gate.scenario, repo);
    if (scenario.layers.empty()) throw GatesError("gate " + gate.id + ": scenario has no layers");
    const auto& layer = scenario.layers.front();

    const auto xs = ns::xs::FewGroupXS::load(repo / "data" / "xs" / "fast4.json");
    std::vector<ns::LoadWarning> warnings;
    const auto lib = ns::material::MaterialLib::load_dir(repo / "data" / "materials", xs, &warnings);
    const int mid = lib.index_of(layer.material);
    if (mid < 0) throw GatesError("gate " + gate.id + ": material '" + layer.material + "' not found");
    const ns::geom::LayerStack stack(
        {ns::geom::Layer{layer.id, layer.r_outer_cm, mid, layer.status}});

    const std::int64_t batch = eigen_batch_override > 0 ? eigen_batch_override : gate.eigen.batch;

    GateReport rep;
    rep.gate = gate.id;
    rep.backend = backend;
    bool all_pass = true;
    int n = 0;
    for (const std::int64_t seed : gate.seeds) {
        double k = 0.0, sigma_pcm = 0.0;
        if (backend == "ref") {
            ns::ref::RefTransport transport(stack, lib, xs, static_cast<std::uint64_t>(seed));
            ns::physics::EigenSpec spec;
            spec.batch = batch;
            spec.inactive = gate.eigen.inactive;
            spec.active = gate.eigen.active;
            spec.seed = static_cast<std::uint64_t>(seed);
            const auto res = ns::physics::run_eigen(transport, spec);
            k = res.k;
            sigma_pcm = res.sigma_pcm;
        } else {  // "gpu" (validated above)
#ifdef NUKESIM_WITH_CUDA
            // The GPU eigen's result is invariant to launch geometry (gpu/eigen.h); 256x128 is a
            // proven config that handles the C-900 batch. GPU-vs-ref parity is ADR-021.
            ns::gpu::EigenResultGpu out;
            if (!ns::gpu::gpu_eigen(stack, lib, static_cast<std::uint64_t>(seed), batch,
                                    gate.eigen.inactive, gate.eigen.active, 256, 128, out)) {
                throw GatesError("gate " + gate.id + ": gpu_eigen failed at seed "
                                 + std::to_string(seed));
            }
            k = out.k;
            sigma_pcm = out.k_sigma * 1e5;  // k_sigma = standard error of k -> pcm
#else
            throw GatesError("run_gate: gpu backend requires a CUDA build (NUKESIM_WITH_CUDA is OFF)");
#endif
        }

        Attempt a;
        a.attempt = ++n;
        a.seed = seed;
        a.k = k;
        a.sigma_pcm = sigma_pcm;
        a.k_deviation_pcm = (k - 1.0) * 1e5;

        bool seed_pass = true;
        for (const auto& c : gate.criteria) {
            CriterionResult cr;
            cr.name = c.name;
            cr.op = c.op;
            cr.value = measured_for(c.name, a);
            cr.threshold = c.value;
            cr.pass = passes(c.op, cr.value, cr.threshold);
            seed_pass = seed_pass && cr.pass;
            a.criteria.push_back(cr);
        }
        a.verdict = seed_pass ? "pass" : "fail";
        all_pass = all_pass && seed_pass;
        rep.attempts.push_back(std::move(a));
    }
    rep.verdict = all_pass ? "pass" : "fail";  // the caller downgrades to conditional if dirty
    return rep;
}

GateReport run_diff(const Gate& gate, const std::filesystem::path& repo,
                    const std::string& backend_a, const std::string& backend_b,
                    std::int64_t eigen_batch_override) {
    // The equivalence bound C-932 (pcm), from the gate's criterion (the loader drift-guarded it).
    double bound_pcm = 0.0;
    for (const auto& c : gate.criteria) {
        if (c.name == "k_equivalence_pcm") bound_pcm = c.value;
    }
    if (bound_pcm <= 0.0) {
        throw GatesError("gate " + gate.id +
                         ": run_diff needs a k_equivalence_pcm criterion (C-932)");
    }

    // Assembly (as in run_gate): the gate's scenario -> material + radius, on fast4. Built ONCE;
    // each seed runs the ref AND gpu eigen on it, keeping the per-generation + per-shell detail
    // criteria (b)/(c) need (which run_gate's summary discards).
    const auto scenario = ns::scenario::Scenario::load(repo / gate.scenario, repo);
    if (scenario.layers.empty()) throw GatesError("gate " + gate.id + ": scenario has no layers");
    const auto& layer = scenario.layers.front();
    const auto xs = ns::xs::FewGroupXS::load(repo / "data" / "xs" / "fast4.json");
    std::vector<ns::LoadWarning> warnings;
    const auto lib = ns::material::MaterialLib::load_dir(repo / "data" / "materials", xs, &warnings);
    const int mid = lib.index_of(layer.material);
    if (mid < 0) throw GatesError("gate " + gate.id + ": material '" + layer.material + "' not found");
    const ns::geom::LayerStack stack(
        {ns::geom::Layer{layer.id, layer.r_outer_cm, mid, layer.status}});
    const double r_max = stack.outermost_radius();
    const std::int64_t batch = eigen_batch_override > 0 ? eigen_batch_override : gate.eigen.batch;

    GateReport rep;
    rep.gate = gate.id;
    rep.backend = backend_a + "|" + backend_b;
    bool all_pass = true;
    int n = 0;
    for (const std::int64_t seed : gate.seeds) {
        const SeedDetail A = run_seed_detail(backend_a, stack, lib, xs, gate, seed, batch, r_max);
        const SeedDetail B = run_seed_detail(backend_b, stack, lib, xs, gate, seed, batch, r_max);

        const double delta_pcm = std::abs(A.k - B.k) * 1e5;
        const double comb_sigma = std::sqrt(A.sigma_pcm * A.sigma_pcm + B.sigma_pcm * B.sigma_pcm);

        Attempt d;
        d.attempt = ++n;
        d.seed = seed;
        d.k = A.k;                  // backend_a's k
        d.k_b = B.k;                // backend_b's k
        d.sigma_pcm = comb_sigma;
        d.k_deviation_pcm = delta_pcm;

        // (a) k-equivalence: |k_a − k_b| ≤ C-932 AND ≤ 3·combined σ (08 §2 a).
        CriterionResult abs_c{"k_equivalence_pcm", "abs_le", delta_pcm, bound_pcm,
                              delta_pcm <= bound_pcm};
        CriterionResult sig_c{"k_equivalence_within_3sigma", "le", delta_pcm, 3.0 * comb_sigma,
                              delta_pcm <= 3.0 * comb_sigma};

        // (b) per-shell fission-source equivalence: max(3·√(σ²), 2%·f_ref) per shell (08 §2 b),
        // on the NORMALIZED radial distribution (fractions) with Poisson per-shell σ. worst_ratio
        // ≤ 1 iff every shell passes.
        const DiffCheck shell =
            per_shell_equivalence(shell_fractions(A.per_shell), shell_fractions(B.per_shell),
                                  shell_fraction_sigma(A.per_shell), shell_fraction_sigma(B.per_shell));
        CriterionResult shell_c{"per_shell_equivalence_ratio", "le", shell.worst_ratio, 1.0,
                                shell.pass && shell.n > 0};

        // (c) population series: |log10 N_a(n) − log10 N_b(n)| ≤ 3·n·σ_k/(k·ln10) ∀n (08 §2 c).
        // σ_k = the combined PER-GENERATION spread of k (std of the active-gen k's), so the linear
        // envelope bounds the independent-RNG random walk yet catches a systematic bias (see
        // diff_criteria.h). The SE of the mean would fail a genuinely-equivalent pair, batch-independently.
        const double sg_a = stddev(A.active_k), sg_b = stddev(B.active_k);
        const double sigma_k = std::sqrt(sg_a * sg_a + sg_b * sg_b);
        const double k_mean = 0.5 * (A.k + B.k);
        const DiffCheck pop = population_series_equivalence(A.active_k, B.active_k, sigma_k, k_mean);
        CriterionResult pop_c{"population_series_equivalence_ratio", "le", pop.worst_ratio, 1.0,
                              pop.pass && pop.n > 0};

        const bool seed_pass = abs_c.pass && sig_c.pass && shell_c.pass && pop_c.pass;
        d.criteria.push_back(abs_c);
        d.criteria.push_back(sig_c);
        d.criteria.push_back(shell_c);
        d.criteria.push_back(pop_c);
        d.verdict = seed_pass ? "pass" : "fail";
        all_pass = all_pass && seed_pass;
        rep.attempts.push_back(std::move(d));
    }
    rep.verdict = all_pass ? "pass" : "fail";  // the caller downgrades to conditional if dirty
    return rep;
}

std::string to_json(const GateReport& r) {
    json j;
    j["schema_version"] = r.schema_version;
    j["gate"] = r.gate;
    j["verdict"] = r.verdict;
    j["gates_toml_sha256"] = r.gates_toml_sha256;
    j["spec_sha256"] = r.spec_sha256;
    j["code_version"] = r.code_version;
    j["git"] = r.git;
    j["dirty"] = r.dirty;
    j["backend"] = r.backend;
    j["device"] = r.device;
    j["started"] = r.started;
    j["finished"] = r.finished;
    j["attempts"] = json::array();
    for (const auto& a : r.attempts) {
        json ja;
        ja["attempt"] = a.attempt;
        ja["seed"] = a.seed;
        ja["verdict"] = a.verdict;
        ja["measurements"] = {{"k", a.k}, {"sigma_pcm", a.sigma_pcm},
                              {"k_deviation_pcm", a.k_deviation_pcm}};
        if (a.k_b != 0.0) ja["measurements"]["k_b"] = a.k_b;  // G0c diff: the second backend's k
        ja["criteria"] = json::array();
        for (const auto& c : a.criteria) {
            ja["criteria"].push_back({{"name", c.name}, {"value", c.value}, {"op", c.op},
                                      {"threshold", c.threshold}, {"pass", c.pass}});
        }
        ja["run_dir"] = a.run_dir;
        j["attempts"].push_back(ja);
    }
    if (!r.notes.empty()) j["notes"] = r.notes;
    return j.dump(2) + "\n";
}

GateReport parse_report_json(const std::string& text) {
    const auto j = json::parse(text);
    GateReport r;
    r.schema_version = j.value("schema_version", 1);
    r.gate = j.value("gate", "");
    r.verdict = j.value("verdict", "");
    r.gates_toml_sha256 = j.value("gates_toml_sha256", "");
    r.spec_sha256 = j.value("spec_sha256", "");
    r.code_version = j.value("code_version", "");
    r.git = j.value("git", "");
    r.dirty = j.value("dirty", false);
    r.backend = j.value("backend", "");
    r.device = j.value("device", "");
    r.started = j.value("started", "");
    r.finished = j.value("finished", "");
    for (const auto& ja : j.value("attempts", json::array())) {
        Attempt a;
        a.attempt = ja.value("attempt", 0);
        a.seed = ja.value("seed", static_cast<std::int64_t>(0));
        a.verdict = ja.value("verdict", "");
        const auto m = ja.value("measurements", json::object());
        a.k = m.value("k", 0.0);
        a.sigma_pcm = m.value("sigma_pcm", 0.0);
        a.k_deviation_pcm = m.value("k_deviation_pcm", 0.0);
        a.k_b = m.value("k_b", 0.0);  // G0c diff (absent in single-backend reports -> 0)
        for (const auto& jc : ja.value("criteria", json::array())) {
            CriterionResult c;
            c.name = jc.value("name", "");
            c.op = jc.value("op", "");
            c.value = jc.value("value", 0.0);
            c.threshold = jc.value("threshold", 0.0);
            c.pass = jc.value("pass", false);
            a.criteria.push_back(c);
        }
        a.run_dir = ja.value("run_dir", "");
        r.attempts.push_back(std::move(a));
    }
    r.notes = j.value("notes", "");
    return r;
}

std::string write_report_append(const std::filesystem::path& path, GateReport report) {
    // Append-only (MAJ-22): merge into any existing report at `path` — new attempts get
    // numbers continuing the old ones, and the verdict is recomputed over ALL attempts, so a
    // re-run cannot drop a failing seed.
    if (std::filesystem::exists(path)) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        GateReport prev;
        try {
            prev = parse_report_json(ss.str());
        } catch (...) {
            prev = GateReport{};  // an unparseable prior report is replaced, not trusted
        }
        int next = 0;
        for (const auto& a : prev.attempts) next = std::max(next, a.attempt);
        for (auto& a : report.attempts) a.attempt += next;  // continue numbering
        std::vector<Attempt> merged = prev.attempts;
        merged.insert(merged.end(), report.attempts.begin(), report.attempts.end());
        report.attempts = std::move(merged);
    }
    bool all_pass = !report.attempts.empty();
    for (const auto& a : report.attempts) all_pass = all_pass && (a.verdict == "pass");
    report.verdict = report.dirty ? "conditional" : (all_pass ? "pass" : "fail");

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << to_json(report);
    return report.verdict;
}

}  // namespace ns::nukebench

#include "app/nukebench/gate_report.h"

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

#include <cmath>
#include <fstream>
#include <sstream>

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
    // Run the SAME gate on both backends (reusing run_gate), then compare per seed (08 §2 G0c a).
    const GateReport ra = run_gate(gate, repo, backend_a, eigen_batch_override);
    const GateReport rb = run_gate(gate, repo, backend_b, eigen_batch_override);
    if (ra.attempts.size() != rb.attempts.size()) {
        throw GatesError("run_diff: backend attempt counts differ for gate " + gate.id);
    }
    // The equivalence bound C-932 (pcm), from the gate's criterion (the loader drift-guarded it).
    double bound_pcm = 0.0;
    for (const auto& c : gate.criteria) {
        if (c.name == "k_equivalence_pcm") bound_pcm = c.value;
    }
    if (bound_pcm <= 0.0) {
        throw GatesError("gate " + gate.id +
                         ": run_diff needs a k_equivalence_pcm criterion (C-932)");
    }

    GateReport rep;
    rep.gate = gate.id;
    rep.backend = backend_a + "|" + backend_b;
    bool all_pass = true;
    for (std::size_t i = 0; i < ra.attempts.size(); ++i) {
        const auto& a = ra.attempts[i];
        const auto& b = rb.attempts[i];
        const double delta_pcm = std::abs(a.k - b.k) * 1e5;
        const double comb_sigma =
            std::sqrt(a.sigma_pcm * a.sigma_pcm + b.sigma_pcm * b.sigma_pcm);

        Attempt d;
        d.attempt = static_cast<int>(i + 1);
        d.seed = a.seed;
        d.k = a.k;                  // backend_a's k
        d.k_b = b.k;                // backend_b's k
        d.sigma_pcm = comb_sigma;
        d.k_deviation_pcm = delta_pcm;

        // 08 §2 G0c (a): |k_a − k_b| ≤ C-932 (absolute equivalence bound) AND ≤ 3·combined σ.
        CriterionResult abs_c{"k_equivalence_pcm", "abs_le", delta_pcm, bound_pcm,
                              delta_pcm <= bound_pcm};
        CriterionResult sig_c{"k_equivalence_within_3sigma", "le", delta_pcm, 3.0 * comb_sigma,
                              delta_pcm <= 3.0 * comb_sigma};
        const bool seed_pass = abs_c.pass && sig_c.pass;
        d.criteria.push_back(abs_c);
        d.criteria.push_back(sig_c);
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

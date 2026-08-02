// M1-T1: geometry, analytic tracker, SHA-256 and canonical_hash (04 §4/§6/§7).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/geometry/geometry.h"
#include "core/hash/sha256.h"
#include "core/scenario/scenario.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <limits>

#include "spec_examples.h"

namespace fs = std::filesystem;
using ns::geom::AnalyticSphereTracker;
using ns::geom::kOutside;
using ns::geom::Layer;
using ns::geom::LayerStack;
using ns::geom::Vec3;

namespace {

/// The canonical layer radii of 03 §4's example.
LayerStack canonical_stack() {
    return LayerStack({
        {"initiator", 1.000, 0, "DECLASSIFIED"},
        {"pit", 4.585, 1, "DECLASSIFIED"},
        {"tamper", 11.430, 2, "RECONSTRUCTED"},
        {"boron", 11.750, 3, "DECLASSIFIED"},
        {"pusher", 23.495, 4, "RECONSTRUCTED"},
    });
}

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

// ---------------------------------------------------------------------------
// LayerStack
// ---------------------------------------------------------------------------

TEST_CASE("locate resolves every layer and the outside", "[geometry]") {
    const auto stack = canonical_stack();

    REQUIRE(stack.locate({0.0, 0.0, 0.0}) == 0);      // dead centre
    REQUIRE(stack.locate({0.5, 0.0, 0.0}) == 0);
    REQUIRE(stack.locate({2.0, 0.0, 0.0}) == 1);
    REQUIRE(stack.locate({8.0, 0.0, 0.0}) == 2);
    REQUIRE(stack.locate({11.6, 0.0, 0.0}) == 3);
    REQUIRE(stack.locate({20.0, 0.0, 0.0}) == 4);
    REQUIRE(stack.locate({100.0, 0.0, 0.0}) == kOutside);

    // Exactly on a boundary belongs to the INNER layer, so locate and
    // distance_to_boundary agree about which side of a crossing we are on.
    REQUIRE(stack.locate({1.0, 0.0, 0.0}) == 0);
    REQUIRE(stack.locate({23.495, 0.0, 0.0}) == 4);
    REQUIRE(stack.locate({23.4950001, 0.0, 0.0}) == kOutside);

    // Direction must not matter — these are spheres.
    REQUIRE(stack.locate({0.0, 8.0, 0.0}) == 2);
    REQUIRE(stack.locate({0.0, 0.0, -8.0}) == 2);
}

TEST_CASE("the innermost layer's inner radius is zero", "[geometry]") {
    // 04 §7 calls this out explicitly: there is no notional cavity below layer 0.
    const auto stack = canonical_stack();
    REQUIRE(stack.inner_radius_of(0) == 0.0);
    REQUIRE(stack.inner_radius_of(1) == 1.000);
    REQUIRE(stack.inner_radius_of(4) == 11.750);
}

TEST_CASE("set_radii takes absolute values and scale_radii is uniform", "[geometry]") {
    auto stack = canonical_stack();

    // MIN-15: absolute, not deltas.
    stack.set_radii({2.0, 5.0, 12.0, 13.0, 25.0});
    REQUIRE(stack.radius_of(0) == 2.0);
    REQUIRE(stack.radius_of(4) == 25.0);

    stack.scale_radii(0.5);
    REQUIRE_THAT(stack.radius_of(0), Catch::Matchers::WithinRel(1.0, 1e-15));
    REQUIRE_THAT(stack.radius_of(4), Catch::Matchers::WithinRel(12.5, 1e-15));

    // Non-monotone radii would make a layer unreachable rather than merely odd.
    REQUIRE_THROWS(stack.set_radii({5.0, 2.0, 12.0, 13.0, 25.0}));
    REQUIRE_THROWS(stack.scale_radii(0.0));
    REQUIRE_THROWS(stack.set_radii({1.0, 2.0}));  // wrong count
}

// ---------------------------------------------------------------------------
// AnalyticSphereTracker — 04 §4's known answers
// ---------------------------------------------------------------------------

TEST_CASE("axial ray hits the expected boundary", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());

    // From the centre, outward: the first boundary is layer 0's outer sphere.
    REQUIRE_THAT(tracker.distance_to_boundary({0, 0, 0}, {1, 0, 0}, 0),
                 Catch::Matchers::WithinRel(1.000, 1e-12));

    // Inside the pit at r = 2, heading out: 4.585 - 2 = 2.585.
    REQUIRE_THAT(tracker.distance_to_boundary({2, 0, 0}, {1, 0, 0}, 1),
                 Catch::Matchers::WithinRel(2.585, 1e-12));

    // Same point heading IN: the inner sphere at r = 1 is 1.0 away.
    REQUIRE_THAT(tracker.distance_to_boundary({2, 0, 0}, {-1, 0, 0}, 1),
                 Catch::Matchers::WithinRel(1.000, 1e-12));

    // Off-axis, still analytic: from (0,3,0) in +y inside the tamper.
    REQUIRE_THAT(tracker.distance_to_boundary({0, 3, 0}, {0, 1, 0}, 1),
                 Catch::Matchers::WithinRel(1.585, 1e-12));
}

TEST_CASE("a chord across an inner sphere resolves to the near root", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());

    // From (0, 2, 0) in the pit, travelling -y: crosses the initiator sphere
    // (r = 1) at y = 1, so the distance is 1.0 and NOT the far root at y = -1.
    REQUIRE_THAT(tracker.distance_to_boundary({0, 2, 0}, {0, -1, 0}, 1),
                 Catch::Matchers::WithinRel(1.0, 1e-12));
}

TEST_CASE("a ray that misses the inner sphere falls through to the outer", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());

    // In the pit at (0, 4, 0) heading +x: the impact parameter is 4 > 1, so the
    // initiator sphere is missed entirely. sqrt(4.585^2 - 4^2) = 2.2359...
    const double expected = std::sqrt(4.585 * 4.585 - 16.0);
    REQUIRE_THAT(tracker.distance_to_boundary({0, 4, 0}, {1, 0, 0}, 1),
                 Catch::Matchers::WithinRel(expected, 1e-12));
}

TEST_CASE("a tangential ray is a hit, not a miss", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());

    // Grazing the initiator sphere exactly: discriminant is zero, one root.
    // Starting just inside the pit at (0, 1, 0) - the tangent point itself -
    // heading +x, the next real crossing is the pit's outer sphere.
    const double expected = std::sqrt(4.585 * 4.585 - 1.0);
    REQUIRE_THAT(tracker.distance_to_boundary({0, 1, 0}, {1, 0, 0}, 1),
                 Catch::Matchers::WithinRel(expected, 1e-12));
}

TEST_CASE("from outside, only the outermost sphere is reachable", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());

    REQUIRE_THAT(tracker.distance_to_boundary({0, 0, -50}, {0, 0, 1}, kOutside),
                 Catch::Matchers::WithinRel(50.0 - 23.495, 1e-12));

    // Pointing away: nothing ahead.
    REQUIRE(tracker.distance_to_boundary({0, 0, -50}, {0, 0, -1}, kOutside) == kInf);

    // Aimed past the body entirely.
    REQUIRE(tracker.distance_to_boundary({0, 100, -50}, {0, 0, 1}, kOutside) == kInf);
}

TEST_CASE("an outbound particle at the outer edge escapes", "[geometry]") {
    const AnalyticSphereTracker tracker(canonical_stack());
    // Standing on the outermost boundary, heading out: no further boundary.
    REQUIRE(tracker.distance_to_boundary({23.495, 0, 0}, {1, 0, 0}, 4) == kInf);
}

TEST_CASE("the boundary nudge follows the direction of travel, both ways", "[geometry]") {
    // 04 §4 / MIN-14. This is the test the rule exists for: nudging
    // unconditionally inward returns an escaping neutron to the body it just
    // left, which shows up as a leakage deficit rather than as a crash — a
    // defect that survives all the way to a gate.
    const AnalyticSphereTracker tracker(canonical_stack());

    const Vec3 on_pit_surface{4.585, 0.0, 0.0};
    REQUIRE(tracker.on_boundary(on_pit_surface));

    // Outbound: must end up in the tamper (layer 2), NOT back in the pit.
    REQUIRE(tracker.nudge_and_locate(on_pit_surface, {1, 0, 0}) == 2);
    // Inbound: must end up in the pit (layer 1).
    REQUIRE(tracker.nudge_and_locate(on_pit_surface, {-1, 0, 0}) == 1);

    // And at the outermost surface, outbound means outside.
    const Vec3 on_outer{0.0, 0.0, 23.495};
    REQUIRE(tracker.on_boundary(on_outer));
    REQUIRE(tracker.nudge_and_locate(on_outer, {0, 0, 1}) == kOutside);
    REQUIRE(tracker.nudge_and_locate(on_outer, {0, 0, -1}) == 4);
}

TEST_CASE("rebuild picks up moved radii", "[geometry]") {
    // Tier-1 hydro scales the stack every few generations; a tracker holding a
    // stale copy would track the uncompressed geometry (05 §4).
    auto stack = canonical_stack();
    AnalyticSphereTracker tracker(stack);
    REQUIRE_THAT(tracker.distance_to_boundary({0, 0, 0}, {1, 0, 0}, 0),
                 Catch::Matchers::WithinRel(1.000, 1e-12));

    stack.scale_radii(0.5);
    tracker.rebuild(stack);
    REQUIRE_THAT(tracker.distance_to_boundary({0, 0, 0}, {1, 0, 0}, 0),
                 Catch::Matchers::WithinRel(0.500, 1e-12));
    REQUIRE(tracker.locate({0.75, 0, 0}) == 1);
}

// ---------------------------------------------------------------------------
// SHA-256 — published NIST vectors
// ---------------------------------------------------------------------------

TEST_CASE("SHA-256 reproduces the published NIST vectors", "[geometry][hash]") {
    // External ground truth. canonical_hash() is only worth anything if the
    // primitive underneath it is genuinely SHA-256, and these are the vectors
    // that settle it — the same standard the Philox KATs meet (04 §2).
    REQUIRE(ns::hash::sha256_hex("")
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(ns::hash::sha256_hex("abc")
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(ns::hash::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Multi-block, and a length that crosses the 56-byte padding boundary.
    REQUIRE(ns::hash::sha256_hex(std::string(1000000, 'a'))
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // The streaming interface must agree with the one-shot form regardless of
    // how the input is chopped up.
    ns::hash::Sha256 chunked;
    chunked.update("abcdbcdecdefdefgefghfghighijhijk");
    chunked.update("ijkljklmklmnlmnomnopnopq");
    REQUIRE(chunked.hex_digest()
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// ---------------------------------------------------------------------------
// canonical_hash — the 04 §6 stability matrix
// ---------------------------------------------------------------------------

namespace {

class HashFixture {
public:
    HashFixture() {
        root_ = fs::temp_directory_path() / "nukesim_hash_fixture";
        fs::remove_all(root_);
        fs::create_directories(root_ / "data" / "xs");
        fs::create_directories(root_ / "data" / "materials");
        fs::create_directories(root_ / "data" / "scenarios");

        nlohmann::json xs = nlohmann::json::parse(spec_examples::xs_example());
        const nlohmann::json base = xs["isotopes"]["Pu239"];
        for (const char* name : {"Pu240", "Ga69", "Ga71", "U238", "B10", "Al27", "Be9"}) {
            nlohmann::json clone = base;
            clone["status"] = "SIM";
            clone["cite"] = "test scaffolding";
            xs["isotopes"][name] = clone;
        }
        spec_examples::write_file(root_ / "data/xs/fast4.json", xs.dump(2));
        spec_examples::write_file(root_ / "data/materials/pu_ga_delta.json",
                                  spec_examples::material_example());
        for (const auto& [name, species] : std::vector<std::pair<std::string, std::string>>{
                 {"be_po_urchin", "Be9"}, {"u_natural", "U238"},
                 {"b10_acrylic", "B10"}, {"aluminum", "Al27"}}) {
            nlohmann::json m = {{"schema_version", 1}, {"name", name}, {"density_g_cm3", 2.7},
                                {"status", "SIM"}, {"cite", "test scaffolding"},
                                {"isotopes", {{species, 1.0}}}};
            spec_examples::write_file(root_ / ("data/materials/" + name + ".json"), m.dump(2));
        }
    }
    ~HashFixture() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    HashFixture(const HashFixture&) = delete;
    HashFixture& operator=(const HashFixture&) = delete;

    /// Writes `text` as the scenario and returns its canonical hash.
    std::string hash_of(const std::string& text) const {
        const auto path = root_ / "data" / "scenarios" / "s.toml";
        spec_examples::write_file(path, text);
        return ns::scenario::Scenario::load(path, root_).canonical_hash();
    }

    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE("canonical_hash is stable across cosmetic rewrites", "[geometry][hash]") {
    // 04 §6's stability matrix. Each of these is a transformation a human or a
    // tool might apply without meaning to change the run.
    const HashFixture fx;
    const std::string original = spec_examples::scenario_example();
    const std::string baseline = fx.hash_of(original);

    REQUIRE(baseline.size() == 64);

    SECTION("comment changes do not move it") {
        std::string text = original + "\n# a trailing comment nobody reads\n";
        REQUIRE(fx.hash_of(text) == baseline);
    }

    SECTION("CRLF line endings do not move it") {
        std::string crlf;
        for (const char c : original) {
            if (c == '\n') {
                crlf += '\r';
            }
            crlf += c;
        }
        REQUIRE(fx.hash_of(crlf) == baseline);
    }

    SECTION("equivalent float spellings do not move it") {
        std::string text = original;
        const auto at = text.find("ratio = 2.2");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string("ratio = 2.2").size(), "ratio = 2.20");
        REQUIRE(fx.hash_of(text) == baseline);

        std::string exp = original;
        const auto e = exp.find("t_max_s                 = 5.0e-6");
        REQUIRE(e != std::string::npos);
        exp.replace(e, std::string("t_max_s                 = 5.0e-6").size(),
                    "t_max_s = 0.000005");
        REQUIRE(fx.hash_of(exp) == baseline);
    }

    SECTION("tally order does not move it") {
        std::string text = original;
        const std::string from =
            R"(tallies = ["k", "population", "fissions_by_isotope", "burnup", "yield", "fission_mesh"])";
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(),
                     R"(tallies = ["yield", "burnup", "k", "fission_mesh", "population", "fissions_by_isotope"])");
        REQUIRE(fx.hash_of(text) == baseline);
    }
}

TEST_CASE("canonical_hash moves when the run would differ", "[geometry][hash]") {
    // The other half of the property: a hash that never changes is useless.
    const HashFixture fx;
    const std::string original = spec_examples::scenario_example();
    const std::string baseline = fx.hash_of(original);

    auto changed = [&](const std::string& from, const std::string& to) {
        std::string text = original;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return fx.hash_of(text);
    };

    REQUIRE(changed("seed = 12345", "seed = 12346") != baseline);
    REQUIRE(changed("ratio = 2.2", "ratio = 2.3") != baseline);
    REQUIRE(changed("r_outer_cm = 4.585", "r_outer_cm = 4.586") != baseline);
    REQUIRE(changed("sim_neutrons = 100000", "sim_neutrons = 200000") != baseline);

    // ... including a change to the DATA the scenario resolves to, which is the
    // whole reason 04 §6 folds the xs and material files into the hash.
    nlohmann::json xs = nlohmann::json::parse(spec_examples::xs_example());
    xs["isotopes"]["Pu239"]["nu"] = nlohmann::json::array({2.99, 2.92, 2.89, 2.89});
    const nlohmann::json base = xs["isotopes"]["Pu239"];
    for (const char* name : {"Pu240", "Ga69", "Ga71", "U238", "B10", "Al27", "Be9"}) {
        nlohmann::json clone = base;
        clone["status"] = "SIM";
        clone["cite"] = "test scaffolding";
        xs["isotopes"][name] = clone;
    }
    spec_examples::write_file(fx.root() / "data/xs/fast4.json", xs.dump(2));
    REQUIRE(fx.hash_of(original) != baseline);
}

TEST_CASE("canonical_form materializes defaults and sorts keys", "[geometry][hash]") {
    const HashFixture fx;
    const auto path = fx.root() / "data" / "scenarios" / "s.toml";
    spec_examples::write_file(path, spec_examples::scenario_example());
    const auto scenario = ns::scenario::Scenario::load(path, fx.root());
    const std::string form = scenario.canonical_form();

    // Absent optionals are present as an explicit "none" rather than omitted —
    // otherwise an absent field and a field equal to the default would hash the
    // same, and 04 §6 requires defaults be materialized.
    REQUIRE_THAT(form, Catch::Matchers::ContainsSubstring("compression.t_c_s=none"));
    REQUIRE_THAT(form, Catch::Matchers::ContainsSubstring("lenses.count=32"));

    // Lexicographically sorted at every level.
    std::vector<std::string> keys;
    std::size_t start = 0;
    while (start < form.size()) {
        const auto eol = form.find('\n', start);
        keys.push_back(form.substr(start, form.find('=', start) - start));
        start = eol + 1;
    }
    REQUIRE(std::is_sorted(keys.begin(), keys.end()));
}

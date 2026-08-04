// M5-T2 — the 03 §7 sweep.db results store: schema, unit_id idempotency
// (rerun = no-op / D6), status/dedup, the cursor singleton, and on-disk
// persistence. Catch2 names are ASCII with no commas (a comma or em-dash breaks
// the ctest<->Catch2 filter — the M5-T1 trap).

#include "core/store/store.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

using namespace ns::store;

TEST_CASE("a fresh in-memory store is empty", "[store]") {
    SweepStore s(":memory:");
    REQUIRE(s.count() == 0);
    REQUIRE_FALSE(s.get_run("nope").has_value());
    REQUIRE_FALSE(s.status_of("nope").has_value());
    REQUIRE_FALSE(s.is_done("nope"));
    REQUIRE_FALSE(s.get_cursor().has_value());
}

TEST_CASE("record_run round-trips a row through get_run", "[store]") {
    SweepStore s(":memory:");
    RunRecord in;
    in.unit_id = "u-abc123";
    in.params_json = R"({"lenses.jitter_ns":3.5,"compression.ratio":2.4})";
    in.tally_json = R"({"schema_version":1,"yield_kt":21.3})";
    in.wall_s = 12.5;
    in.status = status::kDone;

    REQUIRE(s.record_run(in) == true);
    REQUIRE(s.count() == 1);

    auto out = s.get_run("u-abc123");
    REQUIRE(out.has_value());
    REQUIRE(out->unit_id == in.unit_id);
    REQUIRE(out->params_json == in.params_json);
    REQUIRE(out->tally_json == in.tally_json);
    REQUIRE(out->wall_s == in.wall_s);
    REQUIRE(out->status == in.status);
}

TEST_CASE("text binding is injection-safe for adversarial json", "[store]") {
    SweepStore s(":memory:");
    // Single quotes, a semicolon and a SQL-looking payload: if the store bound the
    // text (rather than concatenating it) the table survives and the bytes round
    // trip exactly.
    RunRecord in;
    in.unit_id = "u-inject";
    in.params_json = R"({"note":"o'brien; DROP TABLE runs;--","list":[1,2,3]})";
    in.tally_json = "";
    in.wall_s = 0.0;
    in.status = status::kError;

    REQUIRE(s.record_run(in));
    // The runs table still exists and still holds exactly this one row.
    REQUIRE(s.count() == 1);
    auto out = s.get_run("u-inject");
    REQUIRE(out.has_value());
    REQUIRE(out->params_json == in.params_json);
    REQUIRE(out->status == status::kError);
}

TEST_CASE("record_run is idempotent - a duplicate unit_id is a no-op", "[store]") {
    SweepStore s(":memory:");
    RunRecord first;
    first.unit_id = "u-dup";
    first.params_json = "first";
    first.tally_json = "first-tally";
    first.wall_s = 1.0;
    first.status = status::kDone;

    // First finisher wins: the row is inserted.
    REQUIRE(s.record_run(first) == true);

    // Second finisher for the SAME unit_id, with DIFFERENT content: the write is a
    // no-op (D6 double-count fix / "second finisher's write is a no-op", 06 §2).
    RunRecord second;
    second.unit_id = "u-dup";
    second.params_json = "second";
    second.tally_json = "second-tally";
    second.wall_s = 999.0;
    second.status = status::kError;
    REQUIRE(s.record_run(second) == false);

    // Nothing was overwritten and nothing was double-counted.
    REQUIRE(s.count() == 1);
    auto out = s.get_run("u-dup");
    REQUIRE(out.has_value());
    REQUIRE(out->params_json == "first");
    REQUIRE(out->tally_json == "first-tally");
    REQUIRE(out->wall_s == 1.0);
    REQUIRE(out->status == status::kDone);
}

TEST_CASE("is_done and status_of reflect recorded status", "[store]") {
    SweepStore s(":memory:");
    s.record_run({"u-done", "", "", 2.0, status::kDone});
    s.record_run({"u-err", "", "", 3.0, status::kError});

    REQUIRE(s.is_done("u-done"));
    REQUIRE_FALSE(s.is_done("u-err"));
    REQUIRE_FALSE(s.is_done("u-missing"));

    auto sd = s.status_of("u-done");
    REQUIRE(sd.has_value());
    REQUIRE(*sd == status::kDone);
    auto se = s.status_of("u-err");
    REQUIRE(se.has_value());
    REQUIRE(*se == status::kError);

    REQUIRE(s.count() == 2);
    REQUIRE(s.count_with_status(status::kDone) == 1);
    REQUIRE(s.count_with_status(status::kError) == 1);
    REQUIRE(s.count_with_status(status::kPending) == 0);
}

TEST_CASE("set_cursor replaces the singleton and get_cursor reads it", "[store]") {
    SweepStore s(":memory:");
    REQUIRE_FALSE(s.get_cursor().has_value());

    s.set_cursor(R"({"visited":10,"frontier":["a","b"]})");
    auto c1 = s.get_cursor();
    REQUIRE(c1.has_value());
    REQUIRE(*c1 == R"({"visited":10,"frontier":["a","b"]})");

    // A second set REPLACES the singleton (it does not append a second row).
    s.set_cursor(R"({"visited":50})");
    auto c2 = s.get_cursor();
    REQUIRE(c2.has_value());
    REQUIRE(*c2 == R"({"visited":50})");

    // Recording runs must not disturb the cursor and vice versa.
    s.record_run({"u1", "", "", 1.0, status::kDone});
    auto c3 = s.get_cursor();
    REQUIRE(c3.has_value());
    REQUIRE(*c3 == R"({"visited":50})");
}

TEST_CASE("a store persists across reopen on disk", "[store]") {
    namespace fs = std::filesystem;
    // A case-unique temp dir so a parallel ctest run of another case cannot collide;
    // remove_all also clears any WAL/shm sidecars from a prior run.
    fs::path dir = fs::temp_directory_path() / "nukesim_store_test_persist";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string db = (dir / "sweep.db").string();

    {
        SweepStore s(db);
        REQUIRE(s.record_run({"u-persist", "P", "T", 7.5, status::kDone}));
        s.set_cursor("CUR");
    }  // close (checkpoints WAL into the main file)

    {
        SweepStore s(db);  // reopen the same file
        REQUIRE(s.count() == 1);
        auto out = s.get_run("u-persist");
        REQUIRE(out.has_value());
        REQUIRE(out->params_json == "P");
        REQUIRE(out->tally_json == "T");
        REQUIRE(out->wall_s == 7.5);
        REQUIRE(s.is_done("u-persist"));
        auto cur = s.get_cursor();
        REQUIRE(cur.has_value());
        REQUIRE(*cur == "CUR");
    }

    fs::remove_all(dir);
}

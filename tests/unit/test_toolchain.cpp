// M0-T2 toolchain smoke test.
//
// Not a physics test. It proves that every dependency pinned in vcpkg.json
// compiles, links and runs under the pinned toolchain, and that first-party
// code survives /W4 /WX. Module tests arrive with M0-T3/T4/T5.

#include <catch2/catch_test_macros.hpp>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <toml++/toml.hpp>

#include <array>
#include <string>
#include <string_view>

#if NUKESIM_WITH_CUDA
#include "test_toolchain_cuda.h"
#endif

TEST_CASE("fmt formats", "[toolchain]") {
    REQUIRE(fmt::format("{}-{:.3f}", "k_eff", 1.0) == "k_eff-1.000");
}

TEST_CASE("tomlplusplus parses a scenario-shaped document", "[toolchain]") {
    // Shaped like spec/03-data-contracts.md §4 so a breaking toml++ change is
    // visible here before the scenario loader (M0-T5) is written.
    const auto table = toml::parse(R"(
        [run]
        backend = "gpu"
        seed = 20260802

        [[layers]]
        name = "pit"
        outer_radius_cm = 4.65
    )");

    REQUIRE(table["run"]["backend"].value_or(std::string_view{}) == "gpu");
    REQUIRE(table["run"]["seed"].value_or(0LL) == 20260802LL);

    const auto* layers = table["layers"].as_array();
    REQUIRE(layers != nullptr);
    REQUIRE(layers->size() == 1U);
}

TEST_CASE("nlohmann-json round-trips", "[toolchain]") {
    const nlohmann::json out = {{"k_eff", 1.0}, {"generations", 40}};
    const auto back = nlohmann::json::parse(out.dump());

    REQUIRE(back.at("k_eff").get<double>() == 1.0);
    REQUIRE(back.at("generations").get<int>() == 40);
}

TEST_CASE("CLI11 parses a nukebench-shaped command line", "[toolchain]") {
    CLI::App app{"toolchain smoke"};
    int seed = 0;
    std::string backend;
    app.add_option("--seed", seed);
    app.add_option("--backend", backend);

    const std::array<const char*, 5> argv{"nukebench", "--seed", "20260802", "--backend", "gpu"};
    app.parse(static_cast<int>(argv.size()), argv.data());

    REQUIRE(seed == 20260802);
    REQUIRE(backend == "gpu");
}

TEST_CASE("sqlite3 opens, writes and reads an in-memory database", "[toolchain]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(sqlite3_exec(db, "CREATE TABLE runs(unit_id TEXT PRIMARY KEY, k_eff REAL);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "INSERT INTO runs VALUES('smoke', 1.0);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT k_eff FROM runs WHERE unit_id='smoke';", -1,
                               &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(sqlite3_column_double(stmt, 0) == 1.0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

#if NUKESIM_WITH_CUDA
TEST_CASE("CUDA runtime answers and a kernel round-trips", "[toolchain][cuda]") {
    const int devices = nukesim_cuda_device_count();

    // A CPU-only machine must still build and pass the CPU tests (12 §2), and
    // NUKESIM_WITH_CUDA is on whenever nvcc is on PATH — which says nothing
    // about a GPU being present. Absence of a device is a skip, not a failure.
    if (devices <= 0) {
        SKIP("no CUDA device visible (device_count=" << devices << ")");
    }

    REQUIRE(nukesim_cuda_roundtrip(41) == 42);
}
#endif

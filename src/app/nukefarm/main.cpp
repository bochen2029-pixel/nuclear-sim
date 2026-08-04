// nukefarm — batch/cloud sweep driver (06 §2, D8) — M5-T3-e.
//
// The FIRST src/app executable. A thin CLI11 dispatch over the tested cli.h
// handlers; all logic lives in the nukesim_nukefarm library. Exit codes per 06 §5.

#include "app/nukefarm/cli.h"
#include "app/nukefarm/sweep.h"  // SweepError

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    CLI::App app{"nukefarm - batch/cloud sweep driver (06 §2)"};
    app.require_subcommand(1);

    std::string sweep;
    std::string queue;
    std::string db;
    double stale_lease_s = 600.0;  // ADR-020 fallback default
    bool local = false;

    auto* submit_cmd =
        app.add_subcommand("submit", "Enqueue a sweep's units (--local also runs them)");
    submit_cmd->add_option("--sweep", sweep, "sweep.toml (03 §7)")->required();
    submit_cmd->add_option("--queue", queue, "queue directory")->required();
    submit_cmd->add_option("--db", db, "sweep.db (03 §7)")->required();
    submit_cmd->add_flag("--local", local, "also run a worker in-process after submitting");
    submit_cmd->add_option("--stale-lease-s", stale_lease_s, "stale-lease fallback seconds");

    auto* worker_cmd = app.add_subcommand("worker", "Drain the queue into the store");
    worker_cmd->add_option("--queue", queue, "queue directory")->required();
    worker_cmd->add_option("--db", db, "sweep.db")->required();
    worker_cmd->add_option("--stale-lease-s", stale_lease_s, "stale-lease fallback seconds");

    auto* status_cmd = app.add_subcommand("status", "Report sweep counts");
    status_cmd->add_option("--db", db, "sweep.db")->required();
    status_cmd->add_option("--queue", queue, "queue directory (optional)");

    auto* resume_cmd =
        app.add_subcommand("resume", "Enqueue not-yet-done units and run a worker");
    resume_cmd->add_option("--sweep", sweep, "sweep.toml")->required();
    resume_cmd->add_option("--queue", queue, "queue directory")->required();
    resume_cmd->add_option("--db", db, "sweep.db")->required();
    resume_cmd->add_option("--stale-lease-s", stale_lease_s, "stale-lease fallback seconds");

    CLI11_PARSE(app, argc, argv);

    using namespace ns::nukefarm;
    try {
        if (*submit_cmd) {
            const std::int64_t n = cli_submit(sweep, queue, db);
            fmt::print("submitted {} unit(s)\n", n);
            if (local) {
                const WorkerResult w = cli_worker(queue, db, stale_lease_s);
                fmt::print("worker: processed {}, reclaimed {}, skipped {}\n", w.processed,
                           w.reclaimed, w.skipped);
            }
        } else if (*worker_cmd) {
            const WorkerResult w = cli_worker(queue, db, stale_lease_s);
            fmt::print("worker: processed {}, reclaimed {}, skipped {}\n", w.processed, w.reclaimed,
                       w.skipped);
        } else if (*status_cmd) {
            const StatusReport s = cli_status(db, queue);
            fmt::print("done {} | queue pending {} claimed {} done {}\n", s.store_done,
                       s.queue_pending, s.queue_claimed, s.queue_done);
        } else if (*resume_cmd) {
            const std::int64_t n = cli_submit(sweep, queue, db);
            const WorkerResult w = cli_worker(queue, db, stale_lease_s);
            fmt::print("resumed: enqueued {}, processed {}, reclaimed {}\n", n, w.processed,
                       w.reclaimed);
        }
    } catch (const SweepError& e) {
        fmt::print(stderr, "validation error: {}\n", e.what());
        return 3;  // 06 §5: validation
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;  // 06 §5: general error
    }
    return 0;
}

// The filesystem work-queue (06 §2) — M5-T3-c.

#include "app/nukefarm/queue.h"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ns::nukefarm {

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw QueueError("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw QueueError("cannot write " + p.string());
    out << text;
    if (!out) throw QueueError("write failed for " + p.string());
}

std::int64_t count_json(const fs::path& dir) {
    std::int64_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".json") ++n;
    }
    return n;
}

}  // namespace

WorkQueue::WorkQueue(fs::path dir)
    : dir_(std::move(dir)),
      pending_(dir_ / "pending"),
      claimed_(dir_ / "claimed"),
      done_(dir_ / "done") {
    std::error_code ec;
    fs::create_directories(pending_, ec);
    fs::create_directories(claimed_, ec);
    fs::create_directories(done_, ec);
    if (!fs::is_directory(pending_) || !fs::is_directory(claimed_) || !fs::is_directory(done_)) {
        throw QueueError("cannot create queue directories under " + dir_.string());
    }
}

bool WorkQueue::enqueue(const std::string& unit_id, const std::string& payload) {
    // Already in flight or finished -> do not re-add (idempotent by unit_id).
    if (fs::exists(claimed_ / (unit_id + ".json")) || fs::exists(done_ / (unit_id + ".json"))) {
        return false;
    }
    const bool existed = fs::exists(pending_ / (unit_id + ".json"));
    write_file(pending_ / (unit_id + ".json"), payload);
    return !existed;
}

std::optional<WorkItem> WorkQueue::claim(double now_s) {
    for (const auto& e : fs::directory_iterator(pending_)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        const std::string unit_id = e.path().stem().string();
        WorkItem item{unit_id, read_file(e.path())};
        // Atomic move pending -> claimed, then stamp the lease. Return immediately
        // so the (now-modified) directory iterator is never advanced.
        fs::rename(e.path(), claimed_ / (unit_id + ".json"));
        std::ostringstream ss;
        ss.precision(17);
        ss << now_s;
        write_file(claimed_ / (unit_id + ".lease"), ss.str());
        return item;
    }
    return std::nullopt;
}

void WorkQueue::complete(const std::string& unit_id) {
    const fs::path claimed_json = claimed_ / (unit_id + ".json");
    if (!fs::exists(claimed_json)) throw QueueError("complete: unit not claimed: " + unit_id);
    fs::rename(claimed_json, done_ / (unit_id + ".json"));
    std::error_code ec;
    fs::remove(claimed_ / (unit_id + ".lease"), ec);  // best-effort
}

int WorkQueue::reclaim_stale(double now_s, double threshold_s) {
    // Collect first (do not mutate the directory while iterating it).
    std::vector<std::string> stale;
    for (const auto& e : fs::directory_iterator(claimed_)) {
        if (!e.is_regular_file() || e.path().extension() != ".lease") continue;
        bool is_stale = true;  // an unreadable/unparseable lease is treated as stale
        try {
            is_stale = (now_s - std::stod(read_file(e.path()))) > threshold_s;
        } catch (const std::exception&) {
            is_stale = true;
        }
        if (is_stale) stale.push_back(e.path().stem().string());
    }
    int reclaimed = 0;
    for (const auto& unit_id : stale) {
        const fs::path claimed_json = claimed_ / (unit_id + ".json");
        if (fs::exists(claimed_json)) {
            fs::rename(claimed_json, pending_ / (unit_id + ".json"));
            ++reclaimed;
        }
        std::error_code ec;
        fs::remove(claimed_ / (unit_id + ".lease"), ec);
    }
    return reclaimed;
}

std::int64_t WorkQueue::pending_count() const { return count_json(pending_); }
std::int64_t WorkQueue::claimed_count() const { return count_json(claimed_); }
std::int64_t WorkQueue::done_count() const { return count_json(done_); }

}  // namespace ns::nukefarm

// Extracts the canonical example documents from the spec markdown (M0-T5).
//
// 03's header says tests parse the examples verbatim. Copying them into a
// fixture would defeat that: the copy and the spec would drift, and the loader
// would be validated against a document nobody reads. So the tests read the
// blocks out of `spec/03-data-contracts.md` itself — the same technique the
// 11 §5 oracle uses for the tally example.
//
// A malformed edit to one of those blocks therefore breaks `ctest -R loaders`,
// which is the intended failure direction.

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace spec_examples {

/// Repository root, located by walking up from the build-time source path.
inline std::filesystem::path repo_root() {
    std::filesystem::path here = std::filesystem::path(NUKESIM_SOURCE_DIR);
    if (!std::filesystem::exists(here / "spec" / "03-data-contracts.md")) {
        throw std::runtime_error("cannot locate the repository root from NUKESIM_SOURCE_DIR");
    }
    return here;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// The `index`-th fenced block of the given language, counted from the start of
/// the section whose heading begins with `heading`.
inline std::string fenced_block(const std::string& markdown, const std::string& heading,
                                const std::string& language, int index = 0) {
    const auto section = markdown.find("\n" + heading);
    if (section == std::string::npos) {
        throw std::runtime_error("section not found: " + heading);
    }
    const std::string open = "```" + language + "\n";
    std::size_t cursor = section;
    for (int i = 0; i <= index; ++i) {
        cursor = markdown.find(open, cursor);
        if (cursor == std::string::npos) {
            throw std::runtime_error("fenced block not found in " + heading);
        }
        if (i < index) {
            cursor += open.size();
        }
    }
    const std::size_t body = cursor + open.size();
    const std::size_t close = markdown.find("\n```", body);
    if (close == std::string::npos) {
        throw std::runtime_error("unterminated fenced block in " + heading);
    }
    return markdown.substr(body, close - body);
}

inline std::string contracts() {
    return read_file(repo_root() / "spec" / "03-data-contracts.md");
}

/// The 03 §2 cross-section example.
inline std::string xs_example() {
    return fenced_block(contracts(), "## 2. `data/xs/", "json");
}

/// The 03 §3 material example.
inline std::string material_example() {
    return fenced_block(contracts(), "## 3. `data/materials/", "json");
}

/// The 03 §4 scenario example.
inline std::string scenario_example() {
    return fenced_block(contracts(), "## 4. `scenarios/", "toml");
}

/// The 03 §5 tally example — a normative fixture (QC-04 / 11 §4): it MUST
/// satisfy all nine tally invariants. Read from the spec so it cannot drift.
inline std::string tally_example() {
    return fenced_block(contracts(), "## 5. `tally.json`", "json");
}

inline void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

}  // namespace spec_examples

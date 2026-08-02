// Loader diagnostics (03 §preamble, 02 §4).
//
// "Loaders MUST validate required fields and ranges, and MUST emit diagnostics
// naming file/field/constraint." A loader that says "invalid input" costs the
// next session an hour with a debugger; one that says which file, which field
// and which rule was broken costs it nothing. There are no silent physics
// defaults anywhere in this project (02 §4).

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ns {

/// Thrown by every loader. Carries the three things a diagnostic must name.
class LoadError final : public std::runtime_error {
public:
    LoadError(std::filesystem::path file, std::string field, std::string constraint)
        : std::runtime_error(format(file, field, constraint)),
          file_(std::move(file)),
          field_(std::move(field)),
          constraint_(std::move(constraint)) {}

    const std::filesystem::path& file() const noexcept { return file_; }
    const std::string& field() const noexcept { return field_; }
    const std::string& constraint() const noexcept { return constraint_; }

private:
    static std::string format(const std::filesystem::path& file, const std::string& field,
                              const std::string& constraint) {
        return file.filename().string() + ": " + field + ": " + constraint;
    }

    std::filesystem::path file_;
    std::string field_;
    std::string constraint_;
};

/// Non-fatal finding. 03 §3 has several WARN-not-ERROR rules — reconstructed
/// masses carry real spread, so a 3% mass mismatch is information, not a fault.
struct LoadWarning {
    std::string field;
    std::string message;
};

inline void require(bool condition, const std::filesystem::path& file,
                    std::string_view field, std::string_view constraint) {
    if (!condition) {
        throw LoadError(file, std::string(field), std::string(constraint));
    }
}

}  // namespace ns

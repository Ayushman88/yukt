#pragma once

#include "diagnostics/diagnostics.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace yukt {

struct ProjectManifest {
    std::string name;
    std::string version;
    std::string type;
    std::filesystem::path entry;
    std::filesystem::path root;
};

std::optional<std::filesystem::path> find_manifest(const std::filesystem::path& start);

std::optional<ProjectManifest> load_manifest(const std::filesystem::path& toml_path,
                                             DiagnosticEngine& diagnostics);

}  // namespace yukt

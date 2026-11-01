#pragma once

#include "diagnostics/diagnostics.hpp"
#include "driver/manifest.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace yukt {

bool create_project(const std::filesystem::path& dest, const std::string& name, const std::string& type,
                    DiagnosticEngine& diagnostics);

std::optional<ProjectManifest> require_project(DiagnosticEngine& diagnostics);

int run_project_tests(const ProjectManifest& project, std::ostream& out, std::ostream& err,
                      DiagnosticEngine& diagnostics);

}  // namespace yukt

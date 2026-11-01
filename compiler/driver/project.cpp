#include "driver/project.hpp"

#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "driver/manifest.hpp"
#include "driver/paths.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <utility>
#include <vector>

namespace yukt {
namespace {

namespace fs = std::filesystem;

bool is_ident(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if ((first < 'A' || first > 'Z') && (first < 'a' || first > 'z') && first != '_') {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') {
            return false;
        }
    }
    return true;
}

void fail(DiagnosticEngine& diagnostics, const std::string& title, std::string detail, std::string suggestion) {
    diagnostics.error(SourceLocation{1, 1}, title, std::move(detail), std::move(suggestion));
}

bool rewrite_name(const fs::path& toml_path, const std::string& name) {
    std::ifstream in(toml_path);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    std::string line;
    bool replaced = false;
    while (std::getline(in, line)) {
        if (!replaced && line.find("name") != std::string::npos && line.find('=') != std::string::npos) {
            buffer << "name = \"" << name << "\"\n";
            replaced = true;
            continue;
        }
        buffer << line << '\n';
    }
    in.close();
    std::ofstream out(toml_path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << buffer.str();
    return replaced;
}

}  // namespace

bool create_project(const fs::path& dest, const std::string& name, const std::string& type,
                    DiagnosticEngine& diagnostics) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name == "." || name == ".." ||
        !is_ident(name)) {
        fail(diagnostics, "Invalid project name",
             "`" + name + "` is not a valid project name.",
             "Use a single YUKT identifier as the directory name.");
        return false;
    }
    if (type != "cli" && type != "server") {
        fail(diagnostics, "Invalid project type",
             type.empty() ? "Missing --type." : "`" + type + "` is not a project type.",
             "Pass --type cli or --type server.");
        return false;
    }
    if (fs::exists(dest)) {
        fail(diagnostics, "Cannot create project",
             dest.string() + " already exists.",
             "Choose a new name or remove the existing directory.");
        return false;
    }

    const fs::path from = templates_root() / type;
    if (!fs::is_directory(from)) {
        fail(diagnostics, "Cannot create project",
             "Template `" + type + "` was not found at " + from.string() + ".",
             "Reinstall YUKT so share/yukt/templates is present.");
        return false;
    }

    fs::path stage = dest;
    stage += ".stage.";
    stage += std::to_string(getpid());
    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::copy(from, stage, fs::copy_options::recursive, ec);
    if (ec) {
        fs::remove_all(stage);
        fail(diagnostics, "Cannot create project", "Could not copy the " + type + " template.",
             "Check that the templates directory is readable.");
        return false;
    }
    if (!rewrite_name(stage / "yukt.toml", name)) {
        fs::remove_all(stage);
        fail(diagnostics, "Cannot create project", "Could not write yukt.toml.",
             "Check that the destination is writable.");
        return false;
    }
    fs::rename(stage, dest, ec);
    if (ec) {
        fs::remove_all(stage);
        fail(diagnostics, "Cannot create project", "Could not move the new project into place.",
             "Check that the destination is writable.");
        return false;
    }
    return true;
}

std::optional<ProjectManifest> require_project(DiagnosticEngine& diagnostics) {
    const auto found = find_manifest(fs::current_path());
    if (!found.has_value()) {
        fail(diagnostics, "Cannot find project", "No yukt.toml in this directory or its parents.",
             "Run `yukt new` or pass a `.yukt` file.");
        return std::nullopt;
    }
    return load_manifest(*found, diagnostics);
}

int run_project_tests(const ProjectManifest& project, std::ostream& out, std::ostream& err,
                      DiagnosticEngine& diagnostics) {
    const fs::path tests_dir = project.root / "tests";
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(tests_dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(tests_dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".yukt") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return a.generic_string() < b.generic_string();
    });
    if (files.empty()) {
        fail(diagnostics, "No tests found", "The project has no files under tests/ ending in .yukt.",
             "Add a file under tests/.");
        diagnostics.emit(err);
        return 1;
    }

    bool ok = true;
    for (const auto& path : files) {
        const auto rel = fs::relative(path, project.root).generic_string();
        auto frontend = analyze_file(path.string());
        if (frontend.diagnostics == nullptr) {
            err << "FAIL " << rel << ": compile\n";
            ok = false;
            continue;
        }
        analyze_types(frontend);
        if (frontend.diagnostics->has_errors()) {
            err << "FAIL " << rel << ": compile\n";
            frontend.diagnostics->emit(err);
            ok = false;
            continue;
        }

        CompileRequest request;
        request.action = CompileAction::Run;
        request.input_path = path.string();
        auto output = compile_program(frontend, request, *frontend.diagnostics);
        if (!output.ok || frontend.diagnostics->has_errors()) {
            err << "FAIL " << rel << ": compile\n";
            frontend.diagnostics->emit(err);
            ok = false;
            continue;
        }
        if (output.exit_code != 0) {
            err << "FAIL " << rel << ": exit " << output.exit_code << '\n';
            ok = false;
            continue;
        }

        const fs::path expected_path = fs::path(path.string() + ".out");
        if (fs::exists(expected_path)) {
            std::ifstream expected_file(expected_path, std::ios::binary);
            std::ostringstream expected;
            expected << expected_file.rdbuf();
            if (output.program_output != expected.str()) {
                err << "FAIL " << rel << ": stdout mismatch\n";
                ok = false;
                continue;
            }
        }
        out << "ok " << rel << '\n';
    }
    return ok ? 0 : 1;
}

}  // namespace yukt

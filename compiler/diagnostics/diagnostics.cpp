#include "diagnostics/diagnostics.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <utility>

namespace yukt {
namespace {

bool color_enabled(const std::ostream& out) {
    if (&out != &std::cerr && &out != &std::cout) {
        return false;
    }
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    const char* term = std::getenv("TERM");
    if (term != nullptr && std::strcmp(term, "dumb") == 0) {
        return false;
    }
    const int fd = &out == &std::cout ? STDOUT_FILENO : STDERR_FILENO;
    return isatty(fd) == 1;
}

const char* paint(bool color, const char* code) {
    return color ? code : "";
}

void write_caret(std::ostream& out, std::uint32_t column) {
    if (column == 0) {
        column = 1;
    }
    out << "    ";
    for (std::uint32_t i = 1; i < column; ++i) {
        out << ' ';
    }
    out << "^\n";
}

void write_snippet(std::ostream& out, std::string_view source, SourceLocation location) {
    const auto source_line = line_at(source, location.line);
    if (!source_line.empty() || location.line > 0) {
        out << "    " << source_line << '\n';
        write_caret(out, location.column);
    }
}

}  // namespace

DiagnosticEngine::DiagnosticEngine(std::string file, std::string source)
    : file_(std::move(file)), source_(std::move(source)) {
    sources_[file_] = source_;
}

void DiagnosticEngine::add_source(std::string path, std::string source) {
    sources_[path] = std::move(source);
}

void DiagnosticEngine::error(SourceLocation location, std::string title, std::string detail, std::string suggestion) {
    add(DiagnosticLevel::Error, location, std::move(title), std::move(detail), std::move(suggestion), {});
}

void DiagnosticEngine::error(SourceLocation location, std::string title, std::string detail, std::string suggestion,
                             std::vector<DiagnosticNote> notes) {
    add(DiagnosticLevel::Error, location, std::move(title), std::move(detail), std::move(suggestion), std::move(notes));
}

void DiagnosticEngine::warning(SourceLocation location, std::string title, std::string detail, std::string suggestion) {
    add(DiagnosticLevel::Warning, location, std::move(title), std::move(detail), std::move(suggestion), {});
}

bool DiagnosticEngine::has_errors() const {
    return error_count() > 0;
}

std::size_t DiagnosticEngine::error_count() const {
    std::size_t count = 0;
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.level == DiagnosticLevel::Error) {
            ++count;
        }
    }
    return count;
}

void DiagnosticEngine::emit(std::ostream& out) const {
    const bool color = color_enabled(out);
    const char* red = paint(color, "\033[31m");
    const char* yellow = paint(color, "\033[33m");
    const char* cyan = paint(color, "\033[36m");
    const char* green = paint(color, "\033[32m");
    const char* bold = paint(color, "\033[1m");
    const char* reset = paint(color, "\033[0m");

    for (const auto& diagnostic : diagnostics_) {
        const char* level_color = diagnostic.level == DiagnosticLevel::Error ? red : yellow;
        const char* gnu_level = diagnostic.level == DiagnosticLevel::Error ? "error" : "warning";
        const std::string& path =
            diagnostic.location.file.empty() ? file_ : diagnostic.location.file;
        out << path << ':' << diagnostic.location.line << ':' << diagnostic.location.column << ": " << level_color
            << gnu_level << reset << ": " << bold << diagnostic.title << reset << '\n';
        out << level_color << diagnostic_level_name(diagnostic.level) << reset << ": " << diagnostic.title << '\n';
        out << "File: " << path << '\n';
        out << "Line: " << diagnostic.location.line << ", Column: " << diagnostic.location.column << "\n\n";

        if (!diagnostic.detail.empty()) {
            out << diagnostic.detail << "\n\n";
        }

        const auto source_found = sources_.find(path);
        write_snippet(out, source_found != sources_.end() ? source_found->second : source_, diagnostic.location);

        if (!diagnostic.suggestion.empty()) {
            out << "\n" << green << "Suggestion: " << reset << diagnostic.suggestion << '\n';
        }

        for (const auto& note : diagnostic.notes) {
            const std::string& note_path = note.location.file.empty() ? file_ : note.location.file;
            out << '\n'
                << cyan << "note" << reset << ": " << note_path << ':' << note.location.line << ':'
                << note.location.column << ": " << note.text << '\n';
            const auto note_source = sources_.find(note_path);
            write_snippet(out, note_source != sources_.end() ? note_source->second : source_, note.location);
        }

        out << '\n';
    }
}

void DiagnosticEngine::add(DiagnosticLevel level, SourceLocation location, std::string title, std::string detail,
                           std::string suggestion, std::vector<DiagnosticNote> notes) {
    if (location.file.empty()) {
        location.file = file_;
    }
    for (auto& note : notes) {
        if (note.location.file.empty()) {
            note.location.file = file_;
        }
    }
    diagnostics_.push_back(Diagnostic{
        level,
        std::move(title),
        std::move(detail),
        std::move(suggestion),
        location,
        std::move(notes),
    });
}

std::string_view diagnostic_level_name(DiagnosticLevel level) {
    switch (level) {
        case DiagnosticLevel::Error:
            return "ERROR";
        case DiagnosticLevel::Warning:
            return "WARNING";
    }
    return "DIAGNOSTIC";
}

}  // namespace yukt

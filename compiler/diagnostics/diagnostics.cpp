#include "diagnostics/diagnostics.hpp"

#include <iostream>
#include <utility>

namespace yukt {
namespace {

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

}  // namespace

DiagnosticEngine::DiagnosticEngine(std::string file, std::string source)
    : file_(std::move(file)), source_(std::move(source)) {}

void DiagnosticEngine::error(SourceLocation location, std::string title, std::string detail, std::string suggestion) {
    add(DiagnosticLevel::Error, location, std::move(title), std::move(detail), std::move(suggestion));
}

void DiagnosticEngine::warning(SourceLocation location, std::string title, std::string detail, std::string suggestion) {
    add(DiagnosticLevel::Warning, location, std::move(title), std::move(detail), std::move(suggestion));
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
    for (const auto& diagnostic : diagnostics_) {
        out << diagnostic_level_name(diagnostic.level) << ": " << diagnostic.title << '\n';
        out << "File: " << file_ << '\n';
        out << "Line: " << diagnostic.location.line << ", Column: " << diagnostic.location.column << "\n\n";

        if (!diagnostic.detail.empty()) {
            out << diagnostic.detail << "\n\n";
        }

        const auto source_line = line_at(source_, diagnostic.location.line);
        if (!source_line.empty() || diagnostic.location.line > 0) {
            out << "    " << source_line << '\n';
            write_caret(out, diagnostic.location.column);
        }

        if (!diagnostic.suggestion.empty()) {
            out << "\nSuggestion: " << diagnostic.suggestion << '\n';
        }

        out << '\n';
    }
}

void DiagnosticEngine::add(DiagnosticLevel level, SourceLocation location, std::string title, std::string detail,
                           std::string suggestion) {
    diagnostics_.push_back(Diagnostic{
        level,
        std::move(title),
        std::move(detail),
        std::move(suggestion),
        location,
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

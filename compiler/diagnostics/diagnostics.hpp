#pragma once

#include "common/source.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace yukt {

enum class DiagnosticLevel {
    Error,
    Warning,
};

struct DiagnosticNote {
    SourceLocation location;
    std::string text;
};

struct Diagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    std::string title;
    std::string detail;
    std::string suggestion;
    SourceLocation location;
    std::vector<DiagnosticNote> notes;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(std::string file, std::string source);

    const std::string& file() const { return file_; }
    const std::string& source() const { return source_; }

    void error(SourceLocation location, std::string title, std::string detail, std::string suggestion = {});
    void error(SourceLocation location, std::string title, std::string detail, std::string suggestion,
               std::vector<DiagnosticNote> notes);
    void warning(SourceLocation location, std::string title, std::string detail, std::string suggestion = {});

    bool has_errors() const;
    std::size_t error_count() const;
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    void emit(std::ostream& out) const;

private:
    void add(DiagnosticLevel level, SourceLocation location, std::string title, std::string detail,
             std::string suggestion, std::vector<DiagnosticNote> notes);

    std::string file_;
    std::string source_;
    std::vector<Diagnostic> diagnostics_;
};

std::string_view diagnostic_level_name(DiagnosticLevel level);

}  // namespace yukt

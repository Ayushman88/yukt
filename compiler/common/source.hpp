#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yukt {

struct SourceLocation {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct SourceSpan {
    SourceLocation start;
    SourceLocation end;
};

struct SourceFile {
    std::string path;
    std::string text;
};

inline std::string_view line_at(std::string_view source, std::uint32_t line) {
    if (line == 0) {
        return {};
    }

    std::uint32_t current = 1;
    std::size_t begin = 0;
    while (begin < source.size() && current < line) {
        const auto newline = source.find('\n', begin);
        if (newline == std::string_view::npos) {
            return {};
        }
        begin = newline + 1;
        ++current;
    }

    if (begin >= source.size()) {
        return {};
    }

    const auto end = source.find('\n', begin);
    auto line_text = source.substr(begin, end == std::string_view::npos ? source.size() - begin : end - begin);
    if (!line_text.empty() && line_text.back() == '\r') {
        line_text.remove_suffix(1);
    }
    return line_text;
}

}  // namespace yukt

#include "driver/manifest.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yukt {
namespace {

namespace fs = std::filesystem;

SourceLocation loc_at(const fs::path& path, std::uint32_t line, std::uint32_t column) {
    return SourceLocation{line, column, path.string()};
}

void invalid(DiagnosticEngine& diagnostics, const SourceLocation& location, std::string detail,
             std::string suggestion) {
    diagnostics.error(location, "Invalid manifest", std::move(detail), std::move(suggestion));
}

std::string_view ltrim(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    return text.substr(i);
}

bool is_ident(std::string_view text) {
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

bool is_version(std::string_view text) {
    int dots = 0;
    int part = 0;
    for (const char ch : text) {
        if (ch == '.') {
            if (part == 0) {
                return false;
            }
            ++dots;
            part = 0;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        ++part;
    }
    return dots == 2 && part > 0;
}

bool path_under_root(const fs::path& root, const fs::path& entry) {
    const auto& root_s = root.native();
    const auto& entry_s = entry.native();
    if (entry_s == root_s) {
        return true;
    }
    const char sep = static_cast<char>(fs::path::preferred_separator);
    return entry_s.size() > root_s.size() && entry_s.compare(0, root_s.size(), root_s) == 0 &&
           entry_s[root_s.size()] == sep;
}

struct ParsedLine {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::string key;
    std::string value;
    std::string table;
};

bool parse_lines(const fs::path& path, const std::string& source, DiagnosticEngine& diagnostics,
                 std::vector<ParsedLine>& lines) {
    std::uint32_t line_no = 1;
    std::size_t i = 0;
    while (i <= source.size()) {
        std::size_t end = i;
        while (end < source.size() && source[end] != '\n') {
            ++end;
        }
        std::string_view raw = std::string_view(source).substr(i, end - i);
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }

        std::string_view cursor = ltrim(raw);
        const std::uint32_t column =
            static_cast<std::uint32_t>(cursor.data() - raw.data()) + 1;
        if (!cursor.empty() && cursor[0] != '#') {
            if (cursor[0] == '[') {
                const auto close = cursor.find(']');
                if (close == std::string_view::npos) {
                    invalid(diagnostics, loc_at(path, line_no, column),
                            "Table header is missing a closing ].",
                            "Use [dependencies] only in 0.18.2, or remove the table.");
                    return false;
                }
                std::string table(cursor.substr(1, close - 1));
                invalid(diagnostics, loc_at(path, line_no, column),
                        "Unexpected table [" + table + "].", "Dependencies are 0.18.2.");
                return false;
            }

            std::size_t key_end = 0;
            while (key_end < cursor.size() &&
                   cursor[key_end] != ' ' && cursor[key_end] != '\t' && cursor[key_end] != '=') {
                ++key_end;
            }
            const std::string key(cursor.substr(0, key_end));
            cursor = ltrim(cursor.substr(key_end));
            if (key.empty() || !is_ident(key) || cursor.empty() || cursor[0] != '=') {
                invalid(diagnostics, loc_at(path, line_no, column),
                        "Expected key = \"value\".", "Quote every value and use name, version, type, or entry.");
                return false;
            }
            cursor = ltrim(cursor.substr(1));
            if (cursor.empty() || cursor[0] != '"') {
                invalid(diagnostics, loc_at(path, line_no, column),
                        "Values must be double-quoted strings.",
                        "Write entry = \"src/main.yukt\".");
                return false;
            }
            cursor.remove_prefix(1);
            std::string value;
            bool closed = false;
            for (std::size_t v = 0; v < cursor.size(); ++v) {
                if (cursor[v] == '\\') {
                    invalid(diagnostics, loc_at(path, line_no, column),
                            "Escape sequences are not allowed in 0.18.1 manifests.",
                            "Use a plain quoted string.");
                    return false;
                }
                if (cursor[v] == '"') {
                    value = std::string(cursor.substr(0, v));
                    cursor = ltrim(cursor.substr(v + 1));
                    closed = true;
                    break;
                }
            }
            if (!closed) {
                invalid(diagnostics, loc_at(path, line_no, column), "String value is not closed.",
                        "Add a closing quote.");
                return false;
            }
            if (!cursor.empty() && cursor[0] != '#') {
                invalid(diagnostics, loc_at(path, line_no, column),
                        "Unexpected text after a value.", "Remove the extra tokens or start a comment with #.");
                return false;
            }
            ParsedLine parsed;
            parsed.line = line_no;
            parsed.column = column;
            parsed.key = key;
            parsed.value = std::move(value);
            lines.push_back(std::move(parsed));
        }

        if (end >= source.size()) {
            break;
        }
        i = end + 1;
        ++line_no;
    }
    return true;
}

}  // namespace

std::optional<fs::path> find_manifest(const fs::path& start) {
    std::error_code ec;
    fs::path current = fs::weakly_canonical(start, ec);
    if (ec) {
        current = start;
    }
    if (fs::is_regular_file(current, ec)) {
        current = current.parent_path();
    }
    while (true) {
        const fs::path candidate = current / "yukt.toml";
        if (fs::is_regular_file(candidate, ec)) {
            fs::path canon = fs::canonical(candidate, ec);
            if (ec) {
                return candidate;
            }
            return canon;
        }
        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) {
            return std::nullopt;
        }
        current = parent;
    }
}

std::optional<ProjectManifest> load_manifest(const fs::path& toml_path, DiagnosticEngine& diagnostics) {
    std::ifstream in(toml_path);
    if (!in) {
        invalid(diagnostics, loc_at(toml_path, 1, 1), "Could not read yukt.toml.",
                "Check that the file exists and is readable.");
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();
    diagnostics.add_source(toml_path.string(), source);

    std::vector<ParsedLine> lines;
    if (!parse_lines(toml_path, source, diagnostics, lines)) {
        return std::nullopt;
    }

    std::unordered_map<std::string, ParsedLine> keys;
    for (const auto& line : lines) {
        if (line.key != "name" && line.key != "version" && line.key != "type" && line.key != "entry") {
            invalid(diagnostics, loc_at(toml_path, line.line, line.column),
                    "Unknown key `" + line.key + "`.",
                    "0.18.1 allows only name, version, type, and entry.");
            return std::nullopt;
        }
        if (keys.contains(line.key)) {
            invalid(diagnostics, loc_at(toml_path, line.line, line.column),
                    "Duplicate key `" + line.key + "`.", "Each key may appear once.");
            return std::nullopt;
        }
        keys.emplace(line.key, line);
    }

    const auto require_key = [&](const char* key) -> const ParsedLine* {
        const auto it = keys.find(key);
        if (it == keys.end()) {
            invalid(diagnostics, loc_at(toml_path, 1, 1), std::string("Missing `") + key + "`.",
                    "Add the key to yukt.toml.");
            return nullptr;
        }
        return &it->second;
    };

    const ParsedLine* name = require_key("name");
    const ParsedLine* version = require_key("version");
    const ParsedLine* type = require_key("type");
    const ParsedLine* entry = require_key("entry");
    if (name == nullptr || version == nullptr || type == nullptr || entry == nullptr) {
        return std::nullopt;
    }

    if (!is_ident(name->value)) {
        invalid(diagnostics, loc_at(toml_path, name->line, name->column),
                "`name` must be a YUKT identifier.", "Use letters, digits, and underscore; start with a letter or _.");
        return std::nullopt;
    }
    if (!is_version(version->value)) {
        invalid(diagnostics, loc_at(toml_path, version->line, version->column),
                "`version` must be major.minor.patch.", "Example: version = \"0.1.0\".");
        return std::nullopt;
    }
    if (type->value != "cli" && type->value != "server") {
        invalid(diagnostics, loc_at(toml_path, type->line, type->column),
                "`type` must be cli or server.", "Use type = \"cli\" or type = \"server\".");
        return std::nullopt;
    }

    const fs::path entry_rel(entry->value);
    if (entry->value.empty() || entry_rel.is_absolute()) {
        invalid(diagnostics, loc_at(toml_path, entry->line, entry->column),
                "`entry` must be a relative .yukt path inside the project.",
                "Use entry = \"src/main.yukt\".");
        return std::nullopt;
    }
    if (entry_rel.generic_string().size() < 5 ||
        entry_rel.generic_string().substr(entry_rel.generic_string().size() - 5) != ".yukt") {
        invalid(diagnostics, loc_at(toml_path, entry->line, entry->column),
                "`entry` must end with .yukt.", "Point entry at a YUKT source file.");
        return std::nullopt;
    }
    for (const auto& part : entry_rel) {
        if (part == "..") {
            invalid(diagnostics, loc_at(toml_path, entry->line, entry->column),
                    "Entry path escapes the project root.",
                    "Use a path under the directory that contains yukt.toml.");
            return std::nullopt;
        }
    }

    std::error_code ec;
    const fs::path root = fs::canonical(toml_path.parent_path(), ec);
    if (ec) {
        invalid(diagnostics, loc_at(toml_path, 1, 1), "Could not resolve the project root.",
                "Check that yukt.toml's directory exists.");
        return std::nullopt;
    }

    const fs::path joined = root / entry_rel;
    if (!fs::exists(joined, ec)) {
        invalid(diagnostics, loc_at(toml_path, entry->line, entry->column), "Entry file does not exist.",
                "Create the file or fix the entry path.");
        return std::nullopt;
    }

    const fs::path entry_canon = fs::canonical(joined, ec);
    if (ec || !path_under_root(root, entry_canon)) {
        invalid(diagnostics, loc_at(toml_path, entry->line, entry->column),
                "Entry path escapes the project root.",
                "Use a path under the directory that contains yukt.toml.");
        return std::nullopt;
    }

    ProjectManifest manifest;
    manifest.name = name->value;
    manifest.version = version->value;
    manifest.type = type->value;
    manifest.entry = entry_canon;
    manifest.root = root;
    return manifest;
}

}  // namespace yukt

#include "driver/driver.hpp"

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/analyzer.hpp"

#include <filesystem>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace yukt {
namespace {

std::string display_lexeme(const Token& token) {
    if (token.kind == TokenKind::String) {
        std::string out = "\"";
        for (char c : token.lexeme) {
            switch (c) {
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                default:
                    out.push_back(c);
                    break;
            }
        }
        out += '"';
        return out;
    }
    if (token.kind == TokenKind::Newline) {
        return "\\n";
    }
    if (token.kind == TokenKind::Indent || token.kind == TokenKind::Dedent ||
        token.kind == TokenKind::EndOfFile) {
        return "";
    }
    return token.lexeme;
}

}  // namespace

FrontendResult analyze_source(std::string path, std::string source) {
    FrontendResult result;
    result.path = path;
    result.source = source;
    result.diagnostics = std::make_unique<DiagnosticEngine>(std::move(path), std::move(source));

    Lexer lexer(result.diagnostics->source(), *result.diagnostics);
    result.tokens = lexer.tokenize();

    Parser parser(result.tokens, *result.diagnostics);
    result.program = parser.parse_program();
    return result;
}

FrontendResult analyze_file(const std::string& path) {
    return analyze_file(path, default_stdlib_root());
}

FrontendResult analyze_file(const std::string& path, const std::string& stdlib_root) {
    std::ifstream input(path);
    if (!input) {
        FrontendResult result;
        result.path = path;
        result.diagnostics = std::make_unique<DiagnosticEngine>(path, "");
        result.diagnostics->error(
            SourceLocation{1, 1, path}, "File not found",
            "Could not open `" + path + "` for reading.",
            "Check the file path and try again.");
        result.program = std::make_unique<Program>();
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string source = buffer.str();

    FrontendResult result;
    result.path = path;
    result.source = source;
    result.diagnostics = std::make_unique<DiagnosticEngine>(path, source);
    load_module_graph(result.graph, path, *result.diagnostics, stdlib_root);
    if (result.graph.modules.empty()) {
        result.program = std::make_unique<Program>();
        return result;
    }
    result.tokens = result.graph.modules[0].tokens;
    result.source = result.graph.modules[0].source;
    result.path = result.graph.modules[0].canonical_path;
    if (result.graph.modules.size() == 1 && !result.graph.has_uses()) {
        result.program = std::move(result.graph.modules[0].program);
        result.graph = ModuleGraph{};
    }
    return result;
}

void analyze_types(FrontendResult& result) {
    if (result.diagnostics == nullptr || result.diagnostics->has_errors()) {
        return;
    }
    if (!result.graph.modules.empty()) {
        result.semantic = analyze_semantics(result.graph, *result.diagnostics);
        return;
    }
    if (result.program == nullptr) {
        return;
    }
    result.semantic = analyze_semantics(*result.program, *result.diagnostics);
}

void print_token(const Token& token, std::ostream& out) {
    out << 'L' << token.location.line << ":C" << token.location.column << '\t' << std::left
        << std::setw(16) << token_kind_name(token.kind) << display_lexeme(token) << '\n';
}

void print_tokens(const std::vector<Token>& tokens, std::ostream& out) {
    for (const auto& token : tokens) {
        print_token(token, out);
    }
}

}  // namespace yukt

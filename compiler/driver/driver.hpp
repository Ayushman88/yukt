#pragma once

#include "ast/ast.hpp"
#include "diagnostics/diagnostics.hpp"
#include "driver/module.hpp"
#include "lexer/token.hpp"
#include "semantic/analyzer.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yukt {

struct FrontendResult {
    std::string path;
    std::string source;
    std::vector<Token> tokens;
    std::unique_ptr<Program> program;
    std::unique_ptr<DiagnosticEngine> diagnostics;
    SemanticInfo semantic;
    ModuleGraph graph;

    Program* entry_program() {
        if (program) {
            return program.get();
        }
        if (!graph.modules.empty()) {
            return graph.modules[0].program.get();
        }
        return nullptr;
    }
};

FrontendResult analyze_source(std::string path, std::string source);
FrontendResult analyze_file(const std::string& path);
FrontendResult analyze_file(const std::string& path, const std::string& stdlib_root);
void analyze_types(FrontendResult& result);

void print_tokens(const std::vector<Token>& tokens, std::ostream& out);
void print_token(const Token& token, std::ostream& out);

}  // namespace yukt

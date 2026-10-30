#pragma once

#include "ast/ast.hpp"
#include "diagnostics/diagnostics.hpp"
#include "lexer/token.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace yukt {

struct LoadedModule {
    std::string canonical_path;
    std::string mangle_prefix;
    std::string source;
    std::vector<Token> tokens;
    std::unique_ptr<Program> program;
    bool is_entry = false;
    std::vector<int> import_modules;
};

struct ModuleGraph {
    std::string entry_dir;
    std::string stdlib_root;
    std::vector<LoadedModule> modules;
    std::vector<std::vector<int>> imports;
    bool has_uses() const;
};

std::string default_stdlib_root();
bool is_stdlib_runtime_ir(const std::string& ir_name);
std::string mangle_function(const std::string& prefix, const std::string& name, bool entry_main, bool single_file);
std::string module_prefix_for_path(const std::string& canonical_path, const std::string& entry_dir,
                                   const std::string& stdlib_root, bool is_std);

void load_module_graph(ModuleGraph& graph, const std::string& entry_path, DiagnosticEngine& diagnostics,
                       const std::string& stdlib_root);

}  // namespace yukt

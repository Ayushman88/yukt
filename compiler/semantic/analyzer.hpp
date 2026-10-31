#pragma once

#include "ast/ast.hpp"
#include "diagnostics/diagnostics.hpp"
#include "driver/module.hpp"
#include "semantic/type.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yukt {

struct RecordFieldInfo {
    std::string name;
    Type type;
    std::int64_t offset = 0;
    std::int64_t size = 0;
    std::int64_t align = 1;
    SourceLocation location;
};

struct RecordInfo {
    int id = -1;
    int module_index = 0;
    bool shared = false;
    std::string name;
    std::string identity;
    const RecordDecl* decl = nullptr;
    std::vector<RecordFieldInfo> fields;
    std::int64_t size = 0;
    std::int64_t align = 1;
    bool layout_ready = false;
};

struct InferredBinding {
    std::string name;
    std::string type;
};

struct InferredFunction {
    std::string name;
    std::string ir_name;
    std::vector<InferredBinding> parameters;
    std::string return_type;
    std::vector<InferredBinding> locals;
};

struct SemanticInfo {
    std::vector<InferredFunction> functions;
    std::vector<RecordInfo> records;
    std::unordered_map<const Expr*, Type> expression_types;
    std::unordered_map<std::string, Type> return_types;
    std::unordered_map<std::string, std::vector<Type>> parameter_types;
    std::unordered_map<std::string, std::vector<char>> parameter_refs;
    std::unordered_map<std::string, std::unordered_map<std::string, Type>> local_types;
    std::unordered_map<const FunctionDecl*, std::string> function_ir_names;
    std::unordered_map<const CallExpr*, std::string> call_ir_names;
    std::unordered_map<const CallExpr*, int> record_constructors;
    std::unordered_set<std::string> foreign_functions;
};

SemanticInfo analyze_semantics(const Program& program, DiagnosticEngine& diagnostics);
SemanticInfo analyze_semantics(const ModuleGraph& graph, DiagnosticEngine& diagnostics);
void print_types(const SemanticInfo& info, std::ostream& out);

}  // namespace yukt

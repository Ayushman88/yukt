#pragma once

#include "ast/ast.hpp"
#include "diagnostics/diagnostics.hpp"
#include "semantic/type.hpp"

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yukt {

struct InferredBinding {
    std::string name;
    std::string type;
};

struct InferredFunction {
    std::string name;
    std::vector<InferredBinding> parameters;
    std::string return_type;
    std::vector<InferredBinding> locals;
};

struct SemanticInfo {
    std::vector<InferredFunction> functions;
    std::unordered_map<const Expr*, Type> expression_types;
    std::unordered_map<std::string, Type> return_types;
    std::unordered_map<std::string, std::vector<Type>> parameter_types;
    std::unordered_map<std::string, std::unordered_map<std::string, Type>> local_types;
};

SemanticInfo analyze_semantics(const Program& program, DiagnosticEngine& diagnostics);
void print_types(const SemanticInfo& info, std::ostream& out);

}  // namespace yukt

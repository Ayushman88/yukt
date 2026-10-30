#pragma once

#include "ast/ast.hpp"
#include "driver/module.hpp"
#include "ir/ir.hpp"
#include "semantic/analyzer.hpp"

#include <string>

namespace yukt {

IrModule lower_to_ir(const Program& program, const SemanticInfo& semantic, const std::string& source_path);
IrModule lower_to_ir(const ModuleGraph& graph, const SemanticInfo& semantic);

}  // namespace yukt

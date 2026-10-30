#pragma once

#include "ir/ir.hpp"

#include <string>

namespace yukt {

std::string explain_execution(const IrModule& ir, const std::string& llvm_ir);

}  // namespace yukt

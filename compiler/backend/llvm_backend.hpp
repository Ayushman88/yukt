#pragma once

#include "ir/ir.hpp"

#include <string>

namespace yukt {

bool emit_llvm_ir(const IrModule& module, std::string& llvm_ir, std::string& error);
bool emit_object_file(const IrModule& module, const std::string& object_path, std::string& error);
bool link_executable(const std::string& object_path, const std::string& output_path, const std::string& runtime_path,
                     std::string& error);
int run_executable(const std::string& executable_path, std::string& output, std::string& error);

}  // namespace yukt

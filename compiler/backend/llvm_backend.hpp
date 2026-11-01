#pragma once

#include "ir/ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yukt {

bool emit_llvm_ir(const IrModule& module, std::string& llvm_ir, std::string& error);
bool emit_object_file(const IrModule& module, const std::string& object_path, std::string& error);
bool link_executable(const std::string& object_path, const std::string& output_path, const std::string& runtime_path,
                     std::string& error);
int run_executable(const std::string& executable_path, std::string& output, std::string& error,
                   const std::vector<std::string>& extra_args = {},
                   const std::optional<std::string>& stdin_text = std::nullopt,
                   const std::vector<std::pair<std::string, std::string>>& extra_env = {});

}  // namespace yukt

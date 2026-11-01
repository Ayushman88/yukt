#pragma once

#include "diagnostics/diagnostics.hpp"
#include "driver/driver.hpp"
#include "ir/ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yukt {

enum class CompileAction {
    Check,
    EmitYuktIr,
    EmitLlvm,
    Explain,
    Build,
    Run,
};

struct CompileRequest {
    CompileAction action = CompileAction::Check;
    std::string input_path;
    std::string output_path;
    std::vector<std::string> extra_args;
    std::optional<std::string> stdin_text;
    std::vector<std::pair<std::string, std::string>> extra_env;
};

struct CompileOutput {
    bool ok = false;
    int exit_code = 1;
    IrModule ir;
    std::string llvm_ir;
    std::string explanation;
    std::string program_output;
};

CompileOutput compile_program(FrontendResult& frontend, const CompileRequest& request, DiagnosticEngine& diagnostics);

}  // namespace yukt

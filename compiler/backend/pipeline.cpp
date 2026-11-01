#include "backend/pipeline.hpp"

#include "backend/explain.hpp"
#include "backend/llvm_backend.hpp"
#include "driver/paths.hpp"
#include "ir/lower.hpp"

#include <filesystem>
#include <unistd.h>

#ifndef YUKT_RUNTIME_LIB
#define YUKT_RUNTIME_LIB ""
#endif

namespace yukt {
namespace {

std::string default_output_path(const std::string& input) {
    std::filesystem::path path(input);
    auto stem = path.stem().string();
    if (stem.empty()) {
        stem = "a.out";
    }
    return stem;
}

std::string runtime_path() {
    return runtime_archive().string();
}

void report(DiagnosticEngine& diagnostics, const std::string& title, const std::string& detail,
            const std::string& suggestion) {
    diagnostics.error(SourceLocation{1, 1}, title, detail, suggestion);
}

}  // namespace

CompileOutput compile_program(FrontendResult& frontend, const CompileRequest& request, DiagnosticEngine& diagnostics) {
    CompileOutput output;
    Program* program = frontend.entry_program();
    if (program == nullptr) {
        report(diagnostics, "Compilation failed", "The frontend did not produce a program.",
               "Fix parse errors first.");
        return output;
    }
    if (diagnostics.has_errors()) {
        return output;
    }

    if (!frontend.graph.modules.empty()) {
        output.ir = lower_to_ir(frontend.graph, frontend.semantic);
    } else {
        output.ir = lower_to_ir(*program, frontend.semantic, frontend.path);
    }
    if (request.action == CompileAction::EmitYuktIr) {
        output.ok = !diagnostics.has_errors();
        output.exit_code = output.ok ? 0 : 1;
        return output;
    }

    if (request.action == CompileAction::Check) {
        output.ok = true;
        output.exit_code = 0;
        return output;
    }

    std::string error;
    if (request.action == CompileAction::EmitLlvm || request.action == CompileAction::Explain) {
        output.ok = emit_llvm_ir(output.ir, output.llvm_ir, error);
        if (!output.ok) {
            report(diagnostics, "LLVM lowering failed", error, "This is a compiler bug; the YUKT IR did not lower.");
        } else if (request.action == CompileAction::Explain) {
            output.explanation = explain_execution(output.ir, output.llvm_ir);
        }
        output.exit_code = output.ok ? 0 : 1;
        return output;
    }

    const auto runtime = runtime_path();
    if (runtime.empty()) {
        report(diagnostics, "Missing runtime", "The YUKT runtime library was not found.",
               "Rebuild the compiler so `libyukt_runtime.a` is produced.");
        return output;
    }

    const bool is_run = request.action == CompileAction::Run;
    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / ("yukt-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    const std::string object_path = (temp_dir / "program.o").string();
    const std::string exe_path = request.output_path.empty()
                                     ? (is_run ? (temp_dir / "program").string() : default_output_path(request.input_path))
                                     : request.output_path;
    {
        const auto parent = std::filesystem::path(exe_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
    }

    if (!emit_object_file(output.ir, object_path, error)) {
        report(diagnostics, "Object emission failed", error,
               "LLVM could not produce a native object file for this host.");
        return output;
    }
    if (!link_executable(object_path, exe_path, runtime, error)) {
        if (error == "NO_LINKER") {
            report(diagnostics, "Cannot find linker",
                   "YUKT links user programs with the host C compiler `clang`.",
                   "Install Command Line Tools (macOS) or a clang package (Linux).");
            return output;
        }
        report(diagnostics, "Link failed", error, "Check that `clang` can link the YUKT runtime.");
        return output;
    }

    if (is_run) {
        output.exit_code = run_executable(exe_path, output.program_output, error, request.extra_args,
                                          request.stdin_text, request.extra_env);
        if (!error.empty()) {
            report(diagnostics, "Program failed to run", error, "The generated executable did not start.");
            return output;
        }
        output.ok = true;
        std::filesystem::remove_all(temp_dir, ec);
        return output;
    }

    output.ok = true;
    output.exit_code = 0;
    std::filesystem::remove(object_path, ec);
    return output;
}

}  // namespace yukt

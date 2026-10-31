#include "ast/ast.hpp"
#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "ir/ir.hpp"
#include "semantic/analyzer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view kVersion = "0.13.3";

void print_usage(std::ostream& out) {
    out << "yukt " << kVersion << " — YUKT compiler\n\n"
        << "Usage:\n"
        << "  yukt --tokens <file.yukt>     Print lexer tokens\n"
        << "  yukt --ast <file.yukt>        Print the parsed abstract syntax tree\n"
        << "  yukt --types <file.yukt>      Infer types and print function signatures\n"
        << "  yukt --emit-ir <file.yukt>    Print YUKT IR\n"
        << "  yukt --emit-llvm <file.yukt>  Print LLVM IR\n"
        << "  yukt --explain <file.yukt>    Explain loop execution decisions\n"
        << "  yukt build <file.yukt> [-o <exe>]  Emit a native executable\n"
        << "  yukt run <file.yukt>          Compile and run a native executable\n"
        << "  yukt <file.yukt>              Parse and type-check\n"
        << "  yukt --help                   Show this help\n"
        << "  yukt --version                Show the compiler version\n";
}

int inspect(const std::string& path, bool show_tokens, bool show_ast, bool show_types) {
    if (show_tokens && !show_ast && !show_types) {
        std::ifstream input(path);
        if (!input) {
            std::cerr << path << ":1:1: error: File not found\n";
            return EXIT_FAILURE;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        auto result = yukt::analyze_source(path, buffer.str());
        yukt::print_tokens(result.tokens, std::cout);
        if (result.diagnostics != nullptr && !result.diagnostics->diagnostics().empty()) {
            result.diagnostics->emit(std::cerr);
        }
        const bool ok = result.diagnostics != nullptr && !result.diagnostics->has_errors();
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    auto result = yukt::analyze_file(path);
    const bool run_types = !show_tokens && !show_ast;
    if (run_types || show_types) {
        yukt::analyze_types(result);
    }

    const bool ok = result.diagnostics != nullptr && !result.diagnostics->has_errors();
    if (show_tokens) {
        yukt::print_tokens(result.tokens, std::cout);
    }
    if (show_ast) {
        if (!result.graph.modules.empty()) {
            for (const auto& module : result.graph.modules) {
                if (module.program == nullptr) {
                    continue;
                }
                std::cout << "# " << module.canonical_path << '\n';
                yukt::print_ast(*module.program, std::cout);
            }
        } else if (result.program != nullptr) {
            yukt::print_ast(*result.program, std::cout);
        }
    }
    if (show_types) {
        yukt::print_types(result.semantic, std::cout);
    }
    if (!show_tokens && !show_ast && !show_types && ok) {
        std::cout << "OK: type-checked " << path << '\n';
        std::cout << "Use `yukt build` or `yukt run` to generate a native executable.\n";
    }
    if (result.diagnostics != nullptr && !result.diagnostics->diagnostics().empty()) {
        result.diagnostics->emit(std::cerr);
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int compile(const yukt::CompileRequest& request) {
    auto frontend = yukt::analyze_file(request.input_path);
    if (frontend.diagnostics == nullptr) {
        std::cerr << "Internal error: missing diagnostics\n";
        return EXIT_FAILURE;
    }
    yukt::analyze_types(frontend);
    if (frontend.diagnostics->has_errors()) {
        frontend.diagnostics->emit(std::cerr);
        return EXIT_FAILURE;
    }

    auto output = yukt::compile_program(frontend, request, *frontend.diagnostics);
    if (request.action == yukt::CompileAction::EmitYuktIr) {
        yukt::print_ir(output.ir, std::cout);
    }
    if (request.action == yukt::CompileAction::EmitLlvm) {
        std::cout << output.llvm_ir;
    }
    if (request.action == yukt::CompileAction::Explain) {
        std::cout << output.explanation;
    }
    if (request.action == yukt::CompileAction::Build && output.ok) {
        const std::string exe = request.output_path.empty() ? std::filesystem::path(request.input_path).stem().string()
                                                           : request.output_path;
        std::cout << "OK: wrote native executable " << exe << '\n';
    }
    if (request.action == yukt::CompileAction::Run && output.ok) {
        std::cout << output.program_output;
    }
    if (frontend.diagnostics->has_errors()) {
        frontend.diagnostics->emit(std::cerr);
        return EXIT_FAILURE;
    }
    if (request.action == yukt::CompileAction::Run) {
        return output.exit_code;
    }
    return output.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        print_usage(std::cout);
        return args.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (args[0] == "--version") {
        std::cout << "yukt " << kVersion << '\n';
        return EXIT_SUCCESS;
    }

    if (args[0] == "build" || args[0] == "run" || args[0] == "--emit-ir" || args[0] == "--emit-llvm" ||
        args[0] == "--explain") {
        yukt::CompileRequest request;
        if (args[0] == "build") {
            request.action = yukt::CompileAction::Build;
        } else if (args[0] == "run") {
            request.action = yukt::CompileAction::Run;
        } else if (args[0] == "--emit-ir") {
            request.action = yukt::CompileAction::EmitYuktIr;
        } else if (args[0] == "--explain") {
            request.action = yukt::CompileAction::Explain;
        } else {
            request.action = yukt::CompileAction::EmitLlvm;
        }

        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-o") {
                if (i + 1 >= args.size()) {
                    std::cerr << "Missing output path after -o\n";
                    return EXIT_FAILURE;
                }
                request.output_path = args[++i];
            } else if (!args[i].empty() && args[i][0] == '-') {
                std::cerr << "Unknown option: " << args[i] << '\n';
                return EXIT_FAILURE;
            } else if (request.input_path.empty()) {
                request.input_path = args[i];
            } else {
                std::cerr << "Unexpected extra argument: " << args[i] << '\n';
                return EXIT_FAILURE;
            }
        }
        if (request.input_path.empty()) {
            std::cerr << "Missing input file.\n";
            print_usage(std::cerr);
            return EXIT_FAILURE;
        }
        return compile(request);
    }

    bool show_tokens = false;
    bool show_ast = false;
    bool show_types = false;
    std::string path;
    for (const auto& arg : args) {
        if (arg == "--tokens") {
            show_tokens = true;
        } else if (arg == "--ast") {
            show_ast = true;
        } else if (arg == "--types") {
            show_types = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n\n";
            print_usage(std::cerr);
            return EXIT_FAILURE;
        } else if (path.empty()) {
            path = arg;
        } else {
            std::cerr << "Unexpected extra argument: " << arg << '\n';
            return EXIT_FAILURE;
        }
    }

    if (path.empty()) {
        std::cerr << "Missing input file.\n\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    return inspect(path, show_tokens, show_ast, show_types);
}

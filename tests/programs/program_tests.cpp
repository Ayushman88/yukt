#include "ast/ast.hpp"
#include "driver/driver.hpp"
#include "semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(YUKT_SOURCE_DIR) / relative).string();
}

const yukt::FunctionDecl* function_named(const yukt::Program& program, const std::string& name) {
    for (const auto& function : program.functions) {
        if (function != nullptr && function->name == name) {
            return function.get();
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    {
        auto result = yukt::analyze_file(source_path("examples/hello.yukt"));
        yukt::analyze_types(result);
        REQUIRE_MSG(!result.diagnostics->has_errors(), "hello.yukt must parse and type-check");
        REQUIRE(result.program != nullptr);
        REQUIRE(result.program->functions.size() == 2);

        const auto* greet = function_named(*result.program, "greet");
        const auto* main_fn = function_named(*result.program, "main");
        REQUIRE(greet != nullptr);
        REQUIRE(main_fn != nullptr);
        REQUIRE(greet != nullptr && greet->parameters.size() == 1);
        REQUIRE(main_fn != nullptr && main_fn->body.size() == 4);
    }

    {
        auto result = yukt::analyze_file(source_path("tests/programs/nested_loops.yukt"));
        yukt::analyze_types(result);
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.program != nullptr);
        REQUIRE(!result.program->functions.empty());
    }

    {
        auto result = yukt::analyze_file(source_path("tests/programs/arithmetic.yukt"));
        yukt::analyze_types(result);
        REQUIRE(!result.diagnostics->has_errors());
    }

    {
        auto result = yukt::analyze_file(source_path("tests/programs/conditionals.yukt"));
        yukt::analyze_types(result);
        REQUIRE(!result.diagnostics->has_errors());
    }

    {
        const auto result = yukt::analyze_file(source_path("tests/programs/missing_indent.yukt"));
        REQUIRE(result.diagnostics->has_errors());
        bool saw_missing_indent = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Missing indentation") {
                saw_missing_indent = true;
            }
        }
        REQUIRE(saw_missing_indent);
    }

    {
        const auto result = yukt::analyze_file(source_path("tests/programs/unexpected.yukt"));
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(!result.diagnostics->diagnostics().empty());
        REQUIRE(result.diagnostics->diagnostics().front().title == "Unexpected token");
    }

    {
        const auto result = yukt::analyze_file(source_path("tests/programs/bad_indent.yukt"));
        REQUIRE(result.diagnostics->has_errors());
    }

    return yukt::test::summary("programs");
}

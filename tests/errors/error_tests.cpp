#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return std::string(YUKT_SOURCE_DIR) + "/" + relative;
}

yukt::FrontendResult check_source(const std::string& source) {
    auto result = yukt::analyze_source("<test>", source);
    yukt::analyze_types(result);
    return result;
}

bool has_title(const yukt::FrontendResult& result, const std::string& title) {
    if (result.diagnostics == nullptr) {
        return false;
    }
    for (const auto& diagnostic : result.diagnostics->diagnostics()) {
        if (diagnostic.title == title) {
            return true;
        }
    }
    return false;
}

yukt::CompileOutput run_file(const std::string& relative) {
    const auto path = source_path(relative);
    auto frontend = yukt::analyze_file(path);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = path;
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

}  // namespace

int main() {
    {
        const auto result = run_file("tests/errors/ok_fail.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "7\nbad_int\n");
    }

    {
        const auto result = check_source(
            "create main:\n"
            "    r = ok(1)\n"
            "    say r.value\n");
        REQUIRE(has_title(result, "Invalid result access"));
    }

    {
        const auto result = check_source(
            "create main:\n"
            "    r = ok(1)\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Unused result"));
    }

    {
        const auto result = check_source(
            "create boom:\n"
            "    give ok(1)\n"
            "\n"
            "create main:\n"
            "    boom()\n");
        REQUIRE(has_title(result, "Unused result"));
    }

    {
        const auto result = check_source(
            "create main:\n"
            "    r = ok(1)\n"
            "    if r.ok:\n"
            "        say r.error.code\n");
        REQUIRE(has_title(result, "Invalid result access"));
    }

    {
        const auto result = check_source(
            "create mixed:\n"
            "    give 1\n"
            "    give fail(\"x\")\n"
            "\n"
            "create main:\n"
            "    say mixed()\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    return yukt::test::summary("errors");
}

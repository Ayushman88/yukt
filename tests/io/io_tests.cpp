#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(YUKT_SOURCE_DIR) / relative).string();
}

yukt::CompileOutput run_file(const std::string& relative, const std::vector<std::string>& extra_args = {},
                             const std::optional<std::string>& stdin_text = std::nullopt,
                             const std::vector<std::pair<std::string, std::string>>& extra_env = {}) {
    const auto path = source_path(relative);
    auto frontend = yukt::analyze_file(path);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = path;
    request.extra_args = extra_args;
    request.stdin_text = stdin_text;
    request.extra_env = extra_env;
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

void require_run_with_args(const std::string& relative, const std::vector<std::string>& args,
                           const std::string& stdin_text, const std::string& expected) {
    const auto result = run_file(relative, args, stdin_text);
    REQUIRE_MSG(result.ok, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.exit_code == 0, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.program_output == expected, result.program_output + " != " + expected);
}

void require_run_env(const std::string& relative, const std::string& name, const std::string& value,
                     const std::string& expected) {
    const auto result = run_file(relative, {}, std::nullopt, {{name, value}});
    REQUIRE_MSG(result.ok, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.exit_code == 0, result.program_output + " (" + relative + ")");
    REQUIRE_MSG(result.program_output == expected, result.program_output + " != " + expected);
}

void require_trap_message(const std::string& relative, const std::string& message,
                          const std::optional<std::string>& stdin_text = std::nullopt) {
    const auto result = run_file(relative, {}, stdin_text);
    REQUIRE_MSG(result.ok, "trap program must compile: " + relative + "\n" + result.program_output);
    REQUIRE_MSG(result.exit_code != 0, "expected non-zero exit for " + relative);
    REQUIRE_MSG(result.program_output.find("YUKT runtime error:") != std::string::npos, result.program_output);
    REQUIRE_MSG(result.program_output.find(message) != std::string::npos, result.program_output);
}

}  // namespace

int main() {
    {
        const auto result = run_file("tests/io/args.yukt", {"one", "two"});
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE_MSG(result.exit_code == 0, result.program_output);
        const auto nl = result.program_output.find('\n');
        REQUIRE_MSG(nl != std::string::npos, result.program_output);
        REQUIRE_MSG(nl > 0, "io.program() should be argv[0], not empty");
        REQUIRE_MSG(result.program_output.substr(nl + 1) == "2\n", result.program_output);
    }

    require_run_with_args("tests/io/args_values.yukt", {"one", "two"}, "", "2\none\ntwo\n");
    require_run_with_args("tests/io/eof.yukt", {}, "", "eof\n");
    require_run_with_args("tests/io/last_chunk.yukt", {}, "hello", "hello\n");
    require_run_with_args("tests/io/two_lines.yukt", {}, "one\ntwo\n", "one\ntwo\neof\n");
    require_run_with_args("tests/io/crlf.yukt", {}, "one\r\ntwo\r\n", "one\ntwo\n");
    require_run_with_args("tests/io/read_all.yukt", {}, "a\nb\n", "a\nb\n\n");
    require_run_with_args("tests/io/read_all_empty.yukt", {}, "", "\n");

    require_run_with_args("tests/env/missing.yukt", {}, "", "not_found\n");
    require_run_with_args("tests/env/get_set.yukt", {}, "", "hello\ntrue\nfallback\n");
    require_run_env("tests/env/from_parent.yukt", "YUKT_0162_PARENT", "from-parent", "from-parent\ntrue\nfrom-parent\n");
    require_run_with_args("tests/env/empty_get.yukt", {}, "", "io\n");
    require_trap_message("tests/env/empty_has.yukt", "empty environment name");
    require_trap_message("tests/env/empty_set.yukt", "empty environment name");
    require_trap_message("tests/env/empty_get_or.yukt", "empty environment name");

    return yukt::test::summary("io");
}

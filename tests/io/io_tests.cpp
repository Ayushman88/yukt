#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
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

yukt::FrontendResult check_file(const std::string& relative) {
    auto result = yukt::analyze_file(source_path(relative));
    yukt::analyze_types(result);
    return result;
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

    {
        const auto result = check_file("tests/io/worker_stdin.yukt");
        REQUIRE(has_title(result, "Cannot read stdin from a started function"));
    }
    {
        const auto result = check_file("tests/io/worker_stdin_indirect.yukt");
        REQUIRE(has_title(result, "Cannot read stdin from a started function"));
    }
    require_run_with_args("tests/io/origin_still_reads.yukt", {}, "hello\n", "hello\n");

    {
        const auto result = run_file("tests/io/worker_args.yukt", {"x", "y"});
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.exit_code == 0);
        const auto nl = result.program_output.find('\n');
        REQUIRE(nl != std::string::npos);
        REQUIRE(nl > 0);
        REQUIRE(result.program_output.substr(nl + 1) == "2\n");
    }
    require_run_with_args("tests/io/worker_env.yukt", {}, "", "from-worker\n");
    {
        const auto result = run_file("tests/io/concurrent_say.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.exit_code == 0);
        std::istringstream in(result.program_output);
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            REQUIRE(line.size() == 100);
            const char c = line[0];
            REQUIRE(c == 'A' || c == 'B');
            for (char ch : line) {
                REQUIRE(ch == c);
            }
            ++n;
        }
        REQUIRE(n == 100);
    }
    {
        const auto result = run_file("tests/io/concurrent_record_say.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.exit_code == 0);
        std::istringstream in(result.program_output);
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            REQUIRE_MSG(line == "Point(1, 2)" || line == "Point(3, 4)", line);
            ++n;
        }
        REQUIRE(n == 100);
    }
    {
        const auto result = run_file("tests/io/concurrent_record_list_say.yukt");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.exit_code == 0);
        std::istringstream in(result.program_output);
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            REQUIRE_MSG(line == "[Point(1, 2), Point(1, 2)]" || line == "[Point(3, 4), Point(3, 4)]", line);
            ++n;
        }
        REQUIRE(n == 100);
    }

    return yukt::test::summary("io");
}

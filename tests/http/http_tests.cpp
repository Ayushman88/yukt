#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
#include "ir/ir.hpp"
#include "ir/lower.hpp"
#include "semantic/type.hpp"
#include "test_harness.hpp"

#include <sstream>
#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string source_path(const std::string& relative) {
    return std::string(YUKT_SOURCE_DIR) + "/" + relative;
}

yukt::FrontendResult check_file(const std::string& relative) {
    auto result = yukt::analyze_file(source_path(relative));
    yukt::analyze_types(result);
    return result;
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

}  // namespace

int main() {
    {
        const auto result = run_file("tests/http/https_fail.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "io\n");
    }
    {
        const auto result = run_file("tests/http/bad_url.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "io\n");
    }
    REQUIRE(has_title(check_file("tests/http/unknown_handler.yukt"), "Unknown function"));
    REQUIRE(has_title(check_file("tests/http/bad_handler_arity.yukt"), "Invalid http handler"));
    REQUIRE(has_title(check_file("tests/http/bad_handler_ref.yukt"), "Invalid http handler"));
    REQUIRE(has_title(check_file("tests/http/handler_main.yukt"), "Invalid http handler"));
    REQUIRE(has_title(check_file("tests/http/serve_on_not_listener.yukt"), "Type mismatch"));
    REQUIRE(has_title(check_file("tests/http/serve_addr_not_string.yukt"), "Type mismatch"));
    {
        const auto result = check_file("tests/http/serve_handler_not_stdin_edge.yukt");
        REQUIRE(!has_title(result, "Cannot read stdin from a started function"));
        REQUIRE(!result.diagnostics->has_errors());
    }
    REQUIRE(has_title(check_file("tests/http/start_handler_reads_stdin.yukt"),
                      "Cannot read stdin from a started function"));
    {
        const auto result = run_file("tests/http/bind_fail.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("YUKT runtime error: http bind failed") != std::string::npos);
    }
    {
        const auto result = run_file("tests/http/listener_closed.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.program_output.find("YUKT runtime error: http listener closed") != std::string::npos);
    }
    {
        const auto result = run_file("tests/http/localhost_get.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "200\nok\n");
    }
    {
        const auto result = run_file("tests/http/query_and_path.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "/hi:x=1\n");
    }
    return yukt::test::summary("http");
}

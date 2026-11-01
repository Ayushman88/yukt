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
        const auto result = check_file("tests/net/say_conn.yukt");
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        const auto result = check_file("tests/net/read_non_int.yukt");
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        const auto result = check_file("tests/net/start_ref_conn.yukt");
        REQUIRE(has_title(result, "Cannot start a ref"));
    }
    {
        const auto result = check_file("tests/net/accept_borrow.yukt");
        REQUIRE(!has_title(result, "Use after move"));
        REQUIRE(result.diagnostics != nullptr);
        REQUIRE(!result.diagnostics->has_errors());
    }
    {
        const auto result = run_file("tests/net/localhost.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "hello\n1\n");
    }
    {
        const auto result = run_file("tests/net/closed_read.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "closed\n");
    }
    {
        const auto result = run_file("tests/net/bad_addr.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "io\n");
    }
    {
        const auto result = run_file("tests/net/ipv6_loopback.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output.find("[::1]:") == 0 ||
                result.program_output == "io\n");
    }
    {
        const auto result = run_file("tests/ownership/result_value_take.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "hello\n");
    }
    {
        const auto result = run_file("tests/ownership/give_result_value.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "hello\n");
    }
    {
        const auto result = run_file("tests/ownership/call_result_value.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "hello\n");
    }
    return yukt::test::summary("net");
}

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

[[maybe_unused]] yukt::CompileOutput run_file(const std::string& relative) {
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
        const auto result = check_file("tests/sync/infer_fail.yukt");
        REQUIRE(has_title(result, "Cannot infer type"));
    }
    {
        const auto result = check_file("tests/sync/receiver_twice.yukt");
        REQUIRE(has_title(result, "receiver already taken"));
    }
    {
        const auto result = check_file("tests/sync/say_end.yukt");
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        const auto result = check_file("tests/sync/send_recv_mismatch.yukt");
        REQUIRE(has_title(result, "Type mismatch"));
    }
    {
        const auto result = check_file("tests/sync/start_ref_send.yukt");
        REQUIRE(has_title(result, "Cannot start a ref"));
    }
    {
        const auto result = check_file("tests/sync/recv_borrow.yukt");
        REQUIRE(!has_title(result, "Use after move"));
        REQUIRE(result.diagnostics != nullptr);
        REQUIRE(!result.diagnostics->has_errors());
    }
    {
        const auto result = run_file("tests/sync/basic.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "1\n");
    }
    {
        const auto result = run_file("tests/sync/ping_pong.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        std::string want;
        for (int i = 1; i <= 1000; ++i) {
            want += std::to_string(i);
            want += "\n";
        }
        REQUIRE_MSG(result.program_output == want, result.program_output);
    }
    {
        const auto result = run_file("tests/sync/closed.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "1\nclosed\n");
    }
    {
        const auto result = run_file("tests/sync/drop_close.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "closed\n");
    }
    {
        const auto result = run_file("tests/sync/send_named_string.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "hi\n");
    }
    {
        const auto result = run_file("tests/sync/drop_record8.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "1\n");
    }
    {
        const auto result = run_file("tests/sync/drop_record_str.yukt");
        REQUIRE_MSG(result.ok && result.exit_code == 0, result.program_output);
        REQUIRE(result.program_output == "1\n");
    }
    {
        const auto result = check_file("tests/sync/move_end.yukt");
        REQUIRE(has_title(result, "Use after move"));
    }
    return yukt::test::summary("sync");
}

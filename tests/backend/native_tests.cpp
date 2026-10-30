#include "backend/pipeline.hpp"
#include "driver/driver.hpp"
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

yukt::CompileOutput run_file(const std::string& relative) {
    const auto path = source_path(relative);
    auto frontend = yukt::analyze_file(path);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = path;
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

yukt::CompileOutput run_source(const std::string& source) {
    auto frontend = yukt::analyze_source("<test>", source);
    yukt::analyze_types(frontend);
    yukt::CompileRequest request;
    request.action = yukt::CompileAction::Run;
    request.input_path = "<test>";
    return yukt::compile_program(frontend, request, *frontend.diagnostics);
}

}  // namespace

int main() {
    {
        const auto result = run_file("examples/hello.yukt");
        REQUIRE_MSG(result.ok, "hello.yukt must compile and run");
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.program_output ==
                "Hello, World\n"
                "1\n"
                "2\n"
                "3\n"
                "4\n"
                "5\n"
                "YUKT is running!\n");
    }

    {
        const auto result = run_file("tests/programs/arithmetic.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "7\n");
        REQUIRE(result.exit_code == 54);
    }

    {
        const auto result = run_file("tests/programs/conditionals.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "adult\n");
    }

    {
        const auto result = run_file("tests/programs/nested_loops.yukt");
        REQUIRE(result.ok);
        REQUIRE(result.program_output ==
                "1\n1\n"
                "1\n2\n"
                "1\n3\n"
                "2\n1\n"
                "2\n2\n"
                "2\n3\n"
                "3\n1\n"
                "3\n2\n"
                "3\n3\n");
    }

    {
        const auto result = run_source(
            "create add(left, right):\n"
            "    give left + right\n"
            "\n"
            "create main:\n"
            "    say add(20, 22)\n"
            "    items = [1, 2, 3]\n"
            "    say items\n"
            "    if 3 > 1:\n"
            "        say true\n"
            "    otherwise:\n"
            "        say false\n");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "42\n[1, 2, 3]\ntrue\n");
    }

    {
        auto frontend = yukt::analyze_source("<test>", "create main:\n    greet(1)\n");
        yukt::analyze_types(frontend);
        REQUIRE(frontend.diagnostics->has_errors());
    }

    {
        const auto result = run_source(
            "create add(left, right):\n"
            "    give left + right\n"
            "\n"
            "create main:\n"
            "    items = zeros(20)\n"
            "    repeat i in 0..19:\n"
            "        items[i] = i + 1\n"
            "    total = 0\n"
            "    repeat i in 0..19:\n"
            "        total = total + items[i]\n"
            "    say count(items)\n"
            "    say total\n"
            "    say add(items[0], items[19])\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "20\n210\n21\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 8\n"
            "    a = zeros(n * n)\n"
            "    b = zeros(n * n)\n"
            "    c = zeros(n * n)\n"
            "    repeat i in 0..n - 1:\n"
            "        repeat j in 0..n - 1:\n"
            "            a[i * n + j] = 1\n"
            "            b[i * n + j] = 2\n"
            "    repeat i in 0..n - 1:\n"
            "        repeat j in 0..n - 1:\n"
            "            total = 0\n"
            "            repeat k in 0..n - 1:\n"
            "                total = total + a[i * n + k] * b[k * n + j]\n"
            "            c[i * n + j] = total\n"
            "    checksum = 0\n"
            "    repeat i in 0..n * n - 1:\n"
            "        checksum = checksum + c[i]\n"
            "    say checksum\n");
        REQUIRE(result.ok);
        REQUIRE(result.program_output == "1024\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    total = 0\n"
            "    repeat i in 5..1:\n"
            "        total = total + i\n"
            "    say total\n"
            "    once = 0\n"
            "    repeat i in 7..7:\n"
            "        once = i\n"
            "    say once\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "0\n7\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 8\n"
            "    items = zeros(n)\n"
            "    last = n - 1\n"
            "    repeat i in 0..last:\n"
            "        repeat j in 0..last:\n"
            "            items[j] = items[j] + i\n"
            "    checksum = 0\n"
            "    repeat j in 0..last:\n"
            "        checksum = checksum + items[j]\n"
            "    say checksum\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "224\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 4096\n"
            "    items = zeros(n)\n"
            "    last = n - 1\n"
            "    repeat i in 0..last:\n"
            "        items[i] = i\n"
            "    total = 0\n"
            "    repeat i in 0..last:\n"
            "        total = total + items[i]\n"
            "    say total\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "8386560\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = i\n"
            "    say items[0]\n"
            "    say items[n - 1]\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "0\n999999\n");
    }

    {
        const auto result = run_source(
            "create main:\n"
            "    n = 64\n"
            "    a = zeros(n)\n"
            "    b = zeros(n)\n"
            "    c = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        a[i] = 1\n"
            "        b[i] = 2\n"
            "    repeat i in 0..n - 1:\n"
            "        total = 0\n"
            "        repeat k in 0..n - 1:\n"
            "            total = total + a[k] * b[k]\n"
            "        c[i] = total\n"
            "    checksum = 0\n"
            "    repeat i in 0..n - 1:\n"
            "        checksum = checksum + c[i]\n"
            "    say checksum\n");
        REQUIRE_MSG(result.ok, result.program_output);
        REQUIRE(result.program_output == "8192\n");
    }

    {
        auto frontend = yukt::analyze_file(source_path("examples/hello.yukt"));
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = source_path("examples/hello.yukt");
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE(result.ok);
        REQUIRE(result.explanation.find("planned: sequential") != std::string::npos);
        REQUIRE(result.explanation.find("body contains `say`") != std::string::npos ||
                result.explanation.find("below 1000000") != std::string::npos);
        REQUIRE(result.explanation.find("verified parallel: yes") == std::string::npos);
        REQUIRE(result.explanation.find("PassBuilder default O3") != std::string::npos);
    }

    {
        auto frontend = yukt::analyze_source("<test>",
                                            "create main:\n"
                                            "    n = 1000000\n"
                                            "    items = zeros(n)\n"
                                            "    repeat i in 0..n - 1:\n"
                                            "        items[i] = i\n"
                                            "    say items[0]\n");
        yukt::analyze_types(frontend);
        yukt::CompileRequest request;
        request.action = yukt::CompileAction::Explain;
        request.input_path = "<test>";
        const auto result = yukt::compile_program(frontend, request, *frontend.diagnostics);
        REQUIRE_MSG(result.ok, result.explanation);
        REQUIRE(result.explanation.find("planned: parallel") != std::string::npos);
        REQUIRE(result.explanation.find("verified parallel: yes") != std::string::npos);
        REQUIRE(result.llvm_ir.find("yukt_parallel_for") != std::string::npos);
    }

    return yukt::test::summary("native");
}

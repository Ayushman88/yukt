#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

std::string fixture(const std::string& relative) {
    return std::string(YUKT_SOURCE_DIR) + "/tests/modules/" + relative;
}

yukt::FrontendResult load(const std::string& relative) {
    auto result = yukt::analyze_file(fixture(relative));
    yukt::analyze_types(result);
    return result;
}

yukt::FrontendResult load_std(const std::string& relative, const std::string& stdroot) {
    auto result = yukt::analyze_file(fixture(relative), fixture(stdroot));
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

const yukt::Diagnostic* first_title(const yukt::FrontendResult& result, const std::string& title) {
    if (result.diagnostics == nullptr) {
        return nullptr;
    }
    for (const auto& diagnostic : result.diagnostics->diagnostics()) {
        if (diagnostic.title == title) {
            return &diagnostic;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    {
        auto result = load("ok/app.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 2);
        REQUIRE(result.semantic.call_ir_names.size() >= 1);
        bool saw_math_add = false;
        for (const auto& function : result.semantic.functions) {
            if (function.name == "add" && function.ir_name.find('$') != std::string::npos) {
                saw_math_add = true;
            }
        }
        REQUIRE(saw_math_add);
    }

    {
        auto result = load("nested/a.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 3);
    }

    {
        auto result = load("nont/a.yukt");
        REQUIRE(has_title(result, "Unknown module"));
    }

    {
        auto result = load("missing/app.yukt");
        REQUIRE(has_title(result, "Cannot find module"));
        const auto* diagnostic = first_title(result, "Cannot find module");
        REQUIRE(diagnostic != nullptr && !diagnostic->location.file.empty());
        REQUIRE(diagnostic != nullptr && !diagnostic->notes.empty());
    }

    {
        auto result = load("std_bare/app.yukt");
        REQUIRE(has_title(result, "Invalid module path"));
    }

    {
        auto result = load_std("stduser/app.yukt", "stdroot");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 2);
    }

    {
        auto result = load("cycle/a.yukt");
        REQUIRE(has_title(result, "Circular import"));
        const auto* diagnostic = first_title(result, "Circular import");
        REQUIRE(diagnostic != nullptr && !diagnostic->notes.empty());
    }

    {
        auto result = load("self/app.yukt");
        REQUIRE(has_title(result, "Circular import"));
    }

    {
        auto result = load("samefile/app.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 2);
    }

    {
        auto result = load("private/app.yukt");
        REQUIRE(has_title(result, "Unknown function"));
        const auto* diagnostic = first_title(result, "Unknown function");
        REQUIRE(diagnostic != nullptr && !diagnostic->notes.empty());
    }

    {
        auto result = load("alias_dup/app.yukt");
        REQUIRE(has_title(result, "Duplicate alias"));
    }

    {
        auto result = load("alias_fn/app.yukt");
        REQUIRE(has_title(result, "Name collision"));
    }

    {
        auto result = load("alias_count/app.yukt");
        REQUIRE(has_title(result, "Name collision"));
    }

    {
        auto result = load("dup_export/app.yukt");
        REQUIRE(has_title(result, "Duplicate function"));
    }

    {
        auto result = load("two_add/app.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 3);
    }

    {
        auto result = load("libmain/app.yukt");
        REQUIRE(has_title(result, "Invalid entry point"));
    }

    {
        auto result = load("sharemain/app.yukt");
        REQUIRE(has_title(result, "Invalid entry point"));
    }

    {
        auto result = load("unqual/app.yukt");
        REQUIRE(has_title(result, "Unknown function"));
    }

    {
        auto result = load("types/app.yukt");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Type mismatch") || has_title(result, "Cannot infer type"));
    }

    {
        auto result = load("mangle/app.yukt");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.graph.modules.size() == 3);
        bool saw_left = false;
        bool saw_right = false;
        for (const auto& function : result.semantic.functions) {
            if (function.ir_name.find("a$b_c$") != std::string::npos) {
                saw_left = true;
            }
            if (function.ir_name.find("a_b$c$") != std::string::npos) {
                saw_right = true;
            }
        }
        REQUIRE(saw_left);
        REQUIRE(saw_right);
    }

    return yukt::test::summary("modules");
}

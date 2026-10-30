#include "driver/driver.hpp"
#include "semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

yukt::FrontendResult check(const std::string& source) {
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

bool has_diagnostic(const yukt::FrontendResult& result, const std::string& title, const std::string& detail_substr,
                    const std::string& suggestion) {
    if (result.diagnostics == nullptr) {
        return false;
    }
    for (const auto& diagnostic : result.diagnostics->diagnostics()) {
        if (diagnostic.title == title && diagnostic.detail.find(detail_substr) != std::string::npos &&
            diagnostic.suggestion == suggestion) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const std::string move_suggestion =
        "This value was moved. Use the new owner, or copy with list.copy before moving.";
    const std::string repeat_suggestion =
        "Copy first, or restructure so the move happens outside the loop.";

    // Move via plain assignment, then use.
    {
        const auto result = check(
            "create take(xs):\n"
            "    give count(xs)\n"
            "\n"
            "create main:\n"
            "    xs = zeros(3)\n"
            "    ys = xs\n"
            "    say count(ys)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // Move into a user function argument, then use.
    {
        const auto result = check(
            "create sink(xs):\n"
            "    say count(xs)\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    sink(xs)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // Overwrite is valid (drop happens in IR later, not an error here).
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(2)\n"
            "    xs = zeros(4)\n"
            "    say count(xs)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // give transfers: xs is Moved, ys is Owned. Using ys is fine.
    {
        const auto result = check(
            "create ident(xs):\n"
            "    give xs\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = ident(xs)\n"
            "    say count(ys)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Using xs after the move into ident is rejected.
    {
        const auto result = check(
            "create ident(xs):\n"
            "    give xs\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = ident(xs)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
    }

    // Moving an outer heap name inside a repeat is rejected.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(1)\n"
            "    repeat i in 1..3:\n"
            "        ys = xs\n"
            "        say count(ys)\n");
        REQUIRE(has_title(result, "Cannot move inside repeat"));
        REQUIRE(has_diagnostic(result, "Cannot move inside repeat", "`xs`", repeat_suggestion));
    }

    // Borrows (count/say/index) do not move.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(3)\n"
            "    say count(xs)\n"
            "    say xs[0]\n"
            "    say count(xs)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Strings move too.
    {
        const auto result = check(
            "create sink(s):\n"
            "    say s\n"
            "\n"
            "create main:\n"
            "    s = \"hello\"\n"
            "    t = s\n"
            "    say t\n"
            "    say s\n");
        REQUIRE(has_title(result, "Use after move"));
    }

    // Scalar values are never tracked: reusing them freely is fine.
    {
        const auto result = check(
            "create main:\n"
            "    n = 1\n"
            "    m = n\n"
            "    say n\n"
            "    say m\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    return yukt::test::summary("ownership");
}

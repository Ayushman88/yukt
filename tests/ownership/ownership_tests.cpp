#include "driver/driver.hpp"
#include "semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <cstdint>
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

bool has_title_on_line(const yukt::FrontendResult& result, const std::string& title, std::uint32_t line) {
    if (result.diagnostics == nullptr) {
        return false;
    }
    for (const auto& diagnostic : result.diagnostics->diagnostics()) {
        if (diagnostic.title == title && diagnostic.location.line == line) {
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

    // Heap parameters start Owned. `give xs` marks Moved; using the param after is
    // Use after move (count, second give, pass to another user function).
    {
        const auto result = check(
            "create take(xs):\n"
            "    give xs\n"
            "    say count(xs)\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = take(xs)\n"
            "    say count(ys)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    {
        const auto result = check(
            "create take(xs):\n"
            "    give xs\n"
            "    give xs\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = take(xs)\n"
            "    say count(ys)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    {
        const auto result = check(
            "create sink(xs):\n"
            "    say count(xs)\n"
            "\n"
            "create take(xs):\n"
            "    give xs\n"
            "    sink(xs)\n"
            "\n"
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = take(xs)\n"
            "    say count(ys)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // Integer parameters must not be Owned: assigning and then using the param is fine.
    {
        const auto result = check(
            "create add(n):\n"
            "    m = n\n"
            "    give n + m\n"
            "\n"
            "create main:\n"
            "    say add(3)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // If-join: then moves, otherwise does not, use after if → Use after move.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(2)\n"
            "    if 1 == 1:\n"
            "        ys = xs\n"
            "        say count(ys)\n"
            "    otherwise:\n"
            "        say count(xs)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // If-join: both branches leave Owned → valid.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(2)\n"
            "    if 1 == 1:\n"
            "        say count(xs)\n"
            "    otherwise:\n"
            "        say count(xs)\n"
            "    say count(xs)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // If-join: no otherwise, then moves, use after if → Use after move.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(2)\n"
            "    if 1 == 1:\n"
            "        ys = xs\n"
            "        say count(ys)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // Reassigning a Moved name is Use after move (not a fresh Owned binding).
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(2)\n"
            "    ys = xs\n"
            "    xs = zeros(3)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // If-join of uses after the if is unchanged: any-Moved-wins, so this is
    // Use after move even when the then branch does not run.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(1)\n"
            "    if false:\n"
            "        ys = xs\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    // Overwriting an outer heap name inside repeat drops it; that is an error.
    {
        const auto result = check(
            "create main:\n"
            "    xs = zeros(1)\n"
            "    repeat i in 1..2:\n"
            "        xs = zeros(i)\n");
        REQUIRE(has_title(result, "Cannot move inside repeat"));
        REQUIRE(has_diagnostic(result, "Cannot move inside repeat", "`xs`", repeat_suggestion));
    }

    // A loop-local heap name is dropped at iteration end. Using it after the
    // loop is Use after move; the last allocation must not stay readable.
    {
        const auto result = check(
            "create main:\n"
            "    repeat i in 1..3:\n"
            "        xs = zeros(i)\n"
            "    say count(xs)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`xs`", move_suggestion));
    }

    const std::string field_move_suggestion = "Move the whole record, or copy with list.copy.";
    const std::string list_elem_suggestion = "Copy the list with list.copy, or use the element in place.";

    // Whole-record move of a heap record; using the source field is Use after move.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    say p.name\n"
            "    q = p\n"
            "    say q.name\n"
            "    say p.name\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`p`", move_suggestion));
    }

    // Overwriting a heap field is valid (drop happens in IR later).
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    p.scores = zeros(4)\n"
            "    say count(p.scores)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Field overwrite evaluates the RHS first, so the old field is still readable.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    p.scores = zeros(count(p.scores))\n"
            "    say count(p.scores)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // RHS that moves the object is Use after move on the field-assign place.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create take(p):\n"
            "    give p.scores\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    p.scores = take(p)\n"
            "    say count(p.scores)\n");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`p`", move_suggestion));
        REQUIRE(has_title_on_line(result, "Use after move", 10));
    }

    // Same order bug without a later use of `p` and without field move-out.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create take(p):\n"
            "    say p.name\n"
            "    give zeros(1)\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    p.scores = take(p)\n");
        REQUIRE(has_title(result, "Use after move"));
        REQUIRE(has_diagnostic(result, "Use after move", "`p`", move_suggestion));
    }

    // Whole-record move of a heap record; using only the destination is valid.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    q = p\n"
            "    say q.name\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Binding a heap field out of a record is not a steal.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    stolen = p.scores\n"
            "    say count(stolen)\n");
        REQUIRE(has_title(result, "Cannot move out of field"));
        REQUIRE(has_diagnostic(result, "Cannot move out of field", "scores", field_move_suggestion));
    }

    // Plain Point records stay untracked: copy and use both names.
    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    q = p\n"
            "    say p.x\n"
            "    say q.x\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // A list field whose element type is never inferred is an error.
    {
        const auto result = check(
            "record Box:\n"
            "    items: list\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Cannot infer type"));
        REQUIRE(has_title_on_line(result, "Cannot infer type", 2));
    }

    // Box([]) leaves the list element unbound.
    {
        const auto result = check(
            "record Box:\n"
            "    items: list\n"
            "\n"
            "create main:\n"
            "    say Box([])\n");
        REQUIRE(has_title(result, "Cannot infer type"));
        REQUIRE(has_title_on_line(result, "Cannot infer type", 2) ||
                has_title_on_line(result, "Cannot infer type", 5));
    }

    // A constructor that determines E stays valid.
    {
        const auto result = check(
            "record Box:\n"
            "    items: list\n"
            "\n"
            "create main:\n"
            "    say count(Box(zeros(1)).items)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Binding a heap string out of a list is not a steal.
    {
        const auto result = check(
            "create main:\n"
            "    names = [\"a\"]\n"
            "    stolen = names[0]\n"
            "    say stolen\n");
        REQUIRE(has_title(result, "Cannot move out of list element"));
        REQUIRE(has_diagnostic(result, "Cannot move out of list element", "list element", list_elem_suggestion));
    }

    // Binding a heap record out of a list is not a steal.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    n: integer\n"
            "\n"
            "create main:\n"
            "    people = [Profile(\"a\", 1)]\n"
            "    stolen = people[0]\n"
            "    say stolen.n\n");
        REQUIRE(has_title(result, "Cannot move out of list element"));
        REQUIRE(has_diagnostic(result, "Cannot move out of list element", "list element", list_elem_suggestion));
    }

    // Index overwrite of a string slot is valid (drop happens in IR/runtime).
    {
        const auto result = check(
            "create main:\n"
            "    names = [\"a\", \"b\"]\n"
            "    names[0] = \"c\"\n"
            "    say names[0]\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Index read of a scalar element is still a value copy.
    {
        const auto result = check(
            "create main:\n"
            "    xs = [1, 2]\n"
            "    n = xs[0]\n"
            "    say n\n"
            "    say xs[1]\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Field overwrite on an indexed heap record is valid.
    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    n: integer\n"
            "\n"
            "create main:\n"
            "    people = [Profile(\"a\", 1)]\n"
            "    people[0].name = \"b\"\n"
            "    say people[0].n\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    // Disagreeing constructors stay Type mismatch, not Cannot infer type.
    {
        const auto result = check(
            "record Box:\n"
            "    items: list\n"
            "\n"
            "create main:\n"
            "    a = Box(zeros(1))\n"
            "    b = Box([\"x\"])\n"
            "    say count(a.items)\n");
        REQUIRE(has_title(result, "Type mismatch"));
        REQUIRE(!has_title(result, "Cannot infer type"));
    }

    return yukt::test::summary("ownership");
}

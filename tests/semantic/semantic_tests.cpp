#include "driver/driver.hpp"
#include "semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <sstream>
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

const yukt::InferredFunction* function_named(const yukt::SemanticInfo& info, const std::string& name) {
    for (const auto& function : info.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    {
        const auto result = check(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n"
            "\n"
            "create main:\n"
            "    name = \"World\"\n"
            "    greet(name)\n"
            "    repeat i in 1..5:\n"
            "        say i\n"
            "    say \"YUKT is running!\"\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* greet = function_named(result.semantic, "greet");
        const auto* main_fn = function_named(result.semantic, "main");
        REQUIRE(greet != nullptr && greet->return_type == "void");
        REQUIRE(greet != nullptr && greet->parameters.size() == 1 && greet->parameters[0].type == "string");
        REQUIRE(main_fn != nullptr && main_fn->return_type == "void");
        REQUIRE(main_fn != nullptr && !main_fn->locals.empty() && main_fn->locals[0].name == "name");
        REQUIRE(main_fn != nullptr && main_fn->locals[0].type == "string");
    }

    {
        const auto result = check(
            "create add(left, right):\n"
            "    give left + right\n"
            "\n"
            "create main:\n"
            "    say add(1, 2)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* add = function_named(result.semantic, "add");
        REQUIRE(add != nullptr && add->return_type == "integer");
        REQUIRE(add != nullptr && add->parameters.size() == 2);
        REQUIRE(add != nullptr && add->parameters[0].type == "integer");
        REQUIRE(add != nullptr && add->parameters[1].type == "integer");
    }

    {
        const auto result = check(
            "create check(age):\n"
            "    if age > 18:\n"
            "        say \"adult\"\n"
            "    otherwise:\n"
            "        say \"young\"\n"
            "    give true\n"
            "\n"
            "create main:\n"
            "    check(21)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* check_fn = function_named(result.semantic, "check");
        REQUIRE(check_fn != nullptr && check_fn->parameters[0].type == "integer");
        REQUIRE(check_fn != nullptr && check_fn->return_type == "boolean");
    }

    {
        const auto result = check(
            "create main:\n"
            "    items = [1, 2, 3]\n"
            "    say items\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main_fn = function_named(result.semantic, "main");
        REQUIRE(main_fn != nullptr && !main_fn->locals.empty());
        REQUIRE(main_fn != nullptr && main_fn->locals[0].type == "list[integer]");
    }

    {
        const auto result = check(
            "create greet(name):\n"
            "    say name\n");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(has_title(result, "Missing entry point"));
    }

    {
        const auto result = check(
            "create main(name):\n"
            "    say name\n");
        REQUIRE(has_title(result, "Invalid entry point"));
    }

    {
        const auto result = check(
            "create greet:\n"
            "    say 1\n"
            "\n"
            "create greet:\n"
            "    say 2\n"
            "\n"
            "create main:\n"
            "    greet()\n");
        REQUIRE(has_title(result, "Duplicate function"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    say missing\n");
        REQUIRE(has_title(result, "Unknown name"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    count = 1\n"
            "    count = \"two\"\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n"
            "\n"
            "create main:\n"
            "    greet()\n");
        REQUIRE(has_title(result, "Wrong number of arguments"));
    }

    {
        const auto result = check(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n"
            "\n"
            "create main:\n"
            "    greet(21)\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    if 1:\n"
            "        say \"yes\"\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    repeat i in \"a\"..\"z\":\n"
            "        say i\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    say \"age \" + 21\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    value = 1 + 1.5\n"
            "    say value\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create answer:\n"
            "    give 1\n"
            "    give true\n"
            "\n"
            "create main:\n"
            "    say answer()\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n"
            "\n"
            "create main:\n"
            "    value = greet(\"World\")\n"
            "    say value\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create unused(value):\n"
            "    say 1\n"
            "\n"
            "create main:\n"
            "    unused(1)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* unused = function_named(result.semantic, "unused");
        REQUIRE(unused != nullptr && unused->parameters[0].type == "integer");
    }

    {
        const auto result = check(
            "create unused(value):\n"
            "    say 1\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Cannot infer type"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    items = [1, \"two\"]\n"
            "    say items\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    repeat i in 1..3:\n"
            "        say i\n"
            "    say i\n");
        REQUIRE(has_title(result, "Unknown name"));
    }

    {
        const auto result = check(
            "create missing:\n"
            "    say 1\n"
            "\n"
            "create main:\n"
            "    other()\n");
        REQUIRE(has_title(result, "Unknown function"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    items = []\n"
            "    say items\n");
        REQUIRE(has_title(result, "Cannot infer type"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    items = zeros(4)\n"
            "    items[0] = 7\n"
            "    say items[0]\n"
            "    say count(items)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(result.semantic, "main");
        REQUIRE(main != nullptr);
        bool saw_items = false;
        if (main != nullptr) {
            for (const auto& local : main->locals) {
                if (local.name == "items") {
                    saw_items = local.type == "list[integer]";
                }
            }
        }
        REQUIRE(saw_items);
    }

    {
        const auto result = check(
            "create main:\n"
            "    items = [1, 2, 3]\n"
            "    items[true] = 1\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "create main:\n"
            "    value = 1\n"
            "    value = \"no\"\n");
        REQUIRE(has_title(result, "Type mismatch"));
        bool saw_note = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            for (const auto& note : diagnostic.notes) {
                if (note.text.find("first assigned here") != std::string::npos) {
                    saw_note = true;
                }
            }
        }
        REQUIRE(saw_note);
        std::ostringstream printed;
        result.diagnostics->emit(printed);
        REQUIRE(printed.str().find("<test>:3:") != std::string::npos);
        REQUIRE(printed.str().find("error: Type mismatch") != std::string::npos);
        REQUIRE(printed.str().find("note:") != std::string::npos);
    }

    {
        const auto result = check(
            "create main:\n"
            "    say count()\n");
        REQUIRE(has_title(result, "Wrong number of arguments"));
        bool saw_note = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            for (const auto& note : diagnostic.notes) {
                if (note.text.find("builtin") != std::string::npos) {
                    saw_note = true;
                }
            }
        }
        REQUIRE(saw_note);
    }

    {
        const auto result = check("create greet:\n    say 1\n");
        REQUIRE(has_title(result, "Missing entry point"));
        std::ostringstream printed;
        result.diagnostics->emit(printed);
        REQUIRE(printed.str().find("error: Missing entry point") != std::string::npos);
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    say p.x\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.semantic.records.size() == 1);
        REQUIRE(result.semantic.records[0].name == "Point");
        REQUIRE(result.semantic.records[0].size == 16);
        REQUIRE(result.semantic.records[0].align == 8);
        const auto* main_fn = function_named(result.semantic, "main");
        REQUIRE(main_fn != nullptr && !main_fn->locals.empty());
        REQUIRE(main_fn->locals[0].type == "Point");
    }

    {
        const auto result = check(
            "record Box:\n"
            "    origin: Point\n"
            "    w: integer\n"
            "\n"
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    say Box(Point(1, 2), 3)\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.semantic.records.size() == 2);
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    say p.z\n");
        REQUIRE(has_title(result, "Unknown field"));
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "record Size:\n"
            "    w: integer\n"
            "    h: integer\n"
            "\n"
            "create main:\n"
            "    say Point(1, 2) == Size(1, 2)\n");
        REQUIRE(has_title(result, "Type mismatch"));
    }

    {
        const auto result = check(
            "record Bad:\n"
            "    s: ref\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Unknown name"));
    }

    {
        const auto result = check(
            "record Profile:\n"
            "    name: string\n"
            "    scores: list\n"
            "\n"
            "create main:\n"
            "    p = Profile(\"ann\", zeros(2))\n"
            "    say p.name\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

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

    {
        const auto result = check(
            "record Box:\n"
            "    items: list\n"
            "\n"
            "create main:\n"
            "    say count(Box(zeros(1)).items)\n");
        REQUIRE(!result.diagnostics->has_errors());
    }

    {
        const auto result = check(
            "record A:\n"
            "    b: B\n"
            "\n"
            "record B:\n"
            "    a: A\n"
            "\n"
            "create main:\n"
            "    say 1\n");
        REQUIRE(has_title(result, "Recursive record"));
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    p.add(1)\n");
        REQUIRE(has_title(result, "Invalid expression"));
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    n = 1\n"
            "    say n.x\n");
        REQUIRE(has_title(result, "Invalid expression"));
    }

    {
        const auto result = check(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1)\n");
        REQUIRE(has_title(result, "Wrong number of arguments"));
    }

    {
        const auto result = check(
            "record Flag:\n"
            "    on: boolean\n"
            "    n: integer\n"
            "\n"
            "create main:\n"
            "    say Flag(true, 1)\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.semantic.records.size() == 1);
        REQUIRE(result.semantic.records[0].size == 16);
        REQUIRE(result.semantic.records[0].fields[0].offset == 0);
        REQUIRE(result.semantic.records[0].fields[1].offset == 8);
    }

    return yukt::test::summary("semantic");
}

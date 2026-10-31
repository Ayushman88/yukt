#include "ast/ast.hpp"
#include "driver/driver.hpp"
#include "test_harness.hpp"

#include <memory>
#include <string>

namespace {

yukt::FrontendResult parse(const std::string& source) {
    return yukt::analyze_source("<test>", source);
}

const yukt::FunctionDecl* function_named(const yukt::Program& program, const std::string& name) {
    for (const auto& function : program.functions) {
        if (function != nullptr && function->name == name) {
            return function.get();
        }
    }
    return nullptr;
}

template <typename T>
const T* as(const yukt::Stmt* stmt) {
    return dynamic_cast<const T*>(stmt);
}

template <typename T>
const T* as(const yukt::Expr* expr) {
    return dynamic_cast<const T*>(expr);
}

}  // namespace

int main() {
    {
        const auto result = parse(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.program != nullptr);
        const auto* greet = function_named(*result.program, "greet");
        REQUIRE(greet != nullptr);
        REQUIRE(greet != nullptr && greet->parameters.size() == 1);
        REQUIRE(greet != nullptr && greet->parameters[0] == "name");
        REQUIRE(greet != nullptr && greet->body.size() == 1);
        const auto* say = greet != nullptr ? as<yukt::SayStmt>(greet->body[0].get()) : nullptr;
        REQUIRE(say != nullptr);
        const auto* concat = say != nullptr ? as<yukt::BinaryExpr>(say->value.get()) : nullptr;
        REQUIRE(concat != nullptr);
        REQUIRE(concat != nullptr && concat->op == yukt::TokenKind::Plus);
    }

    {
        const auto result = parse(
            "create increment(value: ref integer):\n"
            "    value = value + 1\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* increment = function_named(*result.program, "increment");
        REQUIRE(increment != nullptr);
        REQUIRE(increment != nullptr && increment->parameters.size() == 1);
        REQUIRE(increment != nullptr && increment->parameters[0] == "value");
        REQUIRE(increment != nullptr && increment->parameter_is_ref.size() == 1 && increment->parameter_is_ref[0]);
        REQUIRE(increment != nullptr && increment->parameter_ref_types.size() == 1 &&
                increment->parameter_ref_types[0].name == "integer");
    }

    {
        const auto result = parse(
            "foreign strlen(s: ref string): integer\n"
            "\n"
            "create main:\n"
            "    say strlen(\"hi\")\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* strlen_fn = function_named(*result.program, "strlen");
        REQUIRE(strlen_fn != nullptr);
        REQUIRE(strlen_fn != nullptr && strlen_fn->foreign);
        REQUIRE(strlen_fn != nullptr && strlen_fn->body.empty());
        REQUIRE(strlen_fn != nullptr && strlen_fn->parameters.size() == 1);
        REQUIRE(strlen_fn != nullptr && strlen_fn->parameter_is_ref.size() == 1 && strlen_fn->parameter_is_ref[0]);
        REQUIRE(strlen_fn != nullptr && strlen_fn->parameter_ref_types[0].name == "string");
        REQUIRE(strlen_fn != nullptr && strlen_fn->has_return_type);
        REQUIRE(strlen_fn != nullptr && strlen_fn->return_type_name.name == "integer");
    }

    {
        const auto result = parse(
            "create main:\n"
            "    r = ok(1)\n"
            "    e = fail(\"x\")\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr);
        REQUIRE(main != nullptr && main->body.size() == 2);
        const auto* assign = main != nullptr ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        REQUIRE(assign != nullptr);
        const auto* call = assign != nullptr ? as<yukt::CallExpr>(assign->value.get()) : nullptr;
        REQUIRE(call != nullptr && call->callee == "ok");
    }

    {
        const auto result = parse(
            "create main:\n"
            "    greet(name)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr);
        REQUIRE(main != nullptr && main->parameters.empty());
        const auto* call_stmt = main != nullptr ? as<yukt::ExprStmt>(main->body[0].get()) : nullptr;
        REQUIRE(call_stmt != nullptr);
        const auto* call = call_stmt != nullptr ? as<yukt::CallExpr>(call_stmt->expr.get()) : nullptr;
        REQUIRE(call != nullptr);
        REQUIRE(call != nullptr && call->callee == "greet");
        REQUIRE(call != nullptr && call->arguments.size() == 1);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    value = 1 + 2 * 3\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* assign = main != nullptr && !main->body.empty() ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        REQUIRE(assign != nullptr);
        const auto* plus = assign != nullptr ? as<yukt::BinaryExpr>(assign->value.get()) : nullptr;
        REQUIRE(plus != nullptr);
        REQUIRE(plus != nullptr && plus->op == yukt::TokenKind::Plus);
        const auto* left = plus != nullptr ? as<yukt::IntegerExpr>(plus->left.get()) : nullptr;
        const auto* times = plus != nullptr ? as<yukt::BinaryExpr>(plus->right.get()) : nullptr;
        REQUIRE(left != nullptr && left->value == 1);
        REQUIRE(times != nullptr && times->op == yukt::TokenKind::Star);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    repeat i in 1..2:\n"
            "        repeat j in 1..2:\n"
            "            say i\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* outer = main != nullptr && !main->body.empty() ? as<yukt::RepeatStmt>(main->body[0].get()) : nullptr;
        REQUIRE(outer != nullptr);
        REQUIRE(outer != nullptr && outer->iterator == "i");
        const auto* inner = outer != nullptr && !outer->body.empty() ? as<yukt::RepeatStmt>(outer->body[0].get()) : nullptr;
        REQUIRE(inner != nullptr);
        REQUIRE(inner != nullptr && inner->iterator == "j");
        REQUIRE(inner != nullptr && inner->body.size() == 1);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    if age > 18:\n"
            "        say \"adult\"\n"
            "    otherwise:\n"
            "        say \"young\"\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* iff = main != nullptr && !main->body.empty() ? as<yukt::IfStmt>(main->body[0].get()) : nullptr;
        REQUIRE(iff != nullptr);
        REQUIRE(iff != nullptr && iff->then_branch.size() == 1);
        REQUIRE(iff != nullptr && iff->otherwise_branch.size() == 1);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    items = [1, 2, 3]\n"
            "    give items\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 2);
        const auto* assign = main != nullptr ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        REQUIRE(assign != nullptr);
        const auto* list = assign != nullptr ? as<yukt::ListExpr>(assign->value.get()) : nullptr;
        REQUIRE(list != nullptr && list->elements.size() == 3);
        REQUIRE(as<yukt::GiveStmt>(main->body[1].get()) != nullptr);
    }

    {
        const auto result = parse("say 1\n");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(result.diagnostics->diagnostics().front().title == "Unexpected token");
    }

    {
        const auto result = parse(
            "create main:\n"
            "say 1\n");
        REQUIRE(result.diagnostics->has_errors());
        bool saw_missing_indent = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Missing indentation") {
                saw_missing_indent = true;
            }
        }
        REQUIRE(saw_missing_indent);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say +\n");
        REQUIRE(result.diagnostics->has_errors());
        bool saw_unexpected = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Unexpected token") {
                saw_unexpected = true;
            }
        }
        REQUIRE(saw_unexpected);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    otherwise:\n"
            "        say 1\n");
        REQUIRE(result.diagnostics->has_errors());
        REQUIRE(result.diagnostics->diagnostics().front().title == "Unexpected token");
    }

    {
        const auto result = parse(
            "create :\n"
            "    say 1\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    items = [1, 2, 3]\n"
            "    items[0] = 9\n"
            "    say items[1]\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 3);
        const auto* index_assign = main != nullptr ? as<yukt::IndexAssignStmt>(main->body[1].get()) : nullptr;
        REQUIRE(index_assign != nullptr);
        const auto* say = main != nullptr ? as<yukt::SayStmt>(main->body[2].get()) : nullptr;
        REQUIRE(say != nullptr && as<yukt::IndexExpr>(say->value.get()) != nullptr);
    }

    {
        const auto result = parse(
            "use math\n"
            "use std/math as m\n"
            "share create add(a, b):\n"
            "    give a + b\n"
            "\n"
            "create main:\n"
            "    say math.add(1, 2)\n"
            "    say m.add(3, 4)\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.program != nullptr && result.program->uses.size() == 2);
        REQUIRE(result.program->uses[0].path.size() == 1 && result.program->uses[0].alias == "math");
        REQUIRE(result.program->uses[1].path.size() == 2 && result.program->uses[1].alias == "m");
        const auto* add = function_named(*result.program, "add");
        REQUIRE(add != nullptr && add->shared);
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 2);
        const auto* say = as<yukt::SayStmt>(main->body[0].get());
        const auto* call = say != nullptr ? as<yukt::CallExpr>(say->value.get()) : nullptr;
        REQUIRE(call != nullptr && call->qualifier == "math" && call->callee == "add");
    }

    {
        const auto result = parse(
            "create main:\n"
            "    use math\n"
            "    say 1\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say 1\n"
            "use math\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say math.add\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* say = main != nullptr && !main->body.empty() ? as<yukt::SayStmt>(main->body[0].get()) : nullptr;
        const auto* field = say != nullptr ? as<yukt::FieldExpr>(say->value.get()) : nullptr;
        REQUIRE(field != nullptr && field->field == "add");
    }

    {
        const auto result = parse(
            "share create repeat(s, n):\n"
            "    give s\n"
            "\n"
            "create main:\n"
            "    say str.repeat(\"ab\", 3)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* fn = function_named(*result.program, "repeat");
        REQUIRE(fn != nullptr && fn->shared);
        const auto* main = function_named(*result.program, "main");
        const auto* say = main != nullptr && !main->body.empty() ? as<yukt::SayStmt>(main->body[0].get()) : nullptr;
        const auto* call = say != nullptr ? as<yukt::CallExpr>(say->value.get()) : nullptr;
        REQUIRE(call != nullptr && call->qualifier == "str" && call->callee == "repeat");
    }

    {
        const auto result = parse("create if:\n    say 1\n");
        REQUIRE(result.diagnostics->has_errors());
        bool reserved = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Reserved word") {
                reserved = true;
            }
        }
        REQUIRE(reserved);
    }

    {
        const auto result = parse("create give(x):\n    say x\n");
        REQUIRE(result.diagnostics->has_errors());
        bool reserved = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Reserved word") {
                reserved = true;
            }
        }
        REQUIRE(reserved);
    }

    {
        const auto result = parse("create main:\n    say math.if(1)\n");
        REQUIRE(result.diagnostics->has_errors());
        bool reserved = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Reserved word") {
                reserved = true;
            }
        }
        REQUIRE(reserved);
    }

    {
        const auto result = parse(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "share record Flag:\n"
            "    on: boolean\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    say p.x\n"
            "    p.y = 9\n");
        REQUIRE(!result.diagnostics->has_errors());
        REQUIRE(result.program != nullptr && result.program->records.size() == 2);
        REQUIRE(result.program->records[0] != nullptr && result.program->records[0]->name == "Point");
        REQUIRE(result.program->records[0] != nullptr && !result.program->records[0]->shared);
        REQUIRE(result.program->records[0] != nullptr && result.program->records[0]->fields.size() == 2);
        REQUIRE(result.program->records[1] != nullptr && result.program->records[1]->shared);
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 3);
        const auto* assign = main != nullptr ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        const auto* ctor = assign != nullptr ? as<yukt::CallExpr>(assign->value.get()) : nullptr;
        REQUIRE(ctor != nullptr && ctor->callee == "Point" && ctor->arguments.size() == 2);
        const auto* say = main != nullptr ? as<yukt::SayStmt>(main->body[1].get()) : nullptr;
        const auto* field = say != nullptr ? as<yukt::FieldExpr>(say->value.get()) : nullptr;
        REQUIRE(field != nullptr && field->field == "x");
        const auto* field_assign = main != nullptr ? as<yukt::FieldAssignStmt>(main->body[2].get()) : nullptr;
        REQUIRE(field_assign != nullptr && field_assign->field == "y");
    }

    {
        const auto result = parse("record Empty:\n    1\n");
        REQUIRE(result.diagnostics->has_errors());
        bool invalid = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Invalid record") {
                invalid = true;
            }
        }
        REQUIRE(invalid);
    }

    {
        const auto result = parse("record if:\n    x: integer\n");
        REQUIRE(result.diagnostics->has_errors());
        bool reserved = false;
        for (const auto& diagnostic : result.diagnostics->diagnostics()) {
            if (diagnostic.title == "Reserved word") {
                reserved = true;
            }
        }
        REQUIRE(reserved);
    }

    {
        const auto result = parse(
            "create work(n):\n"
            "    give n + 1\n"
            "\n"
            "create main:\n"
            "    t = start work(3)\n"
            "    say wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 2);
        const auto* assign = main != nullptr ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        REQUIRE(assign != nullptr && assign->name == "t");
        const auto* start = assign != nullptr ? as<yukt::StartExpr>(assign->value.get()) : nullptr;
        REQUIRE(start != nullptr);
        REQUIRE(start != nullptr && start->qualifier.empty());
        REQUIRE(start != nullptr && start->callee == "work");
        REQUIRE(start != nullptr && start->arguments.size() == 1);
        REQUIRE(start != nullptr && as<yukt::IntegerExpr>(start->arguments[0].get()) != nullptr);
        REQUIRE(start != nullptr && as<yukt::IntegerExpr>(start->arguments[0].get())->value == 3);
        REQUIRE(as<yukt::CallExpr>(assign != nullptr ? assign->value.get() : nullptr) == nullptr);
        const auto* say = main != nullptr ? as<yukt::SayStmt>(main->body[1].get()) : nullptr;
        const auto* wait = say != nullptr ? as<yukt::WaitExpr>(say->value.get()) : nullptr;
        REQUIRE(wait != nullptr);
        const auto* waited = wait != nullptr ? as<yukt::IdentifierExpr>(wait->task.get()) : nullptr;
        REQUIRE(waited != nullptr && waited->name == "t");
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start work()\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* assign = main != nullptr && !main->body.empty() ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        const auto* start = assign != nullptr ? as<yukt::StartExpr>(assign->value.get()) : nullptr;
        REQUIRE(start != nullptr && start->callee == "work" && start->arguments.empty());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start alias.work(3)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* assign = main != nullptr && !main->body.empty() ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        const auto* start = assign != nullptr ? as<yukt::StartExpr>(assign->value.get()) : nullptr;
        REQUIRE(start != nullptr && start->qualifier == "alias" && start->callee == "work");
        REQUIRE(start != nullptr && start->arguments.size() == 1);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start add(1, 2)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* assign = main != nullptr && !main->body.empty() ? as<yukt::AssignStmt>(main->body[0].get()) : nullptr;
        const auto* start = assign != nullptr ? as<yukt::StartExpr>(assign->value.get()) : nullptr;
        REQUIRE(start != nullptr && start->callee == "add" && start->arguments.size() == 2);
        REQUIRE(as<yukt::IntegerExpr>(start->arguments[0].get()) != nullptr &&
                as<yukt::IntegerExpr>(start->arguments[0].get())->value == 1);
        REQUIRE(as<yukt::IntegerExpr>(start->arguments[1].get()) != nullptr &&
                as<yukt::IntegerExpr>(start->arguments[1].get())->value == 2);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* stmt = main != nullptr && !main->body.empty() ? as<yukt::ExprStmt>(main->body[0].get()) : nullptr;
        REQUIRE(stmt != nullptr);
        const auto* wait = stmt != nullptr ? as<yukt::WaitExpr>(stmt->expr.get()) : nullptr;
        REQUIRE(wait != nullptr);
        const auto* name = wait != nullptr ? as<yukt::IdentifierExpr>(wait->task.get()) : nullptr;
        REQUIRE(name != nullptr && name->name == "t");
    }

    {
        const auto result = parse(
            "create work:\n"
            "    give wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* work = function_named(*result.program, "work");
        const auto* give = work != nullptr && !work->body.empty() ? as<yukt::GiveStmt>(work->body[0].get()) : nullptr;
        REQUIRE(give != nullptr && as<yukt::WaitExpr>(give->value.get()) != nullptr);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say wait t + 1\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* say = main != nullptr && !main->body.empty() ? as<yukt::SayStmt>(main->body[0].get()) : nullptr;
        const auto* plus = say != nullptr ? as<yukt::BinaryExpr>(say->value.get()) : nullptr;
        REQUIRE(plus != nullptr && plus->op == yukt::TokenKind::Plus);
        REQUIRE(plus != nullptr && as<yukt::WaitExpr>(plus->left.get()) != nullptr);
        REQUIRE(plus != nullptr && as<yukt::IntegerExpr>(plus->right.get()) != nullptr);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say wait wait t\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* say = main != nullptr && !main->body.empty() ? as<yukt::SayStmt>(main->body[0].get()) : nullptr;
        const auto* outer = say != nullptr ? as<yukt::WaitExpr>(say->value.get()) : nullptr;
        const auto* inner = outer != nullptr ? as<yukt::WaitExpr>(outer->task.get()) : nullptr;
        REQUIRE(inner != nullptr && as<yukt::IdentifierExpr>(inner->task.get()) != nullptr);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    say wait xs[0]\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* say = main != nullptr && !main->body.empty() ? as<yukt::SayStmt>(main->body[0].get()) : nullptr;
        const auto* wait = say != nullptr ? as<yukt::WaitExpr>(say->value.get()) : nullptr;
        REQUIRE(wait != nullptr && as<yukt::IndexExpr>(wait->task.get()) != nullptr);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    greet(name)\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        const auto* stmt = main != nullptr && !main->body.empty() ? as<yukt::ExprStmt>(main->body[0].get()) : nullptr;
        REQUIRE(stmt != nullptr && as<yukt::CallExpr>(stmt->expr.get()) != nullptr);
        REQUIRE(as<yukt::StartExpr>(stmt != nullptr ? stmt->expr.get() : nullptr) == nullptr);
    }

    {
        const auto result = parse(
            "create main:\n"
            "    starter = 1\n"
            "    waiter = 2\n"
            "    restart = 3\n");
        REQUIRE(!result.diagnostics->has_errors());
        const auto* main = function_named(*result.program, "main");
        REQUIRE(main != nullptr && main->body.size() == 3);
        REQUIRE(as<yukt::AssignStmt>(main->body[0].get()) != nullptr &&
                as<yukt::AssignStmt>(main->body[0].get())->name == "starter");
        REQUIRE(as<yukt::AssignStmt>(main->body[1].get()) != nullptr &&
                as<yukt::AssignStmt>(main->body[1].get())->name == "waiter");
        REQUIRE(as<yukt::AssignStmt>(main->body[2].get()) != nullptr &&
                as<yukt::AssignStmt>(main->body[2].get())->name == "restart");
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start work\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start (3)\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    t = start work(1,\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    wait\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    {
        const auto result = parse(
            "create main:\n"
            "    start = 1\n");
        REQUIRE(result.diagnostics->has_errors());
    }

    return yukt::test::summary("parser");
}

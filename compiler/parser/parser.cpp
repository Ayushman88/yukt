#include "parser/parser.hpp"

#include <charconv>
#include <system_error>
#include <utility>

namespace yukt {
namespace {

bool is_binary_operator(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual:
            return true;
        default:
            return false;
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {
    if (tokens_.empty()) {
        tokens_.push_back(Token{TokenKind::EndOfFile, "", SourceLocation{1, 1}});
    }
}

std::unique_ptr<Program> Parser::parse_program() {
    auto program = std::make_unique<Program>();
    skip_newlines();

    if (!is_at_end()) {
        program->span.start = peek().location;
    }

    bool seen_item = false;
    while (!is_at_end()) {
        const std::size_t before = current_;
        skip_newlines();
        if (is_at_end()) {
            break;
        }
        if (check(TokenKind::KwUse)) {
            if (seen_item) {
                error_at(peek(), "Unexpected token",
                         "`use` must appear before any `create` or `record` in this file.",
                         "Move this `use` to the top of the file.");
                synchronize();
            } else {
                program->uses.push_back(parse_use());
            }
        } else if (check(TokenKind::KwForeign)) {
            seen_item = true;
            if (auto function = parse_foreign()) {
                program->functions.push_back(std::move(function));
            }
        } else if (check(TokenKind::KwShare) || check(TokenKind::KwCreate) || check(TokenKind::KwRecord)) {
            seen_item = true;
            bool shared = false;
            if (match(TokenKind::KwShare)) {
                shared = true;
                if (check(TokenKind::KwRecord)) {
                    if (auto record = parse_record(true)) {
                        program->records.push_back(std::move(record));
                    }
                } else if (check(TokenKind::KwCreate)) {
                    if (auto function = parse_function(true)) {
                        program->functions.push_back(std::move(function));
                    }
                } else {
                    error_at(peek(), "Unexpected token",
                             "`share` is only valid immediately before `create` or `record`.",
                             "Write `share create name:` or `share record Name:`.");
                    synchronize();
                }
                (void)shared;
            } else if (check(TokenKind::KwRecord)) {
                if (auto record = parse_record(false)) {
                    program->records.push_back(std::move(record));
                }
            } else if (auto function = parse_function(false)) {
                program->functions.push_back(std::move(function));
            }
        } else {
            error_at(peek(), "Unexpected token",
                     "Top-level code must start with `use`, `record`, `foreign`, or a `create` function declaration. Found " +
                         token_description(peek()) + ".",
                     "Wrap this code in `create main:` or move it into an existing function.");
            synchronize();
        }
        if (current_ == before) {
            advance();
        }
    }

    if (!program->functions.empty()) {
        program->span.end = program->functions.back()->span.end;
    } else if (!program->records.empty()) {
        program->span.end = program->records.back()->span.end;
    }
    return program;
}

UseDecl Parser::parse_use() {
    UseDecl use;
    const Token start = peek();
    use.use_location = start.location;
    use.span.start = start.location;
    advance();

    if (!check(TokenKind::Identifier)) {
        error_at(peek(), "Unexpected token",
                 "Expected a module path after `use`. Found " + token_description(peek()) + ".",
                 "Write `use math` or `use std/math as m`.");
        synchronize();
        return use;
    }

    use.path.push_back(advance().lexeme);
    while (match(TokenKind::Slash)) {
        if (!check(TokenKind::Identifier)) {
            error_at(peek(), "Unexpected token",
                     "Expected a path component after `/`. Found " + token_description(peek()) + ".",
                     "Write a path such as `use std/math`.");
            synchronize();
            return use;
        }
        use.path.push_back(advance().lexeme);
    }

    use.alias = use.path.back();
    use.alias_location = previous().location;
    if (match(TokenKind::KwAs)) {
        if (!check(TokenKind::Identifier)) {
            error_at(peek(), "Unexpected token",
                     "Expected an alias after `as`. Found " + token_description(peek()) + ".",
                     "Write `use std/math as math`.");
            synchronize();
            return use;
        }
        const Token alias = advance();
        use.alias = alias.lexeme;
        use.alias_location = alias.location;
    }

    expect_newline("after this `use`");
    use.span.end = previous().location;
    return use;
}

std::unique_ptr<RecordDecl> Parser::parse_record(bool shared) {
    const Token start = peek();
    auto record = std::make_unique<RecordDecl>();
    record->span.start = start.location;
    record->shared = shared;

    if (!consume(TokenKind::KwRecord, "Unexpected token",
                 "Expected `record` to start a record declaration.",
                 "Write `record Name:` or `share record Name:`.")) {
        synchronize();
        return nullptr;
    }

    if (!check(TokenKind::Identifier)) {
        if (is_keyword(peek().kind)) {
            error_at(peek(), "Reserved word",
                     "`" + peek().lexeme + "` is reserved and cannot be a record name.",
                     "Choose a different record name.");
        } else {
            error_at(peek(), "Unexpected token",
                     "Expected a record name after `record`. Found " + token_description(peek()) + ".",
                     "Write `record Point:`.");
        }
        synchronize();
        return nullptr;
    }
    const Token name = advance();
    record->name = name.lexeme;
    record->name_location = name.location;

    if (!consume(TokenKind::Colon, "Unexpected token",
                 "Expected `:` after the record name.",
                 "Write `record Point:` and indent the fields.")) {
        synchronize();
        return nullptr;
    }
    if (!consume(TokenKind::Newline, "Unexpected token",
                 "Expected a newline after `record " + record->name + ":`.",
                 "Put fields on the following indented lines.")) {
        synchronize();
        return nullptr;
    }
    if (!consume(TokenKind::Indent, "Unexpected token",
                 "Expected an indented field list after `record " + record->name + ":`.",
                 "Indent the fields, for example `    x: integer`.")) {
        synchronize();
        return nullptr;
    }

    while (!is_at_end() && !check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)) {
        skip_newlines();
        if (check(TokenKind::Dedent) || is_at_end()) {
            break;
        }
        if (!check(TokenKind::Identifier)) {
            error_at(peek(), "Unexpected token",
                     "Expected a field name. Found " + token_description(peek()) + ".",
                     "Write `name: integer`.");
            synchronize();
            break;
        }
        FieldDecl field;
        const Token field_name = advance();
        field.name = field_name.lexeme;
        field.name_location = field_name.location;
        field.span.start = field_name.location;
        if (!consume(TokenKind::Colon, "Unexpected token",
                     "Expected `:` after field `" + field.name + "`.",
                     "Write `" + field.name + ": integer`.")) {
            synchronize();
            break;
        }
        if (check(TokenKind::KwRef)) {
            error_at(peek(), "Cannot store a ref",
                     "A `ref` cannot be a record field.",
                     "Keep `ref` as a parameter, or store the owned value.");
            advance();
            if (check(TokenKind::Identifier)) {
                parse_type_name();
            }
        } else {
            field.type = parse_type_name();
        }
        field.span.end = previous().location;
        expect_newline("after this field");
        record->fields.push_back(std::move(field));
    }

    if (!consume(TokenKind::Dedent, "Unexpected token",
                 "Expected the field list to end.",
                 "Dedent after the last field.")) {
        synchronize();
    }
    record->span.end = previous().location;
    if (record->fields.empty()) {
        error_at(name, "Invalid record",
                 "Record `" + record->name + "` has no fields.",
                 "Add at least one field, for example `x: integer`.");
    }
    return record;
}

TypeName Parser::parse_type_name() {
    TypeName type;
    if (!check(TokenKind::Identifier)) {
        error_at(peek(), "Unexpected token",
                 "Expected a type name. Found " + token_description(peek()) + ".",
                 "Use `integer`, `float`, `boolean`, `result[T]`, or a record name.");
        return type;
    }
    const Token first = advance();
    type.span.start = first.location;
    if (check(TokenKind::Dot)) {
        type.qualifier = first.lexeme;
        type.qualifier_location = first.location;
        type.dot_location = peek().location;
        advance();
        if (!check(TokenKind::Identifier)) {
            error_at(peek(), "Unexpected token",
                     "Expected a record name after `" + type.qualifier + ".`. Found " +
                         token_description(peek()) + ".",
                     "Write a qualified type such as `geom.Point`.");
            type.name = first.lexeme;
            type.name_location = first.location;
            type.span.end = first.location;
            return type;
        }
        const Token second = advance();
        type.name = second.lexeme;
        type.name_location = second.location;
        type.span.end = second.location;
        return type;
    }
    type.name = first.lexeme;
    type.name_location = first.location;
    type.span.end = first.location;
    if (type.name == "result" && match(TokenKind::LBracket)) {
        type.arguments.push_back(parse_type_name());
        if (!consume(TokenKind::RBracket, "Unexpected token",
                     "Expected `]` after the payload type of `result`.",
                     "Write `result[integer]`.")) {
            return type;
        }
        type.span.end = previous().location;
    }
    return type;
}

std::unique_ptr<FunctionDecl> Parser::parse_function(bool shared) {
    const Token start = peek();
    auto function = std::make_unique<FunctionDecl>();
    function->span.start = start.location;
    function->shared = shared;

    if (!consume(TokenKind::KwCreate, "Unexpected token",
                 "Expected `create` to start a function declaration.",
                 "Write `create name:` or `create name(arguments):`.")) {
        synchronize();
        return nullptr;
    }

    Token name;
    if (!take_function_name(name, "create")) {
        synchronize();
        return nullptr;
    }
    function->name = name.lexeme;
    function->name_location = name.location;

    if (match(TokenKind::LParen)) {
        if (!check(TokenKind::RParen)) {
            do {
                if (!check(TokenKind::Identifier)) {
                    error_at(peek(), "Unexpected token",
                             "Expected a parameter name. Found " + token_description(peek()) + ".",
                             "Parameter lists look like `create greet(name, age):`.");
                    synchronize();
                    return nullptr;
                }
                const Token param = advance();
                function->parameters.push_back(param.lexeme);
                function->parameter_locations.push_back(param.location);
                if (match(TokenKind::Colon)) {
                    if (!consume(TokenKind::KwRef, "Unexpected token",
                                 "Parameter type annotations must be `name: ref T`.",
                                 "Write `" + param.lexeme + ": ref integer` or drop the annotation.")) {
                        function->parameter_is_ref.push_back(0);
                        function->parameter_ref_types.emplace_back();
                        synchronize();
                        return nullptr;
                    }
                    function->parameter_is_ref.push_back(1);
                    function->parameter_ref_types.push_back(parse_type_name());
                } else {
                    function->parameter_is_ref.push_back(0);
                    function->parameter_ref_types.emplace_back();
                }
            } while (match(TokenKind::Comma));
        }
        if (!consume(TokenKind::RParen, "Unexpected token",
                     "Expected `)` after the parameter list.",
                     "Close the parameter list before `:`, for example `create greet(name):`.")) {
            synchronize();
            return nullptr;
        }
    }

    function->body = parse_block("function body");
    function->span.end = previous().location;
    return function;
}

std::unique_ptr<FunctionDecl> Parser::parse_foreign() {
    const Token start = peek();
    auto function = std::make_unique<FunctionDecl>();
    function->span.start = start.location;
    function->foreign = true;
    if (!consume(TokenKind::KwForeign, "Unexpected token",
                 "Expected `foreign` to start a C declaration.",
                 "Write `foreign strlen(s: ref string): integer`.")) {
        synchronize();
        return nullptr;
    }
    Token name;
    if (!take_function_name(name, "foreign")) {
        synchronize();
        return nullptr;
    }
    function->name = name.lexeme;
    function->name_location = name.location;
    if (!consume(TokenKind::LParen, "Unexpected token",
                 "Expected `(` after `foreign " + function->name + "`.",
                 "Write `foreign " + function->name + "(...): integer`.")) {
        synchronize();
        return nullptr;
    }
    if (!check(TokenKind::RParen)) {
        do {
            if (!check(TokenKind::Identifier)) {
                error_at(peek(), "Unexpected token",
                         "Expected a parameter name. Found " + token_description(peek()) + ".",
                         "Foreign parameters look like `s: ref string`.");
                synchronize();
                return nullptr;
            }
            const Token param = advance();
            function->parameters.push_back(param.lexeme);
            function->parameter_locations.push_back(param.location);
            if (!consume(TokenKind::Colon, "Unexpected token",
                         "Foreign parameters need a type, for example `" + param.lexeme + ": ref string`.",
                         "Write `" + param.lexeme + ": integer` or `" + param.lexeme + ": ref string`.")) {
                synchronize();
                return nullptr;
            }
            if (match(TokenKind::KwRef)) {
                function->parameter_is_ref.push_back(1);
            } else {
                function->parameter_is_ref.push_back(0);
            }
            function->parameter_ref_types.push_back(parse_type_name());
        } while (match(TokenKind::Comma));
    }
    if (!consume(TokenKind::RParen, "Unexpected token",
                 "Expected `)` after the foreign parameter list.",
                 "Close the parameter list before the return type.")) {
        synchronize();
        return nullptr;
    }
    if (!consume(TokenKind::Colon, "Unexpected token",
                 "Expected `:` and a return type after `foreign " + function->name + "(...)`.",
                 "Write `foreign " + function->name + "(...): integer`.")) {
        synchronize();
        return nullptr;
    }
    function->return_type_name = parse_type_name();
    function->has_return_type = true;
    expect_newline("after this foreign declaration");
    function->span.end = previous().location;
    return function;
}

std::vector<std::unique_ptr<Stmt>> Parser::parse_block(std::string_view what) {
    if (!consume(TokenKind::Colon, "Unexpected token",
                 "Expected `:` to start this " + std::string(what) + ".",
                 "Add `:` at the end of the line, then indent the following statements.")) {
        synchronize();
        return {};
    }

    expect_newline("after `:`");

    if (!match(TokenKind::Indent)) {
        error_at(peek(), "Missing indentation",
                 "Expected an indented " + std::string(what) + " after `:`.",
                 "Indent the next line with spaces to start the block.");
        synchronize();
        return {};
    }

    std::vector<std::unique_ptr<Stmt>> statements;
    while (!check(TokenKind::Dedent) && !is_at_end()) {
        const std::size_t before = current_;
        skip_newlines();
        if (check(TokenKind::Dedent) || is_at_end()) {
            break;
        }
        if (auto statement = parse_statement()) {
            statements.push_back(std::move(statement));
        }
        if (current_ == before) {
            advance();
        }
    }

    if (!consume(TokenKind::Dedent, "Unexpected token",
                 "Expected the " + std::string(what) + " to end by reducing indentation.",
                 "Dedent back to the previous level after the last statement in the block.")) {
        synchronize();
    }

    if (statements.empty()) {
        error_at(previous(), "Empty block",
                 "This " + std::string(what) + " does not contain any statements.",
                 "Add at least one statement inside the indented block.");
    }

    return statements;
}

std::unique_ptr<Stmt> Parser::parse_statement() {
    if (check(TokenKind::KwSay)) {
        return parse_say();
    }
    if (check(TokenKind::KwGive)) {
        return parse_give();
    }
    if (check(TokenKind::KwIf)) {
        return parse_if();
    }
    if (check(TokenKind::KwRepeat)) {
        return parse_repeat();
    }
    if (check(TokenKind::KwOtherwise)) {
        error_at(peek(), "Unexpected token",
                 "`otherwise` must follow an `if` block.",
                 "Write `if condition:` and then `otherwise:` at the same indentation.");
        synchronize();
        return nullptr;
    }
    if (check(TokenKind::KwCreate) || check(TokenKind::KwRecord) || check(TokenKind::KwShare)) {
        error_at(peek(), "Unexpected token",
                 "`" + peek().lexeme + "` cannot start a statement inside a function.",
                 "Move this declaration to the top level.");
        synchronize();
        return nullptr;
    }
    if (check(TokenKind::KwStart) || check(TokenKind::KwWait)) {
        auto stmt = std::make_unique<ExprStmt>();
        stmt->span.start = peek().location;
        stmt->expr = parse_expression();
        expect_newline("after this expression");
        stmt->span.end = previous().location;
        return stmt;
    }
    if (check(TokenKind::Identifier)) {
        return parse_assignment_or_call();
    }

    error_at(peek(), "Unexpected token",
             "Expected a statement. Found " + token_description(peek()) + ".",
             "Statements start with `say`, `give`, `if`, `repeat`, an assignment, or a function call.");
    synchronize();
    return nullptr;
}

std::unique_ptr<Stmt> Parser::parse_assignment_or_call() {
    auto target = parse_postfix();
    if (target == nullptr) {
        synchronize();
        return nullptr;
    }

    if (match(TokenKind::Equal)) {
        if (auto* ident = dynamic_cast<IdentifierExpr*>(target.get())) {
            auto stmt = std::make_unique<AssignStmt>();
            stmt->name = ident->name;
            stmt->name_location = ident->span.start;
            stmt->span.start = ident->span.start;
            stmt->value = parse_expression();
            expect_newline("after this assignment");
            stmt->span.end = previous().location;
            return stmt;
        }
        if (auto* index = dynamic_cast<IndexExpr*>(target.get())) {
            auto stmt = std::make_unique<IndexAssignStmt>();
            stmt->span.start = target->span.start;
            stmt->collection = std::move(index->collection);
            stmt->index = std::move(index->index);
            stmt->value = parse_expression();
            expect_newline("after this indexed assignment");
            stmt->span.end = previous().location;
            return stmt;
        }
        if (auto* field = dynamic_cast<FieldExpr*>(target.get())) {
            auto stmt = std::make_unique<FieldAssignStmt>();
            stmt->span.start = target->span.start;
            stmt->object = std::move(field->object);
            stmt->field = field->field;
            stmt->field_location = field->field_location;
            stmt->value = parse_expression();
            expect_newline("after this field assignment");
            stmt->span.end = previous().location;
            return stmt;
        }
        error_at(previous(), "Unexpected token",
                 "This expression cannot be assigned.",
                 "Assign a name, a list index, or a record field.");
        synchronize();
        return nullptr;
    }

    if (dynamic_cast<CallExpr*>(target.get()) != nullptr) {
        auto stmt = std::make_unique<ExprStmt>();
        stmt->span.start = target->span.start;
        stmt->expr = std::move(target);
        expect_newline("after this function call");
        stmt->span.end = previous().location;
        return stmt;
    }

    error_at(peek(), "Unexpected token",
             "Expected `=` after this expression. Found " + token_description(peek()) + ".",
             "Write an assignment or a function call.");
    synchronize();
    return nullptr;
}

std::unique_ptr<Stmt> Parser::parse_say() {
    const Token start = advance();
    auto stmt = std::make_unique<SayStmt>();
    stmt->span.start = start.location;
    stmt->value = parse_expression();
    expect_newline("after `say`");
    stmt->span.end = previous().location;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_give() {
    const Token start = advance();
    auto stmt = std::make_unique<GiveStmt>();
    stmt->span.start = start.location;
    stmt->value = parse_expression();
    expect_newline("after `give`");
    stmt->span.end = previous().location;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_if() {
    const Token start = advance();
    auto stmt = std::make_unique<IfStmt>();
    stmt->span.start = start.location;
    stmt->condition = parse_expression();
    stmt->then_branch = parse_block("if body");

    skip_newlines();
    if (match(TokenKind::KwOtherwise)) {
        stmt->otherwise_branch = parse_block("otherwise body");
    }

    stmt->span.end = previous().location;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_repeat() {
    const Token start = advance();
    auto stmt = std::make_unique<RepeatStmt>();
    stmt->span.start = start.location;

    if (!check(TokenKind::Identifier)) {
        error_at(peek(), "Unexpected token",
                 "Expected a loop variable after `repeat`. Found " + token_description(peek()) + ".",
                 "Write `repeat i in 1..10:`.");
        synchronize();
        return nullptr;
    }

    const Token iterator = advance();
    stmt->iterator = iterator.lexeme;
    stmt->iterator_location = iterator.location;

    if (!consume(TokenKind::KwIn, "Unexpected token",
                 "Expected `in` after the loop variable.",
                 "Write `repeat i in 1..10:`.")) {
        synchronize();
        return nullptr;
    }

    stmt->start = parse_expression();
    if (!consume(TokenKind::DotDot, "Unexpected token",
                 "Expected `..` between the start and end of the range.",
                 "Write a range such as `1..10`.")) {
        synchronize();
        return nullptr;
    }
    stmt->end = parse_expression();
    stmt->body = parse_block("repeat body");
    stmt->span.end = previous().location;
    return stmt;
}

std::unique_ptr<Expr> Parser::parse_expression() {
    return parse_equality();
}

std::unique_ptr<Expr> Parser::parse_equality() {
    auto expr = parse_comparison();
    while (check(TokenKind::EqualEqual) || check(TokenKind::BangEqual)) {
        const Token op = advance();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = op.kind;
        binary->span.start = expr != nullptr ? expr->span.start : op.location;
        binary->left = std::move(expr);
        binary->right = parse_comparison();
        binary->span.end = binary->right != nullptr ? binary->right->span.end : op.location;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_comparison() {
    auto expr = parse_term();
    while (check(TokenKind::Less) || check(TokenKind::Greater) || check(TokenKind::LessEqual) ||
           check(TokenKind::GreaterEqual)) {
        const Token op = advance();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = op.kind;
        binary->span.start = expr != nullptr ? expr->span.start : op.location;
        binary->left = std::move(expr);
        binary->right = parse_term();
        binary->span.end = binary->right != nullptr ? binary->right->span.end : op.location;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_term() {
    auto expr = parse_factor();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        const Token op = advance();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = op.kind;
        binary->span.start = expr != nullptr ? expr->span.start : op.location;
        binary->left = std::move(expr);
        binary->right = parse_factor();
        binary->span.end = binary->right != nullptr ? binary->right->span.end : op.location;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_factor() {
    auto expr = parse_unary();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        const Token op = advance();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = op.kind;
        binary->span.start = expr != nullptr ? expr->span.start : op.location;
        binary->left = std::move(expr);
        binary->right = parse_unary();
        binary->span.end = binary->right != nullptr ? binary->right->span.end : op.location;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (check(TokenKind::Minus)) {
        const Token op = advance();
        auto expr = std::make_unique<UnaryExpr>();
        expr->op = op.kind;
        expr->span.start = op.location;
        expr->operand = parse_unary();
        expr->span.end = expr->operand != nullptr ? expr->operand->span.end : op.location;
        return expr;
    }
    if (check(TokenKind::KwWait)) {
        return parse_wait();
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expr = parse_primary();
    while (true) {
        if (match(TokenKind::LBracket)) {
            auto index = std::make_unique<IndexExpr>();
            index->span.start = expr != nullptr ? expr->span.start : previous().location;
            index->collection = std::move(expr);
            index->index = parse_expression();
            consume(TokenKind::RBracket, "Unexpected token", "Expected `]` after this index.",
                    "Close the index, for example `items[i]`.");
            index->span.end = previous().location;
            expr = std::move(index);
            continue;
        }
        if (match(TokenKind::Dot)) {
            const Token dot = previous();
            if (!check(TokenKind::Identifier) && !check(TokenKind::KwOk)) {
                error_at(peek(), "Unexpected token",
                         "Expected a field name after `.`. Found " + token_description(peek()) + ".",
                         "Write `point.x` or `r.ok`.");
                return expr;
            }
            const Token field = advance();
            auto access = std::make_unique<FieldExpr>();
            access->span.start = expr != nullptr ? expr->span.start : dot.location;
            access->object = std::move(expr);
            access->field = field.lexeme;
            access->field_location = field.location;
            access->span.end = field.location;
            expr = std::move(access);
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    const Token token = peek();

    if (match(TokenKind::Integer)) {
        auto expr = std::make_unique<IntegerExpr>();
        expr->span.start = token.location;
        expr->span.end = token.location;
        const auto* begin = token.lexeme.data();
        const auto* end = begin + token.lexeme.size();
        std::int64_t value = 0;
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            error_at(token, "Invalid number",
                     "Could not parse `" + token.lexeme + "` as an integer.",
                     "Use a decimal integer that fits in 64 bits.");
        }
        expr->value = value;
        return expr;
    }

    if (match(TokenKind::Float)) {
        auto expr = std::make_unique<FloatExpr>();
        expr->span.start = token.location;
        expr->span.end = token.location;
        try {
            expr->value = std::stod(token.lexeme);
        } catch (const std::exception&) {
            error_at(token, "Invalid number",
                     "Could not parse `" + token.lexeme + "` as a floating-point number.",
                     "Use a decimal number such as `3.14`.");
        }
        return expr;
    }

    if (match(TokenKind::String)) {
        auto expr = std::make_unique<StringExpr>();
        expr->span.start = token.location;
        expr->span.end = token.location;
        expr->value = token.lexeme;
        return expr;
    }

    if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
        auto expr = std::make_unique<BoolExpr>();
        expr->span.start = token.location;
        expr->span.end = token.location;
        expr->value = token.kind == TokenKind::KwTrue;
        return expr;
    }

    if (check(TokenKind::KwStart)) {
        return parse_start();
    }

    if (check(TokenKind::KwOk) || check(TokenKind::KwFail)) {
        const Token name = advance();
        if (!check(TokenKind::LParen)) {
            error_at(peek(), "Unexpected token",
                     "Expected `(` after `" + name.lexeme + "`.",
                     "Write `" + name.lexeme + "(...)`.");
            auto expr = std::make_unique<IdentifierExpr>();
            expr->span.start = name.location;
            expr->span.end = name.location;
            expr->name = name.lexeme;
            return expr;
        }
        return parse_call_from_name(name);
    }

    if (check(TokenKind::Identifier)) {
        const Token name = advance();
        if (check(TokenKind::LParen)) {
            return parse_call_from_name(name);
        }
        if (check(TokenKind::Dot) && current_ + 2 < tokens_.size() &&
            tokens_[current_ + 2].kind == TokenKind::LParen) {
            const TokenKind name_kind = tokens_[current_ + 1].kind;
            const bool name_ok =
                name_kind == TokenKind::Identifier || name_kind == TokenKind::KwRepeat || is_keyword(name_kind);
            if (name_ok) {
                const Token dot = advance();
                Token callee;
                if (take_function_name(callee, ".")) {
                    return parse_call_from_tokens(name, callee, dot.location);
                }
            }
        }
        auto expr = std::make_unique<IdentifierExpr>();
        expr->span.start = name.location;
        expr->span.end = name.location;
        expr->name = name.lexeme;
        return expr;
    }

    if (match(TokenKind::LParen)) {
        auto expr = parse_expression();
        consume(TokenKind::RParen, "Unexpected token",
                "Expected `)` to close this grouped expression.",
                "Add the missing `)`.");
        return expr;
    }

    if (match(TokenKind::LBracket)) {
        auto expr = std::make_unique<ListExpr>();
        expr->span.start = token.location;
        if (!check(TokenKind::RBracket)) {
            do {
                expr->elements.push_back(parse_expression());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBracket, "Unexpected token",
                "Expected `]` to close this collection.",
                "Close the list, for example `[1, 2, 3]`.");
        expr->span.end = previous().location;
        return expr;
    }

    error_at(token, "Unexpected token",
             "Expected an expression. Found " + token_description(token) + ".",
             "Use a number, string, boolean, identifier, function call, or list.");
    if (!is_at_end() && peek().kind != TokenKind::Newline && peek().kind != TokenKind::Dedent &&
        !is_binary_operator(peek().kind)) {
        advance();
    }
    return nullptr;
}

std::unique_ptr<Expr> Parser::parse_start() {
    const Token keyword = advance();
    auto expr = std::make_unique<StartExpr>();
    expr->span.start = keyword.location;
    expr->span.end = keyword.location;

    if (!check(TokenKind::Identifier) && !check(TokenKind::KwOk) && !check(TokenKind::KwFail)) {
        error_at(peek(), "Unexpected token",
                 "Expected a function name after `start`. Found " + token_description(peek()) + ".",
                 "Write `start name(args)` or `start alias.name(args)`.");
        return expr;
    }

    const Token first = advance();
    if (check(TokenKind::Dot)) {
        const Token dot = advance();
        Token callee;
        if (!take_function_name(callee, ".")) {
            return expr;
        }
        expr->qualifier = first.lexeme;
        expr->qualifier_location = first.location;
        expr->dot_location = dot.location;
        expr->callee = std::move(callee.lexeme);
        expr->callee_location = callee.location;
    } else {
        expr->callee = first.lexeme;
        expr->callee_location = first.location;
    }

    if (!check(TokenKind::LParen)) {
        error_at(peek(), "Unexpected token",
                 "Expected `(` after `start " +
                     (expr->qualifier.empty() ? expr->callee : expr->qualifier + "." + expr->callee) + "`.",
                 "Write `start " +
                     (expr->qualifier.empty() ? expr->callee : expr->qualifier + "." + expr->callee) + "(...)`.");
        return expr;
    }

    expr->arguments = parse_argument_list();
    expr->span.end = previous().location;
    return expr;
}

std::unique_ptr<Expr> Parser::parse_wait() {
    const Token keyword = advance();
    auto expr = std::make_unique<WaitExpr>();
    expr->span.start = keyword.location;
    expr->task = parse_unary();
    if (expr->task == nullptr) {
        error_at(peek(), "Unexpected token",
                 "Expected an expression after `wait`.",
                 "Write `wait t`.");
        expr->span.end = keyword.location;
        return expr;
    }
    expr->span.end = expr->task->span.end;
    return expr;
}

std::unique_ptr<Expr> Parser::parse_call_from_name(Token name) {
    return parse_call_from_tokens(Token{}, std::move(name), SourceLocation{});
}

std::unique_ptr<Expr> Parser::parse_call_from_tokens(Token qualifier, Token name, SourceLocation dot) {
    auto expr = std::make_unique<CallExpr>();
    expr->span.start = qualifier.lexeme.empty() ? name.location : qualifier.location;
    if (!qualifier.lexeme.empty()) {
        expr->qualifier = std::move(qualifier.lexeme);
        expr->qualifier_location = qualifier.location;
        expr->dot_location = dot;
    }
    expr->callee = std::move(name.lexeme);
    expr->callee_location = name.location;
    expr->arguments = parse_argument_list();
    expr->span.end = previous().location;
    return expr;
}

std::vector<std::unique_ptr<Expr>> Parser::parse_argument_list() {
    std::vector<std::unique_ptr<Expr>> arguments;
    if (!consume(TokenKind::LParen, "Unexpected token",
                 "Expected `(` to start a function argument list.",
                 "Write a call such as `greet(name)`.")) {
        return arguments;
    }

    if (!check(TokenKind::RParen)) {
        do {
            arguments.push_back(parse_expression());
        } while (match(TokenKind::Comma));
    }

    consume(TokenKind::RParen, "Unexpected token",
            "Expected `)` after the function arguments.",
            "Close the argument list.");
    return arguments;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::check_name() const {
    return check(TokenKind::Identifier) || check(TokenKind::KwRepeat);
}

bool Parser::take_function_name(Token& out, std::string_view after) {
    if (check_name()) {
        out = advance();
        return true;
    }
    if (is_keyword(peek().kind)) {
        error_at(peek(), "Reserved word",
                 "`" + peek().lexeme + "` is reserved and cannot be a function name.",
                 "Rename it. The only reserved function name is `repeat`, used by `str.repeat`.");
        return false;
    }
    error_at(peek(), "Unexpected token",
             "Expected a function name after `" + std::string(after) + "`. Found " + token_description(peek()) + ".",
             after == "." ? "Write a qualified call such as `math.add(1, 2)`."
                          : "Use an identifier, for example `create greet(name):`.");
    return false;
}

bool Parser::check(TokenKind kind) const {
    return peek().kind == kind;
}

bool Parser::check_next(TokenKind kind) const {
    return peek_next().kind == kind;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::peek_next() const {
    if (current_ + 1 >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[current_ + 1];
}

const Token& Parser::previous() const {
    if (current_ == 0) {
        return tokens_.front();
    }
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        ++current_;
    }
    return previous();
}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::EndOfFile;
}

bool Parser::consume(TokenKind kind, std::string title, std::string detail, std::string suggestion) {
    if (match(kind)) {
        return true;
    }
    error_at(peek(), std::move(title), std::move(detail), std::move(suggestion));
    return false;
}

void Parser::expect_newline(std::string_view after) {
    if (match(TokenKind::Newline) || match(TokenKind::Dedent) || is_at_end()) {
        if (previous().kind == TokenKind::Dedent) {
            --current_;
        }
        return;
    }
    error_at(peek(), "Unexpected token",
             "Expected a newline " + std::string(after) + ". Found " + token_description(peek()) + ".",
             "Put this statement on its own line.");
    synchronize();
}

void Parser::skip_newlines() {
    while (match(TokenKind::Newline)) {
    }
}

void Parser::synchronize() {
    if (!is_at_end()) {
        advance();
    }

    while (!is_at_end()) {
        if (previous().kind == TokenKind::Newline) {
            return;
        }
        if (peek().kind == TokenKind::Dedent || peek().kind == TokenKind::KwCreate ||
            peek().kind == TokenKind::KwShare || peek().kind == TokenKind::KwUse ||
            peek().kind == TokenKind::KwRecord || peek().kind == TokenKind::KwSay || peek().kind == TokenKind::KwIf ||
            peek().kind == TokenKind::KwRepeat || peek().kind == TokenKind::KwGive) {
            return;
        }
        advance();
    }
}

void Parser::error_at(const Token& token, std::string title, std::string detail, std::string suggestion) {
    diagnostics_.error(token.location, std::move(title), std::move(detail), std::move(suggestion));
}

std::string Parser::token_description(const Token& token) const {
    switch (token.kind) {
        case TokenKind::EndOfFile:
            return "end of file";
        case TokenKind::Newline:
            return "a newline";
        case TokenKind::Indent:
            return "an indented block";
        case TokenKind::Dedent:
            return "the end of an indented block";
        case TokenKind::Identifier:
            return "`" + token.lexeme + "`";
        case TokenKind::Integer:
        case TokenKind::Float:
        case TokenKind::String:
            return "`" + token.lexeme + "`";
        default:
            if (is_keyword(token.kind) || !token.lexeme.empty()) {
                return "`" + token.lexeme + "`";
            }
            return "`" + std::string(token_kind_spelling(token.kind)) + "`";
    }
}

}  // namespace yukt

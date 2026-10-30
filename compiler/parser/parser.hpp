#pragma once

#include "ast/ast.hpp"
#include "diagnostics/diagnostics.hpp"
#include "lexer/token.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yukt {

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics);

    std::unique_ptr<Program> parse_program();

private:
    UseDecl parse_use();
    std::unique_ptr<RecordDecl> parse_record(bool shared);
    TypeName parse_type_name();
    std::unique_ptr<FunctionDecl> parse_function(bool shared);
    std::unique_ptr<Expr> parse_call_from_tokens(Token qualifier, Token name, SourceLocation dot);
    std::vector<std::unique_ptr<Stmt>> parse_block(std::string_view what);
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_assignment_or_call();
    std::unique_ptr<Stmt> parse_say();
    std::unique_ptr<Stmt> parse_give();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_repeat();

    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_equality();
    std::unique_ptr<Expr> parse_comparison();
    std::unique_ptr<Expr> parse_term();
    std::unique_ptr<Expr> parse_factor();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_call_from_name(Token name);
    std::vector<std::unique_ptr<Expr>> parse_argument_list();

    bool match(TokenKind kind);
    bool check(TokenKind kind) const;
    bool check_name() const;
    bool take_function_name(Token& out, std::string_view after);
    bool check_next(TokenKind kind) const;
    const Token& peek() const;
    const Token& peek_next() const;
    const Token& previous() const;
    const Token& advance();
    bool is_at_end() const;

    bool consume(TokenKind kind, std::string title, std::string detail, std::string suggestion);
    void expect_newline(std::string_view after);
    void skip_newlines();
    void synchronize();
    void error_at(const Token& token, std::string title, std::string detail, std::string suggestion);
    std::string token_description(const Token& token) const;

    std::vector<Token> tokens_;
    DiagnosticEngine& diagnostics_;
    std::size_t current_ = 0;
};

}  // namespace yukt

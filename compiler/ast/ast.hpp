#pragma once

#include "common/source.hpp"
#include "lexer/token.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace yukt {

struct Expr {
    SourceSpan span;
    virtual ~Expr() = default;
    virtual void print(std::ostream& out, int indent) const = 0;
};

struct Stmt {
    SourceSpan span;
    virtual ~Stmt() = default;
    virtual void print(std::ostream& out, int indent) const = 0;
};

struct IntegerExpr final : Expr {
    std::int64_t value = 0;
    void print(std::ostream& out, int indent) const override;
};

struct FloatExpr final : Expr {
    double value = 0.0;
    void print(std::ostream& out, int indent) const override;
};

struct StringExpr final : Expr {
    std::string value;
    void print(std::ostream& out, int indent) const override;
};

struct BoolExpr final : Expr {
    bool value = false;
    void print(std::ostream& out, int indent) const override;
};

struct IdentifierExpr final : Expr {
    std::string name;
    void print(std::ostream& out, int indent) const override;
};

struct UnaryExpr final : Expr {
    TokenKind op = TokenKind::Minus;
    std::unique_ptr<Expr> operand;
    void print(std::ostream& out, int indent) const override;
};

struct BinaryExpr final : Expr {
    TokenKind op = TokenKind::Plus;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    void print(std::ostream& out, int indent) const override;
};

struct CallExpr final : Expr {
    std::string callee;
    SourceLocation callee_location;
    std::vector<std::unique_ptr<Expr>> arguments;
    void print(std::ostream& out, int indent) const override;
};

struct ListExpr final : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    void print(std::ostream& out, int indent) const override;
};

struct IndexExpr final : Expr {
    std::unique_ptr<Expr> collection;
    std::unique_ptr<Expr> index;
    void print(std::ostream& out, int indent) const override;
};

struct AssignStmt final : Stmt {
    std::string name;
    SourceLocation name_location;
    std::unique_ptr<Expr> value;
    void print(std::ostream& out, int indent) const override;
};

struct IndexAssignStmt final : Stmt {
    std::unique_ptr<Expr> collection;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
    void print(std::ostream& out, int indent) const override;
};

struct SayStmt final : Stmt {
    std::unique_ptr<Expr> value;
    void print(std::ostream& out, int indent) const override;
};

struct GiveStmt final : Stmt {
    std::unique_ptr<Expr> value;
    void print(std::ostream& out, int indent) const override;
};

struct ExprStmt final : Stmt {
    std::unique_ptr<Expr> expr;
    void print(std::ostream& out, int indent) const override;
};

struct IfStmt final : Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::unique_ptr<Stmt>> otherwise_branch;
    void print(std::ostream& out, int indent) const override;
};

struct RepeatStmt final : Stmt {
    std::string iterator;
    SourceLocation iterator_location;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    std::vector<std::unique_ptr<Stmt>> body;
    void print(std::ostream& out, int indent) const override;
};

struct FunctionDecl {
    SourceSpan span;
    std::string name;
    SourceLocation name_location;
    std::vector<std::string> parameters;
    std::vector<std::unique_ptr<Stmt>> body;
    void print(std::ostream& out, int indent) const;
};

struct Program {
    SourceSpan span;
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    void print(std::ostream& out, int indent = 0) const;
};

void print_ast(const Program& program, std::ostream& out);

}  // namespace yukt

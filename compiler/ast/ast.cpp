#include "ast/ast.hpp"

#include <ostream>

namespace yukt {
namespace {

void pad(std::ostream& out, int indent) {
    for (int i = 0; i < indent; ++i) {
        out << "  ";
    }
}

void print_child(std::ostream& out, const Expr* expr, int indent) {
    if (expr == nullptr) {
        pad(out, indent);
        out << "<missing expression>\n";
        return;
    }
    expr->print(out, indent);
}

void print_child(std::ostream& out, const Stmt* stmt, int indent) {
    if (stmt == nullptr) {
        pad(out, indent);
        out << "<missing statement>\n";
        return;
    }
    stmt->print(out, indent);
}

std::string quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    out += '"';
    return out;
}

}  // namespace

void IntegerExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Integer " << value << '\n';
}

void FloatExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Float " << value << '\n';
}

void StringExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "String " << quote(value) << '\n';
}

void BoolExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Boolean " << (value ? "true" : "false") << '\n';
}

void IdentifierExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Identifier " << name << '\n';
}

void UnaryExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Unary " << token_kind_spelling(op) << '\n';
    print_child(out, operand.get(), indent + 1);
}

void BinaryExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Binary " << token_kind_spelling(op) << '\n';
    print_child(out, left.get(), indent + 1);
    print_child(out, right.get(), indent + 1);
}

void CallExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Call " << callee << '\n';
    for (const auto& argument : arguments) {
        print_child(out, argument.get(), indent + 1);
    }
}

void ListExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "List\n";
    for (const auto& element : elements) {
        print_child(out, element.get(), indent + 1);
    }
}

void IndexExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Index\n";
    print_child(out, collection.get(), indent + 1);
    print_child(out, index.get(), indent + 1);
}

void AssignStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Assign " << name << '\n';
    print_child(out, value.get(), indent + 1);
}

void IndexAssignStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "IndexAssign\n";
    print_child(out, collection.get(), indent + 1);
    print_child(out, index.get(), indent + 1);
    print_child(out, value.get(), indent + 1);
}

void SayStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Say\n";
    print_child(out, value.get(), indent + 1);
}

void GiveStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Give\n";
    print_child(out, value.get(), indent + 1);
}

void ExprStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "ExprStmt\n";
    print_child(out, expr.get(), indent + 1);
}

void IfStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "If\n";
    print_child(out, condition.get(), indent + 1);
    pad(out, indent + 1);
    out << "Then\n";
    for (const auto& stmt : then_branch) {
        print_child(out, stmt.get(), indent + 2);
    }
    if (!otherwise_branch.empty()) {
        pad(out, indent + 1);
        out << "Otherwise\n";
        for (const auto& stmt : otherwise_branch) {
            print_child(out, stmt.get(), indent + 2);
        }
    }
}

void RepeatStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Repeat " << iterator << '\n';
    pad(out, indent + 1);
    out << "From\n";
    print_child(out, start.get(), indent + 2);
    pad(out, indent + 1);
    out << "To\n";
    print_child(out, end.get(), indent + 2);
    pad(out, indent + 1);
    out << "Body\n";
    for (const auto& stmt : body) {
        print_child(out, stmt.get(), indent + 2);
    }
}

void FunctionDecl::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Function " << name << '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << parameters[i];
    }
    out << ")\n";
    for (const auto& stmt : body) {
        print_child(out, stmt.get(), indent + 1);
    }
}

void Program::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Program\n";
    for (const auto& function : functions) {
        if (function == nullptr) {
            pad(out, indent + 1);
            out << "<missing function>\n";
            continue;
        }
        function->print(out, indent + 1);
    }
}

void print_ast(const Program& program, std::ostream& out) {
    program.print(out, 0);
}

}  // namespace yukt

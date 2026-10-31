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
    out << "Call ";
    if (!qualifier.empty()) {
        out << qualifier << '.';
    }
    out << callee << '\n';
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

void FieldExpr::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Field " << field << '\n';
    print_child(out, object.get(), indent + 1);
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

void FieldAssignStmt::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "FieldAssign " << field << '\n';
    print_child(out, object.get(), indent + 1);
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

void UseDecl::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Use";
    for (const auto& part : path) {
        out << ' ' << part;
    }
    if (!alias.empty() && (path.empty() || alias != path.back())) {
        out << " as " << alias;
    }
    out << '\n';
}

void RecordDecl::print(std::ostream& out, int indent) const {
    pad(out, indent);
    if (shared) {
        out << "Share ";
    }
    out << "Record " << name << '\n';
    for (const auto& field : fields) {
        pad(out, indent + 1);
        out << "Field " << field.name << ": ";
        if (!field.type.qualifier.empty()) {
            out << field.type.qualifier << '.';
        }
        out << field.type.name << '\n';
    }
}

void FunctionDecl::print(std::ostream& out, int indent) const {
    pad(out, indent);
    if (shared) {
        out << "Share ";
    }
    if (foreign) {
        out << "Foreign ";
    }
    out << "Function " << name << '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << parameters[i];
        if (i < parameter_is_ref.size() && parameter_is_ref[i]) {
            out << ": ref ";
        } else if (foreign && i < parameter_ref_types.size() && !parameter_ref_types[i].name.empty()) {
            out << ": ";
        }
        if ((i < parameter_is_ref.size() && parameter_is_ref[i]) ||
            (foreign && i < parameter_ref_types.size() && !parameter_ref_types[i].name.empty())) {
            if (i < parameter_ref_types.size() && !parameter_ref_types[i].qualifier.empty()) {
                out << parameter_ref_types[i].qualifier << '.';
            }
            if (i < parameter_ref_types.size()) {
                out << parameter_ref_types[i].name;
            }
        }
    }
    out << ")";
    if (has_return_type && !return_type_name.name.empty()) {
        out << ": ";
        if (!return_type_name.qualifier.empty()) {
            out << return_type_name.qualifier << '.';
        }
        out << return_type_name.name;
    }
    out << '\n';
    for (const auto& stmt : body) {
        print_child(out, stmt.get(), indent + 1);
    }
}

void Program::print(std::ostream& out, int indent) const {
    pad(out, indent);
    out << "Program\n";
    for (const auto& use : uses) {
        use.print(out, indent + 1);
    }
    for (const auto& record : records) {
        if (record == nullptr) {
            pad(out, indent + 1);
            out << "<missing record>\n";
            continue;
        }
        record->print(out, indent + 1);
    }
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

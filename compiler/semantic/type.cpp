#include "semantic/type.hpp"

#include <utility>

namespace yukt {

Type integer_type() {
    return Type{.kind = TypeKind::Integer};
}

Type float_type() {
    return Type{.kind = TypeKind::Float};
}

Type boolean_type() {
    return Type{.kind = TypeKind::Boolean};
}

Type string_type() {
    return Type{.kind = TypeKind::String};
}

Type void_type() {
    return Type{.kind = TypeKind::Void};
}

Type list_type(Type element) {
    Type type;
    type.kind = TypeKind::List;
    type.element = std::make_shared<Type>(std::move(element));
    return type;
}

Type record_type(int record_id, std::string record_name) {
    Type type;
    type.kind = TypeKind::Record;
    type.record_id = record_id;
    type.record_name = std::move(record_name);
    return type;
}

Type TypeContext::fresh_variable() {
    Type type;
    type.kind = TypeKind::Variable;
    type.variable = static_cast<int>(variables_.size());
    variables_.push_back(Slot{});
    return type;
}

Type TypeContext::prune(Type type) const {
    if (type.kind == TypeKind::Variable) {
        if (type.variable < 0 || static_cast<std::size_t>(type.variable) >= variables_.size()) {
            return type;
        }
        const auto& bound = variables_[static_cast<std::size_t>(type.variable)].bound;
        if (bound.has_value()) {
            return prune(*bound);
        }
        return type;
    }
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return list_type(prune(*type.element));
    }
    return type;
}

bool TypeContext::unify(Type left, Type right) {
    left = prune(left);
    right = prune(right);

    if (left.kind == TypeKind::Variable) {
        if (right.kind == TypeKind::Variable && left.variable == right.variable) {
            return true;
        }
        if (occurs(left.variable, right)) {
            return false;
        }
        variables_[static_cast<std::size_t>(left.variable)].bound = right;
        return true;
    }

    if (right.kind == TypeKind::Variable) {
        return unify(right, left);
    }

    if (left.kind != right.kind) {
        return false;
    }

    if (left.kind == TypeKind::List) {
        if (left.element == nullptr || right.element == nullptr) {
            return false;
        }
        return unify(*left.element, *right.element);
    }

    if (left.kind == TypeKind::Record) {
        return left.record_id == right.record_id;
    }

    return true;
}

bool TypeContext::is_resolved(Type type) const {
    type = prune(type);
    if (type.kind == TypeKind::Variable) {
        return false;
    }
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return is_resolved(*type.element);
    }
    return true;
}

bool TypeContext::same(Type left, Type right) const {
    left = prune(left);
    right = prune(right);
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == TypeKind::Variable) {
        return left.variable == right.variable;
    }
    if (left.kind == TypeKind::List) {
        if (left.element == nullptr || right.element == nullptr) {
            return left.element == right.element;
        }
        return same(*left.element, *right.element);
    }
    if (left.kind == TypeKind::Record) {
        return left.record_id == right.record_id;
    }
    return true;
}

std::string TypeContext::to_string(Type type) const {
    type = prune(type);
    switch (type.kind) {
        case TypeKind::Integer:
            return "integer";
        case TypeKind::Float:
            return "float";
        case TypeKind::Boolean:
            return "boolean";
        case TypeKind::String:
            return "string";
        case TypeKind::Void:
            return "void";
        case TypeKind::List:
            return "list[" + (type.element != nullptr ? to_string(*type.element) : "?") + "]";
        case TypeKind::Record:
            return type.record_name.empty() ? "record" : type.record_name;
        case TypeKind::Variable:
            return "?";
    }
    return "unknown";
}

bool TypeContext::occurs(int variable, const Type& type) const {
    const Type pruned = prune(type);
    if (pruned.kind == TypeKind::Variable) {
        return pruned.variable == variable;
    }
    if (pruned.kind == TypeKind::List && pruned.element != nullptr) {
        return occurs(variable, *pruned.element);
    }
    return false;
}

}  // namespace yukt

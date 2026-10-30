#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yukt {

enum class TypeKind {
    Integer,
    Float,
    Boolean,
    String,
    Void,
    List,
    Record,
    Variable,
};

struct Type {
    TypeKind kind = TypeKind::Void;
    int variable = -1;
    std::shared_ptr<Type> element;
    int record_id = -1;
    std::string record_name;
};

Type integer_type();
Type float_type();
Type boolean_type();
Type string_type();
Type void_type();
Type list_type(Type element);
Type record_type(int record_id, std::string record_name);

class TypeContext {
public:
    Type fresh_variable();
    Type prune(Type type) const;
    bool unify(Type left, Type right);
    bool is_resolved(Type type) const;
    bool same(Type left, Type right) const;
    std::string to_string(Type type) const;

private:
    bool occurs(int variable, const Type& type) const;

    struct Slot {
        std::optional<Type> bound;
    };

    std::vector<Slot> variables_;
};

}  // namespace yukt

#include "semantic/ownership.hpp"

namespace yukt {

OwnState join_own(OwnState a, OwnState b) {
    if (a == OwnState::Moved || b == OwnState::Moved) {
        return OwnState::Moved;
    }
    if (a == OwnState::Owned || b == OwnState::Owned) {
        return OwnState::Owned;
    }
    return OwnState::Uninit;
}

bool type_owns_heap(const TypeContext& types, Type type) {
    type = types.prune(type);
    switch (type.kind) {
        case TypeKind::String:
        case TypeKind::List:
            return true;
        case TypeKind::Record:
            // Task 3: records cannot have heap fields yet, so they are plain
            // values. Task 5 will recurse through RecordInfo fields here.
            return false;
        default:
            return false;
    }
}

}  // namespace yukt

#include "semantic/ownership.hpp"

#include "semantic/analyzer.hpp"

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

bool type_owns_heap(const TypeContext& types, Type type, const std::vector<RecordInfo>* records) {
    type = types.prune(type);
    switch (type.kind) {
        case TypeKind::String:
        case TypeKind::List:
            return true;
        case TypeKind::Record: {
            if (records == nullptr || type.record_id < 0 ||
                static_cast<std::size_t>(type.record_id) >= records->size()) {
                return false;
            }
            for (const auto& field : (*records)[static_cast<std::size_t>(type.record_id)].fields) {
                if (type_owns_heap(types, field.type, records)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

}  // namespace yukt

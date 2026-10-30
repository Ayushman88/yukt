#pragma once

#include "common/source.hpp"
#include "semantic/type.hpp"

#include <vector>

namespace yukt {

struct RecordInfo;

// Ownership state of a single heap-typed name (0.13.2).
//
// Only names whose type owns a heap allocation (string, list, or a record that
// recursively contains one) are tracked. Plain values (integer, float, boolean,
// and 0.13.1-style records of only those) are never tracked and never dropped.
enum class OwnState {
    Uninit,  // declared, holds no allocation yet
    Owned,   // owns exactly one live allocation
    Moved,   // allocation was transferred away; the name is dead
};

struct OwnName {
    OwnState state = OwnState::Uninit;
    SourceLocation location;  // where the name was last made Owned
};

// Join two ownership states at a control-flow merge (if / otherwise).
//
// If any path leaves the name Moved, the join is Moved; else if any path leaves
// it Owned, the join is Owned; else Uninit. A Moved name after the merge is a
// use after move.
OwnState join_own(OwnState a, OwnState b);

// True for heap types: string, list, and a record whose fields recursively
// contain a heap type. Plain records (integer/float/boolean only, including
// nested plain records) return false and stay out of the ownership machine.
//
// Pass the analyzer's record table so Record fields can be walked. Without it,
// records are treated as plain values.
bool type_owns_heap(const TypeContext& types, Type type, const std::vector<RecordInfo>* records = nullptr);

}  // namespace yukt

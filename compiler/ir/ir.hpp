#pragma once

#include "common/source.hpp"
#include "semantic/type.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yukt {

enum class IrTypeKind {
    I64,
    F64,
    Bool,
    String,
    Void,
    List,
    Record,
};

struct IrType {
    IrTypeKind kind = IrTypeKind::Void;
    std::shared_ptr<IrType> element;
    int record_id = -1;
};

struct IrRecordField {
    std::string name;
    IrType type;
    std::int64_t offset = 0;
    std::int64_t size = 0;
    std::int64_t align = 1;
};

struct IrRecord {
    int id = -1;
    std::string name;
    std::string identity;
    std::int64_t size = 0;
    std::int64_t align = 1;
    std::vector<IrRecordField> fields;
};

bool operator==(const IrType& left, const IrType& right);
bool operator!=(const IrType& left, const IrType& right);

IrType ir_i64();
IrType ir_f64();
IrType ir_bool();
IrType ir_string();
IrType ir_void();
IrType ir_list(IrType element);
IrType ir_record(int record_id);
IrType ir_type_from(const Type& type);
std::string ir_type_name(const IrType& type);
int list_elem_kind(const IrType& type);

enum class IrBinOp {
    Add,
    Sub,
    Mul,
    Div,
    Rem,
    Concat,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
};

enum class IrUnOp { Neg };

struct IrValue {
    enum class Kind {
        Temp,
        Local,
        ConstI64,
        ConstF64,
        ConstBool,
        ConstString,
        Undef,
    };

    Kind kind = Kind::Undef;
    IrType type;
    int id = -1;
    std::string name;
    std::int64_t i64 = 0;
    double f64 = 0.0;
    bool boolean = false;
    std::string text;
};

enum class IrOp {
    Store,
    Load,
    Bin,
    Un,
    Call,
    List,
    ListSet,
    ListGet,
    ListLen,
    Zeros,
    ParallelFor,
    Say,
    RecordNew,
    FieldGet,
    FieldSet,
    Drop,
    Addr,
};

struct IrInst {
    IrOp op = IrOp::Say;
    IrBinOp bin = IrBinOp::Add;
    IrUnOp un = IrUnOp::Neg;
    IrValue dest;
    std::string name;
    std::vector<IrValue> args;
    std::int64_t work = 1;
    int field_index = -1;
    std::string reduce_local;
    IrBinOp reduce_bin = IrBinOp::Add;
};

enum class IrTermKind {
    Return,
    Jump,
    Branch,
};

struct IrTerm {
    IrTermKind kind = IrTermKind::Return;
    IrValue value;
    std::string target;
    std::string otherwise;
    std::string loop_iterator;
};

struct LoopDecision {
    std::string function;
    std::string iterator;
    SourceLocation location;
    bool parallel = false;
    bool independent = false;
    int nest_depth = 0;
    std::int64_t estimated_trip = -1;
    std::int64_t estimated_work = -1;
    std::int64_t work_per_iter = 1;
    std::string reason;
    std::string reduction;
    std::string reduction_op;
};

struct IrBlock {
    std::string name;
    std::vector<IrInst> insts;
    IrTerm term;
    bool terminated = false;
};

struct IrFunction {
    std::string name;
    IrType return_type;
    std::vector<std::pair<std::string, IrType>> params;
    std::vector<char> param_is_ref;
    std::vector<std::pair<std::string, IrType>> locals;
    std::vector<IrBlock> blocks;
};

struct IrModule {
    std::string source_path;
    std::vector<IrFunction> functions;
    std::vector<IrRecord> records;
    std::vector<LoopDecision> decisions;
};

void print_ir(const IrModule& module, std::ostream& out);

}  // namespace yukt

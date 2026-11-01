#include "ir/ir.hpp"

#include <ostream>

namespace yukt {

bool operator==(const IrType& left, const IrType& right) {
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == IrTypeKind::Record) {
        return left.record_id == right.record_id;
    }
    if (left.kind != IrTypeKind::List && left.kind != IrTypeKind::Map && left.kind != IrTypeKind::Task &&
        left.kind != IrTypeKind::Send && left.kind != IrTypeKind::Recv) {
        return true;
    }
    if (left.element == nullptr || right.element == nullptr) {
        return left.element == right.element;
    }
    return *left.element == *right.element;
}

bool operator!=(const IrType& left, const IrType& right) {
    return !(left == right);
}

IrType ir_i64() {
    IrType type;
    type.kind = IrTypeKind::I64;
    return type;
}

IrType ir_f64() {
    IrType type;
    type.kind = IrTypeKind::F64;
    return type;
}

IrType ir_bool() {
    IrType type;
    type.kind = IrTypeKind::Bool;
    return type;
}

IrType ir_string() {
    IrType type;
    type.kind = IrTypeKind::String;
    return type;
}

IrType ir_void() {
    IrType type;
    type.kind = IrTypeKind::Void;
    return type;
}

IrType ir_list(IrType element) {
    IrType type;
    type.kind = IrTypeKind::List;
    type.element = std::make_shared<IrType>(std::move(element));
    return type;
}

IrType ir_record(int record_id) {
    IrType type;
    type.kind = IrTypeKind::Record;
    type.record_id = record_id;
    return type;
}

IrType ir_map(IrType value) {
    IrType type;
    type.kind = IrTypeKind::Map;
    type.element = std::make_shared<IrType>(std::move(value));
    return type;
}

IrType ir_set() {
    IrType type;
    type.kind = IrTypeKind::Set;
    return type;
}

IrType ir_json() {
    IrType type;
    type.kind = IrTypeKind::Json;
    return type;
}

IrType ir_task(IrType payload) {
    IrType type;
    type.kind = IrTypeKind::Task;
    type.element = std::make_shared<IrType>(std::move(payload));
    return type;
}

IrType ir_send(IrType element) {
    IrType type;
    type.kind = IrTypeKind::Send;
    type.element = std::make_shared<IrType>(std::move(element));
    return type;
}

IrType ir_recv(IrType element) {
    IrType type;
    type.kind = IrTypeKind::Recv;
    type.element = std::make_shared<IrType>(std::move(element));
    return type;
}

IrType ir_conn() {
    IrType type;
    type.kind = IrTypeKind::Conn;
    return type;
}

IrType ir_listener() {
    IrType type;
    type.kind = IrTypeKind::Listener;
    return type;
}

IrType ir_type_from(const Type& type) {
    switch (type.kind) {
        case TypeKind::Integer:
            return ir_i64();
        case TypeKind::Float:
            return ir_f64();
        case TypeKind::Boolean:
            return ir_bool();
        case TypeKind::String:
            return ir_string();
        case TypeKind::List:
            return ir_list(type.element != nullptr ? ir_type_from(*type.element) : ir_i64());
        case TypeKind::Record:
        case TypeKind::Error:
        case TypeKind::Result:
            return ir_record(type.record_id);
        case TypeKind::Map:
            return ir_map(type.element != nullptr ? ir_type_from(*type.element) : ir_i64());
        case TypeKind::Set:
            return ir_set();
        case TypeKind::Json:
            return ir_json();
        case TypeKind::Task:
            return ir_task(type.element != nullptr ? ir_type_from(*type.element) : ir_void());
        case TypeKind::Send:
            return ir_send(type.element != nullptr ? ir_type_from(*type.element) : ir_void());
        case TypeKind::Recv:
            return ir_recv(type.element != nullptr ? ir_type_from(*type.element) : ir_void());
        case TypeKind::Conn:
            return ir_conn();
        case TypeKind::Listener:
            return ir_listener();
        case TypeKind::Void:
        case TypeKind::Variable:
            return ir_void();
    }
    return ir_void();
}

std::string ir_type_name(const IrType& type) {
    switch (type.kind) {
        case IrTypeKind::I64:
            return "i64";
        case IrTypeKind::F64:
            return "f64";
        case IrTypeKind::Bool:
            return "bool";
        case IrTypeKind::String:
            return "string";
        case IrTypeKind::Void:
            return "void";
        case IrTypeKind::List:
            return "list[" + (type.element != nullptr ? ir_type_name(*type.element) : "?") + "]";
        case IrTypeKind::Record:
            return "record#" + std::to_string(type.record_id);
        case IrTypeKind::Map:
            return "map[" + (type.element != nullptr ? ir_type_name(*type.element) : "?") + "]";
        case IrTypeKind::Set:
            return "set";
        case IrTypeKind::Json:
            return "json";
        case IrTypeKind::Task:
            return "task[" + (type.element != nullptr ? ir_type_name(*type.element) : "?") + "]";
        case IrTypeKind::Send:
            return "send[" + (type.element != nullptr ? ir_type_name(*type.element) : "?") + "]";
        case IrTypeKind::Recv:
            return "recv[" + (type.element != nullptr ? ir_type_name(*type.element) : "?") + "]";
        case IrTypeKind::Conn:
            return "conn";
        case IrTypeKind::Listener:
            return "listener";
    }
    return "void";
}

int list_elem_kind(const IrType& type) {
    switch (type.kind) {
        case IrTypeKind::I64:
            return 0;
        case IrTypeKind::F64:
            return 1;
        case IrTypeKind::Bool:
            return 2;
        case IrTypeKind::String:
            return 3;
        case IrTypeKind::List:
            return 4;
        case IrTypeKind::Record:
            return 5;
        case IrTypeKind::Map:
        case IrTypeKind::Set:
        case IrTypeKind::Json:
        case IrTypeKind::Task:
        case IrTypeKind::Send:
        case IrTypeKind::Recv:
        case IrTypeKind::Conn:
        case IrTypeKind::Listener:
        case IrTypeKind::Void:
            return -1;
    }
    return -1;
}

namespace {

std::string value_text(const IrValue& value) {
    switch (value.kind) {
        case IrValue::Kind::Temp:
            return "t" + std::to_string(value.id);
        case IrValue::Kind::Local:
            return "%" + value.name;
        case IrValue::Kind::ConstI64:
            return std::to_string(value.i64);
        case IrValue::Kind::ConstF64:
            return std::to_string(value.f64);
        case IrValue::Kind::ConstBool:
            return value.boolean ? "true" : "false";
        case IrValue::Kind::ConstString:
            return "\"" + value.text + "\"";
        case IrValue::Kind::Undef:
            return "undef";
        case IrValue::Kind::Null:
            return "null";
    }
    return "undef";
}

std::string bin_name(IrBinOp op) {
    switch (op) {
        case IrBinOp::Add:
            return "add";
        case IrBinOp::Sub:
            return "sub";
        case IrBinOp::Mul:
            return "mul";
        case IrBinOp::Div:
            return "div";
        case IrBinOp::Rem:
            return "rem";
        case IrBinOp::Concat:
            return "concat";
        case IrBinOp::Eq:
            return "eq";
        case IrBinOp::Ne:
            return "ne";
        case IrBinOp::Lt:
            return "lt";
        case IrBinOp::Le:
            return "le";
        case IrBinOp::Gt:
            return "gt";
        case IrBinOp::Ge:
            return "ge";
    }
    return "bin";
}

}  // namespace

void print_ir(const IrModule& module, std::ostream& out) {
    out << "; YUKT IR — " << module.source_path << '\n';
    for (const auto& record : module.records) {
        out << "record " << record.name << " id " << record.id << " size " << record.size << " align " << record.align
            << '\n';
        for (const auto& field : record.fields) {
            out << "  " << field.name << ": " << ir_type_name(field.type) << " @" << field.offset << '\n';
        }
    }
    for (const auto& function : module.functions) {
        out << "function " << function.name << '(';
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            if (i < function.param_is_ref.size() && function.param_is_ref[i]) {
                out << function.params[i].first << ": ref " << ir_type_name(function.params[i].second);
            } else {
                out << function.params[i].first << ": " << ir_type_name(function.params[i].second);
            }
        }
        out << ") -> " << ir_type_name(function.return_type) << '\n';
        if (!function.locals.empty()) {
            out << "  locals ";
            for (std::size_t i = 0; i < function.locals.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << function.locals[i].first << ": " << ir_type_name(function.locals[i].second);
            }
            out << '\n';
        }
        for (const auto& block : function.blocks) {
            out << "  block " << block.name << '\n';
            for (const auto& inst : block.insts) {
                out << "    ";
                switch (inst.op) {
                    case IrOp::Store:
                        out << "store " << inst.name << ", " << value_text(inst.args[0]) << '\n';
                        break;
                    case IrOp::Load:
                        out << value_text(inst.dest) << " = load " << inst.name << '\n';
                        break;
                    case IrOp::Bin:
                        out << value_text(inst.dest) << " = " << bin_name(inst.bin) << ' ' << value_text(inst.args[0])
                            << ", " << value_text(inst.args[1]) << '\n';
                        break;
                    case IrOp::Un:
                        out << value_text(inst.dest) << " = neg " << value_text(inst.args[0]) << '\n';
                        break;
                    case IrOp::Call:
                        out << value_text(inst.dest) << " = call " << inst.name << '(';
                        for (std::size_t i = 0; i < inst.args.size(); ++i) {
                            if (i != 0) {
                                out << ", ";
                            }
                            out << value_text(inst.args[i]);
                        }
                        out << ")\n";
                        break;
                    case IrOp::List:
                        out << value_text(inst.dest) << " = list " << inst.args.size() << '\n';
                        break;
                    case IrOp::ListSet:
                        out << "list_set " << value_text(inst.args[0]) << ", " << value_text(inst.args[1]) << ", "
                            << value_text(inst.args[2]) << '\n';
                        break;
                    case IrOp::ListGet:
                        out << value_text(inst.dest) << " = list_get " << value_text(inst.args[0]) << ", "
                            << value_text(inst.args[1]) << '\n';
                        break;
                    case IrOp::ListLen:
                        out << value_text(inst.dest) << " = list_len " << value_text(inst.args[0]) << '\n';
                        break;
                    case IrOp::Zeros:
                        out << value_text(inst.dest) << " = zeros " << value_text(inst.args[0]) << '\n';
                        break;
                    case IrOp::ParallelFor:
                        out << "parallel_for " << inst.name << ' ' << value_text(inst.args[0]) << ".."
                            << value_text(inst.args[1]) << " work " << inst.work << '\n';
                        break;
                    case IrOp::Say:
                        out << "say " << value_text(inst.args[0]) << '\n';
                        break;
                    case IrOp::RecordNew:
                        out << value_text(inst.dest) << " = record " << inst.name << '(';
                        for (std::size_t i = 0; i < inst.args.size(); ++i) {
                            if (i != 0) {
                                out << ", ";
                            }
                            out << value_text(inst.args[i]);
                        }
                        out << ")\n";
                        break;
                    case IrOp::FieldGet:
                        out << value_text(inst.dest) << " = field " << value_text(inst.args[0]) << '.' << inst.name
                            << '\n';
                        break;
                    case IrOp::FieldSet:
                        out << value_text(inst.dest) << " = field_set " << value_text(inst.args[0]) << '.' << inst.name
                            << ", " << value_text(inst.args[1]) << '\n';
                        break;
                    case IrOp::Drop:
                        out << "drop " << (inst.args.empty() ? "undef" : value_text(inst.args[0])) << '\n';
                        break;
                    case IrOp::Addr:
                        out << value_text(inst.dest) << " = addr " << inst.name;
                        if (inst.field_index >= 0) {
                            out << '.' << inst.field_index;
                        }
                        out << '\n';
                        break;
                    case IrOp::Start:
                        out << value_text(inst.dest) << " = start " << inst.name << '(';
                        for (std::size_t i = 0; i < inst.args.size(); ++i) {
                            if (i != 0) {
                                out << ", ";
                            }
                            out << value_text(inst.args[i]);
                        }
                        out << ")\n";
                        break;
                    case IrOp::Wait:
                        out << value_text(inst.dest) << " = wait " << (inst.args.empty() ? "undef" : value_text(inst.args[0]))
                            << '\n';
                        break;
                }
            }
            out << "    ";
            switch (block.term.kind) {
                case IrTermKind::Return:
                    out << "ret " << value_text(block.term.value) << '\n';
                    break;
                case IrTermKind::Jump:
                    out << "jmp " << block.term.target << '\n';
                    break;
                case IrTermKind::Branch:
                    out << "br " << value_text(block.term.value) << ", " << block.term.target << ", "
                        << block.term.otherwise << '\n';
                    break;
            }
        }
        out << '\n';
    }
    if (!module.decisions.empty()) {
        out << "loop decisions:\n";
        for (const auto& decision : module.decisions) {
            out << "  " << decision.function << " `" << decision.iterator << "` "
                << (decision.parallel ? "parallel" : "sequential") << " — " << decision.reason << '\n';
        }
    }
}

}  // namespace yukt

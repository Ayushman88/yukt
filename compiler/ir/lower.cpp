#include "ir/lower.hpp"

#include "lexer/token.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yukt {
namespace {

bool expr_mentions(const Expr* expr, const std::string& name) {
    if (expr == nullptr) {
        return false;
    }
    if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        return ident->name == name;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return expr_mentions(unary->operand.get(), name);
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return expr_mentions(binary->left.get(), name) || expr_mentions(binary->right.get(), name);
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        for (const auto& argument : call->arguments) {
            if (expr_mentions(argument.get(), name)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        for (const auto& element : list->elements) {
            if (expr_mentions(element.get(), name)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return expr_mentions(index->collection.get(), name) || expr_mentions(index->index.get(), name);
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return expr_mentions(field->object.get(), name);
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        for (const auto& argument : start->arguments) {
            if (expr_mentions(argument.get(), name)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        return expr_mentions(wait->task.get(), name);
    }
    return false;
}

bool is_start_or_wait(const Expr* expr) {
    return dynamic_cast<const StartExpr*>(expr) != nullptr || dynamic_cast<const WaitExpr*>(expr) != nullptr;
}

bool has_user_call(const Expr* expr, bool ignore_tasks = false) {
    if (expr == nullptr) {
        return false;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (call->callee != "count" && call->callee != "zeros") {
            return true;
        }
        for (const auto& argument : call->arguments) {
            if (has_user_call(argument.get(), ignore_tasks)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return has_user_call(unary->operand.get(), ignore_tasks);
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return has_user_call(binary->left.get(), ignore_tasks) || has_user_call(binary->right.get(), ignore_tasks);
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        for (const auto& element : list->elements) {
            if (has_user_call(element.get(), ignore_tasks)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return has_user_call(index->collection.get(), ignore_tasks) ||
               has_user_call(index->index.get(), ignore_tasks);
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return has_user_call(field->object.get(), ignore_tasks);
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        if (!ignore_tasks) {
            return true;
        }
        for (const auto& argument : start->arguments) {
            if (has_user_call(argument.get(), true)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        return has_user_call(wait->task.get(), ignore_tasks);
    }
    return false;
}

bool ir_is_heap_pointer(const IrType& type) {
    return type.kind == IrTypeKind::List || type.kind == IrTypeKind::String || type.kind == IrTypeKind::Map ||
           type.kind == IrTypeKind::Set || type.kind == IrTypeKind::Json || type.kind == IrTypeKind::Task ||
           type.kind == IrTypeKind::Send || type.kind == IrTypeKind::Recv || type.kind == IrTypeKind::Conn ||
           type.kind == IrTypeKind::Listener;
}

bool call_name_allocates(const std::string& qualifier, const std::string& callee) {
    if (callee == "zeros") {
        return true;
    }
    if (callee == "ok" || callee == "fail" || callee == "std$list$copy" || callee == "std$list$slice" ||
        callee == "std$list$append" ||         callee == "std$str$concat" || callee == "std$str$slice" ||
        callee == "std$str$repeat" || callee == "std$str$join" || callee == "std$str$split" ||
        callee == "std$str$trim" || callee == "std$str$trim_left" || callee == "std$str$trim_right" ||
        callee == "std$str$replace" || callee == "std$str$lower" || callee == "std$str$upper" ||
        callee == "std$map$new" || callee == "std$set$new" || callee == "std$json$parse_unchecked" ||
        callee == "std$file$read_unchecked" || callee == "std$io$program" || callee == "std$io$args" ||
        callee == "std$io$read_line_unchecked" || callee == "std$io$read_all_unchecked" ||
        callee == "std$env$get_unchecked" || callee == "std$sync$channel" || callee == "std$sync$receiver" ||
        callee == "std$sync$send" ||         callee == "std$net$listen_unchecked" || callee == "std$net$connect_unchecked" ||
        callee == "std$net$accept_unchecked" || callee == "std$net$addr" || callee == "std$net$read_unchecked" ||
        callee == "std$http$get_status" || callee == "std$http$get_headers" || callee == "std$http$get_body" ||
        callee == "std$http$get" || callee == "std$http$serve" || callee == "std$http$serve_on") {
        return true;
    }
    const std::string qualified = qualifier.empty() ? callee : qualifier + "." + callee;
    return qualified == "list.copy" || qualified == "list.slice" || qualified == "list.append" ||
           qualified == "str.concat" ||
           qualified == "str.slice" || qualified == "str.repeat" || qualified == "str.join" ||
           qualified == "str.split" || qualified == "str.trim" || qualified == "str.trim_left" ||
           qualified == "str.trim_right" || qualified == "str.replace" || qualified == "str.lower" ||
           qualified == "str.upper" || qualified == "map.new" || qualified == "set.new" ||
           qualified == "json.parse" || qualified == "file.read" || qualified == "io.program" ||
           qualified == "io.args" || qualified == "io.read_line" || qualified == "io.read_all" ||
           qualified == "env.get" || qualified == "env.get_or" || qualified == "sync.channel" ||
           qualified == "sync.receiver" || qualified == "sync.send" || qualified == "sync.recv" ||
           qualified == "net.listen" || qualified == "net.connect" || qualified == "net.accept" ||
           qualified == "net.addr" || qualified == "net.read" || qualified == "http.get" ||
           qualified == "http.serve" || qualified == "http.serve_on";
}

void collect_reads(const Expr* expr, std::set<std::string>& names) {
    if (expr == nullptr) {
        return;
    }
    if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        names.insert(ident->name);
        return;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        collect_reads(unary->operand.get(), names);
        return;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        collect_reads(binary->left.get(), names);
        collect_reads(binary->right.get(), names);
        return;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        for (const auto& argument : call->arguments) {
            collect_reads(argument.get(), names);
        }
        return;
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        for (const auto& element : list->elements) {
            collect_reads(element.get(), names);
        }
        return;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        collect_reads(index->collection.get(), names);
        collect_reads(index->index.get(), names);
        return;
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        collect_reads(field->object.get(), names);
        return;
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        for (const auto& argument : start->arguments) {
            collect_reads(argument.get(), names);
        }
        return;
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        collect_reads(wait->task.get(), names);
    }
}

bool stmt_uses_name(const Stmt* stmt, const std::string& name);

bool stmts_use_name(const std::vector<std::unique_ptr<Stmt>>& stmts, const std::string& name) {
    for (const auto& stmt : stmts) {
        if (stmt_uses_name(stmt.get(), name)) {
            return true;
        }
    }
    return false;
}

bool stmt_uses_name(const Stmt* stmt, const std::string& name) {
    if (stmt == nullptr) {
        return false;
    }
    if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        return assign->name == name || expr_mentions(assign->value.get(), name);
    }
    if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt)) {
        return expr_mentions(index_assign->collection.get(), name) ||
               expr_mentions(index_assign->index.get(), name) || expr_mentions(index_assign->value.get(), name);
    }
    if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt)) {
        return expr_mentions(field_assign->object.get(), name) || expr_mentions(field_assign->value.get(), name);
    }
    if (const auto* say = dynamic_cast<const SayStmt*>(stmt)) {
        return expr_mentions(say->value.get(), name);
    }
    if (const auto* give = dynamic_cast<const GiveStmt*>(stmt)) {
        return expr_mentions(give->value.get(), name);
    }
    if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        return expr_mentions(expr_stmt->expr.get(), name);
    }
    if (const auto* iff = dynamic_cast<const IfStmt*>(stmt)) {
        return expr_mentions(iff->condition.get(), name) || stmts_use_name(iff->then_branch, name) ||
               stmts_use_name(iff->otherwise_branch, name);
    }
    if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt)) {
        return repeat->iterator == name || expr_mentions(repeat->start.get(), name) ||
               expr_mentions(repeat->end.get(), name) || stmts_use_name(repeat->body, name);
    }
    return false;
}

bool scan_used_after(const std::vector<std::unique_ptr<Stmt>>& stmts, const RepeatStmt* target,
                     const std::string& name, bool& found) {
    for (const auto& stmt : stmts) {
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (repeat == target) {
                found = true;
                continue;
            }
            if (scan_used_after(repeat->body, target, name, found)) {
                return true;
            }
            if (found && stmt_uses_name(repeat, name)) {
                return true;
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (scan_used_after(iff->then_branch, target, name, found) ||
                scan_used_after(iff->otherwise_branch, target, name, found)) {
                return true;
            }
            if (found && stmt_uses_name(iff, name)) {
                return true;
            }
            continue;
        }
        if (found && stmt_uses_name(stmt.get(), name)) {
            return true;
        }
    }
    return false;
}

bool assigned_before_loop(const std::vector<std::unique_ptr<Stmt>>& stmts, const RepeatStmt* target,
                          const std::string& name, bool& found) {
    for (const auto& stmt : stmts) {
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (repeat == target) {
                found = true;
                return false;
            }
            if (assigned_before_loop(repeat->body, target, name, found)) {
                return true;
            }
            if (found) {
                return false;
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (assigned_before_loop(iff->then_branch, target, name, found) ||
                assigned_before_loop(iff->otherwise_branch, target, name, found)) {
                return true;
            }
            if (found) {
                return false;
            }
            continue;
        }
        if (found) {
            return false;
        }
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (assign->name == name) {
                return true;
            }
        }
    }
    return false;
}

bool first_use_is_read(const std::vector<std::unique_ptr<Stmt>>& stmts, const std::string& name, bool& seen) {
    for (const auto& stmt : stmts) {
        if (seen) {
            return false;
        }
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (assign->name == name) {
                seen = true;
                return expr_mentions(assign->value.get(), name);
            }
            if (expr_mentions(assign->value.get(), name)) {
                seen = true;
                return true;
            }
            continue;
        }
        if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt.get())) {
            if (expr_mentions(index_assign->collection.get(), name) || expr_mentions(index_assign->index.get(), name) ||
                expr_mentions(index_assign->value.get(), name)) {
                seen = true;
                return true;
            }
            continue;
        }
        if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt.get())) {
            if (expr_mentions(field_assign->object.get(), name) || expr_mentions(field_assign->value.get(), name)) {
                seen = true;
                return true;
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (expr_mentions(iff->condition.get(), name)) {
                seen = true;
                return true;
            }
            if (first_use_is_read(iff->then_branch, name, seen) || first_use_is_read(iff->otherwise_branch, name, seen)) {
                return true;
            }
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (expr_mentions(repeat->start.get(), name) || expr_mentions(repeat->end.get(), name)) {
                seen = true;
                return true;
            }
            if (first_use_is_read(repeat->body, name, seen)) {
                return true;
            }
            continue;
        }
        if (stmt_uses_name(stmt.get(), name)) {
            seen = true;
            return true;
        }
    }
    return false;
}

std::optional<std::int64_t> eval_const_i64(const Expr* expr, const std::map<std::string, std::int64_t>& env,
                                          const std::map<std::string, std::int64_t>& list_len) {
    if (expr == nullptr) {
        return std::nullopt;
    }
    if (const auto* integer = dynamic_cast<const IntegerExpr*>(expr)) {
        return integer->value;
    }
    if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        const auto found = env.find(ident->name);
        if (found == env.end()) {
            return std::nullopt;
        }
        return found->second;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        const auto operand = eval_const_i64(unary->operand.get(), env, list_len);
        if (!operand || unary->op != TokenKind::Minus) {
            return std::nullopt;
        }
        if (*operand == std::numeric_limits<std::int64_t>::min()) {
            return std::nullopt;
        }
        return -*operand;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        const auto left = eval_const_i64(binary->left.get(), env, list_len);
        const auto right = eval_const_i64(binary->right.get(), env, list_len);
        if (!left || !right) {
            return std::nullopt;
        }
        switch (binary->op) {
            case TokenKind::Plus:
                return *left + *right;
            case TokenKind::Minus:
                return *left - *right;
            case TokenKind::Star:
                return *left * *right;
            default:
                return std::nullopt;
        }
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (call->callee == "count" && call->arguments.size() == 1) {
            const auto* ident = dynamic_cast<const IdentifierExpr*>(call->arguments[0].get());
            if (ident != nullptr) {
                const auto found = list_len.find(ident->name);
                if (found != list_len.end()) {
                    return found->second;
                }
            }
            if (const auto* list = dynamic_cast<const ListExpr*>(call->arguments[0].get())) {
                return static_cast<std::int64_t>(list->elements.size());
            }
            if (const auto* zeros = dynamic_cast<const CallExpr*>(call->arguments[0].get())) {
                if (zeros->callee == "zeros" && zeros->arguments.size() == 1) {
                    return eval_const_i64(zeros->arguments[0].get(), env, list_len);
                }
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void record_list_len(const AssignStmt& assign, const std::map<std::string, std::int64_t>& env,
                     std::map<std::string, std::int64_t>& list_len) {
    if (const auto* list = dynamic_cast<const ListExpr*>(assign.value.get())) {
        list_len[assign.name] = static_cast<std::int64_t>(list->elements.size());
        return;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(assign.value.get())) {
        if (call->callee == "zeros" && call->arguments.size() == 1) {
            if (const auto n = eval_const_i64(call->arguments[0].get(), env, list_len)) {
                list_len[assign.name] = *n;
                return;
            }
        }
    }
    list_len.erase(assign.name);
}

bool collect_const_env(const std::vector<std::unique_ptr<Stmt>>& stmts, const RepeatStmt* target,
                       std::map<std::string, std::int64_t>& env, std::map<std::string, std::int64_t>& list_len) {
    for (const auto& stmt : stmts) {
        if (stmt.get() == target) {
            return true;
        }
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (const auto value = eval_const_i64(assign->value.get(), env, list_len)) {
                env[assign->name] = *value;
            } else {
                env.erase(assign->name);
            }
            record_list_len(*assign, env, list_len);
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (collect_const_env(iff->then_branch, target, env, list_len) ||
                collect_const_env(iff->otherwise_branch, target, env, list_len)) {
                return true;
            }
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (collect_const_env(repeat->body, target, env, list_len)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::int64_t> constant_trip_count(const RepeatStmt& stmt,
                                                const std::map<std::string, std::int64_t>& env,
                                                const std::map<std::string, std::int64_t>& list_len) {
    const auto start = eval_const_i64(stmt.start.get(), env, list_len);
    const auto end = eval_const_i64(stmt.end.get(), env, list_len);
    if (!start || !end || *end < *start) {
        return std::nullopt;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(*end - *start) + 1;
    if (span > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(span);
}

constexpr std::int64_t kMinParallelWork = 1'000'000;
constexpr std::int64_t kUnknownNestedTrip = 256;

std::int64_t saturating_mul(std::int64_t left, std::int64_t right) {
    if (left <= 0 || right <= 0) {
        return 0;
    }
    if (left > std::numeric_limits<std::int64_t>::max() / right) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return left * right;
}

std::int64_t body_work(const std::vector<std::unique_ptr<Stmt>>& stmts,
                       const std::map<std::string, std::int64_t>& env,
                       const std::map<std::string, std::int64_t>& list_len) {
    std::int64_t work = 0;
    for (const auto& stmt : stmts) {
        if (dynamic_cast<const AssignStmt*>(stmt.get()) != nullptr) {
            work += 1;
            continue;
        }
        if (dynamic_cast<const IndexAssignStmt*>(stmt.get()) != nullptr ||
            dynamic_cast<const FieldAssignStmt*>(stmt.get()) != nullptr) {
            work += 2;
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            work += body_work(iff->then_branch, env, list_len);
            work += body_work(iff->otherwise_branch, env, list_len);
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            const auto trip = constant_trip_count(*repeat, env, list_len);
            const std::int64_t inner = trip ? *trip : kUnknownNestedTrip;
            work += saturating_mul(inner, body_work(repeat->body, env, list_len) + 1);
        }
    }
    return work;
}

bool worth_parallelizing(const RepeatStmt& stmt, bool nested, const std::map<std::string, std::int64_t>& env,
                         const std::map<std::string, std::int64_t>& list_len) {
    const auto trip = constant_trip_count(stmt, env, list_len);
    if (nested) {
        return trip.has_value() && *trip >= kMinParallelWork;
    }
    if (!trip) {
        return true;
    }
    return saturating_mul(*trip, body_work(stmt.body, env, list_len) + 1) >= kMinParallelWork;
}

struct SimpleReduction {
    std::string name;
    TokenKind op = TokenKind::Plus;
};

bool match_simple_reduction(const AssignStmt& assign, TokenKind& op) {
    const auto* bin = dynamic_cast<const BinaryExpr*>(assign.value.get());
    if (bin == nullptr || (bin->op != TokenKind::Plus && bin->op != TokenKind::Star)) {
        return false;
    }
    const auto* left = dynamic_cast<const IdentifierExpr*>(bin->left.get());
    const auto* right = dynamic_cast<const IdentifierExpr*>(bin->right.get());
    if (left != nullptr && left->name == assign.name) {
        if (expr_mentions(bin->right.get(), assign.name)) {
            return false;
        }
        op = bin->op;
        return true;
    }
    if (right != nullptr && right->name == assign.name) {
        if (expr_mentions(bin->left.get(), assign.name)) {
            return false;
        }
        op = bin->op;
        return true;
    }
    return false;
}

void collect_reduction_assigns(const std::vector<std::unique_ptr<Stmt>>& stmts,
                               std::map<std::string, TokenKind>& simple,
                               std::set<std::string>& other_assigns, bool& nested_control) {
    for (const auto& stmt : stmts) {
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            TokenKind op = TokenKind::Plus;
            if (match_simple_reduction(*assign, op)) {
                const auto found = simple.find(assign->name);
                if (found != simple.end() && found->second != op) {
                    other_assigns.insert(assign->name);
                } else {
                    simple[assign->name] = op;
                }
            } else {
                other_assigns.insert(assign->name);
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            nested_control = true;
            collect_reduction_assigns(iff->then_branch, simple, other_assigns, nested_control);
            collect_reduction_assigns(iff->otherwise_branch, simple, other_assigns, nested_control);
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            nested_control = true;
            collect_reduction_assigns(repeat->body, simple, other_assigns, nested_control);
        }
    }
}

std::optional<SimpleReduction> classify_simple_reduction(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    std::map<std::string, TokenKind> simple;
    std::set<std::string> other_assigns;
    bool nested_control = false;
    collect_reduction_assigns(stmts, simple, other_assigns, nested_control);
    if (nested_control || simple.size() != 1) {
        return std::nullopt;
    }
    const auto& entry = *simple.begin();
    if (other_assigns.contains(entry.first)) {
        return std::nullopt;
    }
    return SimpleReduction{entry.first, entry.second};
}

bool reduction_used_unsafely(const std::vector<std::unique_ptr<Stmt>>& stmts, const std::string& name) {
    for (const auto& stmt : stmts) {
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            TokenKind op = TokenKind::Plus;
            if (assign->name == name && match_simple_reduction(*assign, op)) {
                continue;
            }
            if (assign->name != name && expr_mentions(assign->value.get(), name)) {
                return true;
            }
            continue;
        }
        if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt.get())) {
            if (expr_mentions(index_assign->collection.get(), name) ||
                expr_mentions(index_assign->index.get(), name) || expr_mentions(index_assign->value.get(), name)) {
                return true;
            }
            continue;
        }
        if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt.get())) {
            if (expr_mentions(field_assign->object.get(), name) || expr_mentions(field_assign->value.get(), name)) {
                return true;
            }
            continue;
        }
        if (stmt_uses_name(stmt.get(), name)) {
            return true;
        }
    }
    return false;
}

void collect_loop_effects(const std::vector<std::unique_ptr<Stmt>>& stmts, const std::string& iterator,
                          std::set<std::string>& assigned, std::set<std::string>& reads,
                          std::set<std::string>& inner_iterators, bool& ok, std::string& reason,
                          bool ignore_tasks = false) {
    for (const auto& stmt : stmts) {
        if (!ok) {
            return;
        }
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (has_user_call(assign->value.get(), ignore_tasks)) {
                ok = false;
                reason = "body contains a user function call";
                return;
            }
            assigned.insert(assign->name);
            collect_reads(assign->value.get(), reads);
            continue;
        }
        if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt.get())) {
            const auto* collection = dynamic_cast<const IdentifierExpr*>(index_assign->collection.get());
            if (collection == nullptr || !expr_mentions(index_assign->index.get(), iterator) ||
                has_user_call(index_assign->index.get(), ignore_tasks) ||
                has_user_call(index_assign->value.get(), ignore_tasks)) {
                ok = false;
                reason = "list write is not indexed solely by this iterator";
                return;
            }
            reads.insert(collection->name);
            collect_reads(index_assign->index.get(), reads);
            collect_reads(index_assign->value.get(), reads);
            continue;
        }
        if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt.get())) {
            const Expr* cursor = field_assign->object.get();
            const IndexExpr* indexed = nullptr;
            while (const auto* field = dynamic_cast<const FieldExpr*>(cursor)) {
                cursor = field->object.get();
            }
            indexed = dynamic_cast<const IndexExpr*>(cursor);
            if (indexed != nullptr) {
                const auto* collection = dynamic_cast<const IdentifierExpr*>(indexed->collection.get());
                if (collection == nullptr || !expr_mentions(indexed->index.get(), iterator) ||
                    has_user_call(indexed->index.get(), ignore_tasks) ||
                    has_user_call(field_assign->value.get(), ignore_tasks)) {
                    ok = false;
                    reason = "list write is not indexed solely by this iterator";
                    return;
                }
                reads.insert(collection->name);
                collect_reads(indexed->index.get(), reads);
                collect_reads(field_assign->value.get(), reads);
                continue;
            }
            if (const auto* ident = dynamic_cast<const IdentifierExpr*>(cursor)) {
                if (has_user_call(field_assign->value.get(), ignore_tasks)) {
                    ok = false;
                    reason = "body contains a user function call";
                    return;
                }
                assigned.insert(ident->name);
                collect_reads(field_assign->value.get(), reads);
                continue;
            }
            ok = false;
            reason = "body contains a statement that is not known to be independent";
            return;
        }
        if (dynamic_cast<const SayStmt*>(stmt.get()) != nullptr) {
            ok = false;
            reason = "body contains `say`";
            return;
        }
        if (dynamic_cast<const GiveStmt*>(stmt.get()) != nullptr) {
            ok = false;
            reason = "body contains `give`";
            return;
        }
        if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
            if (ignore_tasks && is_start_or_wait(expr_stmt->expr.get())) {
                if (has_user_call(expr_stmt->expr.get(), true)) {
                    ok = false;
                    reason = "body contains a user function call";
                    return;
                }
                collect_reads(expr_stmt->expr.get(), reads);
                continue;
            }
            const auto* call = dynamic_cast<const CallExpr*>(expr_stmt->expr.get());
            if (call == nullptr || (call->callee != "count" && call->callee != "zeros")) {
                ok = false;
                reason = "body contains a user function call";
                return;
            }
            collect_reads(expr_stmt->expr.get(), reads);
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (has_user_call(iff->condition.get(), ignore_tasks)) {
                ok = false;
                reason = "body contains a user function call";
                return;
            }
            collect_reads(iff->condition.get(), reads);
            collect_loop_effects(iff->then_branch, iterator, assigned, reads, inner_iterators, ok, reason,
                                 ignore_tasks);
            collect_loop_effects(iff->otherwise_branch, iterator, assigned, reads, inner_iterators, ok, reason,
                                 ignore_tasks);
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            inner_iterators.insert(repeat->iterator);
            collect_reads(repeat->start.get(), reads);
            collect_reads(repeat->end.get(), reads);
            collect_loop_effects(repeat->body, iterator, assigned, reads, inner_iterators, ok, reason, ignore_tasks);
            continue;
        }
        ok = false;
        reason = "body contains a statement that is not known to be independent";
    }
}

class Lowerer {
public:
    Lowerer(const Program& program, const SemanticInfo& semantic, std::string source_path);
    Lowerer(const ModuleGraph& graph, const SemanticInfo& semantic);

    IrModule run();

private:
    void lower_function(const FunctionDecl& function);
    void lower_statement(const Stmt* stmt);
    void lower_assignment(const AssignStmt& stmt);
    void lower_index_assign(const IndexAssignStmt& stmt);
    void lower_field_assign(const FieldAssignStmt& stmt);
    IrValue read_place(const Expr* expr);
    void write_place(const Expr* expr, const IrValue& value);
    int field_index_of(int record_id, const std::string& name) const;
    IrValue emit_field_get(const IrValue& object, const std::string& field, IrType type);
    IrValue emit_field_set(const IrValue& object, const std::string& field, const IrValue& value);
    IrValue emit_list_len(const IrValue& list);
    IrValue emit_list_get(const IrValue& list, const IrValue& index, IrType element);
    void emit_list_set(const IrValue& list, const IrValue& index, const IrValue& value);
    IrValue emit_list_new(const IrValue& length, IrType type);
    void emit_counted_loop(const IrValue& count, const std::function<void(const IrValue&)>& body_fn);
    bool list_needs_compiler_clone(const IrType& type) const;
    bool list_needs_element_drop(const IrType& type) const;
    void emit_drop_heap_contents(const IrValue& value);
    IrValue emit_clone_value(const IrValue& value);
    IrValue emit_clone_list_range(const IrValue& src, const IrValue& start, const IrValue& length);
    void emit_list_slice_check(const IrValue& src, const IrValue& start, const IrValue& end);
    void copy_records();
    void lower_seq(const std::vector<const Stmt*>& stmts);
    void lower_say(const SayStmt& stmt);
    void lower_give(const GiveStmt& stmt);
    void lower_expr_stmt(const ExprStmt& stmt);
    void lower_if(const IfStmt& stmt, const std::vector<const Stmt*>& rest);
    void lower_repeat(const RepeatStmt& stmt);
    void lower_counted_repeat(const RepeatStmt& stmt, IrValue start, IrValue end);
    void lower_parallel_repeat(const RepeatStmt& stmt, const std::vector<std::string>& captures,
                               std::int64_t work_per_iter, const std::optional<SimpleReduction>& reduction);
    bool can_parallelize(const RepeatStmt& stmt, std::vector<std::string>& captures, LoopDecision& decision,
                         std::optional<SimpleReduction>& reduction) const;
    bool stmts_allocate(const std::vector<std::unique_ptr<Stmt>>& stmts) const;
    bool expr_allocates(const Expr* expr) const;
    bool call_allocates(const CallExpr& call) const;
    bool expr_is_string_concat(const Expr* expr) const;
    IrValue const_i64(std::int64_t value);
    IrValue const_f64(double value);
    std::string rewrite_name(const std::string& name) const;
    IrValue emit_bin(IrBinOp op, const IrValue& left, const IrValue& right, IrType type);
    IrValue lower_expression(const Expr* expr);
    IrValue lower_unary(const UnaryExpr& expr);
    IrValue lower_binary(const BinaryExpr& expr);
    IrValue lower_call(const CallExpr& expr);
    IrValue lower_start(const StartExpr& expr);
    IrValue lower_wait(const WaitExpr& expr);
    IrValue lower_addr(const Expr* expr);
    IrValue lower_list(const ListExpr& expr);
    IrValue lower_index(const IndexExpr& expr);

    IrType type_of(const Expr* expr) const;
    IrType type_of_name(const std::string& name) const;
    void ensure_local(const std::string& name, IrType type);
    static void ensure_local_on(IrFunction& function, const std::string& name, IrType type);

    IrBlock& current();
    IrBlock& new_block(const std::string& prefix);
    void switch_to(const std::string& name);
    IrBlock* find_block(const std::string& name);
    void finish_jump(const std::string& target, std::string loop_iterator = {});
    void finish_branch(const IrValue& condition, const std::string& then_name, const std::string& else_name);
    void finish_return(IrValue value);

    IrValue make_temp(IrType type);
    void emit(IrInst inst);
    IrValue emit_load(const std::string& name);
    void emit_store(const std::string& name, const IrValue& value);
    void emit_drop(const IrValue& value);
    void emit_named_drops(const std::vector<std::string>& names);
    void emit_owned_drops();
    void emit_path_exit();
    void note_user_call_moves(const CallExpr& expr);
    void consume_heap_expr(const Expr* expr);
    bool owns_heap(const IrType& type) const;
    const RecordInfo* semantic_record(int record_id) const;
    IrType field_ir_type(int record_id, const std::string& field) const;
    IrValue default_return_value() const;
    IrValue emit_zero(const IrType& type);

    const Program* program_ = nullptr;
    std::vector<const Program*> programs_;
    const SemanticInfo& semantic_;
    IrModule module_;
    std::vector<IrFunction> outlined_;
    IrFunction* function_ = nullptr;
    const FunctionDecl* current_function_decl_ = nullptr;
    std::string current_block_;
    std::string current_function_name_;
    int next_temp_ = 0;
    int next_block_ = 0;
    int next_outlined_ = 0;
    bool in_parallel_ = false;
    int in_loop_ = 0;
    std::string reduction_from_;
    std::string reduction_to_;
    std::unordered_set<std::string> owned_;
    std::function<void()> seq_footer_;
};

Lowerer::Lowerer(const Program& program, const SemanticInfo& semantic, std::string source_path)
    : program_(&program), semantic_(semantic) {
    programs_.push_back(&program);
    module_.source_path = std::move(source_path);
}

Lowerer::Lowerer(const ModuleGraph& graph, const SemanticInfo& semantic)
    : semantic_(semantic) {
    if (!graph.modules.empty()) {
        module_.source_path = graph.modules[0].canonical_path;
    }
    for (const auto& module : graph.modules) {
        if (module.program != nullptr) {
            programs_.push_back(module.program.get());
        }
    }
    if (!programs_.empty()) {
        program_ = programs_.front();
    }
}

IrModule Lowerer::run() {
    copy_records();
    for (const Program* program : programs_) {
        program_ = program;
        for (const auto& function : program->functions) {
            if (function != nullptr) {
                lower_function(*function);
            }
        }
    }
    for (auto& function : outlined_) {
        module_.functions.push_back(std::move(function));
    }
    return module_;
}

void Lowerer::lower_function(const FunctionDecl& function) {
    current_function_decl_ = &function;
    auto ir_name = function.name;
    if (const auto found = semantic_.function_ir_names.find(&function); found != semantic_.function_ir_names.end()) {
        ir_name = found->second;
    }
    if (is_stdlib_runtime_ir(ir_name)) {
        return;
    }
    IrFunction ir_fn;
    ir_fn.name = ir_name;
    ir_fn.foreign = function.foreign;
    const auto return_found = semantic_.return_types.find(ir_name);
    ir_fn.return_type = return_found != semantic_.return_types.end() ? ir_type_from(return_found->second) : ir_void();

    const auto params_found = semantic_.parameter_types.find(ir_name);
    const auto refs_found = semantic_.parameter_refs.find(ir_name);
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        IrType type = ir_i64();
        if (params_found != semantic_.parameter_types.end() && i < params_found->second.size()) {
            type = ir_type_from(params_found->second[i]);
        }
        ir_fn.params.emplace_back(function.parameters[i], type);
        ir_fn.locals.emplace_back(function.parameters[i], type);
        const char is_ref =
            refs_found != semantic_.parameter_refs.end() && i < refs_found->second.size() ? refs_found->second[i] : 0;
        ir_fn.param_is_ref.push_back(is_ref);
    }

    if (function.foreign) {
        module_.functions.push_back(std::move(ir_fn));
        current_function_decl_ = nullptr;
        return;
    }

    const auto locals_found = semantic_.local_types.find(ir_name);
    if (locals_found != semantic_.local_types.end()) {
        for (const auto& [name, type] : locals_found->second) {
            ensure_local_on(ir_fn, name, ir_type_from(type));
        }
    }

    module_.functions.push_back(std::move(ir_fn));
    function_ = &module_.functions.back();
    current_function_name_ = ir_name;
    next_temp_ = 0;
    next_block_ = 0;

    IrBlock entry;
    entry.name = "entry";
    function_->blocks.push_back(std::move(entry));
    current_block_ = "entry";

    owned_.clear();
    seq_footer_ = {};
    for (std::size_t i = 0; i < function_->params.size(); ++i) {
        const bool is_ref = i < function_->param_is_ref.size() && function_->param_is_ref[i];
        const bool recv_borrow = ir_name == "std$sync$recv" && i == 0;
        const bool net_borrow =
            i == 0 && (ir_name == "std$net$accept" || ir_name == "std$net$read" || ir_name == "std$net$write");
        if (!is_ref && !recv_borrow && !net_borrow && owns_heap(function_->params[i].second)) {
            owned_.insert(function_->params[i].first);
        }
    }

    std::vector<const Stmt*> body;
    body.reserve(function.body.size());
    for (const auto& stmt : function.body) {
        body.push_back(stmt.get());
    }
    lower_seq(body);

    function_ = nullptr;
    current_function_decl_ = nullptr;
}

void Lowerer::ensure_local_on(IrFunction& function, const std::string& name, IrType type) {
    for (const auto& local : function.locals) {
        if (local.first == name) {
            return;
        }
    }
    function.locals.emplace_back(name, type);
}

void Lowerer::lower_statement(const Stmt* stmt) {
    if (stmt == nullptr || current().terminated) {
        return;
    }
    if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        lower_assignment(*assign);
        return;
    }
    if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt)) {
        lower_index_assign(*index_assign);
        return;
    }
    if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt)) {
        lower_field_assign(*field_assign);
        return;
    }
    if (const auto* say = dynamic_cast<const SayStmt*>(stmt)) {
        lower_say(*say);
        return;
    }
    if (const auto* give = dynamic_cast<const GiveStmt*>(stmt)) {
        lower_give(*give);
        return;
    }
    if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        lower_expr_stmt(*expr_stmt);
        return;
    }
    if (const auto* iff = dynamic_cast<const IfStmt*>(stmt)) {
        lower_if(*iff, {});
        return;
    }
    if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt)) {
        lower_repeat(*repeat);
        return;
    }
}

void Lowerer::lower_seq(const std::vector<const Stmt*>& stmts) {
    for (std::size_t i = 0; i < stmts.size(); ++i) {
        if (current().terminated) {
            return;
        }
        const Stmt* stmt = stmts[i];
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt)) {
            std::vector<const Stmt*> rest(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1, stmts.end());
            lower_if(*iff, rest);
            return;
        }
        lower_statement(stmt);
    }
    emit_path_exit();
}

void Lowerer::lower_assignment(const AssignStmt& stmt) {
    const IrValue value = lower_expression(stmt.value.get());
    ensure_local(stmt.name, value.type);
    const auto* id = dynamic_cast<const IdentifierExpr*>(stmt.value.get());
    const bool self_move = id != nullptr && id->name == stmt.name;
    if (owns_heap(value.type) && !self_move && owned_.contains(stmt.name)) {
        emit_drop(emit_load(stmt.name));
    }
    if (owns_heap(value.type)) {
        if (id != nullptr && id->name != stmt.name) {
            owned_.erase(id->name);
        } else if (id == nullptr) {
            consume_heap_expr(stmt.value.get());
        }
        owned_.insert(stmt.name);
    }
    emit_store(stmt.name, value);
}

void Lowerer::lower_index_assign(const IndexAssignStmt& stmt) {
    const IrValue value = lower_expression(stmt.value.get());
    const bool heap = owns_heap(value.type);
    const bool borrowed_element = dynamic_cast<const IndexExpr*>(stmt.value.get()) != nullptr;
    IrValue stored = value;
    if (heap && borrowed_element) {
        stored = emit_clone_value(value);
    }
    const IrValue collection = lower_expression(stmt.collection.get());
    const IrValue index = lower_expression(stmt.index.get());
    if (list_needs_element_drop(collection.type) && collection.type.element != nullptr) {
        emit_drop_heap_contents(emit_list_get(collection, index, *collection.type.element));
    }
    if (heap && !borrowed_element) {
        consume_heap_expr(stmt.value.get());
    }
    emit_list_set(collection, index, stored);
}

void Lowerer::copy_records() {
    module_.records.clear();
    for (const auto& record : semantic_.records) {
        IrRecord ir;
        ir.id = record.id;
        ir.name = record.name;
        ir.identity = record.identity;
        ir.size = record.size;
        ir.align = record.align;
        for (const auto& field : record.fields) {
            IrRecordField ir_field;
            ir_field.name = field.name;
            ir_field.type = ir_type_from(field.type);
            ir_field.offset = field.offset;
            ir_field.size = field.size;
            ir_field.align = field.align;
            ir.fields.push_back(std::move(ir_field));
        }
        module_.records.push_back(std::move(ir));
    }
}

int Lowerer::field_index_of(int record_id, const std::string& name) const {
    for (const auto& record : semantic_.records) {
        if (record.id != record_id) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(record.fields.size()); ++i) {
            if (record.fields[static_cast<std::size_t>(i)].name == name) {
                return i;
            }
        }
    }
    return -1;
}

IrValue Lowerer::emit_field_get(const IrValue& object, const std::string& field, IrType type) {
    IrInst inst;
    inst.op = IrOp::FieldGet;
    inst.name = field;
    inst.field_index = field_index_of(object.type.record_id, field);
    inst.args.push_back(object);
    inst.dest = make_temp(type);
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::emit_field_set(const IrValue& object, const std::string& field, const IrValue& value) {
    IrInst inst;
    inst.op = IrOp::FieldSet;
    inst.name = field;
    inst.field_index = field_index_of(object.type.record_id, field);
    inst.args.push_back(object);
    inst.args.push_back(value);
    inst.dest = make_temp(object.type);
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::emit_list_len(const IrValue& list) {
    IrInst inst;
    inst.op = IrOp::ListLen;
    inst.args.push_back(list);
    inst.dest = make_temp(ir_i64());
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::emit_list_get(const IrValue& list, const IrValue& index, IrType element) {
    IrInst inst;
    inst.op = IrOp::ListGet;
    inst.args.push_back(list);
    inst.args.push_back(index);
    inst.dest = make_temp(std::move(element));
    emit(inst);
    return inst.dest;
}

void Lowerer::emit_list_set(const IrValue& list, const IrValue& index, const IrValue& value) {
    IrInst inst;
    inst.op = IrOp::ListSet;
    inst.args.push_back(list);
    inst.args.push_back(index);
    inst.args.push_back(value);
    emit(std::move(inst));
}

IrValue Lowerer::emit_list_new(const IrValue& length, IrType type) {
    IrInst inst;
    inst.op = IrOp::List;
    inst.dest = make_temp(std::move(type));
    inst.args.push_back(length);
    emit(inst);
    return inst.dest;
}

void Lowerer::emit_counted_loop(const IrValue& count, const std::function<void(const IrValue&)>& body_fn) {
    if (function_ == nullptr || current().terminated) {
        return;
    }
    const std::string i_name = "$i" + std::to_string(next_temp_++);
    ensure_local(i_name, ir_i64());
    emit_store(i_name, const_i64(0));
    const std::string cond_name = new_block("ownloop").name;
    const std::string body_name = new_block("ownbody").name;
    const std::string after_name = new_block("ownafter").name;
    finish_jump(cond_name);

    switch_to(cond_name);
    const IrValue keep = emit_bin(IrBinOp::Lt, emit_load(i_name), count, ir_bool());
    finish_branch(keep, body_name, after_name);

    switch_to(body_name);
    body_fn(emit_load(i_name));
    if (!current().terminated) {
        emit_store(i_name, emit_bin(IrBinOp::Add, emit_load(i_name), const_i64(1), ir_i64()));
        finish_jump(cond_name, i_name);
    }

    switch_to(after_name);
}

bool Lowerer::list_needs_compiler_clone(const IrType& type) const {
    if (type.kind != IrTypeKind::List || type.element == nullptr) {
        return false;
    }
    const IrType& element = *type.element;
    if (element.kind == IrTypeKind::Record) {
        return owns_heap(element);
    }
    if (element.kind == IrTypeKind::List) {
        return list_needs_compiler_clone(element);
    }
    return false;
}

bool Lowerer::list_needs_element_drop(const IrType& type) const {
    if (type.kind != IrTypeKind::List || type.element == nullptr) {
        return false;
    }
    const IrType& element = *type.element;
    if (element.kind == IrTypeKind::Record) {
        return owns_heap(element);
    }
    if (element.kind == IrTypeKind::List) {
        return list_needs_element_drop(element);
    }
    return false;
}

IrValue Lowerer::emit_clone_value(const IrValue& value) {
    if (value.type.kind == IrTypeKind::String) {
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = "yukt_str_clone";
        inst.args.push_back(value);
        inst.dest = make_temp(ir_string());
        emit(inst);
        return inst.dest;
    }
    if (value.type.kind == IrTypeKind::List) {
        if (list_needs_compiler_clone(value.type)) {
            return emit_clone_list_range(value, const_i64(0), emit_list_len(value));
        }
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = "std$list$copy";
        inst.args.push_back(value);
        inst.dest = make_temp(value.type);
        emit(inst);
        return inst.dest;
    }
    if (value.type.kind == IrTypeKind::Record && owns_heap(value.type)) {
        const RecordInfo* record = semantic_record(value.type.record_id);
        if (record == nullptr) {
            return value;
        }
        IrInst inst;
        inst.op = IrOp::RecordNew;
        inst.name = record->identity;
        for (const auto& field : record->fields) {
            const IrType field_type = ir_type_from(field.type);
            inst.args.push_back(emit_clone_value(emit_field_get(value, field.name, field_type)));
        }
        inst.dest = make_temp(value.type);
        emit(inst);
        return inst.dest;
    }
    return value;
}

IrValue Lowerer::emit_clone_list_range(const IrValue& src, const IrValue& start, const IrValue& length) {
    const IrValue dest = emit_list_new(length, src.type);
    if (src.type.element == nullptr) {
        return dest;
    }
    const IrType element = *src.type.element;
    emit_counted_loop(length, [this, src, start, dest, element](const IrValue& index) {
        const IrValue src_index = emit_bin(IrBinOp::Add, start, index, ir_i64());
        emit_list_set(dest, index, emit_clone_value(emit_list_get(src, src_index, element)));
    });
    return dest;
}

void Lowerer::emit_list_slice_check(const IrValue& src, const IrValue& start, const IrValue& end) {
    IrInst inst;
    inst.op = IrOp::Call;
    inst.name = "yukt_list_check_slice";
    inst.args.push_back(src);
    inst.args.push_back(start);
    inst.args.push_back(end);
    inst.dest = make_temp(ir_void());
    emit(inst);
}

IrValue Lowerer::read_place(const Expr* expr) {
    if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        return emit_load(ident->name);
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return lower_index(*index);
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return emit_field_get(read_place(field->object.get()), field->field, type_of(expr));
    }
    return lower_expression(expr);
}

void Lowerer::write_place(const Expr* expr, const IrValue& value) {
    if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        ensure_local(ident->name, value.type);
        emit_store(ident->name, value);
        return;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        IrInst inst;
        inst.op = IrOp::ListSet;
        inst.args.push_back(lower_expression(index->collection.get()));
        inst.args.push_back(lower_expression(index->index.get()));
        inst.args.push_back(value);
        emit(std::move(inst));
        return;
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        const IrValue parent = read_place(field->object.get());
        write_place(field->object.get(), emit_field_set(parent, field->field, value));
    }
}

void Lowerer::lower_field_assign(const FieldAssignStmt& stmt) {
    const IrValue value = lower_expression(stmt.value.get());
    const IrValue parent = read_place(stmt.object.get());
    const IrType field_type = field_ir_type(parent.type.record_id, stmt.field);
    const bool heap_field = owns_heap(field_type);
    if (heap_field) {
        consume_heap_expr(stmt.value.get());
        emit_drop(emit_field_get(parent, stmt.field, field_type));
    }
    write_place(stmt.object.get(), emit_field_set(parent, stmt.field, value));
}

void Lowerer::lower_say(const SayStmt& stmt) {
    IrInst inst;
    inst.op = IrOp::Say;
    inst.args.push_back(lower_expression(stmt.value.get()));
    emit(std::move(inst));
}

void Lowerer::lower_give(const GiveStmt& stmt) {
    const IrValue value = lower_expression(stmt.value.get());
    consume_heap_expr(stmt.value.get());
    emit_owned_drops();
    finish_return(value);
}

void Lowerer::lower_expr_stmt(const ExprStmt& stmt) {
    const IrValue value = lower_expression(stmt.expr.get());
    if (dynamic_cast<const CallExpr*>(stmt.expr.get()) != nullptr && owns_heap(value.type)) {
        emit_drop(value);
    }
}

void Lowerer::lower_if(const IfStmt& stmt, const std::vector<const Stmt*>& rest) {
    const IrValue condition = lower_expression(stmt.condition.get());
    const std::string then_name = new_block("then").name;
    const std::string else_name = new_block("else").name;
    finish_branch(condition, then_name, else_name);

    const std::unordered_set<std::string> snapshot = owned_;

    switch_to(then_name);
    std::vector<const Stmt*> then_seq;
    then_seq.reserve(stmt.then_branch.size() + rest.size());
    for (const auto& then_stmt : stmt.then_branch) {
        then_seq.push_back(then_stmt.get());
    }
    then_seq.insert(then_seq.end(), rest.begin(), rest.end());
    lower_seq(then_seq);

    owned_ = snapshot;
    switch_to(else_name);
    std::vector<const Stmt*> else_seq;
    else_seq.reserve(stmt.otherwise_branch.size() + rest.size());
    for (const auto& otherwise_stmt : stmt.otherwise_branch) {
        else_seq.push_back(otherwise_stmt.get());
    }
    else_seq.insert(else_seq.end(), rest.begin(), rest.end());
    lower_seq(else_seq);
}

bool Lowerer::can_parallelize(const RepeatStmt& stmt, std::vector<std::string>& captures, LoopDecision& decision,
                              std::optional<SimpleReduction>& reduction) const {
    decision.function = current_function_name_;
    decision.iterator = stmt.iterator;
    decision.location = stmt.span.start;
    decision.nest_depth = in_loop_;
    decision.parallel = false;
    decision.independent = false;
    reduction.reset();

    if (in_parallel_ || current_function_decl_ == nullptr) {
        decision.reason = "already inside a parallel region";
        return false;
    }
    std::map<std::string, std::int64_t> env;
    std::map<std::string, std::int64_t> list_len;
    collect_const_env(current_function_decl_->body, &stmt, env, list_len);
    const auto trip = constant_trip_count(stmt, env, list_len);
    decision.estimated_trip = trip.value_or(-1);
    const std::int64_t cost = body_work(stmt.body, env, list_len) + 1;
    decision.work_per_iter = cost < 1 ? 1 : cost;
    decision.estimated_work = trip ? saturating_mul(*trip, cost) : -1;

    std::set<std::string> assigned;
    std::set<std::string> reads;
    std::set<std::string> inner_iterators;
    bool ok = true;
    std::string effect_reason;
    collect_loop_effects(stmt.body, stmt.iterator, assigned, reads, inner_iterators, ok, effect_reason);
    if (stmts_allocate(stmt.body)) {
        decision.reason = "body allocates heap memory";
        return false;
    }
    if (!ok) {
        decision.reason = effect_reason.empty() ? "loop body is not independent" : effect_reason;
        return false;
    }
    auto candidate = classify_simple_reduction(stmt.body);
    if (candidate && reduction_used_unsafely(stmt.body, candidate->name)) {
        candidate.reset();
    }
    if (candidate) {
        const IrType acc_type = type_of_name(candidate->name);
        if (acc_type.kind != IrTypeKind::I64 && acc_type.kind != IrTypeKind::F64) {
            candidate.reset();
        }
    }
    decision.independent = !candidate.has_value();

    if (!worth_parallelizing(stmt, in_loop_ != 0, env, list_len)) {
        if (in_loop_ != 0) {
            decision.reason =
                trip ? "nested inside another repeat with trip count " + std::to_string(*trip) +
                           " (need a proven trip of at least 1000000)"
                     : "nested inside another repeat without a proven trip count of at least 1000000";
        } else if (trip) {
            decision.reason = "estimated work " + std::to_string(decision.estimated_work) +
                              " is below 1000000 (trip " + std::to_string(*trip) + ")";
        } else {
            decision.reason = "estimated work is below the parallel threshold";
        }
        return false;
    }

    for (const auto& name : assigned) {
        if (candidate && name == candidate->name) {
            continue;
        }
        bool found = false;
        if (scan_used_after(current_function_decl_->body, &stmt, name, found)) {
            decision.independent = false;
            decision.reason = "scalar `" + name + "` is used after the loop";
            return false;
        }
        bool seen = false;
        const bool read_first = first_use_is_read(stmt.body, name, seen);
        bool before_found = false;
        const bool existed =
            assigned_before_loop(current_function_decl_->body, &stmt, name, before_found);
        for (const auto& param : current_function_decl_->parameters) {
            if (param == name) {
                if (read_first) {
                    decision.independent = false;
                    decision.reason = "scalar `" + name + "` is a reduction or live-in value";
                    return false;
                }
            }
        }
        if (existed && read_first) {
            decision.independent = false;
            decision.reason = "scalar `" + name + "` is a reduction";
            return false;
        }
    }

    reads.erase(stmt.iterator);
    for (const auto& name : inner_iterators) {
        reads.erase(name);
    }
    if (candidate) {
        reads.erase(candidate->name);
    }
    for (const auto& name : assigned) {
        bool seen = false;
        const bool read_first = first_use_is_read(stmt.body, name, seen);
        bool before_found = false;
        const bool existed =
            assigned_before_loop(current_function_decl_->body, &stmt, name, before_found);
        if (!read_first || !existed) {
            reads.erase(name);
        }
    }

    captures.assign(reads.begin(), reads.end());
    reduction = std::move(candidate);
    decision.independent = true;
    if (reduction) {
        decision.reduction = reduction->name;
        decision.reduction_op = reduction->op == TokenKind::Star ? "mul" : "add";
    }
    decision.parallel = true;
    decision.reason = reduction ? "simple " + decision.reduction_op + " reduction `" + decision.reduction +
                                      "`, estimated work "
                                : "independent iterations, estimated work ";
    decision.reason +=
        (decision.estimated_work >= 0 ? std::to_string(decision.estimated_work) : std::string("unknown"));
    if (decision.estimated_trip >= 0) {
        decision.reason += ", trip " + std::to_string(decision.estimated_trip);
    }
    return true;
}

bool Lowerer::call_allocates(const CallExpr& call) const {
    if (call_name_allocates(call.qualifier, call.callee)) {
        return true;
    }
    if (const auto found = semantic_.call_ir_names.find(&call); found != semantic_.call_ir_names.end()) {
        return call_name_allocates("", found->second);
    }
    return false;
}

bool Lowerer::expr_allocates(const Expr* expr) const {
    if (expr == nullptr) {
        return false;
    }
    if (dynamic_cast<const StringExpr*>(expr) != nullptr) {
        return true;
    }
    if (dynamic_cast<const ListExpr*>(expr) != nullptr) {
        return true;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (call_allocates(*call)) {
            return true;
        }
        for (const auto& argument : call->arguments) {
            if (expr_allocates(argument.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return expr_allocates(unary->operand.get());
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return expr_allocates(binary->left.get()) || expr_allocates(binary->right.get());
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return expr_allocates(index->collection.get()) || expr_allocates(index->index.get());
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return expr_allocates(field->object.get());
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        for (const auto& argument : start->arguments) {
            if (expr_allocates(argument.get())) {
                return true;
            }
        }
        return true;
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        return expr_allocates(wait->task.get());
    }
    return false;
}

bool Lowerer::expr_is_string_concat(const Expr* expr) const {
    if (expr == nullptr) {
        return false;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        if (binary->op == TokenKind::Plus && type_of(expr).kind == IrTypeKind::String) {
            return true;
        }
        return expr_is_string_concat(binary->left.get()) || expr_is_string_concat(binary->right.get());
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return expr_is_string_concat(unary->operand.get());
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        for (const auto& argument : call->arguments) {
            if (expr_is_string_concat(argument.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        for (const auto& element : list->elements) {
            if (expr_is_string_concat(element.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return expr_is_string_concat(index->collection.get()) || expr_is_string_concat(index->index.get());
    }
    return false;
}

bool Lowerer::stmts_allocate(const std::vector<std::unique_ptr<Stmt>>& stmts) const {
    for (const auto& stmt : stmts) {
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (expr_allocates(assign->value.get()) || expr_is_string_concat(assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt.get())) {
            if (expr_allocates(index_assign->index.get()) || expr_allocates(index_assign->value.get()) ||
                expr_is_string_concat(index_assign->index.get()) || expr_is_string_concat(index_assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt.get())) {
            if (expr_allocates(field_assign->object.get()) || expr_allocates(field_assign->value.get()) ||
                expr_is_string_concat(field_assign->object.get()) || expr_is_string_concat(field_assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
            if (expr_allocates(expr_stmt->expr.get()) || expr_is_string_concat(expr_stmt->expr.get())) {
                return true;
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (expr_allocates(iff->condition.get()) || expr_is_string_concat(iff->condition.get()) ||
                stmts_allocate(iff->then_branch) || stmts_allocate(iff->otherwise_branch)) {
                return true;
            }
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (expr_allocates(repeat->start.get()) || expr_allocates(repeat->end.get()) ||
                stmts_allocate(repeat->body)) {
                return true;
            }
        }
    }
    return false;
}

IrValue Lowerer::const_i64(std::int64_t value) {
    IrValue result;
    result.kind = IrValue::Kind::ConstI64;
    result.type = ir_i64();
    result.i64 = value;
    return result;
}

IrValue Lowerer::const_f64(double value) {
    IrValue result;
    result.kind = IrValue::Kind::ConstF64;
    result.type = ir_f64();
    result.f64 = value;
    return result;
}

std::string Lowerer::rewrite_name(const std::string& name) const {
    if (!reduction_from_.empty() && name == reduction_from_) {
        return reduction_to_;
    }
    return name;
}

IrValue Lowerer::emit_bin(IrBinOp op, const IrValue& left, const IrValue& right, IrType type) {
    IrInst inst;
    inst.op = IrOp::Bin;
    inst.bin = op;
    inst.dest = make_temp(std::move(type));
    inst.args.push_back(left);
    inst.args.push_back(right);
    emit(inst);
    return inst.dest;
}

void Lowerer::lower_counted_repeat(const RepeatStmt& stmt, IrValue start, IrValue end) {
    ensure_local(stmt.iterator, ir_i64());
    const std::string k_name = stmt.iterator + "$k";
    const std::string n_name = stmt.iterator + "$n";
    ensure_local(k_name, ir_i64());
    ensure_local(n_name, ir_i64());

    const std::string empty_name = new_block("empty").name;
    const std::string pre_name = new_block("pre").name;
    const std::string cond_name = new_block("loop").name;
    const std::string body_name = new_block("body").name;
    const std::string after_name = new_block("after").name;

    const IrValue too_small = emit_bin(IrBinOp::Lt, end, start, ir_bool());
    finish_branch(too_small, empty_name, pre_name);

    switch_to(empty_name);
    finish_jump(after_name);

    switch_to(pre_name);
    const IrValue span = emit_bin(IrBinOp::Sub, end, start, ir_i64());
    const IrValue niter = emit_bin(IrBinOp::Add, span, const_i64(1), ir_i64());
    emit_store(n_name, niter);
    emit_store(k_name, const_i64(0));
    emit_store(stmt.iterator, start);
    std::vector<std::pair<std::string, std::string>> gen_guards;
    if (const auto found = semantic_.repeat_iter_lists.find(&stmt); found != semantic_.repeat_iter_lists.end()) {
        for (const auto& name : found->second) {
            if (type_of_name(name).kind != IrTypeKind::List) {
                continue;
            }
            const std::string slot = "$igen$" + stmt.iterator + "$" + name;
            ensure_local(slot, ir_i64());
            IrInst gen;
            gen.op = IrOp::Call;
            gen.name = "yukt_list_generation";
            gen.args.push_back(emit_load(name));
            gen.dest = make_temp(ir_i64());
            emit(gen);
            emit_store(slot, gen.dest);
            gen_guards.emplace_back(name, slot);
        }
    }
    finish_jump(cond_name);

    switch_to(cond_name);
    const IrValue k = emit_load(k_name);
    const IrValue n = emit_load(n_name);
    const IrValue keep_going = emit_bin(IrBinOp::Lt, k, n, ir_bool());
    finish_branch(keep_going, body_name, after_name);

    switch_to(body_name);
    ++in_loop_;
    const std::unordered_set<std::string> snapshot = owned_;
    std::function<void()> previous_footer = seq_footer_;
    seq_footer_ = [this, iterator = stmt.iterator, k_name, cond_name, snapshot, gen_guards]() {
        if (current().terminated) {
            return;
        }
        std::vector<std::string> drops;
        for (const auto& name : owned_) {
            if (snapshot.find(name) == snapshot.end()) {
                drops.push_back(name);
            }
        }
        std::sort(drops.begin(), drops.end());
        emit_named_drops(drops);
        for (const auto& name : drops) {
            owned_.erase(name);
        }
        for (const auto& [list_name, slot] : gen_guards) {
            IrInst check;
            check.op = IrOp::Call;
            check.name = "yukt_list_check_generation";
            check.args.push_back(emit_load(list_name));
            check.args.push_back(emit_load(slot));
            check.dest = make_temp(ir_void());
            emit(check);
        }
        const IrValue i = emit_load(iterator);
        emit_store(iterator, emit_bin(IrBinOp::Add, i, const_i64(1), ir_i64()));
        const IrValue next_k = emit_bin(IrBinOp::Add, emit_load(k_name), const_i64(1), ir_i64());
        emit_store(k_name, next_k);
        finish_jump(cond_name, iterator);
    };
    std::vector<const Stmt*> body;
    body.reserve(stmt.body.size());
    for (const auto& body_stmt : stmt.body) {
        body.push_back(body_stmt.get());
    }
    lower_seq(body);
    seq_footer_ = std::move(previous_footer);
    owned_ = snapshot;
    --in_loop_;

    switch_to(after_name);
}

void Lowerer::lower_parallel_repeat(const RepeatStmt& stmt, const std::vector<std::string>& captures,
                                    std::int64_t work_per_iter, const std::optional<SimpleReduction>& reduction) {
    const IrValue start = lower_expression(stmt.start.get());
    const IrValue end = lower_expression(stmt.end.get());
    const IrType reduce_type = reduction ? type_of_name(reduction->name) : ir_void();

    IrFunction outlined;
    outlined.name = "$par$" + std::to_string(next_outlined_++);
    outlined.return_type = reduction ? reduce_type : ir_void();
    outlined.params.emplace_back("$lo", ir_i64());
    outlined.params.emplace_back("$hi", ir_i64());
    outlined.locals.emplace_back("$lo", ir_i64());
    outlined.locals.emplace_back("$hi", ir_i64());
    outlined.locals.emplace_back(stmt.iterator, ir_i64());
    for (const auto& capture : captures) {
        const IrType type = type_of_name(capture);
        outlined.params.emplace_back(capture, type);
        outlined.locals.emplace_back(capture, type);
    }
    if (reduction) {
        outlined.locals.emplace_back("$red", reduce_type);
    }

    IrFunction* parent = function_;
    const std::string parent_block = current_block_;
    const int parent_temp = next_temp_;
    const int parent_block_n = next_block_;
    const std::unordered_set<std::string> parent_owned = owned_;
    std::function<void()> parent_footer = seq_footer_;

    function_ = &outlined;
    next_temp_ = 0;
    next_block_ = 0;
    in_parallel_ = true;
    owned_.clear();
    seq_footer_ = {};
    if (reduction) {
        reduction_from_ = reduction->name;
        reduction_to_ = "$red";
    }

    IrBlock entry;
    entry.name = "entry";
    outlined.blocks.push_back(std::move(entry));
    current_block_ = "entry";

    if (reduction) {
        if (reduction->op == TokenKind::Star) {
            emit_store("$red", reduce_type.kind == IrTypeKind::F64 ? const_f64(1.0) : const_i64(1));
        } else {
            emit_store("$red", reduce_type.kind == IrTypeKind::F64 ? const_f64(0.0) : const_i64(0));
        }
    }

    lower_counted_repeat(stmt, emit_load("$lo"), emit_load("$hi"));
    if (!current().terminated) {
        if (reduction) {
            finish_return(emit_load("$red"));
        } else {
            IrValue value;
            value.kind = IrValue::Kind::Undef;
            value.type = ir_void();
            finish_return(value);
        }
    }

    reduction_from_.clear();
    reduction_to_.clear();
    in_parallel_ = false;
    function_ = parent;
    current_block_ = parent_block;
    next_temp_ = parent_temp;
    next_block_ = parent_block_n;
    owned_ = parent_owned;
    seq_footer_ = std::move(parent_footer);

    const std::string outlined_name = outlined.name;
    outlined_.push_back(std::move(outlined));

    IrInst inst;
    inst.op = IrOp::ParallelFor;
    inst.name = outlined_name;
    inst.work = work_per_iter < 1 ? 1 : work_per_iter;
    inst.args.push_back(start);
    inst.args.push_back(end);
    for (const auto& capture : captures) {
        inst.args.push_back(emit_load(capture));
    }
    if (reduction) {
        inst.reduce_local = reduction->name;
        inst.reduce_bin = reduction->op == TokenKind::Star ? IrBinOp::Mul : IrBinOp::Add;
    }
    emit(std::move(inst));
}

void Lowerer::lower_repeat(const RepeatStmt& stmt) {
    std::vector<std::string> captures;
    LoopDecision decision;
    std::optional<SimpleReduction> reduction;
    if (can_parallelize(stmt, captures, decision, reduction)) {
        module_.decisions.push_back(decision);
        lower_parallel_repeat(stmt, captures, decision.work_per_iter, reduction);
        return;
    }
    if (decision.iterator.empty()) {
        decision.function = current_function_name_;
        decision.iterator = stmt.iterator;
        decision.location = stmt.span.start;
        decision.reason = "sequential";
    }
    module_.decisions.push_back(decision);
    lower_counted_repeat(stmt, lower_expression(stmt.start.get()), lower_expression(stmt.end.get()));
}

IrValue Lowerer::lower_expression(const Expr* expr) {
    if (expr == nullptr) {
        IrValue value;
        value.kind = IrValue::Kind::Undef;
        return value;
    }
    if (const auto* integer = dynamic_cast<const IntegerExpr*>(expr)) {
        IrValue value;
        value.kind = IrValue::Kind::ConstI64;
        value.type = ir_i64();
        value.i64 = integer->value;
        return value;
    }
    if (const auto* floating = dynamic_cast<const FloatExpr*>(expr)) {
        IrValue value;
        value.kind = IrValue::Kind::ConstF64;
        value.type = ir_f64();
        value.f64 = floating->value;
        return value;
    }
    if (const auto* string = dynamic_cast<const StringExpr*>(expr)) {
        IrValue value;
        value.kind = IrValue::Kind::ConstString;
        value.type = ir_string();
        value.text = string->value;
        return value;
    }
    if (const auto* boolean = dynamic_cast<const BoolExpr*>(expr)) {
        IrValue value;
        value.kind = IrValue::Kind::ConstBool;
        value.type = ir_bool();
        value.boolean = boolean->value;
        return value;
    }
    if (const auto* name = dynamic_cast<const IdentifierExpr*>(expr)) {
        return emit_load(name->name);
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return lower_unary(*unary);
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return lower_binary(*binary);
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        return lower_call(*call);
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        return lower_list(*list);
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return lower_index(*index);
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return emit_field_get(lower_expression(field->object.get()), field->field, type_of(expr));
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        return lower_start(*start);
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        return lower_wait(*wait);
    }
    IrValue value;
    value.kind = IrValue::Kind::Undef;
    return value;
}

IrValue Lowerer::lower_unary(const UnaryExpr& expr) {
    IrInst inst;
    inst.op = IrOp::Un;
    inst.un = IrUnOp::Neg;
    inst.args.push_back(lower_expression(expr.operand.get()));
    inst.dest = make_temp(type_of(&expr));
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::lower_binary(const BinaryExpr& expr) {
    IrInst inst;
    inst.op = IrOp::Bin;
    inst.args.push_back(lower_expression(expr.left.get()));
    inst.args.push_back(lower_expression(expr.right.get()));
    const IrType result_type = type_of(&expr);
    switch (expr.op) {
        case TokenKind::Plus:
            inst.bin = result_type.kind == IrTypeKind::String ? IrBinOp::Concat : IrBinOp::Add;
            break;
        case TokenKind::Minus:
            inst.bin = IrBinOp::Sub;
            break;
        case TokenKind::Star:
            inst.bin = IrBinOp::Mul;
            break;
        case TokenKind::Slash:
            inst.bin = IrBinOp::Div;
            break;
        case TokenKind::Percent:
            inst.bin = IrBinOp::Rem;
            break;
        case TokenKind::EqualEqual:
            inst.bin = IrBinOp::Eq;
            break;
        case TokenKind::BangEqual:
            inst.bin = IrBinOp::Ne;
            break;
        case TokenKind::Less:
            inst.bin = IrBinOp::Lt;
            break;
        case TokenKind::LessEqual:
            inst.bin = IrBinOp::Le;
            break;
        case TokenKind::Greater:
            inst.bin = IrBinOp::Gt;
            break;
        case TokenKind::GreaterEqual:
            inst.bin = IrBinOp::Ge;
            break;
        default:
            inst.bin = IrBinOp::Add;
            break;
    }
    inst.dest = make_temp(result_type);
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::lower_index(const IndexExpr& expr) {
    IrInst inst;
    inst.op = IrOp::ListGet;
    inst.args.push_back(lower_expression(expr.collection.get()));
    inst.args.push_back(lower_expression(expr.index.get()));
    inst.dest = make_temp(type_of(&expr));
    emit(inst);
    return inst.dest;
}

bool is_user_function(const Program& program, const std::string& name) {
    for (const auto& function : program.functions) {
        if (function != nullptr && function->name == name) {
            return true;
        }
    }
    return false;
}

IrValue Lowerer::emit_zero(const IrType& type) {
    IrValue value;
    value.type = type;
    switch (type.kind) {
        case IrTypeKind::I64:
            value.kind = IrValue::Kind::ConstI64;
            return value;
        case IrTypeKind::F64:
            value.kind = IrValue::Kind::ConstF64;
            return value;
        case IrTypeKind::Bool:
            value.kind = IrValue::Kind::ConstBool;
            return value;
        case IrTypeKind::Record: {
            IrInst inst;
            inst.op = IrOp::RecordNew;
            const RecordInfo* record = semantic_record(type.record_id);
            inst.name = record != nullptr ? record->identity : "record";
            if (record != nullptr) {
                for (const auto& field : record->fields) {
                    inst.args.push_back(emit_zero(ir_type_from(field.type)));
                }
            }
            inst.dest = make_temp(type);
            emit(inst);
            return inst.dest;
        }
        default:
            value.kind = IrValue::Kind::Null;
            return value;
    }
}

IrValue Lowerer::lower_call(const CallExpr& expr) {
    if (expr.qualifier.empty() && (expr.callee == "ok" || expr.callee == "fail") &&
        (program_ == nullptr || !is_user_function(*program_, expr.callee))) {
        const IrType result_ty = type_of(&expr);
        const RecordInfo* record = semantic_record(result_ty.record_id);
        IrInst inst;
        inst.op = IrOp::RecordNew;
        inst.name = record != nullptr ? record->identity : "result";
        IrValue tag;
        tag.kind = IrValue::Kind::ConstBool;
        tag.type = ir_bool();
        tag.boolean = expr.callee == "ok";
        inst.args.push_back(tag);
        IrType payload = ir_i64();
        IrType error_ty = ir_record(-1);
        if (record != nullptr && record->fields.size() >= 3) {
            payload = ir_type_from(record->fields[1].type);
            error_ty = ir_type_from(record->fields[2].type);
        }
        if (expr.callee == "ok") {
            if (!expr.arguments.empty()) {
                inst.args.push_back(lower_expression(expr.arguments[0].get()));
                consume_heap_expr(expr.arguments[0].get());
            } else {
                inst.args.push_back(emit_zero(payload));
            }
            inst.args.push_back(emit_zero(error_ty));
        } else {
            inst.args.push_back(emit_zero(payload));
            IrValue code = !expr.arguments.empty() ? lower_expression(expr.arguments[0].get()) : emit_zero(ir_string());
            consume_heap_expr(expr.arguments.empty() ? nullptr : expr.arguments[0].get());
            IrInst error_inst;
            error_inst.op = IrOp::RecordNew;
            const RecordInfo* error_record = semantic_record(error_ty.record_id);
            error_inst.name = error_record != nullptr ? error_record->identity : "$error";
            error_inst.args.push_back(code);
            error_inst.dest = make_temp(error_ty);
            emit(error_inst);
            inst.args.push_back(error_inst.dest);
        }
        inst.dest = make_temp(result_ty);
        emit(inst);
        return inst.dest;
    }

    if (expr.qualifier.empty() && expr.callee == "count" && program_ != nullptr &&
        !is_user_function(*program_, "count")) {
        IrInst inst;
        inst.op = IrOp::ListLen;
        if (!expr.arguments.empty()) {
            inst.args.push_back(lower_expression(expr.arguments[0].get()));
        }
        inst.dest = make_temp(ir_i64());
        emit(inst);
        return inst.dest;
    }
    if (expr.qualifier.empty() && expr.callee == "zeros" && program_ != nullptr &&
        !is_user_function(*program_, "zeros")) {
        IrInst inst;
        inst.op = IrOp::Zeros;
        if (!expr.arguments.empty()) {
            inst.args.push_back(lower_expression(expr.arguments[0].get()));
        }
        inst.dest = make_temp(ir_list(ir_i64()));
        emit(inst);
        return inst.dest;
    }

    if (const auto found = semantic_.record_constructors.find(&expr); found != semantic_.record_constructors.end()) {
        IrInst inst;
        inst.op = IrOp::RecordNew;
        inst.name = "record";
        for (const auto& record : semantic_.records) {
            if (record.id == found->second) {
                inst.name = record.identity;
                break;
            }
        }
        for (const auto& argument : expr.arguments) {
            inst.args.push_back(lower_expression(argument.get()));
            consume_heap_expr(argument.get());
        }
        inst.dest = make_temp(ir_record(found->second));
        emit(inst);
        return inst.dest;
    }

    std::string ir_name = expr.callee;
    if (const auto found = semantic_.call_ir_names.find(&expr); found != semantic_.call_ir_names.end()) {
        ir_name = found->second;
    }

    if (ir_name == "std$list$copy" && !expr.arguments.empty()) {
        const IrValue src = lower_expression(expr.arguments[0].get());
        if (list_needs_compiler_clone(src.type)) {
            return emit_clone_list_range(src, const_i64(0), emit_list_len(src));
        }
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        inst.args.push_back(src);
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    if (ir_name == "std$list$fill" && expr.arguments.size() >= 2) {
        const IrValue dest = lower_expression(expr.arguments[0].get());
        const IrValue proto_src = lower_expression(expr.arguments[1].get());
        if (list_needs_compiler_clone(dest.type) && dest.type.element != nullptr) {
            const IrValue proto = emit_clone_value(proto_src);
            const IrValue n = emit_list_len(dest);
            const IrType element = *dest.type.element;
            emit_counted_loop(n, [this, dest, proto, element](const IrValue& index) {
                emit_drop_heap_contents(emit_list_get(dest, index, element));
                emit_list_set(dest, index, emit_clone_value(proto));
            });
            emit_drop(proto);
            return make_temp(ir_void());
        }
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        inst.args.push_back(dest);
        inst.args.push_back(proto_src);
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    if (ir_name == "std$list$append" && expr.arguments.size() >= 2) {
        const IrValue dest = lower_expression(expr.arguments[0].get());
        const IrValue value = lower_expression(expr.arguments[1].get());
        consume_heap_expr(expr.arguments[1].get());
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        inst.args.push_back(dest);
        inst.args.push_back(value);
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    if (ir_name == "std$sync$send" && expr.arguments.size() >= 2) {
        const IrValue dest = lower_expression(expr.arguments[0].get());
        const IrValue value = lower_expression(expr.arguments[1].get());
        consume_heap_expr(expr.arguments[1].get());
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        inst.args.push_back(dest);
        inst.args.push_back(value);
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    if (ir_name == "std$list$slice" && expr.arguments.size() >= 3) {
        const IrValue src = lower_expression(expr.arguments[0].get());
        const IrValue start = lower_expression(expr.arguments[1].get());
        const IrValue end = lower_expression(expr.arguments[2].get());
        if (list_needs_compiler_clone(src.type)) {
            emit_list_slice_check(src, start, end);
            const IrValue length = emit_bin(IrBinOp::Sub, end, start, ir_i64());
            return emit_clone_list_range(src, start, length);
        }
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        inst.args.push_back(src);
        inst.args.push_back(start);
        inst.args.push_back(end);
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    if (ir_name == "std$http$serve" || ir_name == "std$http$serve_on") {
        IrInst inst;
        inst.op = IrOp::Call;
        inst.name = ir_name;
        const auto handler = semantic_.http_handlers.find(&expr);
        inst.extra = handler != semantic_.http_handlers.end() ? handler->second : "";
        if (!expr.arguments.empty()) {
            inst.args.push_back(lower_expression(expr.arguments[0].get()));
            if (ir_name == "std$http$serve_on") {
                consume_heap_expr(expr.arguments[0].get());
            }
        }
        inst.dest = make_temp(type_of(&expr));
        emit(inst);
        return inst.dest;
    }

    IrInst inst;
    inst.op = IrOp::Call;
    inst.name = std::move(ir_name);
    const auto refs_found = semantic_.parameter_refs.find(inst.name);
    for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
        const bool is_ref =
            refs_found != semantic_.parameter_refs.end() && i < refs_found->second.size() && refs_found->second[i];
        if (is_ref) {
            const bool foreign_string = semantic_.foreign_functions.contains(inst.name) &&
                                        expr.arguments[i] != nullptr &&
                                        type_of(expr.arguments[i].get()).kind == IrTypeKind::String;
            if (foreign_string) {
                inst.args.push_back(lower_expression(expr.arguments[i].get()));
            } else {
                inst.args.push_back(lower_addr(expr.arguments[i].get()));
            }
        } else {
            inst.args.push_back(lower_expression(expr.arguments[i].get()));
        }
    }
    inst.dest = make_temp(type_of(&expr));
    emit(inst);
    note_user_call_moves(expr);
    return inst.dest;
}

IrValue Lowerer::lower_start(const StartExpr& expr) {
    IrInst inst;
    inst.op = IrOp::Start;
    const auto found = semantic_.start_ir_names.find(&expr);
    inst.name = found != semantic_.start_ir_names.end() ? found->second : expr.callee;
    for (const auto& argument : expr.arguments) {
        inst.args.push_back(lower_expression(argument.get()));
        consume_heap_expr(argument.get());
    }
    inst.dest = make_temp(type_of(&expr));
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::lower_wait(const WaitExpr& expr) {
    IrInst inst;
    inst.op = IrOp::Wait;
    inst.args.push_back(lower_expression(expr.task.get()));
    consume_heap_expr(expr.task.get());
    inst.dest = make_temp(type_of(&expr));
    emit(inst);
    return inst.dest;
}

IrValue Lowerer::lower_list(const ListExpr& expr) {
    const IrType type = type_of(&expr);
    IrInst create;
    create.op = IrOp::List;
    create.dest = make_temp(type);
    create.args.push_back([&] {
        IrValue count;
        count.kind = IrValue::Kind::ConstI64;
        count.type = ir_i64();
        count.i64 = static_cast<std::int64_t>(expr.elements.size());
        return count;
    }());
    emit(create);
    for (std::size_t i = 0; i < expr.elements.size(); ++i) {
        IrValue index;
        index.kind = IrValue::Kind::ConstI64;
        index.type = ir_i64();
        index.i64 = static_cast<std::int64_t>(i);
        const IrValue element = lower_expression(expr.elements[i].get());
        IrInst set;
        set.op = IrOp::ListSet;
        set.args.push_back(create.dest);
        set.args.push_back(index);
        set.args.push_back(element);
        emit(std::move(set));
        if (owns_heap(element.type)) {
            consume_heap_expr(expr.elements[i].get());
        }
    }
    return create.dest;
}

IrType Lowerer::type_of(const Expr* expr) const {
    if (expr == nullptr) {
        return ir_void();
    }
    const auto found = semantic_.expression_types.find(expr);
    if (found != semantic_.expression_types.end()) {
        return ir_type_from(found->second);
    }
    return ir_void();
}

IrType Lowerer::type_of_name(const std::string& name) const {
    const auto function = semantic_.local_types.find(current_function_name_);
    if (function != semantic_.local_types.end()) {
        const auto local = function->second.find(name);
        if (local != function->second.end()) {
            return ir_type_from(local->second);
        }
    }
    if (function_ != nullptr) {
        for (const auto& local : function_->locals) {
            if (local.first == name) {
                return local.second;
            }
        }
    }
    return ir_i64();
}

void Lowerer::ensure_local(const std::string& name, IrType type) {
    if (function_ == nullptr) {
        return;
    }
    ensure_local_on(*function_, name, type);
}

IrBlock& Lowerer::current() {
    return *find_block(current_block_);
}

IrBlock& Lowerer::new_block(const std::string& prefix) {
    IrBlock block;
    block.name = prefix + std::to_string(next_block_++);
    function_->blocks.push_back(std::move(block));
    return function_->blocks.back();
}

void Lowerer::switch_to(const std::string& name) {
    current_block_ = name;
}

IrBlock* Lowerer::find_block(const std::string& name) {
    for (auto& block : function_->blocks) {
        if (block.name == name) {
            return &block;
        }
    }
    return &function_->blocks.front();
}

void Lowerer::finish_jump(const std::string& target, std::string loop_iterator) {
    current().term.kind = IrTermKind::Jump;
    current().term.target = target;
    current().term.loop_iterator = std::move(loop_iterator);
    current().terminated = true;
}

void Lowerer::finish_branch(const IrValue& condition, const std::string& then_name, const std::string& else_name) {
    current().term.kind = IrTermKind::Branch;
    current().term.value = condition;
    current().term.target = then_name;
    current().term.otherwise = else_name;
    current().terminated = true;
}

void Lowerer::finish_return(IrValue value) {
    current().term.kind = IrTermKind::Return;
    current().term.value = std::move(value);
    current().terminated = true;
}

IrValue Lowerer::make_temp(IrType type) {
    IrValue value;
    value.kind = IrValue::Kind::Temp;
    value.type = std::move(type);
    value.id = next_temp_++;
    return value;
}

void Lowerer::emit(IrInst inst) {
    current().insts.push_back(std::move(inst));
}

IrValue Lowerer::emit_load(const std::string& name) {
    const std::string resolved = rewrite_name(name);
    IrInst inst;
    inst.op = IrOp::Load;
    inst.name = resolved;
    inst.dest = make_temp(type_of_name(resolved));
    emit(inst);
    return inst.dest;
}

void Lowerer::emit_store(const std::string& name, const IrValue& value) {
    IrInst inst;
    inst.op = IrOp::Store;
    inst.name = rewrite_name(name);
    inst.args.push_back(value);
    emit(std::move(inst));
}

void Lowerer::emit_drop_heap_contents(const IrValue& value) {
    if (value.type.kind == IrTypeKind::Record) {
        const RecordInfo* record = semantic_record(value.type.record_id);
        if (record == nullptr) {
            return;
        }
        for (const auto& field : record->fields) {
            const IrType field_type = ir_type_from(field.type);
            if (!owns_heap(field_type)) {
                continue;
            }
            emit_drop(emit_field_get(value, field.name, field_type));
        }
        return;
    }
    if (list_needs_element_drop(value.type) && value.type.element != nullptr) {
        const IrValue n = emit_list_len(value);
        emit_counted_loop(n, [this, value](const IrValue& index) {
            emit_drop_heap_contents(emit_list_get(value, index, *value.type.element));
        });
    }
}

void Lowerer::emit_drop(const IrValue& value) {
    emit_drop_heap_contents(value);
    if (value.type.kind == IrTypeKind::Record) {
        return;
    }
    if (!ir_is_heap_pointer(value.type)) {
        return;
    }
    IrInst inst;
    inst.op = IrOp::Drop;
    inst.args.push_back(value);
    emit(std::move(inst));
}

void Lowerer::consume_heap_expr(const Expr* expr) {
    if (const auto* id = dynamic_cast<const IdentifierExpr*>(expr)) {
        owned_.erase(id->name);
        return;
    }
    const auto* field = dynamic_cast<const FieldExpr*>(expr);
    if (field == nullptr || field->field != "value" || field->object == nullptr) {
        return;
    }
    const auto found = semantic_.expression_types.find(field->object.get());
    if (found == semantic_.expression_types.end() || found->second.kind != TypeKind::Result) {
        return;
    }
    const IrValue object = lower_expression(field->object.get());
    write_place(field->object.get(), emit_field_set(object, field->field, emit_zero(type_of(expr))));
}

bool Lowerer::owns_heap(const IrType& type) const {
    if (ir_is_heap_pointer(type)) {
        return true;
    }
    if (type.kind != IrTypeKind::Record) {
        return false;
    }
    const RecordInfo* record = semantic_record(type.record_id);
    if (record == nullptr) {
        return false;
    }
    for (const auto& field : record->fields) {
        if (owns_heap(ir_type_from(field.type))) {
            return true;
        }
    }
    return false;
}

const RecordInfo* Lowerer::semantic_record(int record_id) const {
    for (const auto& record : semantic_.records) {
        if (record.id == record_id) {
            return &record;
        }
    }
    return nullptr;
}

IrType Lowerer::field_ir_type(int record_id, const std::string& field) const {
    const RecordInfo* record = semantic_record(record_id);
    if (record == nullptr) {
        return ir_void();
    }
    for (const auto& candidate : record->fields) {
        if (candidate.name == field) {
            return ir_type_from(candidate.type);
        }
    }
    return ir_void();
}

void Lowerer::emit_named_drops(const std::vector<std::string>& names) {
    for (const auto& name : names) {
        emit_drop(emit_load(name));
    }
}

void Lowerer::emit_owned_drops() {
    std::vector<std::string> names(owned_.begin(), owned_.end());
    std::sort(names.begin(), names.end());
    emit_named_drops(names);
}

void Lowerer::emit_path_exit() {
    if (current().terminated) {
        return;
    }
    if (seq_footer_) {
        seq_footer_();
        return;
    }
    emit_owned_drops();
    finish_return(default_return_value());
}

IrValue Lowerer::lower_addr(const Expr* expr) {
    IrInst inst;
    inst.op = IrOp::Addr;
    if (const auto* id = dynamic_cast<const IdentifierExpr*>(expr)) {
        inst.name = rewrite_name(id->name);
        inst.dest = make_temp(type_of_name(inst.name));
        emit(inst);
        return inst.dest;
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        if (const auto* object = dynamic_cast<const IdentifierExpr*>(field->object.get())) {
            inst.name = rewrite_name(object->name);
            inst.field_index = field_index_of(type_of_name(inst.name).record_id, field->field);
            inst.dest = make_temp(type_of(expr));
            emit(inst);
            return inst.dest;
        }
    }
    return lower_expression(expr);
}

void Lowerer::note_user_call_moves(const CallExpr& expr) {
    const auto found = semantic_.call_ir_names.find(&expr);
    if (found == semantic_.call_ir_names.end() || is_stdlib_runtime_ir(found->second)) {
        return;
    }
    const auto refs_found = semantic_.parameter_refs.find(found->second);
    for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
        const bool is_ref =
            refs_found != semantic_.parameter_refs.end() && i < refs_found->second.size() && refs_found->second[i];
        const bool recv_borrow = found->second == "std$sync$recv" && i == 0;
        const bool net_borrow = i == 0 && (found->second == "std$net$accept" || found->second == "std$net$read" ||
                                           found->second == "std$net$write");
        if (is_ref || recv_borrow || net_borrow) {
            continue;
        }
        consume_heap_expr(expr.arguments[i].get());
    }
}

bool expr_allocates_ignore_tasks(const Expr* expr) {
    if (expr == nullptr) {
        return false;
    }
    if (dynamic_cast<const StringExpr*>(expr) != nullptr || dynamic_cast<const ListExpr*>(expr) != nullptr) {
        return true;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (call_name_allocates(call->qualifier, call->callee)) {
            return true;
        }
        for (const auto& argument : call->arguments) {
            if (expr_allocates_ignore_tasks(argument.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return expr_allocates_ignore_tasks(unary->operand.get());
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return expr_allocates_ignore_tasks(binary->left.get()) || expr_allocates_ignore_tasks(binary->right.get());
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return expr_allocates_ignore_tasks(index->collection.get()) || expr_allocates_ignore_tasks(index->index.get());
    }
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return expr_allocates_ignore_tasks(field->object.get());
    }
    if (const auto* start = dynamic_cast<const StartExpr*>(expr)) {
        for (const auto& argument : start->arguments) {
            if (expr_allocates_ignore_tasks(argument.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* wait = dynamic_cast<const WaitExpr*>(expr)) {
        return expr_allocates_ignore_tasks(wait->task.get());
    }
    return false;
}

bool stmts_allocate_ignore_tasks(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& stmt : stmts) {
        if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(index_assign->index.get()) ||
                expr_allocates_ignore_tasks(index_assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(field_assign->object.get()) ||
                expr_allocates_ignore_tasks(field_assign->value.get())) {
                return true;
            }
            continue;
        }
        if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(expr_stmt->expr.get())) {
                return true;
            }
            continue;
        }
        if (const auto* iff = dynamic_cast<const IfStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(iff->condition.get()) || stmts_allocate_ignore_tasks(iff->then_branch) ||
                stmts_allocate_ignore_tasks(iff->otherwise_branch)) {
                return true;
            }
            continue;
        }
        if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt.get())) {
            if (expr_allocates_ignore_tasks(repeat->start.get()) || expr_allocates_ignore_tasks(repeat->end.get()) ||
                stmts_allocate_ignore_tasks(repeat->body)) {
                return true;
            }
        }
    }
    return false;
}

bool would_parallelize_loop(const RepeatStmt& stmt, const FunctionDecl& function, bool nested) {
    std::map<std::string, std::int64_t> env;
    std::map<std::string, std::int64_t> list_len;
    collect_const_env(function.body, &stmt, env, list_len);
    if (!worth_parallelizing(stmt, nested, env, list_len)) {
        return false;
    }
    std::set<std::string> assigned;
    std::set<std::string> reads;
    std::set<std::string> inner_iterators;
    bool ok = true;
    std::string effect_reason;
    collect_loop_effects(stmt.body, stmt.iterator, assigned, reads, inner_iterators, ok, effect_reason, true);
    if (!ok || stmts_allocate_ignore_tasks(stmt.body)) {
        return false;
    }
    auto candidate = classify_simple_reduction(stmt.body);
    if (candidate && reduction_used_unsafely(stmt.body, candidate->name)) {
        candidate.reset();
    }
    for (const auto& name : assigned) {
        if (candidate && name == candidate->name) {
            continue;
        }
        bool found = false;
        if (scan_used_after(function.body, &stmt, name, found)) {
            return false;
        }
        bool seen = false;
        const bool read_first = first_use_is_read(stmt.body, name, seen);
        bool before_found = false;
        const bool existed = assigned_before_loop(function.body, &stmt, name, before_found);
        for (const auto& param : function.parameters) {
            if (param == name && read_first) {
                return false;
            }
        }
        if (existed && read_first) {
            return false;
        }
    }
    return true;
}

IrValue Lowerer::default_return_value() const {
    IrValue value;
    value.type = function_ != nullptr ? function_->return_type : ir_void();
    if (value.type.kind == IrTypeKind::I64) {
        value.kind = IrValue::Kind::ConstI64;
    } else if (value.type.kind == IrTypeKind::F64) {
        value.kind = IrValue::Kind::ConstF64;
    } else if (value.type.kind == IrTypeKind::Bool) {
        value.kind = IrValue::Kind::ConstBool;
    } else if (value.type.kind == IrTypeKind::Record) {
        value.kind = IrValue::Kind::Undef;
    } else {
        value.kind = IrValue::Kind::Undef;
    }
    return value;
}

}  // namespace

bool would_parallelize(const RepeatStmt& stmt, const FunctionDecl& function, bool nested) {
    return would_parallelize_loop(stmt, function, nested);
}

IrModule lower_to_ir(const Program& program, const SemanticInfo& semantic, const std::string& source_path) {
    Lowerer lowerer(program, semantic, source_path);
    return lowerer.run();
}

IrModule lower_to_ir(const ModuleGraph& graph, const SemanticInfo& semantic) {
    Lowerer lowerer(graph, semantic);
    return lowerer.run();
}

}  // namespace yukt

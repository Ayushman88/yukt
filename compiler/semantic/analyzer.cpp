#include "semantic/analyzer.hpp"

#include "lexer/token.hpp"

#include <algorithm>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <utility>

namespace yukt {
namespace {

struct Symbol {
    Type type;
    SourceLocation location;
    bool assigned = false;
    bool iterator = false;
};

struct LocalBinding {
    std::string name;
    Type type;
    SourceLocation location;
};

struct FunctionSymbol {
    const FunctionDecl* decl = nullptr;
    int module_index = 0;
    std::string ir_name;
    std::vector<Type> parameters;
    Type return_type = void_type();
    bool saw_give = false;
    std::vector<LocalBinding> locals;
};

enum class ConstraintKind {
    Numeric,
    Integer,
    Plus,
    Comparable,
};

struct Constraint {
    Type type;
    ConstraintKind kind = ConstraintKind::Numeric;
    SourceLocation location;
    std::string operator_text;
};

struct NonVoidUse {
    Type type;
    SourceLocation location;
    std::string detail;
    std::string suggestion;
};

class Analyzer {
public:
    Analyzer(const Program& program, DiagnosticEngine& diagnostics);
    Analyzer(const ModuleGraph& graph, DiagnosticEngine& diagnostics);

    SemanticInfo run();

private:
    void collect_functions();
    void collect_aliases();
    void analyze_functions();
    std::string ir_name_for(int module_index, const FunctionDecl& function) const;
    const Program* module_program(int index) const;
    FunctionSymbol* lookup_shared(int module_index, const std::string& name);
    void check_constraints();
    void finalize_functions();
    void check_non_void_uses();
    void require_main();
    void require_non_void(Type type, SourceLocation location, std::string detail, std::string suggestion);
    void add_constraint(Type type, ConstraintKind kind, SourceLocation location, std::string operator_text);
    void analyze_function(FunctionSymbol& function);
    void analyze_statement(const Stmt* stmt);
    void analyze_assignment(const AssignStmt& stmt);
    void analyze_index_assign(const IndexAssignStmt& stmt);
    void analyze_say(const SayStmt& stmt);
    void analyze_give(const GiveStmt& stmt);
    void analyze_expr_stmt(const ExprStmt& stmt);
    void analyze_if(const IfStmt& stmt);
    void analyze_repeat(const RepeatStmt& stmt);
    Type analyze_expression(const Expr* expr);
    Type analyze_expression_body(const Expr* expr);
    Type analyze_unary(const UnaryExpr& expr);
    Type analyze_binary(const BinaryExpr& expr);
    Type analyze_call(const CallExpr& expr, bool result_used);
    Type analyze_list(const ListExpr& expr);

    bool declare_local(const std::string& name, Type type, SourceLocation location, bool iterator);
    Symbol* lookup_local(const std::string& name);
    FunctionSymbol* lookup_function(const std::string& name);

    bool expect_type(Type actual, Type expected, SourceLocation location, std::string title, std::string detail,
                     std::string suggestion);
    void error(SourceLocation location, std::string title, std::string detail, std::string suggestion);
    void error(SourceLocation location, std::string title, std::string detail, std::string suggestion,
               std::vector<DiagnosticNote> notes);

    void push_scope();
    void pop_scope();

    const Program& program_;
    const ModuleGraph* graph_ = nullptr;
    DiagnosticEngine& diagnostics_;
    TypeContext types_;
    std::unordered_map<std::string, std::unique_ptr<FunctionSymbol>> functions_;
    std::vector<std::unordered_map<std::string, FunctionSymbol*>> module_lookup_;
    std::vector<std::unordered_map<std::string, int>> aliases_;
    std::unordered_map<const FunctionDecl*, std::string> function_ir_names_;
    std::unordered_map<const CallExpr*, std::string> call_ir_names_;
    int current_module_ = 0;
    bool single_file_ = true;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    std::vector<Constraint> constraints_;
    std::vector<NonVoidUse> non_void_uses_;
    std::unordered_map<const Expr*, Type> expr_types_;
    FunctionSymbol* current_function_ = nullptr;
};

bool is_numeric(Type type) {
    return type.kind == TypeKind::Integer || type.kind == TypeKind::Float;
}

bool is_comparison(TokenKind op) {
    return op == TokenKind::Less || op == TokenKind::Greater || op == TokenKind::LessEqual ||
           op == TokenKind::GreaterEqual;
}

bool is_equality(TokenKind op) {
    return op == TokenKind::EqualEqual || op == TokenKind::BangEqual;
}

bool is_arithmetic(TokenKind op) {
    return op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star || op == TokenKind::Slash ||
           op == TokenKind::Percent;
}

Analyzer::Analyzer(const Program& program, DiagnosticEngine& diagnostics)
    : program_(program), diagnostics_(diagnostics), single_file_(true) {
    module_lookup_.emplace_back();
    aliases_.emplace_back();
}

Analyzer::Analyzer(const ModuleGraph& graph, DiagnosticEngine& diagnostics)
    : program_(*graph.modules.at(0).program), graph_(&graph), diagnostics_(diagnostics),
      single_file_(graph.modules.size() <= 1 && !graph.has_uses()) {
    module_lookup_.resize(graph.modules.size());
    aliases_.resize(graph.modules.size());
}

SemanticInfo Analyzer::run() {
    collect_functions();
    collect_aliases();
    analyze_functions();
    check_constraints();
    finalize_functions();
    check_non_void_uses();
    require_main();

    SemanticInfo info;
    info.function_ir_names = function_ir_names_;
    info.call_ir_names = call_ir_names_;
    const int module_count = graph_ == nullptr ? 1 : static_cast<int>(graph_->modules.size());
    for (int module_index = 0; module_index < module_count; ++module_index) {
        const Program* program = module_program(module_index);
        if (program == nullptr) {
            continue;
        }
        for (const auto& function : program->functions) {
            if (function == nullptr) {
                continue;
            }
            const std::string ir_name = ir_name_for(module_index, *function);
            auto found = functions_.find(ir_name);
            if (found == functions_.end() || found->second == nullptr) {
                continue;
            }
            const FunctionSymbol& symbol = *found->second;
            InferredFunction inferred;
            inferred.name = function->name;
            inferred.ir_name = ir_name;
            inferred.return_type = types_.to_string(symbol.return_type);
            for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                inferred.parameters.push_back(InferredBinding{
                    function->parameters[i],
                    types_.to_string(symbol.parameters[i]),
                });
            }
            for (const auto& local : symbol.locals) {
                inferred.locals.push_back(InferredBinding{local.name, types_.to_string(local.type)});
            }
            std::sort(inferred.locals.begin(), inferred.locals.end(),
                      [](const InferredBinding& left, const InferredBinding& right) { return left.name < right.name; });
            info.functions.push_back(std::move(inferred));
            info.return_types[ir_name] = types_.prune(symbol.return_type);
            info.parameter_types[ir_name] = symbol.parameters;
            for (auto& parameter : info.parameter_types[ir_name]) {
                parameter = types_.prune(parameter);
            }
            std::unordered_map<std::string, Type> locals;
            for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                locals[function->parameters[i]] = info.parameter_types[ir_name][i];
            }
            for (const auto& local : symbol.locals) {
                locals[local.name] = types_.prune(local.type);
            }
            info.local_types[ir_name] = std::move(locals);
        }
    }
    for (const auto& [expr, type] : expr_types_) {
        info.expression_types[expr] = types_.prune(type);
    }
    return info;
}

const Program* Analyzer::module_program(int index) const {
    if (graph_ == nullptr) {
        return index == 0 ? &program_ : nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= graph_->modules.size()) {
        return nullptr;
    }
    return graph_->modules[static_cast<std::size_t>(index)].program.get();
}

std::string Analyzer::ir_name_for(int module_index, const FunctionDecl& function) const {
    const bool entry_main = module_index == 0 && function.name == "main";
    std::string prefix;
    if (graph_ != nullptr && static_cast<std::size_t>(module_index) < graph_->modules.size()) {
        prefix = graph_->modules[static_cast<std::size_t>(module_index)].mangle_prefix;
    }
    return mangle_function(prefix, function.name, entry_main, single_file_);
}

void Analyzer::collect_functions() {
    const int module_count = graph_ == nullptr ? 1 : static_cast<int>(graph_->modules.size());
    for (int module_index = 0; module_index < module_count; ++module_index) {
        const Program* program = module_program(module_index);
        if (program == nullptr) {
            continue;
        }
        const bool is_entry = module_index == 0;
        for (const auto& function : program->functions) {
            if (function == nullptr) {
                continue;
            }
            if (function->shared && function->name == "main") {
                error(function->name_location, "Invalid entry point",
                      "`create main` cannot be `share`.",
                      "Remove `share` from `create main`.");
            }
            if (!is_entry && function->name == "main") {
                error(function->name_location, "Invalid entry point",
                      "Only the entry file may define `create main`.",
                      "Move `main` to the file passed to `yukt build`, or rename this function.");
            }
            if (module_lookup_[static_cast<std::size_t>(module_index)].contains(function->name)) {
                error(function->name_location, "Duplicate function",
                      "A function named `" + function->name + "` is already defined.",
                      "Give this function a different name.");
                continue;
            }
            const std::string ir_name = ir_name_for(module_index, *function);
            if (functions_.contains(ir_name)) {
                error(function->name_location, "Duplicate function",
                      "Mangled name `" + ir_name + "` collides with another function.",
                      "Rename the function or the module path.");
                continue;
            }
            auto symbol = std::make_unique<FunctionSymbol>();
            symbol->decl = function.get();
            symbol->module_index = module_index;
            symbol->ir_name = ir_name;
            symbol->return_type = types_.fresh_variable();
            for (const auto& parameter : function->parameters) {
                symbol->parameters.push_back(types_.fresh_variable());
                (void)parameter;
            }
            FunctionSymbol* stored = symbol.get();
            functions_.emplace(ir_name, std::move(symbol));
            module_lookup_[static_cast<std::size_t>(module_index)].emplace(function->name, stored);
            function_ir_names_[function.get()] = ir_name;
        }
    }
}

void Analyzer::collect_aliases() {
    const int module_count = graph_ == nullptr ? 1 : static_cast<int>(graph_->modules.size());
    for (int module_index = 0; module_index < module_count; ++module_index) {
        const Program* program = module_program(module_index);
        if (program == nullptr) {
            continue;
        }
        const std::vector<int>* import_modules = nullptr;
        if (graph_ != nullptr) {
            import_modules = &graph_->modules[static_cast<std::size_t>(module_index)].import_modules;
        }
        for (std::size_t i = 0; i < program->uses.size(); ++i) {
            const UseDecl& use = program->uses[i];
            if (use.alias == "count" || use.alias == "zeros") {
                error(use.alias_location, "Name collision",
                      "Alias `" + use.alias + "` collides with a compiler builtin.",
                      "Choose a different alias with `as`.");
                continue;
            }
            if (module_lookup_[static_cast<std::size_t>(module_index)].contains(use.alias)) {
                error(use.alias_location, "Name collision",
                      "Alias `" + use.alias + "` collides with a function in this file.",
                      "Choose a different alias with `as`.");
                continue;
            }
            if (aliases_[static_cast<std::size_t>(module_index)].contains(use.alias)) {
                error(use.alias_location, "Duplicate alias",
                      "Alias `" + use.alias + "` is already used in this file.",
                      "Use `as` to give this import a different name.");
                continue;
            }
            int target = -1;
            if (import_modules != nullptr && i < import_modules->size()) {
                target = (*import_modules)[i];
            }
            if (target < 0) {
                continue;
            }
            aliases_[static_cast<std::size_t>(module_index)].emplace(use.alias, target);
        }
    }
}

void Analyzer::analyze_functions() {
    const int module_count = graph_ == nullptr ? 1 : static_cast<int>(graph_->modules.size());
    for (int module_index = 0; module_index < module_count; ++module_index) {
        current_module_ = module_index;
        const Program* program = module_program(module_index);
        if (program == nullptr) {
            continue;
        }
        for (const auto& function : program->functions) {
            if (function == nullptr) {
                continue;
            }
            const std::string ir_name = ir_name_for(module_index, *function);
            auto found = functions_.find(ir_name);
            if (found == functions_.end() || found->second == nullptr) {
                continue;
            }
            analyze_function(*found->second);
        }
    }
    current_module_ = 0;
}

FunctionSymbol* Analyzer::lookup_shared(int module_index, const std::string& name) {
    if (module_index < 0 || static_cast<std::size_t>(module_index) >= module_lookup_.size()) {
        return nullptr;
    }
    auto found = module_lookup_[static_cast<std::size_t>(module_index)].find(name);
    if (found == module_lookup_[static_cast<std::size_t>(module_index)].end()) {
        return nullptr;
    }
    return found->second;
}

void Analyzer::check_constraints() {
    for (const auto& constraint : constraints_) {
        const Type resolved = types_.prune(constraint.type);
        switch (constraint.kind) {
            case ConstraintKind::Plus:
                if (resolved.kind == TypeKind::String || is_numeric(resolved)) {
                    break;
                }
                if (resolved.kind == TypeKind::Variable) {
                    error(constraint.location, "Cannot infer type",
                          "The operands of `+` never received a concrete type.",
                          "Use the result in a way that makes the type clear, or add a typed literal.");
                    break;
                }
                error(constraint.location, "Type mismatch",
                      "`+` concatenates strings or adds numbers, not " + types_.to_string(resolved) + ".",
                      "Use two strings or two values of the same numeric type.");
                break;
            case ConstraintKind::Numeric:
                if (is_numeric(resolved)) {
                    break;
                }
                if (resolved.kind == TypeKind::Variable) {
                    error(constraint.location, "Cannot infer type",
                          "The operands of `" + constraint.operator_text + "` never received a numeric type.",
                          "Use integers or floats with this operator.");
                    break;
                }
                error(constraint.location, "Type mismatch",
                      "`" + constraint.operator_text + "` requires integers or floats, not " +
                          types_.to_string(resolved) + ".",
                      "Use numeric operands.");
                break;
            case ConstraintKind::Integer:
                if (resolved.kind == TypeKind::Integer) {
                    break;
                }
                error(constraint.location, "Type mismatch",
                      "`" + constraint.operator_text + "` requires integers, not " + types_.to_string(resolved) + ".",
                      "Use integer operands.");
                break;
            case ConstraintKind::Comparable:
                if (is_numeric(resolved)) {
                    break;
                }
                if (resolved.kind == TypeKind::Variable) {
                    error(constraint.location, "Cannot infer type",
                          "This comparison never received a numeric type.",
                          "Compare integers or floats.");
                    break;
                }
                error(constraint.location, "Type mismatch",
                      "Comparisons require integers or floats, not " + types_.to_string(resolved) + ".",
                      "Compare numbers, for example `age > 18`.");
                break;
        }
    }
}

void Analyzer::finalize_functions() {
    for (auto& [name, function] : functions_) {
        if (function == nullptr) {
            continue;
        }
        if (!types_.is_resolved(function->return_type)) {
            if (function->saw_give) {
                error(function->decl != nullptr ? function->decl->name_location : SourceLocation{},
                      "Cannot infer type",
                      "The return type of `" + name + "` could not be inferred from its `give` statements.",
                      "Give a value with a known type, such as an integer or string.");
            } else if (!types_.unify(function->return_type, void_type())) {
                error(function->decl != nullptr ? function->decl->name_location : SourceLocation{},
                      "Type mismatch",
                      "Function `" + name + "` has no `give` statement, so it must return void.",
                      "Remove `give` or return a value from every path you intend to use.");
            }
        }

        if (function->decl == nullptr) {
            continue;
        }
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            if (!types_.is_resolved(function->parameters[i])) {
                error(function->decl->name_location, "Cannot infer type",
                      "The type of parameter `" + function->decl->parameters[i] + "` in `" + name +
                          "` could not be inferred.",
                      "Use the parameter in the function body, or pass a value at a call site.");
            }
        }
        for (const auto& local : function->locals) {
            if (!types_.is_resolved(local.type)) {
                error(local.location, "Cannot infer type",
                      "The type of `" + local.name + "` could not be inferred.",
                      "Initialize it with a concrete value, such as `0`, `\"\"`, or `[1]`.");
            }
        }
    }
}

void Analyzer::require_main() {
    const auto found = functions_.find("main");
    if (found == functions_.end() || found->second == nullptr) {
        error(SourceLocation{1, 1}, "Missing entry point",
              "Every YUKT program must define `create main`.",
              "Add `create main:` and put the program's starting statements there.",
              {{SourceLocation{1, 1}, "YUKT programs start at `create main`."}});
        return;
    }
    if (!found->second->parameters.empty()) {
        error(found->second->decl != nullptr ? found->second->decl->name_location : SourceLocation{},
              "Invalid entry point", "`create main` cannot take parameters.",
              "Write `create main:` or `create main():`.");
    }
}

void Analyzer::analyze_function(FunctionSymbol& function) {
    current_function_ = &function;
    scopes_.clear();
    push_scope();

    if (function.decl != nullptr) {
        std::unordered_map<std::string, SourceLocation> seen;
        for (std::size_t i = 0; i < function.decl->parameters.size(); ++i) {
            const auto& name = function.decl->parameters[i];
            if (lookup_function(name) != nullptr) {
                error(function.decl->name_location, "Name collision",
                      "Parameter `" + name + "` has the same name as a function.",
                      "Rename the parameter.");
            }
            if (seen.contains(name)) {
                error(function.decl->name_location, "Duplicate parameter",
                      "Parameter `" + name + "` is declared more than once.",
                      "Give each parameter a unique name.");
                continue;
            }
            seen.emplace(name, function.decl->name_location);
            declare_local(name, function.parameters[i], function.decl->name_location, false);
        }

        for (const auto& stmt : function.decl->body) {
            analyze_statement(stmt.get());
        }

        if (!scopes_.empty()) {
            for (const auto& [name, symbol] : scopes_.front()) {
                bool parameter = false;
                for (const auto& parameter_name : function.decl->parameters) {
                    if (parameter_name == name) {
                        parameter = true;
                        break;
                    }
                }
                if (!parameter) {
                    function.locals.push_back(LocalBinding{name, symbol.type, symbol.location});
                }
            }
        }
    }

    pop_scope();
    current_function_ = nullptr;
}

void Analyzer::analyze_statement(const Stmt* stmt) {
    if (stmt == nullptr) {
        return;
    }
    if (const auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        analyze_assignment(*assign);
        return;
    }
    if (const auto* index_assign = dynamic_cast<const IndexAssignStmt*>(stmt)) {
        analyze_index_assign(*index_assign);
        return;
    }
    if (const auto* say = dynamic_cast<const SayStmt*>(stmt)) {
        analyze_say(*say);
        return;
    }
    if (const auto* give = dynamic_cast<const GiveStmt*>(stmt)) {
        analyze_give(*give);
        return;
    }
    if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        analyze_expr_stmt(*expr_stmt);
        return;
    }
    if (const auto* iff = dynamic_cast<const IfStmt*>(stmt)) {
        analyze_if(*iff);
        return;
    }
    if (const auto* repeat = dynamic_cast<const RepeatStmt*>(stmt)) {
        analyze_repeat(*repeat);
        return;
    }
}

void Analyzer::analyze_assignment(const AssignStmt& stmt) {
    if (lookup_function(stmt.name) != nullptr) {
        error(stmt.name_location, "Name collision",
              "`" + stmt.name + "` is a function and cannot be assigned.",
              "Use a different variable name.");
        analyze_expression(stmt.value.get());
        return;
    }

    const Type value_type = analyze_expression(stmt.value.get());
    require_non_void(value_type, stmt.name_location, "Cannot assign a void value to `" + stmt.name + "`.",
                     "Call a function that gives a value, or assign a literal.");
    if (types_.prune(value_type).kind == TypeKind::Void) {
        return;
    }
    if (auto* existing = lookup_local(stmt.name)) {
        if (existing->iterator) {
            error(stmt.name_location, "Invalid assignment",
                  "Loop variable `" + stmt.name + "` cannot be reassigned.",
                  "Choose a different name for this value.");
            return;
        }
        if (!types_.unify(value_type, existing->type)) {
            error(stmt.name_location, "Type mismatch",
                  "`" + stmt.name + "` has type " + types_.to_string(existing->type) + " but this assignment has type " +
                      types_.to_string(value_type) + ".",
                  "Assign a value of type " + types_.to_string(existing->type) + ".",
                  {{existing->location, "first assigned here as " + types_.to_string(existing->type)}});
        }
        existing->assigned = true;
        return;
    }

    declare_local(stmt.name, value_type, stmt.name_location, false);
}

void Analyzer::analyze_index_assign(const IndexAssignStmt& stmt) {
    const Type collection = analyze_expression(stmt.collection.get());
    const Type index = analyze_expression(stmt.index.get());
    const Type value = analyze_expression(stmt.value.get());
    expect_type(index, integer_type(), stmt.span.start, "Type mismatch",
                "List indexes must be integers, not " + types_.to_string(index) + ".",
                "Use an integer index, for example `items[0]`.");
    const Type resolved = types_.prune(collection);
    if (resolved.kind != TypeKind::List && resolved.kind != TypeKind::Variable) {
        error(stmt.span.start, "Type mismatch",
              "Only lists can be indexed. Found " + types_.to_string(collection) + ".",
              "Index a list, for example `items[i] = value`.");
        return;
    }
    Type element = resolved.kind == TypeKind::List && resolved.element != nullptr ? *resolved.element
                                                                                : types_.fresh_variable();
    if (!types_.unify(collection, list_type(element)) || !types_.unify(value, element)) {
        error(stmt.span.start, "Type mismatch",
              "Cannot store " + types_.to_string(value) + " in a list of " + types_.to_string(element) + ".",
              "Store a value that matches the list element type.");
    }
}

void Analyzer::analyze_say(const SayStmt& stmt) {
    const Type type = analyze_expression(stmt.value.get());
    require_non_void(type, stmt.span.start, "`say` cannot print a void value.",
                     "Call a function that gives a value, or print a literal.");
}

void Analyzer::analyze_give(const GiveStmt& stmt) {
    const Type type = analyze_expression(stmt.value.get());
    if (current_function_ == nullptr) {
        return;
    }
    current_function_->saw_give = true;
    require_non_void(type, stmt.span.start, "`give` needs a value, but this expression is void.",
                     "Give a number, string, boolean, or list.");
    if (types_.prune(type).kind == TypeKind::Void) {
        return;
    }
    if (!types_.unify(current_function_->return_type, type)) {
        error(stmt.span.start, "Type mismatch",
              "This `give` has type " + types_.to_string(type) + ", but `" + current_function_->decl->name +
                  "` already gives " + types_.to_string(current_function_->return_type) + ".",
              "Every `give` in a function must have the same type.");
    }
}

void Analyzer::analyze_expr_stmt(const ExprStmt& stmt) {
    if (const auto* call = dynamic_cast<const CallExpr*>(stmt.expr.get())) {
        analyze_call(*call, false);
        return;
    }
    analyze_expression(stmt.expr.get());
}

void Analyzer::analyze_if(const IfStmt& stmt) {
    const Type condition = analyze_expression(stmt.condition.get());
    expect_type(condition, boolean_type(), stmt.span.start, "Type mismatch",
                "`if` conditions must be boolean, but this condition has type " + types_.to_string(condition) + ".",
                "Use a comparison such as `age > 18`.");

    for (const auto& then_stmt : stmt.then_branch) {
        analyze_statement(then_stmt.get());
    }
    for (const auto& otherwise_stmt : stmt.otherwise_branch) {
        analyze_statement(otherwise_stmt.get());
    }
}

void Analyzer::analyze_repeat(const RepeatStmt& stmt) {
    const Type start = analyze_expression(stmt.start.get());
    const Type end = analyze_expression(stmt.end.get());
    expect_type(start, integer_type(), stmt.span.start, "Type mismatch",
                "The start of a `repeat` range must be an integer, not " + types_.to_string(start) + ".",
                "Write a range such as `1..10`.");
    expect_type(end, integer_type(), stmt.span.start, "Type mismatch",
                "The end of a `repeat` range must be an integer, not " + types_.to_string(end) + ".",
                "Write a range such as `1..10`.");

    if (lookup_function(stmt.iterator) != nullptr) {
        error(stmt.iterator_location, "Name collision",
              "`" + stmt.iterator + "` is a function and cannot be a loop variable.",
              "Choose a different loop variable name.");
    }

    push_scope();
    if (!declare_local(stmt.iterator, integer_type(), stmt.iterator_location, true)) {
        // declare_local already reported a collision with an existing name.
    }
    for (const auto& body_stmt : stmt.body) {
        analyze_statement(body_stmt.get());
    }
    pop_scope();
}

Type Analyzer::analyze_expression(const Expr* expr) {
    const Type result = analyze_expression_body(expr);
    if (expr != nullptr) {
        expr_types_[expr] = result;
    }
    return result;
}

Type Analyzer::analyze_expression_body(const Expr* expr) {
    if (expr == nullptr) {
        return types_.fresh_variable();
    }
    if (dynamic_cast<const IntegerExpr*>(expr) != nullptr) {
        return integer_type();
    }
    if (dynamic_cast<const FloatExpr*>(expr) != nullptr) {
        return float_type();
    }
    if (dynamic_cast<const StringExpr*>(expr) != nullptr) {
        return string_type();
    }
    if (dynamic_cast<const BoolExpr*>(expr) != nullptr) {
        return boolean_type();
    }
    if (const auto* name = dynamic_cast<const IdentifierExpr*>(expr)) {
        if (auto* symbol = lookup_local(name->name)) {
            if (!symbol->assigned) {
                error(name->span.start, "Use before assignment",
                      "`" + name->name + "` is used before it is assigned.",
                      "Assign `" + name->name + "` before using it.");
            }
            return symbol->type;
        }
        if (lookup_function(name->name) != nullptr) {
            error(name->span.start, "Invalid expression",
                  "`" + name->name + "` is a function. Call it with `()`.",
                  "Write `" + name->name + "(...)`.");
            return types_.fresh_variable();
        }
        error(name->span.start, "Unknown name",
              "`" + name->name + "` is not defined in this scope.",
              "Assign it before use, or check for a typo.");
        return types_.fresh_variable();
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        return analyze_unary(*unary);
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return analyze_binary(*binary);
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        return analyze_call(*call, true);
    }
    if (const auto* list = dynamic_cast<const ListExpr*>(expr)) {
        return analyze_list(*list);
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        const Type collection = analyze_expression(index->collection.get());
        const Type index_type = analyze_expression(index->index.get());
        expect_type(index_type, integer_type(), index->span.start, "Type mismatch",
                    "List indexes must be integers, not " + types_.to_string(index_type) + ".",
                    "Use an integer index, for example `items[0]`.");
        const Type resolved = types_.prune(collection);
        if (resolved.kind == TypeKind::List && resolved.element != nullptr) {
            return *resolved.element;
        }
        Type element = types_.fresh_variable();
        if (!types_.unify(collection, list_type(element))) {
            error(index->span.start, "Type mismatch",
                  "Only lists can be indexed. Found " + types_.to_string(collection) + ".",
                  "Index a list, for example `items[i]`.");
        }
        return element;
    }
    return types_.fresh_variable();
}

Type Analyzer::analyze_unary(const UnaryExpr& expr) {
    const Type operand = analyze_expression(expr.operand.get());
    const Type resolved = types_.prune(operand);
    if (resolved.kind == TypeKind::Variable) {
        add_constraint(operand, ConstraintKind::Numeric, expr.span.start, "-");
        return operand;
    }
    if (!is_numeric(resolved)) {
        error(expr.span.start, "Type mismatch",
              "Unary `-` requires an integer or float, not " + types_.to_string(operand) + ".",
              "Negate a number, for example `-count`.");
        return integer_type();
    }
    return resolved;
}

Type Analyzer::analyze_binary(const BinaryExpr& expr) {
    const Type left = analyze_expression(expr.left.get());
    const Type right = analyze_expression(expr.right.get());

    if (expr.op == TokenKind::Plus) {
        const Type left_p = types_.prune(left);
        const Type right_p = types_.prune(right);
        if (left_p.kind == TypeKind::String || right_p.kind == TypeKind::String) {
            if (!types_.unify(left, string_type()) || !types_.unify(right, string_type())) {
                error(expr.span.start, "Type mismatch",
                      "`+` concatenates two strings, or adds two numbers. Found " + types_.to_string(left) + " and " +
                          types_.to_string(right) + ".",
                      "Use two strings or two values of the same numeric type.");
            }
            return string_type();
        }
    }

    if (is_arithmetic(expr.op)) {
        if (expr.op == TokenKind::Percent) {
            if (!types_.unify(left, integer_type()) || !types_.unify(right, integer_type())) {
                error(expr.span.start, "Type mismatch",
                      "`%` requires two integers, but found " + types_.to_string(left) + " and " +
                          types_.to_string(right) + ".",
                      "Use integer operands, for example `n % 2`.");
            }
            return integer_type();
        }

        if (!types_.unify(left, right)) {
            error(expr.span.start, "Type mismatch",
                  "Both sides of `" + std::string(token_kind_spelling(expr.op)) + "` must have the same type. Found " +
                      types_.to_string(left) + " and " + types_.to_string(right) + ".",
                  "YUKT does not convert integers to floats automatically.");
            return left;
        }
        const Type resolved = types_.prune(left);
        if (expr.op == TokenKind::Plus) {
            add_constraint(resolved, ConstraintKind::Plus, expr.span.start, "+");
        } else {
            add_constraint(resolved, ConstraintKind::Numeric, expr.span.start,
                           std::string(token_kind_spelling(expr.op)));
        }
        return resolved;
    }

    if (is_comparison(expr.op)) {
        if (!types_.unify(left, right)) {
            error(expr.span.start, "Type mismatch",
                  "Both sides of `" + std::string(token_kind_spelling(expr.op)) + "` must have the same type. Found " +
                      types_.to_string(left) + " and " + types_.to_string(right) + ".",
                  "Compare values of the same type.");
            return boolean_type();
        }
        add_constraint(left, ConstraintKind::Comparable, expr.span.start, std::string(token_kind_spelling(expr.op)));
        return boolean_type();
    }

    if (is_equality(expr.op)) {
        if (!types_.unify(left, right)) {
            error(expr.span.start, "Type mismatch",
                  "Both sides of `" + std::string(token_kind_spelling(expr.op)) + "` must have the same type. Found " +
                      types_.to_string(left) + " and " + types_.to_string(right) + ".",
                  "Compare values of the same type.");
        }
        return boolean_type();
    }

    return types_.fresh_variable();
}

Type Analyzer::analyze_call(const CallExpr& expr, bool result_used) {
    if (!expr.qualifier.empty()) {
        const auto alias = aliases_[static_cast<std::size_t>(current_module_)].find(expr.qualifier);
        if (alias == aliases_[static_cast<std::size_t>(current_module_)].end()) {
            error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Unknown module",
                  "`" + expr.qualifier + "` is not an imported module.",
                  "Add `use " + expr.qualifier + "` or fix the alias.");
            for (const auto& argument : expr.arguments) {
                analyze_expression(argument.get());
            }
            return types_.fresh_variable();
        }
        FunctionSymbol* function = lookup_shared(alias->second, expr.callee);
        const Program* imported = module_program(alias->second);
        if (function == nullptr || function->decl == nullptr || !function->decl->shared) {
            std::string shared_list = "this module shares no functions";
            if (imported != nullptr) {
                std::string names;
                for (const auto& fn : imported->functions) {
                    if (fn != nullptr && fn->shared) {
                        if (!names.empty()) {
                            names += ", ";
                        }
                        names += fn->name;
                    }
                }
                if (!names.empty()) {
                    shared_list = "shared: " + names;
                }
            }
            error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Unknown function",
                  "`" + expr.qualifier + "." + expr.callee + "` is not a shared function.",
                  "Export it with `share create " + expr.callee + "`, or call a shared name.",
                  {{expr.dot_location, shared_list}});
            for (const auto& argument : expr.arguments) {
                analyze_expression(argument.get());
            }
            return types_.fresh_variable();
        }
        call_ir_names_[&expr] = function->ir_name;
        if (expr.arguments.size() != function->parameters.size()) {
            error(expr.callee_location, "Wrong number of arguments",
                  "`" + expr.callee + "` takes " + std::to_string(function->parameters.size()) + " argument" +
                      (function->parameters.size() == 1 ? "" : "s") + ", but this call passes " +
                      std::to_string(expr.arguments.size()) + ".",
                  "Pass exactly " + std::to_string(function->parameters.size()) + " argument" +
                      (function->parameters.size() == 1 ? "" : "s") + ".");
        }
        const std::size_t count = std::min(expr.arguments.size(), function->parameters.size());
        for (std::size_t i = 0; i < count; ++i) {
            const Type argument = analyze_expression(expr.arguments[i].get());
            if (!types_.unify(argument, function->parameters[i])) {
                const std::string param =
                    function->decl != nullptr && i < function->decl->parameters.size() ? function->decl->parameters[i]
                                                                                       : "argument";
                error(expr.arguments[i] != nullptr ? expr.arguments[i]->span.start : expr.callee_location,
                      "Type mismatch",
                      "Argument `" + param + "` of `" + expr.callee + "` has type " +
                          types_.to_string(function->parameters[i]) + ", but this call passes " +
                          types_.to_string(argument) + ".",
                      "Pass a value of type " + types_.to_string(function->parameters[i]) + ".");
            }
        }
        for (std::size_t i = count; i < expr.arguments.size(); ++i) {
            analyze_expression(expr.arguments[i].get());
        }
        const Type result = function->return_type;
        if (result_used) {
            require_non_void(result, expr.callee_location,
                             "`" + expr.callee + "` does not give a value, so it cannot be used in an expression.",
                             "Call `" + expr.callee + "(...)` as a statement, or add a `give` to the function.");
        }
        return result;
    }

    if (expr.callee == "count" && lookup_function("count") == nullptr) {
        if (expr.arguments.size() != 1) {
            error(expr.callee_location, "Wrong number of arguments", "`count` takes one list.",
                  "Write `count(items)`.",
                  {{expr.callee_location, "`count` is a compiler builtin, not a user function."}});
        } else {
            const Type argument = analyze_expression(expr.arguments[0].get());
            const Type resolved = types_.prune(argument);
            if (resolved.kind != TypeKind::List && resolved.kind != TypeKind::Variable) {
                error(expr.callee_location, "Type mismatch",
                      "`count` expects a list, not " + types_.to_string(argument) + ".",
                      "Pass a list, for example `count(items)`.");
            }
        }
        return integer_type();
    }
    if (expr.callee == "zeros" && lookup_function("zeros") == nullptr) {
        if (expr.arguments.size() != 1) {
            error(expr.callee_location, "Wrong number of arguments", "`zeros` takes one integer length.",
                  "Write `zeros(n)`.",
                  {{expr.callee_location, "`zeros` is a compiler builtin, not a user function."}});
        } else {
            expect_type(analyze_expression(expr.arguments[0].get()), integer_type(), expr.callee_location,
                        "Type mismatch", "`zeros` expects an integer length.", "Write `zeros(256)`.");
        }
        return list_type(integer_type());
    }

    auto* function = lookup_function(expr.callee);
    if (function == nullptr) {
        error(expr.callee_location, "Unknown function",
              "There is no function named `" + expr.callee + "`.",
              "Define it with `create " + expr.callee + "(...):` or fix the name.");
        for (const auto& argument : expr.arguments) {
            analyze_expression(argument.get());
        }
        return types_.fresh_variable();
    }
    call_ir_names_[&expr] = function->ir_name;

    if (expr.arguments.size() != function->parameters.size()) {
        error(expr.callee_location, "Wrong number of arguments",
              "`" + expr.callee + "` takes " + std::to_string(function->parameters.size()) + " argument" +
                  (function->parameters.size() == 1 ? "" : "s") + ", but this call passes " +
                  std::to_string(expr.arguments.size()) + ".",
              "Pass exactly " + std::to_string(function->parameters.size()) + " argument" +
                  (function->parameters.size() == 1 ? "" : "s") + ".");
    }

    const std::size_t count = std::min(expr.arguments.size(), function->parameters.size());
    for (std::size_t i = 0; i < count; ++i) {
        const Type argument = analyze_expression(expr.arguments[i].get());
        if (!types_.unify(argument, function->parameters[i])) {
            const std::string param =
                function->decl != nullptr && i < function->decl->parameters.size() ? function->decl->parameters[i] : "argument";
            error(expr.arguments[i] != nullptr ? expr.arguments[i]->span.start : expr.callee_location, "Type mismatch",
                  "Argument `" + param + "` of `" + expr.callee + "` has type " +
                      types_.to_string(function->parameters[i]) + ", but this call passes " + types_.to_string(argument) +
                      ".",
                  "Pass a value of type " + types_.to_string(function->parameters[i]) + ".");
        }
    }
    for (std::size_t i = count; i < expr.arguments.size(); ++i) {
        analyze_expression(expr.arguments[i].get());
    }

    const Type result = function->return_type;
    if (result_used) {
        require_non_void(result, expr.callee_location,
                         "`" + expr.callee + "` does not give a value, so it cannot be used in an expression.",
                         "Call `" + expr.callee + "(...)` as a statement, or add a `give` to the function.");
    }
    return result;
}

Type Analyzer::analyze_list(const ListExpr& expr) {
    Type element = types_.fresh_variable();
    for (const auto& value : expr.elements) {
        const Type item = analyze_expression(value.get());
        if (!types_.unify(element, item)) {
            error(value != nullptr ? value->span.start : expr.span.start, "Type mismatch",
                  "List elements must all have the same type. Found " + types_.to_string(element) + " and " +
                      types_.to_string(item) + ".",
                  "Use one element type in the list.");
        }
    }
    return list_type(element);
}

bool Analyzer::declare_local(const std::string& name, Type type, SourceLocation location, bool iterator) {
    if (scopes_.empty()) {
        return false;
    }

    if (auto* existing = lookup_local(name)) {
        error(location, "Duplicate name",
              "`" + name + "` is already defined in this function.",
              "Choose a different name, or reuse the existing variable with assignment.");
        (void)existing;
        return false;
    }

    if (iterator) {
        scopes_.back().emplace(name, Symbol{type, location, true, true});
        return true;
    }

    scopes_.front().emplace(name, Symbol{type, location, true, false});
    return true;
}

Symbol* Analyzer::lookup_local(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

FunctionSymbol* Analyzer::lookup_function(const std::string& name) {
    if (current_module_ < 0 || static_cast<std::size_t>(current_module_) >= module_lookup_.size()) {
        return nullptr;
    }
    auto found = module_lookup_[static_cast<std::size_t>(current_module_)].find(name);
    if (found == module_lookup_[static_cast<std::size_t>(current_module_)].end()) {
        return nullptr;
    }
    return found->second;
}

bool Analyzer::expect_type(Type actual, Type expected, SourceLocation location, std::string title, std::string detail,
                          std::string suggestion) {
    if (types_.unify(actual, expected)) {
        return true;
    }
    error(location, std::move(title), std::move(detail), std::move(suggestion));
    return false;
}

void Analyzer::error(SourceLocation location, std::string title, std::string detail, std::string suggestion) {
    diagnostics_.error(location, std::move(title), std::move(detail), std::move(suggestion));
}

void Analyzer::error(SourceLocation location, std::string title, std::string detail, std::string suggestion,
                     std::vector<DiagnosticNote> notes) {
    diagnostics_.error(location, std::move(title), std::move(detail), std::move(suggestion), std::move(notes));
}

void Analyzer::add_constraint(Type type, ConstraintKind kind, SourceLocation location, std::string operator_text) {
    constraints_.push_back(Constraint{type, kind, location, std::move(operator_text)});
}

void Analyzer::require_non_void(Type type, SourceLocation location, std::string detail, std::string suggestion) {
    if (types_.prune(type).kind == TypeKind::Void) {
        error(location, "Type mismatch", std::move(detail), std::move(suggestion));
        return;
    }
    non_void_uses_.push_back(NonVoidUse{type, location, std::move(detail), std::move(suggestion)});
}

void Analyzer::check_non_void_uses() {
    for (const auto& use : non_void_uses_) {
        if (types_.prune(use.type).kind == TypeKind::Void) {
            error(use.location, "Type mismatch", use.detail, use.suggestion);
        }
    }
}

void Analyzer::push_scope() {
    scopes_.emplace_back();
}

void Analyzer::pop_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

}  // namespace

SemanticInfo analyze_semantics(const Program& program, DiagnosticEngine& diagnostics) {
    Analyzer analyzer(program, diagnostics);
    return analyzer.run();
}

SemanticInfo analyze_semantics(const ModuleGraph& graph, DiagnosticEngine& diagnostics) {
    if (graph.modules.empty() || graph.modules[0].program == nullptr) {
        Program empty;
        Analyzer analyzer(empty, diagnostics);
        return analyzer.run();
    }
    Analyzer analyzer(graph, diagnostics);
    return analyzer.run();
}

void print_types(const SemanticInfo& info, std::ostream& out) {
    for (const auto& function : info.functions) {
        out << "create " << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << function.parameters[i].name << ": " << function.parameters[i].type;
        }
        out << ") -> " << function.return_type << '\n';
        for (const auto& local : function.locals) {
            out << "  " << local.name << ": " << local.type << '\n';
        }
    }
}

}  // namespace yukt

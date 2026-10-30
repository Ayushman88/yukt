#include "semantic/analyzer.hpp"

#include "driver/module.hpp"
#include "lexer/token.hpp"
#include "semantic/ownership.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
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
    NumericList,
    Field,
};

struct Constraint {
    Type type;
    ConstraintKind kind = ConstraintKind::Numeric;
    SourceLocation location;
    std::string operator_text;
    std::string field_name;
    Type field_type;
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
    void collect_records();
    void collect_aliases();
    void resolve_record_fields();
    void compute_record_layouts();
    void check_shared_signatures();
    void analyze_functions();
    void walk_functions();
    std::string ir_name_for(int module_index, const FunctionDecl& function) const;
    const Program* module_program(int index) const;
    FunctionSymbol* lookup_shared(int module_index, const std::string& name);
    void check_constraints();
    void finalize_functions();
    void finalize_records();
    void check_non_void_uses();
    void require_main();
    void require_non_void(Type type, SourceLocation location, std::string detail, std::string suggestion);
    void add_constraint(Type type, ConstraintKind kind, SourceLocation location, std::string operator_text);
    void add_field_constraint(Type object, std::string field, Type field_type, SourceLocation location);
    void analyze_function(FunctionSymbol& function);
    void analyze_statement(const Stmt* stmt);
    void analyze_assignment(const AssignStmt& stmt);
    void analyze_index_assign(const IndexAssignStmt& stmt);
    void analyze_field_assign(const FieldAssignStmt& stmt);
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
    Type analyze_constructor(RecordInfo& record, const CallExpr& expr);
    Type analyze_stdlib_call(FunctionSymbol& function, const CallExpr& expr, bool result_used);
    Type analyze_list(const ListExpr& expr);
    Type analyze_field(const FieldExpr& expr);
    Type resolve_type_name(const TypeName& name, int module_index, bool in_shared_record);
    RecordInfo* lookup_record(int module_index, const std::string& name);
    RecordInfo* lookup_shared_record(int module_index, const std::string& name);
    bool type_contains_private_record(Type type, int module_index);
    static std::int64_t align_up(std::int64_t n, std::int64_t a);
    bool field_abi(Type type, std::int64_t& size, std::int64_t& align);

    bool declare_local(const std::string& name, Type type, SourceLocation location, bool iterator);
    Symbol* lookup_local(const std::string& name);
    FunctionSymbol* lookup_function(const std::string& name);

    // 0.13.2 ownership (use-after-move) tracking for heap-typed names.
    bool name_is_heap(const std::string& name);
    void own_check_read(const std::string& name, SourceLocation location);
    void own_move(const std::string& name, SourceLocation location);
    void own_move_call_args(const CallExpr& expr);
    void consume_heap_expr(const Expr* expr);
    void reject_field_move(const Expr* expr, SourceLocation location);
    void reject_list_element_move(const Expr* expr, SourceLocation location);

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
    std::vector<std::unordered_map<std::string, int>> module_records_;
    std::vector<RecordInfo> records_;
    std::unordered_map<const CallExpr*, int> record_constructors_;
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
    std::unordered_map<std::string, OwnName> own_;
    std::unordered_set<std::string> loop_protected_;
    // False during type inference; true on the second walk, after parameter types
    // have been unified, so heap parameters can enter the ownership map as Owned.
    bool ownership_pass_ = false;
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
    module_records_.emplace_back();
    aliases_.emplace_back();
}

Analyzer::Analyzer(const ModuleGraph& graph, DiagnosticEngine& diagnostics)
    : program_(*graph.modules.at(0).program), graph_(&graph), diagnostics_(diagnostics),
      single_file_(graph.modules.size() <= 1 && !graph.has_uses()) {
    module_lookup_.resize(graph.modules.size());
    module_records_.resize(graph.modules.size());
    aliases_.resize(graph.modules.size());
}

SemanticInfo Analyzer::run() {
    collect_functions();
    collect_records();
    collect_aliases();
    resolve_record_fields();
    compute_record_layouts();
    analyze_functions();
    check_shared_signatures();
    check_constraints();
    finalize_functions();
    finalize_records();
    check_non_void_uses();
    require_main();
    for (auto& record : records_) {
        for (auto& field : record.fields) {
            field.type = types_.prune(field.type);
        }
    }

    SemanticInfo info;
    info.function_ir_names = function_ir_names_;
    info.call_ir_names = call_ir_names_;
    info.record_constructors = record_constructors_;
    info.records = records_;
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

bool is_builtin_type_name(const std::string& name) {
    return name == "integer" || name == "float" || name == "boolean" || name == "string" || name == "list";
}

void Analyzer::collect_records() {
    const int module_count = graph_ == nullptr ? 1 : static_cast<int>(graph_->modules.size());
    for (int module_index = 0; module_index < module_count; ++module_index) {
        const Program* program = module_program(module_index);
        if (program == nullptr) {
            continue;
        }
        std::string prefix;
        if (graph_ != nullptr && static_cast<std::size_t>(module_index) < graph_->modules.size()) {
            prefix = graph_->modules[static_cast<std::size_t>(module_index)].mangle_prefix;
        }
        for (const auto& record : program->records) {
            if (record == nullptr) {
                continue;
            }
            if (is_builtin_type_name(record->name)) {
                error(record->name_location, "Invalid record",
                      "Record `" + record->name + "` collides with a builtin type name.",
                      "Choose a different record name.");
                continue;
            }
            if (record->name == "count" || record->name == "zeros") {
                error(record->name_location, "Name collision",
                      "Record `" + record->name + "` collides with a compiler builtin.",
                      "Choose a different record name.");
                continue;
            }
            if (module_lookup_[static_cast<std::size_t>(module_index)].contains(record->name)) {
                error(record->name_location, "Name collision",
                      "Record `" + record->name + "` collides with a function in this file.",
                      "Choose a different name.");
                continue;
            }
            if (module_records_[static_cast<std::size_t>(module_index)].contains(record->name)) {
                error(record->name_location, "Duplicate record",
                      "A record named `" + record->name + "` is already defined.",
                      "Give this record a different name.");
                continue;
            }
            RecordInfo info;
            info.id = static_cast<int>(records_.size());
            info.module_index = module_index;
            info.shared = record->shared;
            info.name = record->name;
            info.identity = prefix.empty() ? record->name : prefix + "$" + record->name;
            info.decl = record.get();
            std::unordered_set<std::string> field_names;
            for (const auto& field : record->fields) {
                if (!field_names.insert(field.name).second) {
                    error(field.name_location, "Duplicate field",
                          "Field `" + field.name + "` is already declared on `" + record->name + "`.",
                          "Give this field a different name.");
                }
            }
            module_records_[static_cast<std::size_t>(module_index)].emplace(record->name, info.id);
            records_.push_back(std::move(info));
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
            if (module_records_[static_cast<std::size_t>(module_index)].contains(use.alias)) {
                error(use.alias_location, "Name collision",
                      "Alias `" + use.alias + "` collides with a record in this file.",
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

RecordInfo* Analyzer::lookup_record(int module_index, const std::string& name) {
    if (module_index < 0 || static_cast<std::size_t>(module_index) >= module_records_.size()) {
        return nullptr;
    }
    auto found = module_records_[static_cast<std::size_t>(module_index)].find(name);
    if (found == module_records_[static_cast<std::size_t>(module_index)].end()) {
        return nullptr;
    }
    if (found->second < 0 || static_cast<std::size_t>(found->second) >= records_.size()) {
        return nullptr;
    }
    return &records_[static_cast<std::size_t>(found->second)];
}

RecordInfo* Analyzer::lookup_shared_record(int module_index, const std::string& name) {
    RecordInfo* record = lookup_record(module_index, name);
    if (record == nullptr || !record->shared) {
        return nullptr;
    }
    return record;
}

std::int64_t Analyzer::align_up(std::int64_t n, std::int64_t a) {
    if (a <= 1) {
        return n;
    }
    return (n + a - 1) & ~(a - 1);
}

Type Analyzer::resolve_type_name(const TypeName& name, int module_index, bool in_shared_record) {
    if (name.name.empty()) {
        return types_.fresh_variable();
    }
    if (name.qualifier.empty()) {
        if (name.name == "integer") {
            return integer_type();
        }
        if (name.name == "float") {
            return float_type();
        }
        if (name.name == "boolean") {
            return boolean_type();
        }
        if (name.name == "string") {
            return string_type();
        }
        if (name.name == "list") {
            // One element variable per list field, unified across constructors.
            return list_type(types_.fresh_variable());
        }
        RecordInfo* record = lookup_record(module_index, name.name);
        if (record == nullptr) {
            error(name.name_location, "Unknown name",
                  "`" + name.name + "` is not a type in this file.",
                  "Use `integer`, `float`, `boolean`, `string`, `list`, or a record declared in this file.");
            return types_.fresh_variable();
        }
        if (in_shared_record && !record->shared) {
            error(name.name_location, "Private type",
                  "Shared record fields cannot use private record `" + record->name + "`.",
                  "Export `" + record->name + "` with `share record`, or keep both records private.");
        }
        return record_type(record->id, record->name);
    }

    const auto alias = aliases_[static_cast<std::size_t>(module_index)].find(name.qualifier);
    if (alias == aliases_[static_cast<std::size_t>(module_index)].end()) {
        error(name.dot_location.line == 0 ? name.name_location : name.dot_location, "Unknown module",
              "`" + name.qualifier + "` is not an imported module.",
              "Add `use " + name.qualifier + "` or fix the alias.");
        return types_.fresh_variable();
    }
    RecordInfo* record = lookup_record(alias->second, name.name);
    if (record == nullptr) {
        error(name.name_location, "Unknown name",
              "`" + name.qualifier + "." + name.name + "` is not a record.",
              "Import a module that shares this record.");
        return types_.fresh_variable();
    }
    if (!record->shared) {
        error(name.name_location, "Private type",
              "`" + name.qualifier + "." + name.name + "` is a private record.",
              "Export it with `share record " + name.name + "`.");
        return types_.fresh_variable();
    }
    return record_type(record->id, record->name);
}

void Analyzer::resolve_record_fields() {
    for (auto& record : records_) {
        if (record.decl == nullptr) {
            continue;
        }
        record.fields.clear();
        for (const auto& field : record.decl->fields) {
            RecordFieldInfo info;
            info.name = field.name;
            info.location = field.name_location;
            info.type = resolve_type_name(field.type, record.module_index, record.shared);
            record.fields.push_back(std::move(info));
        }
    }
}

bool Analyzer::field_abi(Type type, std::int64_t& size, std::int64_t& align) {
    type = types_.prune(type);
    switch (type.kind) {
        case TypeKind::Integer:
        case TypeKind::Float:
            size = 8;
            align = 8;
            return true;
        case TypeKind::Boolean:
            size = 1;
            align = 1;
            return true;
        case TypeKind::String:
        case TypeKind::List:
            size = 8;
            align = 8;
            return true;
        case TypeKind::Record:
            if (type.record_id < 0 || static_cast<std::size_t>(type.record_id) >= records_.size()) {
                return false;
            }
            if (!records_[static_cast<std::size_t>(type.record_id)].layout_ready) {
                return false;
            }
            size = records_[static_cast<std::size_t>(type.record_id)].size;
            align = records_[static_cast<std::size_t>(type.record_id)].align;
            return true;
        default:
            return false;
    }
}

void Analyzer::compute_record_layouts() {
    const int n = static_cast<int>(records_.size());
    std::vector<std::vector<int>> edges(static_cast<std::size_t>(n));
    std::vector<std::vector<int>> field_edge(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (int f = 0; f < static_cast<int>(records_[static_cast<std::size_t>(i)].fields.size()); ++f) {
            const Type field_type = types_.prune(records_[static_cast<std::size_t>(i)].fields[static_cast<std::size_t>(f)].type);
            if (field_type.kind == TypeKind::Record && field_type.record_id >= 0 && field_type.record_id < n) {
                edges[static_cast<std::size_t>(i)].push_back(field_type.record_id);
                field_edge[static_cast<std::size_t>(i)].push_back(f);
            }
        }
    }

    enum class Color { White, Gray, Black };
    std::vector<Color> color(static_cast<std::size_t>(n), Color::White);
    std::vector<int> parent(static_cast<std::size_t>(n), -1);
    std::vector<int> parent_field(static_cast<std::size_t>(n), -1);
    std::vector<int> order;
    bool cyclic = false;

    std::function<void(int)> dfs = [&](int node) {
        color[static_cast<std::size_t>(node)] = Color::Gray;
        for (std::size_t e = 0; e < edges[static_cast<std::size_t>(node)].size(); ++e) {
            const int next = edges[static_cast<std::size_t>(node)][e];
            if (color[static_cast<std::size_t>(next)] == Color::Gray) {
                cyclic = true;
                const int field_i = field_edge[static_cast<std::size_t>(node)][e];
                SourceLocation loc = records_[static_cast<std::size_t>(node)].decl != nullptr
                                         ? records_[static_cast<std::size_t>(node)].decl->name_location
                                         : SourceLocation{};
                if (field_i >= 0 && static_cast<std::size_t>(field_i) < records_[static_cast<std::size_t>(node)].fields.size()) {
                    loc = records_[static_cast<std::size_t>(node)].fields[static_cast<std::size_t>(field_i)].location;
                }
                std::string cycle = records_[static_cast<std::size_t>(next)].name;
                int walk = node;
                std::vector<std::string> parts;
                parts.push_back(records_[static_cast<std::size_t>(node)].name);
                while (walk >= 0 && walk != next) {
                    parts.push_back(records_[static_cast<std::size_t>(walk)].name);
                    walk = parent[static_cast<std::size_t>(walk)];
                    if (static_cast<int>(parts.size()) > n + 1) {
                        break;
                    }
                }
                parts.push_back(records_[static_cast<std::size_t>(next)].name);
                std::string listed;
                for (int p = static_cast<int>(parts.size()) - 1; p >= 0; --p) {
                    if (!listed.empty()) {
                        listed += " -> ";
                    }
                    listed += parts[static_cast<std::size_t>(p)];
                }
                error(loc, "Recursive record",
                      "Record types cannot form a cycle.",
                      "Remove the recursive field.",
                      {{loc, "cycle: " + (listed.empty() ? cycle : listed)}});
            } else if (color[static_cast<std::size_t>(next)] == Color::White) {
                parent[static_cast<std::size_t>(next)] = node;
                parent_field[static_cast<std::size_t>(next)] = field_edge[static_cast<std::size_t>(node)][e];
                dfs(next);
            }
        }
        color[static_cast<std::size_t>(node)] = Color::Black;
        order.push_back(node);
    };

    for (int i = 0; i < n; ++i) {
        if (color[static_cast<std::size_t>(i)] == Color::White) {
            dfs(i);
        }
    }
    if (cyclic) {
        return;
    }

    for (int node : order) {
        RecordInfo& record = records_[static_cast<std::size_t>(node)];
        std::int64_t offset = 0;
        std::int64_t rec_align = 1;
        bool ok = true;
        for (auto& field : record.fields) {
            std::int64_t size = 0;
            std::int64_t align = 1;
            if (!field_abi(field.type, size, align)) {
                ok = false;
                break;
            }
            offset = align_up(offset, align);
            field.offset = offset;
            field.size = size;
            field.align = align;
            offset += size;
            if (align > rec_align) {
                rec_align = align;
            }
        }
        if (!ok) {
            continue;
        }
        record.align = rec_align < 1 ? 1 : rec_align;
        record.size = align_up(offset, record.align);
        record.layout_ready = true;
    }
}

bool Analyzer::type_contains_private_record(Type type, int module_index) {
    type = types_.prune(type);
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return type_contains_private_record(*type.element, module_index);
    }
    if (type.kind != TypeKind::Record || type.record_id < 0 ||
        static_cast<std::size_t>(type.record_id) >= records_.size()) {
        return false;
    }
    const RecordInfo& record = records_[static_cast<std::size_t>(type.record_id)];
    (void)module_index;
    return !record.shared;
}

void Analyzer::check_shared_signatures() {
    for (const auto& [ir_name, symbol] : functions_) {
        if (symbol == nullptr || symbol->decl == nullptr || !symbol->decl->shared) {
            continue;
        }
        auto check_type = [&](Type type, SourceLocation location, const std::string& where) {
            if (type_contains_private_record(type, symbol->module_index)) {
                error(location, "Private type",
                      "Shared function `" + symbol->decl->name + "` " + where + " a private record.",
                      "Export the record with `share record`, or keep the function private.");
            }
        };
        for (std::size_t i = 0; i < symbol->parameters.size(); ++i) {
            SourceLocation loc = symbol->decl->name_location;
            check_type(symbol->parameters[i], loc, "uses");
        }
        check_type(symbol->return_type, symbol->decl->name_location, "gives");
    }
}

void Analyzer::analyze_functions() {
    walk_functions();
    // Parameter types are unified only after every call site has been seen.
    // Walk again so heap parameters start Owned and `give xs` can mark Moved.
    ownership_pass_ = true;
    walk_functions();
    ownership_pass_ = false;
}

void Analyzer::walk_functions() {
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
            if (is_stdlib_runtime_ir(ir_name)) {
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
            case ConstraintKind::NumericList: {
                Type elem = resolved;
                if (resolved.kind == TypeKind::List && resolved.element != nullptr) {
                    elem = types_.prune(*resolved.element);
                }
                if (elem.kind == TypeKind::Integer || elem.kind == TypeKind::Float) {
                    break;
                }
                if (elem.kind == TypeKind::Variable) {
                    error(constraint.location, "Cannot infer type",
                          "This sort or search never received an integer or float list.",
                          "Pass a list of integers or floats, for example `[3, 1, 2]`.");
                    break;
                }
                error(constraint.location, "Type mismatch",
                      "`sort` requires a list of integers or floats, not " + types_.to_string(resolved) + ".",
                      "Pass `list[integer]` or `list[float]`.");
                break;
            }
            case ConstraintKind::Field: {
                if (resolved.kind == TypeKind::Variable) {
                    error(constraint.location, "Cannot infer type",
                          "`.` is used on a value whose type was never inferred.",
                          "Construct a record, or pass a record into this function.");
                    break;
                }
                if (resolved.kind != TypeKind::Record || resolved.record_id < 0 ||
                    static_cast<std::size_t>(resolved.record_id) >= records_.size()) {
                    error(constraint.location, "Invalid expression",
                          "`.` is field access or a module call, not valid on " + types_.to_string(resolved) + ".",
                          "Access a field of a record, for example `point.x`.");
                    break;
                }
                const RecordInfo& record = records_[static_cast<std::size_t>(resolved.record_id)];
                const RecordFieldInfo* field = nullptr;
                std::string known;
                for (const auto& candidate : record.fields) {
                    if (!known.empty()) {
                        known += ", ";
                    }
                    known += candidate.name;
                    if (candidate.name == constraint.field_name) {
                        field = &candidate;
                    }
                }
                if (field == nullptr) {
                    error(constraint.location, "Unknown field",
                          "`" + record.name + "` has no field `" + constraint.field_name + "`.",
                          known.empty() ? "Check the record declaration." : "Known fields: " + known + ".");
                    break;
                }
                if (!types_.unify(constraint.field_type, field->type)) {
                    error(constraint.location, "Type mismatch",
                          "Field `" + record.name + "." + constraint.field_name + "` has type " +
                              types_.to_string(field->type) + ", but this use has type " +
                              types_.to_string(constraint.field_type) + ".",
                          "Use a value of type " + types_.to_string(field->type) + ".");
                }
                break;
            }
        }
    }
}

void Analyzer::finalize_functions() {
    for (auto& [name, function] : functions_) {
        if (function == nullptr || is_stdlib_runtime_ir(name)) {
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

void Analyzer::finalize_records() {
    for (const auto& record : records_) {
        for (const auto& field : record.fields) {
            const Type resolved = types_.prune(field.type);
            if (resolved.kind != TypeKind::List || types_.is_resolved(resolved)) {
                continue;
            }
            error(field.location, "Cannot infer type",
                  "The element type of field `" + record.name + "." + field.name + "` could not be inferred.",
                  "Construct `" + record.name + "` with a list whose element type is known, for example `" +
                      record.name + "(zeros(1))`.");
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
    own_.clear();
    loop_protected_.clear();
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
            if (ownership_pass_ && type_owns_heap(types_, function.parameters[i], &records_)) {
                own_[name] = OwnName{OwnState::Owned, function.decl->name_location};
            }
        }

        for (const auto& stmt : function.decl->body) {
            analyze_statement(stmt.get());
        }

        if (!ownership_pass_ && !scopes_.empty()) {
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
    if (const auto* field_assign = dynamic_cast<const FieldAssignStmt*>(stmt)) {
        analyze_field_assign(*field_assign);
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
    if (lookup_record(current_module_, stmt.name) != nullptr) {
        error(stmt.name_location, "Name collision",
              "`" + stmt.name + "` is a record type and cannot be assigned.",
              "Construct it with `" + stmt.name + "(...)`, or choose a different variable name.");
        analyze_expression(stmt.value.get());
        return;
    }

    const Type value_type = analyze_expression(stmt.value.get());
    require_non_void(value_type, stmt.name_location, "Cannot assign a void value to `" + stmt.name + "`.",
                     "Call a function that gives a value, or assign a literal.");
    if (types_.prune(value_type).kind == TypeKind::Void) {
        return;
    }

    const bool heap = type_owns_heap(types_, value_type, &records_);
    // A direct move is `lhs = rhs` where `rhs` is a bare heap-typed name.
    // Binding a heap field or list element is not a steal.
    std::string moved_from;
    SourceLocation moved_from_location;
    if (heap) {
        if (dynamic_cast<const FieldExpr*>(stmt.value.get()) != nullptr) {
            reject_field_move(stmt.value.get(), stmt.value != nullptr ? stmt.value->span.start : stmt.name_location);
        } else if (dynamic_cast<const IndexExpr*>(stmt.value.get()) != nullptr) {
            reject_list_element_move(stmt.value.get(),
                                     stmt.value != nullptr ? stmt.value->span.start : stmt.name_location);
        } else if (const auto* id = dynamic_cast<const IdentifierExpr*>(stmt.value.get())) {
            if (lookup_local(id->name) != nullptr && name_is_heap(id->name)) {
                moved_from = id->name;
                moved_from_location = id->span.start;
            }
        }
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
        if (heap) {
            auto state = own_.find(stmt.name);
            if (state != own_.end() && state->second.state == OwnState::Moved) {
                // Reassigning a moved-from name is not allowed in 0.13.2.
                error(stmt.name_location, "Use after move",
                      "`" + stmt.name + "` was moved, so it cannot be used here.",
                      "This value was moved. Use the new owner, or copy with list.copy before moving.");
            } else {
                // Overwrite: drop the previous allocation, then the name stays Owned.
                // `xs = xs` is a no-op move, not a drop of the only pointer.
                if (ownership_pass_ && state != own_.end() && state->second.state == OwnState::Owned &&
                    moved_from != stmt.name) {
                    if (loop_protected_.contains(stmt.name)) {
                        error(stmt.name_location, "Cannot move inside repeat",
                              "Moving `" + stmt.name + "` inside `repeat` would leave later iterations with a moved value.",
                              "Copy first, or restructure so the move happens outside the loop.");
                    }
                }
                own_[stmt.name] = OwnName{OwnState::Owned, stmt.name_location};
            }
        }
        if (!moved_from.empty() && moved_from != stmt.name) {
            own_move(moved_from, moved_from_location);
        }
        return;
    }

    declare_local(stmt.name, value_type, stmt.name_location, false);
    if (heap) {
        own_[stmt.name] = OwnName{OwnState::Owned, stmt.name_location};
    }
    if (!moved_from.empty() && moved_from != stmt.name) {
        own_move(moved_from, moved_from_location);
    }
}

void Analyzer::analyze_index_assign(const IndexAssignStmt& stmt) {
    // RHS first so a call that moves the collection is Use after move on the place,
    // matching field assign (`names[0] = take(names)`).
    const Type value = analyze_expression(stmt.value.get());
    const Type collection = analyze_expression(stmt.collection.get());
    const Type index = analyze_expression(stmt.index.get());
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
    if (type_owns_heap(types_, value, &records_)) {
        // Index reads stay borrows. Cloning the slot happens in lowering.
        if (dynamic_cast<const IndexExpr*>(stmt.value.get()) == nullptr) {
            consume_heap_expr(stmt.value.get());
        }
    }
}

void Analyzer::analyze_field_assign(const FieldAssignStmt& stmt) {
    // RHS first so a call that moves the object is Use after move on the place,
    // matching evaluation order (`p.scores = zeros(count(p.scores))` stays valid).
    const Type value = analyze_expression(stmt.value.get());
    const Type object = analyze_expression(stmt.object.get());
    const Type resolved = types_.prune(object);
    if (resolved.kind == TypeKind::Variable) {
        add_field_constraint(object, stmt.field, value, stmt.field_location);
        return;
    }
    if (resolved.kind != TypeKind::Record) {
        error(stmt.field_location, "Invalid expression",
              "`.` is field access or a module call, not valid on " + types_.to_string(object) + ".",
              "Access a field of a record, for example `point.x = 1`.");
        return;
    }
    if (resolved.record_id < 0 || static_cast<std::size_t>(resolved.record_id) >= records_.size()) {
        return;
    }
    const RecordInfo& record = records_[static_cast<std::size_t>(resolved.record_id)];
    const RecordFieldInfo* field = nullptr;
    std::string known;
    for (const auto& candidate : record.fields) {
        if (!known.empty()) {
            known += ", ";
        }
        known += candidate.name;
        if (candidate.name == stmt.field) {
            field = &candidate;
        }
    }
    if (field == nullptr) {
        error(stmt.field_location, "Unknown field",
              "`" + record.name + "` has no field `" + stmt.field + "`.",
              known.empty() ? "Check the record declaration." : "Known fields: " + known + ".");
        return;
    }
    if (!types_.unify(value, field->type)) {
        error(stmt.span.start, "Type mismatch",
              "Field `" + record.name + "." + stmt.field + "` has type " + types_.to_string(field->type) +
                  ", but this assignment has type " + types_.to_string(value) + ".",
              "Assign a value of type " + types_.to_string(field->type) + ".");
    }
    if (type_owns_heap(types_, field->type, &records_)) {
        consume_heap_expr(stmt.value.get());
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
    // `give x` marks x Moved so the callee does not release it. Remaining
    // Owned names are dropped by IR lowering from the path-local owned set.
    if (type_owns_heap(types_, type, &records_)) {
        consume_heap_expr(stmt.value.get());
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

    // Snapshot ownership, analyze each branch from the same snapshot, then join.
    const std::unordered_map<std::string, OwnName> snapshot = own_;
    for (const auto& then_stmt : stmt.then_branch) {
        analyze_statement(then_stmt.get());
    }
    const std::unordered_map<std::string, OwnName> after_then = own_;
    own_ = snapshot;
    for (const auto& otherwise_stmt : stmt.otherwise_branch) {
        analyze_statement(otherwise_stmt.get());
    }
    const std::unordered_map<std::string, OwnName> after_otherwise = own_;

    std::unordered_set<std::string> names;
    for (const auto& [name, own] : after_then) {
        names.insert(name);
    }
    for (const auto& [name, own] : after_otherwise) {
        names.insert(name);
    }
    auto branch_state = [&](const std::unordered_map<std::string, OwnName>& branch, const std::string& name) {
        auto found = branch.find(name);
        if (found != branch.end()) {
            return found->second.state;
        }
        auto base = snapshot.find(name);
        return base != snapshot.end() ? base->second.state : OwnState::Uninit;
    };
    std::unordered_map<std::string, OwnName> joined;
    for (const auto& name : names) {
        SourceLocation location;
        auto then_it = after_then.find(name);
        auto else_it = after_otherwise.find(name);
        if (then_it != after_then.end()) {
            location = then_it->second.location;
        } else if (else_it != after_otherwise.end()) {
            location = else_it->second.location;
        }
        joined[name] = OwnName{join_own(branch_state(after_then, name), branch_state(after_otherwise, name)), location};
    }
    own_ = std::move(joined);
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
    if (lookup_record(current_module_, stmt.iterator) != nullptr) {
        error(stmt.iterator_location, "Name collision",
              "`" + stmt.iterator + "` is a record type and cannot be a loop variable.",
              "Choose a different loop variable name.");
    }

    // Names that already exist before the loop must not be moved inside it: the
    // first iteration would leave later iterations with a moved value.
    const std::unordered_map<std::string, OwnName> snapshot = own_;
    std::vector<std::string> protected_here;
    for (const auto& [name, own] : own_) {
        if (loop_protected_.insert(name).second) {
            protected_here.push_back(name);
        }
    }

    push_scope();
    if (!declare_local(stmt.iterator, integer_type(), stmt.iterator_location, true)) {
        // declare_local already reported a collision with an existing name.
    }
    for (const auto& body_stmt : stmt.body) {
        analyze_statement(body_stmt.get());
    }
    pop_scope();

    if (ownership_pass_) {
        std::vector<std::string> local_drops;
        for (const auto& [name, own] : own_) {
            if (own.state != OwnState::Owned) {
                continue;
            }
            const auto was = snapshot.find(name);
            if (was == snapshot.end() || was->second.state != OwnState::Owned) {
                local_drops.push_back(name);
            }
        }
        // Dropped at iteration end: the name is function-scoped but dead.
        // Reading it after the loop is Use after move. Do not erase — a
        // missing own_ entry would let the assigned local be read.
        for (const auto& name : local_drops) {
            own_[name].state = OwnState::Moved;
        }
    }

    for (const auto& name : protected_here) {
        loop_protected_.erase(name);
    }
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
            own_check_read(name->name, name->span.start);
            return symbol->type;
        }
        if (lookup_function(name->name) != nullptr) {
            error(name->span.start, "Invalid expression",
                  "`" + name->name + "` is a function. Call it with `()`.",
                  "Write `" + name->name + "(...)`.");
            return types_.fresh_variable();
        }
        if (lookup_record(current_module_, name->name) != nullptr) {
            error(name->span.start, "Invalid expression",
                  "`" + name->name + "` is a record type. Construct it with `()`.",
                  "Write `" + name->name + "(...)`.");
            return types_.fresh_variable();
        }
        if (current_module_ >= 0 && static_cast<std::size_t>(current_module_) < aliases_.size() &&
            aliases_[static_cast<std::size_t>(current_module_)].contains(name->name)) {
            error(name->span.start, "Invalid expression",
                  "`" + name->name + "` is a module, not a value.",
                  "Call a shared name such as `" + name->name + ".name(...)`.");
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
    if (const auto* field = dynamic_cast<const FieldExpr*>(expr)) {
        return analyze_field(*field);
    }
    return types_.fresh_variable();
}

Type Analyzer::analyze_field(const FieldExpr& expr) {
    if (const auto* name = dynamic_cast<const IdentifierExpr*>(expr.object.get())) {
        if (lookup_local(name->name) == nullptr &&
            current_module_ >= 0 && static_cast<std::size_t>(current_module_) < aliases_.size() &&
            aliases_[static_cast<std::size_t>(current_module_)].contains(name->name)) {
            error(expr.field_location, "Invalid expression",
                  "`" + name->name + "` is a module, not a value.",
                  "Call a shared name such as `" + name->name + "." + expr.field + "(...)`.");
            return types_.fresh_variable();
        }
    }
    const Type object = analyze_expression(expr.object.get());
    const Type resolved = types_.prune(object);
    if (resolved.kind != TypeKind::Record) {
        if (resolved.kind == TypeKind::Variable) {
            Type result = types_.fresh_variable();
            add_field_constraint(object, expr.field, result, expr.field_location);
            return result;
        }
        error(expr.field_location, "Invalid expression",
              "`.` is field access or a module call, not valid on " + types_.to_string(object) + ".",
              "Access a field of a record, for example `point.x`.");
        return types_.fresh_variable();
    }
    if (resolved.record_id < 0 || static_cast<std::size_t>(resolved.record_id) >= records_.size()) {
        return types_.fresh_variable();
    }
    const RecordInfo& record = records_[static_cast<std::size_t>(resolved.record_id)];
    const RecordFieldInfo* field = nullptr;
    std::string known;
    for (const auto& candidate : record.fields) {
        if (!known.empty()) {
            known += ", ";
        }
        known += candidate.name;
        if (candidate.name == expr.field) {
            field = &candidate;
        }
    }
    if (field == nullptr) {
        error(expr.field_location, "Unknown field",
              "`" + record.name + "` has no field `" + expr.field + "`.",
              known.empty() ? "Check the record declaration." : "Known fields: " + known + ".");
        return types_.fresh_variable();
    }
    return field->type;
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

Type Analyzer::analyze_constructor(RecordInfo& record, const CallExpr& expr) {
    record_constructors_[&expr] = record.id;
    if (expr.arguments.size() != record.fields.size()) {
        error(expr.callee_location, "Wrong number of arguments",
              "`" + record.name + "` takes " + std::to_string(record.fields.size()) + " field" +
                  (record.fields.size() == 1 ? "" : "s") + ", but this constructor passes " +
                  std::to_string(expr.arguments.size()) + ".",
              "Pass each field in declaration order.");
    }
    const std::size_t count = std::min(expr.arguments.size(), record.fields.size());
    for (std::size_t i = 0; i < count; ++i) {
        const Type argument = analyze_expression(expr.arguments[i].get());
        if (!types_.unify(argument, record.fields[i].type)) {
            error(expr.arguments[i] != nullptr ? expr.arguments[i]->span.start : expr.callee_location, "Type mismatch",
                  "Field `" + record.fields[i].name + "` of `" + record.name + "` has type " +
                      types_.to_string(record.fields[i].type) + ", but this constructor passes " +
                      types_.to_string(argument) + ".",
                  "Pass a value of type " + types_.to_string(record.fields[i].type) + ".");
        }
    }
    for (std::size_t i = count; i < expr.arguments.size(); ++i) {
        analyze_expression(expr.arguments[i].get());
    }
    own_move_call_args(expr);
    return record_type(record.id, record.name);
}

Type Analyzer::analyze_call(const CallExpr& expr, bool result_used) {
    if (!expr.qualifier.empty()) {
        const auto alias = aliases_[static_cast<std::size_t>(current_module_)].find(expr.qualifier);
        if (alias == aliases_[static_cast<std::size_t>(current_module_)].end()) {
            if (auto* local = lookup_local(expr.qualifier)) {
                const Type resolved = types_.prune(local->type);
                if (resolved.kind == TypeKind::Record) {
                    error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Invalid expression",
                          "`" + expr.qualifier + "." + expr.callee + "(...)` is a method call. Records have no methods.",
                          "Read or assign a field, for example `" + expr.qualifier + ".x`.");
                } else {
                    error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Invalid expression",
                          "`.` is field access or a module call, not valid on " + types_.to_string(local->type) + ".",
                          "Call a function or access a record field.");
                }
                for (const auto& argument : expr.arguments) {
                    analyze_expression(argument.get());
                }
                return types_.fresh_variable();
            }
            error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Unknown name",
                  "`" + expr.qualifier + "` is not defined in this scope.",
                  "Assign it before use, or add `use " + expr.qualifier + "`.");
            for (const auto& argument : expr.arguments) {
                analyze_expression(argument.get());
            }
            return types_.fresh_variable();
        }
        if (RecordInfo* record = lookup_shared_record(alias->second, expr.callee)) {
            return analyze_constructor(*record, expr);
        }
        if (RecordInfo* private_record = lookup_record(alias->second, expr.callee);
            private_record != nullptr && !private_record->shared) {
            error(expr.callee_location, "Private type",
                  "`" + expr.qualifier + "." + expr.callee + "` is a private record.",
                  "Export it with `share record " + expr.callee + "`.");
            for (const auto& argument : expr.arguments) {
                analyze_expression(argument.get());
            }
            return types_.fresh_variable();
        }
        FunctionSymbol* function = lookup_shared(alias->second, expr.callee);
        const Program* imported = module_program(alias->second);
        if (function == nullptr || function->decl == nullptr || !function->decl->shared) {
            std::string shared_list = "this module shares no functions or records";
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
                for (const auto& rec : imported->records) {
                    if (rec != nullptr && rec->shared) {
                        if (!names.empty()) {
                            names += ", ";
                        }
                        names += rec->name;
                    }
                }
                if (!names.empty()) {
                    shared_list = "shared: " + names;
                }
            }
            error(expr.dot_location.line == 0 ? expr.callee_location : expr.dot_location, "Unknown function",
                  "`" + expr.qualifier + "." + expr.callee + "` is not a shared function or record.",
                  "Export it with `share create` or `share record`, or call a shared name.",
                  {{expr.dot_location, shared_list}});
            for (const auto& argument : expr.arguments) {
                analyze_expression(argument.get());
            }
            return types_.fresh_variable();
        }
        if (is_stdlib_runtime_ir(function->ir_name)) {
            return analyze_stdlib_call(*function, expr, result_used);
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
        own_move_call_args(expr);
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

    if (RecordInfo* record = lookup_record(current_module_, expr.callee)) {
        return analyze_constructor(*record, expr);
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
    own_move_call_args(expr);

    const Type result = function->return_type;
        if (result_used) {
            require_non_void(result, expr.callee_location,
                             "`" + expr.callee + "` does not give a value, so it cannot be used in an expression.",
                             "Call `" + expr.callee + "(...)` as a statement, or add a `give` to the function.");
        }
        return result;
}

Type Analyzer::analyze_stdlib_call(FunctionSymbol& function, const CallExpr& expr, bool result_used) {
    call_ir_names_[&expr] = function.ir_name;
    const std::string& ir = function.ir_name;
    const std::string display = expr.qualifier.empty() ? expr.callee : expr.qualifier + "." + expr.callee;

    auto arg_location = [&](std::size_t i) -> SourceLocation {
        if (i < expr.arguments.size() && expr.arguments[i] != nullptr) {
            return expr.arguments[i]->span.start;
        }
        return expr.callee_location;
    };
    auto analyze_arg = [&](std::size_t i) -> Type {
        if (i >= expr.arguments.size()) {
            return types_.fresh_variable();
        }
        return analyze_expression(expr.arguments[i].get());
    };
    auto require_arity = [&](std::size_t n) {
        if (expr.arguments.size() == n) {
            return true;
        }
        error(expr.callee_location, "Wrong number of arguments",
              "`" + display + "` takes " + std::to_string(n) + " argument" + (n == 1 ? "" : "s") +
                  ", but this call passes " + std::to_string(expr.arguments.size()) + ".",
              "Pass exactly " + std::to_string(n) + " argument" + (n == 1 ? "" : "s") + ".");
        return false;
    };
    auto finish = [&](Type result) {
        if (result_used) {
            require_non_void(result, expr.callee_location,
                             "`" + display + "` does not give a value, so it cannot be used in an expression.",
                             "Call `" + display + "(...)` as a statement, or add a `give` to the function.");
        }
        return result;
    };

    if (ir == "std$str$len") {
        require_arity(1);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        return finish(integer_type());
    }
    if (ir == "std$str$at") {
        require_arity(2);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        expect_type(analyze_arg(1), integer_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects an integer index.", "Pass an integer byte offset.");
        return finish(integer_type());
    }
    if (ir == "std$str$slice") {
        require_arity(3);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        expect_type(analyze_arg(1), integer_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects an integer start.", "Pass an integer byte offset.");
        expect_type(analyze_arg(2), integer_type(), arg_location(2), "Type mismatch",
                    "`" + display + "` expects an integer end.", "Pass an integer byte offset.");
        return finish(string_type());
    }
    if (ir == "std$str$find" || ir == "std$str$contains" || ir == "std$str$starts" || ir == "std$str$ends") {
        require_arity(2);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        expect_type(analyze_arg(1), string_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects a string substring.", "Pass a string.");
        return finish(ir == "std$str$find" ? integer_type() : boolean_type());
    }
    if (ir == "std$str$repeat") {
        require_arity(2);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        expect_type(analyze_arg(1), integer_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects an integer count.", "Pass an integer.");
        return finish(string_type());
    }
    if (ir == "std$str$join") {
        require_arity(2);
        expect_type(analyze_arg(0), list_type(string_type()), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list of strings.", "Pass `list[string]`.");
        expect_type(analyze_arg(1), string_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects a string separator.", "Pass a string.");
        return finish(string_type());
    }
    if (ir == "std$str$split") {
        require_arity(2);
        expect_type(analyze_arg(0), string_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a string.", "Pass a string.");
        expect_type(analyze_arg(1), string_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects a string separator.", "Pass a non-empty string.");
        return finish(list_type(string_type()));
    }
    if (ir == "std$list$len") {
        require_arity(1);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list.");
        return finish(integer_type());
    }
    if (ir == "std$list$copy") {
        require_arity(1);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list.");
        return finish(list_type(elem));
    }
    if (ir == "std$list$fill") {
        require_arity(2);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list.");
        expect_type(analyze_arg(1), elem, arg_location(1), "Type mismatch",
                    "`" + display + "` requires a value that matches the list element type.",
                    "Pass a value of the same type as the list elements.");
        if (types_.prune(elem).kind == TypeKind::Record) {
            error(arg_location(0), "Type mismatch", "`" + display + "` cannot fill a list of records.",
                  "Assign each element with `items[i] = value`.");
        }
        return finish(void_type());
    }
    if (ir == "std$list$reverse") {
        require_arity(1);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list.");
        return finish(void_type());
    }
    if (ir == "std$list$slice") {
        require_arity(3);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list.");
        expect_type(analyze_arg(1), integer_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects an integer start.", "Pass an integer index.");
        expect_type(analyze_arg(2), integer_type(), arg_location(2), "Type mismatch",
                    "`" + display + "` expects an integer end.", "Pass an integer index.");
        return finish(list_type(elem));
    }
    if (ir == "std$sort$asc" || ir == "std$sort$desc") {
        require_arity(1);
        Type elem = types_.fresh_variable();
        const Type items = analyze_arg(0);
        expect_type(items, list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list of integers or floats.");
        add_constraint(elem, ConstraintKind::NumericList, arg_location(0), display);
        return finish(void_type());
    }
    if (ir == "std$sort$sorted") {
        require_arity(1);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a list of integers or floats.");
        add_constraint(elem, ConstraintKind::NumericList, arg_location(0), display);
        return finish(list_type(elem));
    }
    if (ir == "std$sort$search") {
        require_arity(2);
        Type elem = types_.fresh_variable();
        expect_type(analyze_arg(0), list_type(elem), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list.", "Pass a sorted list of integers or floats.");
        expect_type(analyze_arg(1), elem, arg_location(1), "Type mismatch",
                    "`" + display + "` requires a value that matches the list element type.",
                    "Pass an integer or float that matches the list.");
        add_constraint(elem, ConstraintKind::NumericList, arg_location(0), display);
        return finish(integer_type());
    }
    if (ir == "std$math$abs_i") {
        require_arity(1);
        expect_type(analyze_arg(0), integer_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects an integer.", "Use `math.abs_f` for floats.");
        return finish(integer_type());
    }
    if (ir == "std$math$abs_f") {
        require_arity(1);
        expect_type(analyze_arg(0), float_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a float.", "Use `math.abs_i` for integers.");
        return finish(float_type());
    }
    if (ir == "std$math$min_i" || ir == "std$math$max_i") {
        require_arity(2);
        expect_type(analyze_arg(0), integer_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects an integer.", "Use the `_f` name for floats.");
        expect_type(analyze_arg(1), integer_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects an integer.", "Use the `_f` name for floats.");
        return finish(integer_type());
    }
    if (ir == "std$math$min_f" || ir == "std$math$max_f") {
        require_arity(2);
        expect_type(analyze_arg(0), float_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a float.", "Use the `_i` name for integers.");
        expect_type(analyze_arg(1), float_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects a float.", "Use the `_i` name for integers.");
        return finish(float_type());
    }
    if (ir == "std$math$floor" || ir == "std$math$ceil" || ir == "std$math$sqrt" || ir == "std$math$sin" ||
        ir == "std$math$cos" || ir == "std$math$tan" || ir == "std$math$exp" || ir == "std$math$log") {
        require_arity(1);
        expect_type(analyze_arg(0), float_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a float.", "Pass a float, for example `2.0`.");
        return finish(float_type());
    }
    if (ir == "std$math$pow") {
        require_arity(2);
        expect_type(analyze_arg(0), float_type(), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a float.", "Pass a float.");
        expect_type(analyze_arg(1), float_type(), arg_location(1), "Type mismatch",
                    "`" + display + "` expects a float exponent.", "Pass a float.");
        return finish(float_type());
    }
    if (ir == "std$stat$min_i" || ir == "std$stat$max_i") {
        require_arity(1);
        expect_type(analyze_arg(0), list_type(integer_type()), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list of integers.", "Pass `list[integer]`.");
        return finish(integer_type());
    }
    if (ir == "std$stat$min_f" || ir == "std$stat$max_f" || ir == "std$stat$mean_f") {
        require_arity(1);
        expect_type(analyze_arg(0), list_type(float_type()), arg_location(0), "Type mismatch",
                    "`" + display + "` expects a list of floats.", "Pass `list[float]`.");
        return finish(float_type());
    }

    for (const auto& argument : expr.arguments) {
        analyze_expression(argument.get());
    }
    return finish(types_.fresh_variable());
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
        if (type_owns_heap(types_, item, &records_)) {
            consume_heap_expr(value.get());
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
    if (lookup_record(current_module_, name) != nullptr) {
        error(location, "Name collision",
              "`" + name + "` is a record type and cannot be a variable.",
              "Choose a different name.");
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

bool Analyzer::name_is_heap(const std::string& name) {
    Symbol* symbol = lookup_local(name);
    if (symbol == nullptr) {
        return false;
    }
    return type_owns_heap(types_, symbol->type, &records_);
}

void Analyzer::own_check_read(const std::string& name, SourceLocation location) {
    auto found = own_.find(name);
    if (found != own_.end() && found->second.state == OwnState::Moved) {
        error(location, "Use after move",
              "`" + name + "` was moved, so it cannot be used here.",
              "This value was moved. Use the new owner, or copy with list.copy before moving.");
    }
}

void Analyzer::own_move(const std::string& name, SourceLocation location) {
    auto found = own_.find(name);
    if (found == own_.end() || found->second.state != OwnState::Owned) {
        return;
    }
    if (loop_protected_.contains(name)) {
        error(location, "Cannot move inside repeat",
              "Moving `" + name + "` inside `repeat` would leave later iterations with a moved value.",
              "Copy first, or restructure so the move happens outside the loop.");
        return;
    }
    found->second.state = OwnState::Moved;
    found->second.location = location;
}

void Analyzer::own_move_call_args(const CallExpr& expr) {
    for (const auto& argument : expr.arguments) {
        consume_heap_expr(argument.get());
    }
}

void Analyzer::consume_heap_expr(const Expr* expr) {
    if (expr == nullptr) {
        return;
    }
    if (const auto* id = dynamic_cast<const IdentifierExpr*>(expr)) {
        if (lookup_local(id->name) != nullptr && name_is_heap(id->name)) {
            own_move(id->name, id->span.start);
        }
        return;
    }
    reject_list_element_move(expr, expr->span.start);
    reject_field_move(expr, expr->span.start);
}

void Analyzer::reject_list_element_move(const Expr* expr, SourceLocation location) {
    const auto* index = dynamic_cast<const IndexExpr*>(expr);
    if (index == nullptr) {
        return;
    }
    Type type;
    const auto found = expr_types_.find(expr);
    if (found != expr_types_.end()) {
        type = found->second;
    } else {
        return;
    }
    if (!type_owns_heap(types_, type, &records_)) {
        return;
    }
    error(location, "Cannot move out of list element",
          "A list element cannot be moved out of a list.",
          "Copy the list with list.copy, or use the element in place.");
}

void Analyzer::reject_field_move(const Expr* expr, SourceLocation location) {
    const auto* field = dynamic_cast<const FieldExpr*>(expr);
    if (field == nullptr) {
        return;
    }
    Type type;
    const auto found = expr_types_.find(expr);
    if (found != expr_types_.end()) {
        type = found->second;
    } else {
        type = analyze_field(*field);
    }
    if (!type_owns_heap(types_, type, &records_)) {
        return;
    }
    error(location, "Cannot move out of record field",
          "Field `" + field->field + "` cannot be moved out of a record.",
          "Move the whole record, or copy with list.copy.");
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
    const bool ownership_title = title == "Use after move" || title == "Cannot move inside repeat" ||
                                 title == "Cannot move out of record field" || title == "Cannot move out of list element";
    if (ownership_pass_ != ownership_title) {
        return;
    }
    diagnostics_.error(location, std::move(title), std::move(detail), std::move(suggestion));
}

void Analyzer::error(SourceLocation location, std::string title, std::string detail, std::string suggestion,
                     std::vector<DiagnosticNote> notes) {
    const bool ownership_title = title == "Use after move" || title == "Cannot move inside repeat" ||
                                 title == "Cannot move out of record field" || title == "Cannot move out of list element";
    if (ownership_pass_ != ownership_title) {
        return;
    }
    diagnostics_.error(location, std::move(title), std::move(detail), std::move(suggestion), std::move(notes));
}

void Analyzer::add_constraint(Type type, ConstraintKind kind, SourceLocation location, std::string operator_text) {
    if (ownership_pass_) {
        return;
    }
    Constraint constraint;
    constraint.type = std::move(type);
    constraint.kind = kind;
    constraint.location = location;
    constraint.operator_text = std::move(operator_text);
    constraints_.push_back(std::move(constraint));
}

void Analyzer::add_field_constraint(Type object, std::string field, Type field_type, SourceLocation location) {
    if (ownership_pass_) {
        return;
    }
    Constraint constraint;
    constraint.type = std::move(object);
    constraint.kind = ConstraintKind::Field;
    constraint.location = location;
    constraint.field_name = std::move(field);
    constraint.field_type = std::move(field_type);
    constraints_.push_back(std::move(constraint));
}

void Analyzer::require_non_void(Type type, SourceLocation location, std::string detail, std::string suggestion) {
    if (ownership_pass_) {
        return;
    }
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
    auto field_type_name = [](const Type& type) -> std::string {
        if (!type.record_name.empty()) {
            return type.record_name;
        }
        switch (type.kind) {
            case TypeKind::Integer:
                return "integer";
            case TypeKind::Float:
                return "float";
            case TypeKind::Boolean:
                return "boolean";
            case TypeKind::String:
                return "string";
            case TypeKind::List:
                return "list";
            case TypeKind::Record:
                return "record";
            case TypeKind::Void:
                return "void";
            case TypeKind::Variable:
                return "?";
        }
        return "?";
    };
    for (const auto& record : info.records) {
        if (record.shared) {
            out << "share ";
        }
        out << "record " << record.name;
        if (!record.identity.empty() && record.identity != record.name) {
            out << " [" << record.identity << "]";
        }
        out << " size " << record.size << " align " << record.align << '\n';
        for (const auto& field : record.fields) {
            out << "  " << field.name << ": " << field_type_name(field.type) << " @" << field.offset << '\n';
        }
    }
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

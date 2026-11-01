#include "driver/module.hpp"

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace yukt {
namespace {

namespace fs = std::filesystem;

bool is_identifier(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if ((first < 'A' || first > 'Z') && (first < 'a' || first > 'z') && text[0] != '_') {
        return false;
    }
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u < 'A' || u > 'Z') && (u < 'a' || u > 'z') && (u < '0' || u > '9') && c != '_') {
            return false;
        }
    }
    return true;
}

std::string slash_to_dollar(const std::string& relative) {
    std::string out;
    out.reserve(relative.size());
    for (char c : relative) {
        out.push_back(c == '/' ? '$' : c);
    }
    return out;
}

std::string read_file(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Could not open `" + path + "` for reading.";
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string join_components(const std::vector<std::string>& parts, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (!out.empty()) {
            out += '/';
        }
        out += parts[i];
    }
    return out;
}

struct ResolveResult {
    std::string canonical;
    bool ok = false;
    bool is_std = false;
    std::vector<std::string> tried;
};

ResolveResult resolve_use(const UseDecl& use, const std::string& importer_dir, const std::string& entry_dir,
                          const std::string& stdlib_root) {
    ResolveResult result;
    if (use.path.empty()) {
        return result;
    }
    if (use.path[0] == "std") {
        result.is_std = true;
        if (use.path.size() < 2) {
            return result;
        }
        const std::string rest = join_components(use.path, 1) + ".yukt";
        const fs::path candidate = fs::path(stdlib_root) / rest;
        result.tried.push_back(candidate.string());
        std::error_code ec;
        if (!stdlib_root.empty() && fs::exists(candidate, ec)) {
            result.canonical = fs::canonical(candidate, ec).string();
            result.ok = !ec;
        }
        return result;
    }

    const std::string relative = join_components(use.path, 0) + ".yukt";
    const fs::path importer_candidate = fs::path(importer_dir) / relative;
    result.tried.push_back(importer_candidate.string());
    std::error_code ec;
    if (fs::exists(importer_candidate, ec)) {
        result.canonical = fs::canonical(importer_candidate, ec).string();
        result.ok = !ec;
        return result;
    }
    const fs::path entry_candidate = fs::path(entry_dir) / relative;
    if (fs::absolute(entry_candidate) != fs::absolute(importer_candidate)) {
        result.tried.push_back(entry_candidate.string());
        if (fs::exists(entry_candidate, ec)) {
            result.canonical = fs::canonical(entry_candidate, ec).string();
            result.ok = !ec;
        }
    }
    return result;
}

int color_dfs(int node, const std::vector<std::vector<int>>& edges, std::vector<int>& color, std::vector<int>& stack,
              std::vector<int>& cycle) {
    color[static_cast<std::size_t>(node)] = 1;
    stack.push_back(node);
    for (int next : edges[static_cast<std::size_t>(node)]) {
        if (color[static_cast<std::size_t>(next)] == 1) {
            cycle.clear();
            bool started = false;
            for (int item : stack) {
                if (item == next) {
                    started = true;
                }
                if (started) {
                    cycle.push_back(item);
                }
            }
            cycle.push_back(next);
            return 1;
        }
        if (color[static_cast<std::size_t>(next)] == 0) {
            if (color_dfs(next, edges, color, stack, cycle) != 0) {
                return 1;
            }
        }
    }
    stack.pop_back();
    color[static_cast<std::size_t>(node)] = 2;
    return 0;
}

void report_cycle(ModuleGraph& graph, DiagnosticEngine& diagnostics, const std::vector<int>& cycle) {
    std::string note = "cycle:";
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (i != 0) {
            note += " -> ";
        } else {
            note += " ";
        }
        note += graph.modules[static_cast<std::size_t>(cycle[i])].canonical_path;
    }
    SourceLocation location{1, 1, graph.modules[0].canonical_path};
    if (!cycle.empty()) {
        const int from = cycle[0];
        const auto& module = graph.modules[static_cast<std::size_t>(from)];
        if (module.program != nullptr && !module.program->uses.empty()) {
            location = module.program->uses[0].use_location;
            location.file = module.canonical_path;
        }
    }
    diagnostics.error(location, "Circular import", "These modules import each other.",
                      "Break the cycle so compilation is a DAG. Mutual recursion belongs in one file.",
                      {{location, note}});
}

}  // namespace

bool ModuleGraph::has_uses() const {
    for (const auto& module : modules) {
        if (module.program != nullptr && !module.program->uses.empty()) {
            return true;
        }
    }
    return false;
}

std::string default_stdlib_root() {
    if (const char* env = std::getenv("YUKT_STD")) {
        if (env[0] != '\0') {
            return env;
        }
    }
#ifdef YUKT_STDLIB_DIR
    {
        const std::string configured = YUKT_STDLIB_DIR;
        if (!configured.empty()) {
            return configured;
        }
    }
#endif
    std::error_code ec;
    fs::path exe;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        exe = fs::canonical(fs::path(buffer.c_str()), ec);
    }
#elif defined(__linux__)
    exe = fs::canonical("/proc/self/exe", ec);
#endif
    if (exe.empty() || ec) {
        return {};
    }
    return (exe.parent_path().parent_path() / "share" / "yukt" / "std").string();
}

bool is_stdlib_runtime_ir(const std::string& ir_name) {
    if (ir_name == "std$str$to_int" || ir_name == "std$str$to_float") {
        return false;
    }
    if (ir_name.rfind("std$str$", 0) == 0 || ir_name.rfind("std$list$", 0) == 0 ||
        ir_name.rfind("std$sort$", 0) == 0 || ir_name.rfind("std$math$", 0) == 0) {
        return true;
    }
    if (ir_name == "std$stat$min_i" || ir_name == "std$stat$max_i" || ir_name == "std$stat$min_f" ||
        ir_name == "std$stat$max_f" || ir_name == "std$stat$mean_f") {
        return true;
    }
    return ir_name == "std$map$new" || ir_name == "std$map$set" || ir_name == "std$map$has" ||
           ir_name == "std$map$get_unchecked" || ir_name == "std$map$len" || ir_name == "std$map$remove" ||
           ir_name == "std$set$new" || ir_name == "std$set$add" || ir_name == "std$set$has" ||
           ir_name == "std$set$len" || ir_name == "std$set$remove" ||            ir_name == "std$file$exists" ||
           ir_name == "std$file$read_unchecked" || ir_name == "std$file$write_unchecked" ||
           ir_name == "std$json$parse_unchecked" || ir_name == "std$json$is_valid" ||
           ir_name == "std$json$has_int" || ir_name == "std$json$has_string" ||
           ir_name == "std$json$get_int_unchecked" || ir_name == "std$json$get_string_unchecked" ||
           ir_name == "std$json$encode" || ir_name == "std$io$program" || ir_name == "std$io$args" ||
           ir_name == "std$io$read_line_unchecked" || ir_name == "std$io$read_line_status" ||
           ir_name == "std$io$read_all_unchecked" || ir_name == "std$io$read_all_status" ||
           ir_name == "std$env$get_unchecked" || ir_name == "std$env$has" || ir_name == "std$env$set" ||
           ir_name == "std$sync$channel" || ir_name == "std$sync$receiver" || ir_name == "std$sync$send" ||
           ir_name == "std$sync$recv_status" || ir_name == "std$sync$recv_unchecked" ||
           ir_name == "std$sync$close" || ir_name == "std$net$listen_status" || ir_name == "std$net$listen_unchecked" ||
           ir_name == "std$net$connect_status" || ir_name == "std$net$connect_unchecked" ||
           ir_name == "std$net$accept_status" || ir_name == "std$net$accept_unchecked" || ir_name == "std$net$addr" ||
           ir_name == "std$net$port" || ir_name == "std$net$read_status" || ir_name == "std$net$read_unchecked" ||
           ir_name == "std$net$write_status" || ir_name == "std$net$write_unchecked" || ir_name == "std$net$close" ||
           ir_name == "std$http$get_status" || ir_name == "std$http$get_code" || ir_name == "std$http$get_headers" ||
           ir_name == "std$http$get_body" || ir_name == "std$http$serve" || ir_name == "std$http$serve_on";
}

std::string mangle_function(const std::string& prefix, const std::string& name, bool entry_main, bool single_file) {
    if (entry_main && name == "main") {
        return "main";
    }
    if (single_file) {
        return name;
    }
    if (prefix.empty()) {
        return name;
    }
    return prefix + "$" + name;
}

std::string module_prefix_for_path(const std::string& canonical_path, const std::string& entry_dir,
                                   const std::string& stdlib_root, bool is_std) {
    std::error_code ec;
    const fs::path file(canonical_path);
    if (is_std && !stdlib_root.empty()) {
        fs::path relative = fs::relative(file, fs::path(stdlib_root), ec);
        if (ec) {
            relative = file.stem();
        }
        relative.replace_extension();
        return "std$" + slash_to_dollar(relative.generic_string());
    }
    fs::path relative = fs::relative(file, fs::path(entry_dir), ec);
    if (ec) {
        return file.stem().string();
    }
    relative.replace_extension();
    return slash_to_dollar(relative.generic_string());
}

void load_module_graph(ModuleGraph& graph, const std::string& entry_path, DiagnosticEngine& diagnostics,
                       const std::string& stdlib_root) {
    graph.stdlib_root = stdlib_root;
    std::error_code ec;
    const fs::path entry = fs::absolute(entry_path, ec);
    graph.entry_dir = entry.parent_path().string();

    std::unordered_map<std::string, int> index_by_canonical;
    std::vector<int> pending;

    auto load_one = [&](const std::string& path, bool is_entry, bool is_std) -> int {
        std::string error;
        std::string source = read_file(path, error);
        if (!error.empty() && source.empty()) {
            diagnostics.error(SourceLocation{1, 1, path}, "Cannot find module", error,
                              "Check the file path and try again.");
            return -1;
        }

        std::error_code canon_ec;
        std::string canonical = fs::canonical(fs::path(path), canon_ec).string();
        if (canon_ec) {
            canonical = path;
        }
        if (const auto found = index_by_canonical.find(canonical); found != index_by_canonical.end()) {
            return found->second;
        }

        diagnostics.add_source(canonical, source);

        LoadedModule module;
        module.canonical_path = canonical;
        module.source = std::move(source);
        module.is_entry = is_entry;
        module.mangle_prefix = module_prefix_for_path(canonical, graph.entry_dir, graph.stdlib_root, is_std);

        Lexer lexer(module.source, diagnostics);
        module.tokens = lexer.tokenize();
        for (auto& token : module.tokens) {
            token.location.file = canonical;
        }
        Parser parser(module.tokens, diagnostics);
        module.program = parser.parse_program();

        const int index = static_cast<int>(graph.modules.size());
        index_by_canonical[canonical] = index;
        graph.modules.push_back(std::move(module));
        graph.imports.emplace_back();
        pending.push_back(index);
        return index;
    };

    const int entry_index = load_one(entry.string(), true, false);
    if (entry_index < 0) {
        return;
    }

    while (!pending.empty()) {
        const int index = pending.back();
        pending.pop_back();
        if (graph.modules[static_cast<std::size_t>(index)].program == nullptr) {
            continue;
        }
        const std::string importer_dir =
            fs::path(graph.modules[static_cast<std::size_t>(index)].canonical_path).parent_path().string();
        const std::string canonical = graph.modules[static_cast<std::size_t>(index)].canonical_path;
        const std::size_t use_count = graph.modules[static_cast<std::size_t>(index)].program->uses.size();
        graph.modules[static_cast<std::size_t>(index)].import_modules.assign(use_count, -1);
        for (std::size_t i = 0; i < use_count; ++i) {
            const UseDecl use = graph.modules[static_cast<std::size_t>(index)].program->uses[i];
            SourceLocation use_location = use.use_location;
            use_location.file = canonical;
            if (use.path.empty()) {
                continue;
            }
            if (use.path[0] == "std" && use.path.size() < 2) {
                diagnostics.error(use_location, "Invalid module path",
                                  "`use std` is not a module. Standard-library imports need a path after `std`.",
                                  "Write `use std/math`.");
                continue;
            }
            bool invalid_component = false;
            for (const auto& part : use.path) {
                if (part == "." || part == "..") {
                    diagnostics.error(use_location, "Invalid module path",
                                      "Module paths cannot contain `.` or `..`.",
                                      "Use a path relative to the importer or `std/`, without parent segments.");
                    invalid_component = true;
                    break;
                }
            }
            if (invalid_component) {
                continue;
            }
            const ResolveResult resolved = resolve_use(use, importer_dir, graph.entry_dir, graph.stdlib_root);
            if (!resolved.ok) {
                std::vector<DiagnosticNote> notes;
                for (const auto& tried : resolved.tried) {
                    notes.push_back(DiagnosticNote{use_location, "looked in `" + tried + "`"});
                }
                if (notes.empty()) {
                    notes.push_back(DiagnosticNote{use_location, "no search path was available"});
                }
                diagnostics.error(use_location, "Cannot find module",
                                  "Could not find module `" + join_components(use.path, 0) + "`.",
                                  "Put a `.yukt` file at one of the searched paths, or fix the `use` path.",
                                  std::move(notes));
                continue;
            }
            const int target = load_one(resolved.canonical, false, resolved.is_std);
            if (target < 0) {
                continue;
            }
            if (target == index) {
                diagnostics.error(use_location, "Circular import", "A module cannot import itself.",
                                  "Remove this `use`.");
            }
            graph.modules[static_cast<std::size_t>(index)].import_modules[i] = target;
            graph.imports[static_cast<std::size_t>(index)].push_back(target);
        }
    }

    if (graph.modules.empty()) {
        return;
    }
    if (graph.has_uses() && graph.modules[0].program != nullptr) {
        bool has_other = false;
        for (const auto& function : graph.modules[0].program->functions) {
            if (function != nullptr && function->name != "main") {
                has_other = true;
                break;
            }
        }
        const std::string stem = fs::path(graph.modules[0].canonical_path).stem().string();
        if (has_other && !is_identifier(stem)) {
            diagnostics.error(SourceLocation{1, 1, graph.modules[0].canonical_path}, "Invalid entry file name",
                              "The entry file stem must be a YUKT identifier when the file defines functions other "
                              "than `main`.",
                              "Rename the file to an identifier such as `app.yukt`.");
        }
    }
    std::vector<int> color(graph.modules.size(), 0);
    std::vector<int> stack;
    std::vector<int> cycle;
    for (int i = 0; i < static_cast<int>(graph.modules.size()); ++i) {
        if (color[static_cast<std::size_t>(i)] == 0) {
            if (color_dfs(i, graph.imports, color, stack, cycle) != 0) {
                report_cycle(graph, diagnostics, cycle);
                break;
            }
        }
    }
}

}  // namespace yukt

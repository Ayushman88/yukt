#include "backend/explain.hpp"

#include <sstream>
#include <string>
#include <string_view>

namespace yukt {
namespace {

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

std::string function_body(const std::string& llvm_ir, const std::string& mangled) {
    const std::string marker = "@" + mangled + "(";
    std::size_t pos = 0;
    while ((pos = llvm_ir.find(marker, pos)) != std::string::npos) {
        const std::size_t line = llvm_ir.rfind('\n', pos);
        const std::size_t start = line == std::string::npos ? 0 : line + 1;
        if (llvm_ir.compare(start, 7, "define ") == 0) {
            const std::size_t open = llvm_ir.find('{', pos);
            if (open == std::string::npos) {
                return {};
            }
            int depth = 1;
            std::size_t i = open + 1;
            while (i < llvm_ir.size() && depth > 0) {
                if (llvm_ir[i] == '{') {
                    ++depth;
                } else if (llvm_ir[i] == '}') {
                    --depth;
                }
                ++i;
            }
            return llvm_ir.substr(open, i - open);
        }
        pos += marker.size();
    }
    return {};
}

bool function_has(const std::string& llvm_ir, const std::string& mangled, std::string_view needle) {
    const auto body = function_body(llvm_ir, mangled);
    return !body.empty() && contains(body, needle);
}

std::string format_count(std::int64_t value) {
    if (value < 0) {
        return "unknown";
    }
    return std::to_string(value);
}

bool iterator_metadata(const std::string& llvm_ir, const std::string& iterator) {
    return contains(llvm_ir, "\"yukt.iterator\", !\"" + iterator + "\"") ||
           contains(llvm_ir, "\"yukt.iterator\", \"" + iterator + "\"");
}

}  // namespace

std::string explain_execution(const IrModule& ir, const std::string& llvm_ir) {
    std::ostringstream out;
    out << "YUKT execution explanation for " << (ir.source_path.empty() ? "<source>" : ir.source_path) << "\n\n";

    const std::size_t parallel_calls = count_occurrences(llvm_ir, "call void @yukt_parallel_for");
    const std::size_t vector_bodies = count_occurrences(llvm_ir, "vector.body");
    const std::size_t vector_reduces = count_occurrences(llvm_ir, "llvm.vector.reduce");
    const std::size_t assumes = count_occurrences(llvm_ir, "llvm.assume");
    const bool nsw = contains(llvm_ir, " nsw ");
    const bool alwaysinline = contains(llvm_ir, "alwaysinline");
    const bool mustprogress = contains(llvm_ir, "llvm.loop.mustprogress");

    out << "Loops\n";
    if (ir.decisions.empty()) {
        out << "  (no repeat loops)\n";
    }
    for (const auto& decision : ir.decisions) {
        const std::string mangled = "yukt_" + decision.function;
        const bool in_function_vector =
            function_has(llvm_ir, mangled, "vector.body") ||
            function_has(llvm_ir, mangled, "llvm.vector.reduce");
        const bool thunk_vector = contains(llvm_ir, "@yukt_par_thunk") &&
                                  (vector_bodies > 0 || vector_reduces > 0);
        const bool parallel_verified = decision.parallel && parallel_calls > 0;
        const bool parallel_mismatch = decision.parallel && parallel_calls == 0;
        const bool outlined_body = decision.reason.find("already inside a parallel region") != std::string::npos;
        const bool vector_verified =
            (decision.parallel || outlined_body) ? thunk_vector : in_function_vector;
        const bool seq_loop_present = iterator_metadata(llvm_ir, decision.iterator);

        out << "  repeat `" << decision.iterator << "` in `" << decision.function << "` at "
            << decision.location.line << ":" << decision.location.column << "\n";
        out << "    planned: " << (decision.parallel ? "parallel" : "sequential") << "\n";
        out << "    independent: " << (decision.independent ? "yes" : "no") << "\n";
        out << "    nest depth: " << decision.nest_depth << "\n";
        out << "    estimated trip: " << format_count(decision.estimated_trip) << "\n";
        out << "    estimated work: " << format_count(decision.estimated_work) << "\n";
        out << "    work per iteration: " << format_count(decision.work_per_iter) << "\n";
        out << "    reason: " << (decision.reason.empty() ? "(none)" : decision.reason) << "\n";
        if (parallel_mismatch) {
            out << "    verified parallel: no — YUKT IR planned parallel but LLVM IR has no yukt_parallel_for\n";
        } else if (parallel_verified) {
            out << "    verified parallel: yes — LLVM IR contains yukt_parallel_for\n";
        } else {
            out << "    verified parallel: no — LLVM IR has no yukt_parallel_for for this decision\n";
        }
        if (vector_verified) {
            out << "    verified vectorized: yes — LLVM IR contains vector.body or llvm.vector.reduce";
            if (decision.parallel || outlined_body) {
                out << " in a parallel thunk";
            } else {
                out << " in `" << mangled << "`";
            }
            out << "\n";
        } else {
            out << "    verified vectorized: no — no vector.body or llvm.vector.reduce attributed to this loop\n";
        }
        if (!decision.parallel && seq_loop_present) {
            out << "    verified sequential loop: yes — yukt.iterator metadata for `" << decision.iterator << "`\n";
        }
        out << "\n";
    }

    out << "LLVM optimizations (only listed when observed in generated IR, except the pipeline itself)\n";
    out << "  applied: LLVM PassBuilder default O3 pipeline (mem2reg, instcombine, loop vectorize, SLP, inlining)\n";
    out << "  yukt_parallel_for calls: " << parallel_calls << "\n";
    out << "  vector.body labels: " << vector_bodies << "\n";
    out << "  llvm.vector.reduce uses: " << vector_reduces << "\n";
    out << "  llvm.assume: " << assumes << (assumes > 0 ? " (alignment hints present)" : "") << "\n";
    out << "  nsw arithmetic: " << (nsw ? "yes" : "not observed") << "\n";
    out << "  llvm.loop.mustprogress: " << (mustprogress ? "yes" : "not observed") << "\n";
    out << "  alwaysinline on outlined helpers: " << (alwaysinline ? "yes" : "not observed") << "\n";
    out << "\n";
    out << "Investigated, not applied as YUKT-IR transforms (no general legality proof yet)\n";
    out << "  loop fusion, loop interchange, compiler-driven unroll, parallel reductions\n";
    out << "  LLVM O3 may still unroll, vectorize, inline, and eliminate dead code on its own.\n";
    return out.str();
}

}  // namespace yukt

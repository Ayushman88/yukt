#include "driver/driver.hpp"
#include "ir/ir.hpp"
#include "ir/lower.hpp"
#include "test_harness.hpp"

#include <sstream>
#include <string>

#ifndef YUKT_SOURCE_DIR
#define YUKT_SOURCE_DIR "."
#endif

namespace {

yukt::IrModule ir_from(const std::string& source) {
    auto result = yukt::analyze_source("<test>", source);
    yukt::analyze_types(result);
    return yukt::lower_to_ir(*result.program, result.semantic, "<test>");
}

const yukt::IrFunction* function_named(const yukt::IrModule& module, const std::string& name) {
    for (const auto& function : module.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

bool has_op(const yukt::IrFunction& function, yukt::IrOp op) {
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.insts) {
            if (inst.op == op) {
                return true;
            }
        }
    }
    return false;
}

bool has_bin(const yukt::IrFunction& function, yukt::IrBinOp op) {
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.insts) {
            if (inst.op == yukt::IrOp::Bin && inst.bin == op) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

int main() {
    {
        const auto module = ir_from(
            "create greet(name):\n"
            "    say \"Hello, \" + name\n"
            "\n"
            "create main:\n"
            "    name = \"World\"\n"
            "    greet(name)\n"
            "    repeat i in 1..5:\n"
            "        say i\n"
            "    say \"YUKT is running!\"\n");
        const auto* greet = function_named(module, "greet");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(greet != nullptr);
        REQUIRE(main_fn != nullptr);
        REQUIRE(greet != nullptr && has_bin(*greet, yukt::IrBinOp::Concat));
        REQUIRE(greet != nullptr && has_op(*greet, yukt::IrOp::Say));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::Call));
        REQUIRE(main_fn != nullptr && main_fn->blocks.size() >= 3);

        std::ostringstream printed;
        yukt::print_ir(module, printed);
        REQUIRE(printed.str().find("function greet") != std::string::npos);
        REQUIRE(printed.str().find("function main") != std::string::npos);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    value = 1 + 2 * 3\n"
            "    say value\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_bin(*main_fn, yukt::IrBinOp::Add));
        REQUIRE(main_fn != nullptr && has_bin(*main_fn, yukt::IrBinOp::Mul));
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = [1, 2, 3]\n"
            "    say items\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::List));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ListSet));
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = zeros(8)\n"
            "    items[0] = 1\n"
            "    say items[0]\n"
            "    say count(items)\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::Zeros));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ListGet));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ListLen));
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = zeros(16)\n"
            "    repeat i in 0..15:\n"
            "        items[i] = i\n"
            "    total = 0\n"
            "    repeat i in 0..15:\n"
            "        total = total + items[i]\n"
            "    say total\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && !has_op(*main_fn, yukt::IrOp::ParallelFor));
        REQUIRE(function_named(module, "$par$0") == nullptr);
        REQUIRE(module.decisions.size() >= 2);
        for (const auto& decision : module.decisions) {
            REQUIRE(!decision.parallel);
        }
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 8\n"
            "    items = zeros(n)\n"
            "    last = n - 1\n"
            "    repeat i in 0..last:\n"
            "        repeat j in 0..last:\n"
            "            items[j] = items[j] + i\n"
            "    checksum = 0\n"
            "    repeat j in 0..last:\n"
            "        checksum = checksum + items[j]\n"
            "    say checksum\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr);
        REQUIRE(main_fn != nullptr && !has_op(*main_fn, yukt::IrOp::ParallelFor));
        REQUIRE(function_named(module, "$par$0") == nullptr);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat p in 1..2:\n"
            "        repeat i in 0..n - 1:\n"
            "            items[i] = i + p\n"
            "    say items[0]\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ParallelFor));
        REQUIRE(function_named(module, "$par$0") != nullptr);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 8\n"
            "    a = zeros(n)\n"
            "    b = zeros(n)\n"
            "    c = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        a[i] = 1\n"
            "        b[i] = 2\n"
            "    repeat i in 0..n - 1:\n"
            "        total = 0\n"
            "        repeat k in 0..n - 1:\n"
            "            total = total + a[k] * b[k]\n"
            "        c[i] = total\n"
            "    checksum = 0\n"
            "    repeat i in 0..n - 1:\n"
            "        checksum = checksum + c[i]\n"
            "    say checksum\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && !has_op(*main_fn, yukt::IrOp::ParallelFor));
        REQUIRE(function_named(module, "$par$0") == nullptr);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = i\n"
            "    say items[0]\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ParallelFor));
        REQUIRE(function_named(module, "$par$0") != nullptr);
        bool planned_parallel = false;
        for (const auto& decision : module.decisions) {
            if (decision.iterator == "i" && decision.parallel) {
                planned_parallel = true;
                REQUIRE(decision.independent);
                REQUIRE(decision.estimated_trip == 1000000);
            }
        }
        REQUIRE(planned_parallel);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = [1, 2, 3, 4, 5]\n"
            "    repeat i in 1..count(items):\n"
            "        items[i - 1] = i\n"
            "    say items[0]\n");
        bool saw = false;
        for (const auto& decision : module.decisions) {
            if (decision.iterator == "i") {
                REQUIRE(decision.estimated_trip == 5);
                saw = true;
            }
        }
        REQUIRE(saw);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = zeros(1000000)\n"
            "    repeat i in 0..count(items) - 1:\n"
            "        items[i] = 1\n"
            "    say items[0]\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ParallelFor));
        bool planned_parallel = false;
        for (const auto& decision : module.decisions) {
            if (decision.iterator == "i" && decision.parallel) {
                planned_parallel = true;
                REQUIRE(decision.estimated_trip == 1000000);
            }
        }
        REQUIRE(planned_parallel);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 1000000\n"
            "    items = zeros(n)\n"
            "    repeat i in 0..n - 1:\n"
            "        items[i] = 1\n"
            "    total = 0\n"
            "    repeat i in 0..n - 1:\n"
            "        total = total + items[i]\n"
            "    say total\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::ParallelFor));
        bool saw_reduction = false;
        for (const auto& decision : module.decisions) {
            if (decision.iterator == "i" && decision.reduction == "total") {
                saw_reduction = true;
                REQUIRE(decision.parallel);
                REQUIRE(decision.reduction_op == "add");
            }
        }
        REQUIRE(saw_reduction);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    n = 1000000\n"
            "    repeat i in 0..n - 1:\n"
            "        tmp = zeros(1)\n"
            "    say 1\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && !has_op(*main_fn, yukt::IrOp::ParallelFor));
        bool saw_alloc = false;
        for (const auto& decision : module.decisions) {
            if (decision.iterator == "i") {
                REQUIRE(!decision.parallel);
                REQUIRE(decision.reason.find("allocates") != std::string::npos);
                saw_alloc = true;
            }
        }
        REQUIRE(saw_alloc);
    }

    {
        const auto module = ir_from(
            "create main:\n"
            "    items = zeros(100)\n"
            "    repeat i in 0..99:\n"
            "        items[i] = i\n"
            "    say items[0]\n");
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr && !has_op(*main_fn, yukt::IrOp::ParallelFor));
    }

    {
        const auto module = ir_from(
            "record Point:\n"
            "    x: integer\n"
            "    y: integer\n"
            "\n"
            "create main:\n"
            "    p = Point(1, 2)\n"
            "    say p.x\n"
            "    p.y = 9\n"
            "    points = [p]\n"
            "    say points[0]\n");
        REQUIRE(module.records.size() == 1);
        REQUIRE(module.records[0].name == "Point");
        REQUIRE(module.records[0].size == 16);
        const auto* main_fn = function_named(module, "main");
        REQUIRE(main_fn != nullptr);
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::RecordNew));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::FieldGet));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::FieldSet));
        REQUIRE(main_fn != nullptr && has_op(*main_fn, yukt::IrOp::List));
        std::ostringstream printed;
        yukt::print_ir(module, printed);
        REQUIRE(printed.str().find("record Point") != std::string::npos);
    }

    return yukt::test::summary("ir");
}

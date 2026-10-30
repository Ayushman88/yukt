#include "backend/llvm_backend.hpp"
#include "driver/module.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef YUKT_LLVM_CLANG
#define YUKT_LLVM_CLANG ""
#endif

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace yukt {
namespace {

std::string quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string first_line_from(const char* command) {
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return {};
    }
    char buffer[512];
    std::string line;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
    }
    pclose(pipe);
    return line;
}

bool path_executable(const std::string& path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::string find_clang() {
    if (const char* env = std::getenv("YUKT_CLANG")) {
        if (env[0] != '\0') {
            return env;
        }
    }
    const std::string from_llvm = YUKT_LLVM_CLANG;
    if (path_executable(from_llvm)) {
        return from_llvm;
    }
#if defined(__APPLE__)
    const std::string path = first_line_from("xcrun -f clang 2>/dev/null");
    if (!path.empty()) {
        return path;
    }
#endif
    return "clang";
}

std::string macos_sysroot() {
#if defined(__APPLE__)
    return first_line_from("xcrun --show-sdk-path 2>/dev/null");
#else
    return {};
#endif
}

void initialize_native_targets() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    initialized = true;
}

llvm::Type* llvm_type(llvm::LLVMContext& context, const IrType& type) {
    switch (type.kind) {
        case IrTypeKind::I64:
            return llvm::Type::getInt64Ty(context);
        case IrTypeKind::F64:
            return llvm::Type::getDoubleTy(context);
        case IrTypeKind::Bool:
            return llvm::Type::getInt1Ty(context);
        case IrTypeKind::String:
        case IrTypeKind::List:
            return llvm::PointerType::getUnqual(context);
        case IrTypeKind::Record:
            return llvm::PointerType::getUnqual(context);
        case IrTypeKind::Void:
            return llvm::Type::getVoidTy(context);
    }
    return llvm::Type::getVoidTy(context);
}

std::string mangle(const std::string& name) {
    return "yukt_" + name;
}

class LlvmEmitter {
public:
    explicit LlvmEmitter(const IrModule& ir);

    bool emit_text(std::string& out, std::string& error);
    bool emit_object(const std::string& path, std::string& error);

private:
    bool build(std::string& error);
    bool configure_target(std::string& error);
    void optimize();
    void declare_runtime();
    void declare_functions();
    void emit_functions();
    void emit_function(const IrFunction& function);
    llvm::Value* materialize(const IrValue& value);
    llvm::Value* emit_inst(const IrInst& inst);
    llvm::Value* emit_stdlib_call(const IrInst& inst);
    llvm::FunctionCallee runtime(const std::string& name) const;
    llvm::StructType* list_struct_type();
    llvm::Value* list_data_pointer(llvm::Value* list);
    void attach_loop_metadata(llvm::Instruction* latch, const std::string& iterator);
    llvm::Type* type_of(const IrType& type);
    const IrRecord* find_record(int id) const;
    llvm::StructType* record_type(int id);
    unsigned record_llvm_index(int record_id, int field_index);
    llvm::Type* stored_field_type(const IrType& type);
    llvm::Value* to_stored_field(llvm::Value* value, const IrType& type);
    llvm::Value* from_stored_field(llvm::Value* value, const IrType& type);
    llvm::Value* emit_record_eq(llvm::Value* left, llvm::Value* right, int record_id);
    bool type_has_record(const IrType& type) const;
    std::int64_t record_byte_size(int id) const;
    llvm::Value* cstr_ptr(const std::string& text);
    void print_cstr(const std::string& text);
    llvm::Value* list_get_value(llvm::Value* list, llvm::Value* index, const IrType& elem);
    void emit_print_value(llvm::Value* value, const IrType& type);
    void emit_print_record(llvm::Value* value, int record_id);
    void emit_print_list(llvm::Value* list, const IrType& type);

    const IrModule& ir_;
    llvm::LLVMContext context_;
    llvm::Module module_;
    llvm::IRBuilder<> builder_;
    std::unique_ptr<llvm::TargetMachine> machine_;
    std::unordered_map<std::string, llvm::Function*> functions_;
    std::unordered_map<std::string, llvm::FunctionCallee> runtime_;
    std::unordered_map<int, llvm::Value*> temps_;
    std::unordered_map<std::string, llvm::AllocaInst*> locals_;
    llvm::Function* current_fn_ = nullptr;
    int next_thunk_ = 0;
    std::unordered_map<int, llvm::StructType*> record_types_;
    std::unordered_map<int, std::vector<unsigned>> record_llvm_indices_;
};

LlvmEmitter::LlvmEmitter(const IrModule& ir)
    : ir_(ir), module_(ir.source_path.empty() ? "yukt" : ir.source_path, context_), builder_(context_) {}

bool LlvmEmitter::emit_text(std::string& out, std::string& error) {
    if (!build(error)) {
        return false;
    }
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    module_.print(stream, nullptr);
    out = stream.str();
    return true;
}

bool LlvmEmitter::emit_object(const std::string& path, std::string& error) {
    if (!build(error)) {
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error = "could not write object file `" + path + "`: " + ec.message();
        return false;
    }

    llvm::legacy::PassManager pass;
    if (machine_->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        error = "the LLVM target cannot emit an object file";
        return false;
    }
    pass.run(module_);
    dest.flush();
    return true;
}

llvm::StringMap<bool> host_cpu_features() {
#if LLVM_VERSION_MAJOR >= 21
    return llvm::sys::getHostCPUFeatures();
#else
    llvm::StringMap<bool> features;
    llvm::sys::getHostCPUFeatures(features);
    return features;
#endif
}

bool LlvmEmitter::configure_target(std::string& error) {
    initialize_native_targets();
    const std::string triple_str = llvm::sys::getDefaultTargetTriple();
    std::string lookup_error;
#if LLVM_VERSION_MAJOR >= 21
    const llvm::Triple triple(triple_str);
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
#else
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple_str, lookup_error);
#endif
    if (target == nullptr) {
        error = lookup_error;
        return false;
    }

    const llvm::StringRef cpu = llvm::sys::getHostCPUName();
    const llvm::StringMap<bool> features = host_cpu_features();
    std::string feature_str;
    for (const auto& feature : features) {
        feature_str += feature.getValue() ? '+' : '-';
        feature_str += feature.getKey().str();
        feature_str += ',';
    }

    const llvm::TargetOptions options;
    auto reloc = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
#if LLVM_VERSION_MAJOR >= 21
    machine_.reset(target->createTargetMachine(triple, cpu, feature_str, options, reloc));
#else
    machine_.reset(target->createTargetMachine(triple_str, cpu, feature_str, options, reloc));
#endif
    if (!machine_) {
        error = "failed to create an LLVM target machine for " + triple_str;
        return false;
    }

    module_.setDataLayout(machine_->createDataLayout());
#if LLVM_VERSION_MAJOR >= 21
    module_.setTargetTriple(triple);
#else
    module_.setTargetTriple(triple_str);
#endif
    return true;
}

void LlvmEmitter::optimize() {
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager cgscc;
    llvm::ModuleAnalysisManager modules;
    llvm::PassBuilder builder(machine_.get());
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);
    llvm::ModulePassManager pipeline = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
    pipeline.run(module_, modules);
}

bool LlvmEmitter::build(std::string& error) {
    if (!configure_target(error)) {
        return false;
    }
    declare_runtime();
    declare_functions();
    emit_functions();
    optimize();

    std::string verify_error;
    llvm::raw_string_ostream stream(verify_error);
    if (llvm::verifyModule(module_, &stream)) {
        error = stream.str();
        return false;
    }
    return true;
}

void LlvmEmitter::declare_runtime() {
    auto* void_ty = llvm::Type::getVoidTy(context_);
    auto* i64 = llvm::Type::getInt64Ty(context_);
    auto* f64 = llvm::Type::getDoubleTy(context_);
    auto* i8 = llvm::Type::getInt8Ty(context_);
    auto* ptr = llvm::PointerType::getUnqual(context_);

    runtime_["yukt_say_i64"] = module_.getOrInsertFunction("yukt_say_i64", void_ty, i64);
    runtime_["yukt_say_f64"] = module_.getOrInsertFunction("yukt_say_f64", void_ty, f64);
    runtime_["yukt_say_bool"] = module_.getOrInsertFunction("yukt_say_bool", void_ty, i8);
    runtime_["yukt_say_cstr"] = module_.getOrInsertFunction("yukt_say_cstr", void_ty, ptr);
    runtime_["yukt_say_list"] = module_.getOrInsertFunction("yukt_say_list", void_ty, ptr);
    runtime_["yukt_str_concat"] = module_.getOrInsertFunction("yukt_str_concat", ptr, ptr, ptr);
    runtime_["yukt_list_new"] = module_.getOrInsertFunction("yukt_list_new", ptr, i64, i64);
    runtime_["yukt_list_new_rec"] = module_.getOrInsertFunction("yukt_list_new_rec", ptr, i64, i64);
    runtime_["yukt_list_set_rec"] = module_.getOrInsertFunction("yukt_list_set_rec", void_ty, ptr, i64, ptr, i64);
    runtime_["yukt_list_get_rec"] = module_.getOrInsertFunction("yukt_list_get_rec", void_ty, ptr, i64, ptr, i64);
    runtime_["yukt_print_i64"] = module_.getOrInsertFunction("yukt_print_i64", void_ty, i64);
    runtime_["yukt_print_f64"] = module_.getOrInsertFunction("yukt_print_f64", void_ty, f64);
    runtime_["yukt_print_bool"] = module_.getOrInsertFunction("yukt_print_bool", void_ty, i8);
    runtime_["yukt_print_cstr"] = module_.getOrInsertFunction("yukt_print_cstr", void_ty, ptr);
    runtime_["yukt_list_set_i64"] = module_.getOrInsertFunction("yukt_list_set_i64", void_ty, ptr, i64, i64);
    runtime_["yukt_list_set_f64"] = module_.getOrInsertFunction("yukt_list_set_f64", void_ty, ptr, i64, f64);
    runtime_["yukt_list_set_bool"] = module_.getOrInsertFunction("yukt_list_set_bool", void_ty, ptr, i64, i8);
    runtime_["yukt_list_set_cstr"] = module_.getOrInsertFunction("yukt_list_set_cstr", void_ty, ptr, i64, ptr);
    runtime_["yukt_list_set_list"] = module_.getOrInsertFunction("yukt_list_set_list", void_ty, ptr, i64, ptr);
    runtime_["yukt_list_get_i64"] = module_.getOrInsertFunction("yukt_list_get_i64", i64, ptr, i64);
    runtime_["yukt_list_get_f64"] = module_.getOrInsertFunction("yukt_list_get_f64", f64, ptr, i64);
    runtime_["yukt_list_get_bool"] = module_.getOrInsertFunction("yukt_list_get_bool", i8, ptr, i64);
    runtime_["yukt_list_get_cstr"] = module_.getOrInsertFunction("yukt_list_get_cstr", ptr, ptr, i64);
    runtime_["yukt_list_get_list"] = module_.getOrInsertFunction("yukt_list_get_list", ptr, ptr, i64);
    runtime_["yukt_list_len"] = module_.getOrInsertFunction("yukt_list_len", i64, ptr);
    runtime_["yukt_list_zeros"] = module_.getOrInsertFunction("yukt_list_zeros", ptr, i64);
    runtime_["yukt_parallel_for"] =
        module_.getOrInsertFunction("yukt_parallel_for", void_ty, i64, i64, ptr, ptr, i64);
    runtime_["yukt_reduce_i64"] = module_.getOrInsertFunction("yukt_reduce_i64", void_ty, ptr, i64, i64);
    runtime_["yukt_reduce_f64"] = module_.getOrInsertFunction("yukt_reduce_f64", void_ty, ptr, f64, i64);
    runtime_["yukt_runtime_release"] = module_.getOrInsertFunction("yukt_runtime_release", void_ty);
    runtime_["yukt_release"] = module_.getOrInsertFunction("yukt_release", void_ty, ptr);
    runtime_["yukt_str_from_lit"] = module_.getOrInsertFunction("yukt_str_from_lit", ptr, ptr);
    runtime_["yukt_str_clone"] = module_.getOrInsertFunction("yukt_str_clone", ptr, ptr);
    runtime_["yukt_str_len"] = module_.getOrInsertFunction("yukt_str_len", i64, ptr);
    runtime_["yukt_str_at"] = module_.getOrInsertFunction("yukt_str_at", i64, ptr, i64);
    runtime_["yukt_str_slice"] = module_.getOrInsertFunction("yukt_str_slice", ptr, ptr, i64, i64);
    runtime_["yukt_str_find"] = module_.getOrInsertFunction("yukt_str_find", i64, ptr, ptr);
    runtime_["yukt_str_contains"] = module_.getOrInsertFunction("yukt_str_contains", i8, ptr, ptr);
    runtime_["yukt_str_starts"] = module_.getOrInsertFunction("yukt_str_starts", i8, ptr, ptr);
    runtime_["yukt_str_ends"] = module_.getOrInsertFunction("yukt_str_ends", i8, ptr, ptr);
    runtime_["yukt_str_repeat"] = module_.getOrInsertFunction("yukt_str_repeat", ptr, ptr, i64);
    runtime_["yukt_str_join"] = module_.getOrInsertFunction("yukt_str_join", ptr, ptr, ptr);
    runtime_["yukt_str_split"] = module_.getOrInsertFunction("yukt_str_split", ptr, ptr, ptr);
    runtime_["yukt_list_copy"] = module_.getOrInsertFunction("yukt_list_copy", ptr, ptr);
    runtime_["yukt_list_fill_i64"] = module_.getOrInsertFunction("yukt_list_fill_i64", void_ty, ptr, i64);
    runtime_["yukt_list_fill_f64"] = module_.getOrInsertFunction("yukt_list_fill_f64", void_ty, ptr, f64);
    runtime_["yukt_list_fill_bool"] = module_.getOrInsertFunction("yukt_list_fill_bool", void_ty, ptr, i8);
    runtime_["yukt_list_fill_cstr"] = module_.getOrInsertFunction("yukt_list_fill_cstr", void_ty, ptr, ptr);
    runtime_["yukt_list_fill_list"] = module_.getOrInsertFunction("yukt_list_fill_list", void_ty, ptr, ptr);
    runtime_["yukt_list_reverse"] = module_.getOrInsertFunction("yukt_list_reverse", void_ty, ptr);
    runtime_["yukt_list_slice"] = module_.getOrInsertFunction("yukt_list_slice", ptr, ptr, i64, i64);
    runtime_["yukt_list_sort_asc"] = module_.getOrInsertFunction("yukt_list_sort_asc", void_ty, ptr);
    runtime_["yukt_list_sort_desc"] = module_.getOrInsertFunction("yukt_list_sort_desc", void_ty, ptr);
    runtime_["yukt_list_sorted"] = module_.getOrInsertFunction("yukt_list_sorted", ptr, ptr);
    runtime_["yukt_list_search"] = module_.getOrInsertFunction("yukt_list_search", i64, ptr, i64, f64);
    runtime_["yukt_math_abs_i"] = module_.getOrInsertFunction("yukt_math_abs_i", i64, i64);
    runtime_["yukt_math_abs_f"] = module_.getOrInsertFunction("yukt_math_abs_f", f64, f64);
    runtime_["yukt_math_min_i"] = module_.getOrInsertFunction("yukt_math_min_i", i64, i64, i64);
    runtime_["yukt_math_max_i"] = module_.getOrInsertFunction("yukt_math_max_i", i64, i64, i64);
    runtime_["yukt_math_min_f"] = module_.getOrInsertFunction("yukt_math_min_f", f64, f64, f64);
    runtime_["yukt_math_max_f"] = module_.getOrInsertFunction("yukt_math_max_f", f64, f64, f64);
    runtime_["yukt_math_floor"] = module_.getOrInsertFunction("yukt_math_floor", f64, f64);
    runtime_["yukt_math_ceil"] = module_.getOrInsertFunction("yukt_math_ceil", f64, f64);
    runtime_["yukt_math_sqrt"] = module_.getOrInsertFunction("yukt_math_sqrt", f64, f64);
    runtime_["yukt_math_pow"] = module_.getOrInsertFunction("yukt_math_pow", f64, f64, f64);
    runtime_["yukt_math_sin"] = module_.getOrInsertFunction("yukt_math_sin", f64, f64);
    runtime_["yukt_math_cos"] = module_.getOrInsertFunction("yukt_math_cos", f64, f64);
    runtime_["yukt_math_tan"] = module_.getOrInsertFunction("yukt_math_tan", f64, f64);
    runtime_["yukt_math_exp"] = module_.getOrInsertFunction("yukt_math_exp", f64, f64);
    runtime_["yukt_math_log"] = module_.getOrInsertFunction("yukt_math_log", f64, f64);
    runtime_["yukt_stat_min_i"] = module_.getOrInsertFunction("yukt_stat_min_i", i64, ptr);
    runtime_["yukt_stat_max_i"] = module_.getOrInsertFunction("yukt_stat_max_i", i64, ptr);
    runtime_["yukt_stat_min_f"] = module_.getOrInsertFunction("yukt_stat_min_f", f64, ptr);
    runtime_["yukt_stat_max_f"] = module_.getOrInsertFunction("yukt_stat_max_f", f64, ptr);
    runtime_["yukt_stat_mean_f"] = module_.getOrInsertFunction("yukt_stat_mean_f", f64, ptr);
    if (auto* zeros = module_.getFunction("yukt_list_zeros")) {
        zeros->addRetAttr(llvm::Attribute::NoAlias);
    }
    if (auto* created = module_.getFunction("yukt_list_new")) {
        created->addRetAttr(llvm::Attribute::NoAlias);
    }
    for (auto& entry : runtime_) {
        if (auto* fn = module_.getFunction(entry.first)) {
            fn->setDoesNotThrow();
        }
    }
}

void LlvmEmitter::declare_functions() {
    for (const auto& function : ir_.functions) {
        std::vector<llvm::Type*> params;
        params.reserve(function.params.size());
        for (const auto& param : function.params) {
            params.push_back(type_of(param.second));
        }
        auto* type = llvm::FunctionType::get(type_of(function.return_type), params, false);
        const auto linkage =
            function.name == "main" ? llvm::Function::ExternalLinkage : llvm::Function::InternalLinkage;
        auto* llvm_fn = llvm::Function::Create(type, linkage, mangle(function.name), module_);
        if (function.name != "main") {
            llvm_fn->addFnAttr(function.name.rfind("$par$", 0) == 0 ? llvm::Attribute::AlwaysInline
                                                                   : llvm::Attribute::InlineHint);
        }
        llvm_fn->addFnAttr(llvm::Attribute::MustProgress);
        unsigned index = 0;
        for (auto& arg : llvm_fn->args()) {
            arg.setName(function.params[index].first);
            ++index;
        }
        functions_[function.name] = llvm_fn;
    }
}

void LlvmEmitter::emit_functions() {
    for (const auto& function : ir_.functions) {
        emit_function(function);
    }

    auto* i32 = llvm::Type::getInt32Ty(context_);
    auto* main_ty = llvm::FunctionType::get(i32, false);
    auto* main_fn = llvm::Function::Create(main_ty, llvm::Function::ExternalLinkage, "main", module_);
    auto* block = llvm::BasicBlock::Create(context_, "entry", main_fn);
    builder_.SetInsertPoint(block);
    auto found = functions_.find("main");
    if (found != functions_.end()) {
        llvm::Value* result = builder_.CreateCall(found->second->getFunctionType(), found->second);
        if (found->second->getReturnType()->isIntegerTy(64)) {
            builder_.CreateRet(builder_.CreateTrunc(result, i32));
            return;
        }
    }
    builder_.CreateRet(llvm::ConstantInt::get(i32, 0));
}

void LlvmEmitter::emit_function(const IrFunction& function) {
    current_fn_ = functions_.at(function.name);
    temps_.clear();
    locals_.clear();

    std::unordered_map<std::string, llvm::BasicBlock*> blocks;
    for (const auto& block : function.blocks) {
        blocks[block.name] = llvm::BasicBlock::Create(context_, block.name, current_fn_);
    }

    builder_.SetInsertPoint(blocks.at("entry"));
    for (const auto& local : function.locals) {
        locals_[local.first] = builder_.CreateAlloca(type_of(local.second), nullptr, local.first);
    }

    unsigned index = 0;
    for (auto& arg : current_fn_->args()) {
        const auto& name = function.params[index].first;
        builder_.CreateStore(&arg, locals_.at(name));
        ++index;
    }

    for (const auto& block : function.blocks) {
        builder_.SetInsertPoint(blocks.at(block.name));
        for (const auto& inst : block.insts) {
            llvm::Value* produced = emit_inst(inst);
            if (produced != nullptr && inst.dest.kind == IrValue::Kind::Temp) {
                temps_[inst.dest.id] = produced;
            }
        }
        switch (block.term.kind) {
            case IrTermKind::Return:
                if (function.name == "main") {
                    builder_.CreateCall(runtime("yukt_runtime_release"), {});
                }
                if (function.return_type.kind == IrTypeKind::Void) {
                    builder_.CreateRetVoid();
                } else {
                    builder_.CreateRet(materialize(block.term.value));
                }
                break;
            case IrTermKind::Jump: {
                llvm::Instruction* jump = builder_.CreateBr(blocks.at(block.term.target));
                if (block.name.rfind("body", 0) == 0 && block.term.target.rfind("loop", 0) == 0) {
                    attach_loop_metadata(jump, block.term.loop_iterator);
                }
                break;
            }
            case IrTermKind::Branch:
                builder_.CreateCondBr(materialize(block.term.value), blocks.at(block.term.target),
                                      blocks.at(block.term.otherwise));
                break;
        }
    }
}

llvm::Value* LlvmEmitter::materialize(const IrValue& value) {
    switch (value.kind) {
        case IrValue::Kind::Temp:
            return temps_.at(value.id);
        case IrValue::Kind::Local:
            return builder_.CreateLoad(type_of(value.type), locals_.at(value.name));
        case IrValue::Kind::ConstI64:
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), static_cast<uint64_t>(value.i64), true);
        case IrValue::Kind::ConstF64:
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), value.f64);
        case IrValue::Kind::ConstBool:
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context_), value.boolean ? 1 : 0);
        case IrValue::Kind::ConstString: {
            llvm::GlobalVariable* text = builder_.CreateGlobalString(value.text);
            llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
            llvm::Value* raw = builder_.CreateInBoundsGEP(text->getValueType(), text, {zero, zero});
            return builder_.CreateCall(runtime("yukt_str_from_lit"), {raw});
        }
        case IrValue::Kind::Undef:
            return llvm::PoisonValue::get(type_of(value.type));
    }
    return llvm::PoisonValue::get(llvm::Type::getInt64Ty(context_));
}

llvm::Value* LlvmEmitter::emit_inst(const IrInst& inst) {
    switch (inst.op) {
        case IrOp::Store:
            builder_.CreateStore(materialize(inst.args[0]), locals_.at(inst.name));
            return nullptr;
        case IrOp::Load:
            return builder_.CreateLoad(type_of(inst.dest.type), locals_.at(inst.name));
        case IrOp::Un: {
            llvm::Value* operand = materialize(inst.args[0]);
            if (inst.args[0].type.kind == IrTypeKind::F64) {
                return builder_.CreateFNeg(operand);
            }
            return builder_.CreateNeg(operand);
        }
        case IrOp::Bin: {
            llvm::Value* left = materialize(inst.args[0]);
            llvm::Value* right = materialize(inst.args[1]);
            if ((inst.bin == IrBinOp::Eq || inst.bin == IrBinOp::Ne) &&
                inst.args[0].type.kind == IrTypeKind::Record) {
                llvm::Value* eq = emit_record_eq(left, right, inst.args[0].type.record_id);
                if (inst.bin == IrBinOp::Ne) {
                    return builder_.CreateNot(eq);
                }
                return eq;
            }
            const bool floating = inst.args[0].type.kind == IrTypeKind::F64;
            switch (inst.bin) {
                case IrBinOp::Add:
                    return floating ? builder_.CreateFAdd(left, right) : builder_.CreateNSWAdd(left, right);
                case IrBinOp::Sub:
                    return floating ? builder_.CreateFSub(left, right) : builder_.CreateNSWSub(left, right);
                case IrBinOp::Mul:
                    return floating ? builder_.CreateFMul(left, right) : builder_.CreateNSWMul(left, right);
                case IrBinOp::Div:
                    return floating ? builder_.CreateFDiv(left, right) : builder_.CreateSDiv(left, right);
                case IrBinOp::Rem:
                    return builder_.CreateSRem(left, right);
                case IrBinOp::Concat:
                    return builder_.CreateCall(runtime("yukt_str_concat"), {left, right});
                case IrBinOp::Eq:
                    return floating ? builder_.CreateFCmpOEQ(left, right) : builder_.CreateICmpEQ(left, right);
                case IrBinOp::Ne:
                    return floating ? builder_.CreateFCmpONE(left, right) : builder_.CreateICmpNE(left, right);
                case IrBinOp::Lt:
                    return floating ? builder_.CreateFCmpOLT(left, right) : builder_.CreateICmpSLT(left, right);
                case IrBinOp::Le:
                    return floating ? builder_.CreateFCmpOLE(left, right) : builder_.CreateICmpSLE(left, right);
                case IrBinOp::Gt:
                    return floating ? builder_.CreateFCmpOGT(left, right) : builder_.CreateICmpSGT(left, right);
                case IrBinOp::Ge:
                    return floating ? builder_.CreateFCmpOGE(left, right) : builder_.CreateICmpSGE(left, right);
            }
            break;
        }
        case IrOp::Call: {
            if (inst.name == "yukt_str_clone") {
                return builder_.CreateCall(runtime("yukt_str_clone"), {materialize(inst.args[0])});
            }
            if (is_stdlib_runtime_ir(inst.name)) {
                return emit_stdlib_call(inst);
            }
            auto* callee = functions_.at(inst.name);
            std::vector<llvm::Value*> args;
            args.reserve(inst.args.size());
            for (const auto& arg : inst.args) {
                args.push_back(materialize(arg));
            }
            llvm::Value* call = builder_.CreateCall(callee->getFunctionType(), callee, args);
            if (inst.dest.type.kind == IrTypeKind::Void) {
                return nullptr;
            }
            return call;
        }
        case IrOp::List: {
            llvm::Value* length = materialize(inst.args[0]);
            const int kind = inst.dest.type.element != nullptr ? list_elem_kind(*inst.dest.type.element) : 0;
            if (kind == 5 && inst.dest.type.element != nullptr) {
                const std::int64_t stride = record_byte_size(inst.dest.type.element->record_id);
                llvm::Value* stride_value =
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), static_cast<uint64_t>(stride));
                return builder_.CreateCall(runtime("yukt_list_new_rec"), {length, stride_value});
            }
            llvm::Value* kind_value =
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), static_cast<uint64_t>(kind));
            return builder_.CreateCall(runtime("yukt_list_new"), {length, kind_value});
        }
        case IrOp::ListSet: {
            llvm::Value* list = materialize(inst.args[0]);
            llvm::Value* index = materialize(inst.args[1]);
            llvm::Value* value = materialize(inst.args[2]);
            const IrType elem = inst.args[2].type;
            if (elem.kind == IrTypeKind::I64 || elem.kind == IrTypeKind::F64) {
                llvm::Type* elem_ty = type_of(elem);
                llvm::Value* data = list_data_pointer(list);
                llvm::Value* slot = builder_.CreateInBoundsGEP(elem_ty, data, index);
                builder_.CreateStore(value, slot);
            } else if (elem.kind == IrTypeKind::Bool) {
                builder_.CreateCall(runtime("yukt_list_set_bool"),
                                    {list, index, builder_.CreateZExt(value, llvm::Type::getInt8Ty(context_))});
            } else if (elem.kind == IrTypeKind::String) {
                builder_.CreateCall(runtime("yukt_list_set_cstr"), {list, index, value});
            } else if (elem.kind == IrTypeKind::Record) {
                llvm::Type* rec_ty = type_of(elem);
                llvm::Value* tmp = builder_.CreateAlloca(rec_ty);
                builder_.CreateStore(value, tmp);
                llvm::Value* stride = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                                             static_cast<uint64_t>(record_byte_size(elem.record_id)));
                builder_.CreateCall(runtime("yukt_list_set_rec"), {list, index, tmp, stride});
            } else {
                builder_.CreateCall(runtime("yukt_list_set_list"), {list, index, value});
            }
            return nullptr;
        }
        case IrOp::ListGet: {
            llvm::Value* list = materialize(inst.args[0]);
            llvm::Value* index = materialize(inst.args[1]);
            return list_get_value(list, index, inst.dest.type);
        }
        case IrOp::ListLen: {
            llvm::Value* list = materialize(inst.args[0]);
            llvm::Value* field = builder_.CreateStructGEP(list_struct_type(), list, 0);
            return builder_.CreateLoad(llvm::Type::getInt64Ty(context_), field);
        }
        case IrOp::Zeros:
            return builder_.CreateCall(runtime("yukt_list_zeros"), {materialize(inst.args[0])});
        case IrOp::ParallelFor: {
            llvm::Value* start = materialize(inst.args[0]);
            llvm::Value* end = materialize(inst.args[1]);
            auto* void_ty = llvm::Type::getVoidTy(context_);
            auto* i64 = llvm::Type::getInt64Ty(context_);
            auto* ptr = llvm::PointerType::getUnqual(context_);
            std::vector<llvm::Type*> field_types;
            std::vector<llvm::Value*> captures;
            field_types.reserve(inst.args.size() > 2 ? inst.args.size() - 1 : 1);
            for (std::size_t i = 2; i < inst.args.size(); ++i) {
                captures.push_back(materialize(inst.args[i]));
                field_types.push_back(type_of(inst.args[i].type));
            }
            const bool reducing = !inst.reduce_local.empty();
            if (reducing) {
                captures.push_back(locals_.at(inst.reduce_local));
                field_types.push_back(ptr);
            }
            llvm::StructType* ctx_ty = llvm::StructType::get(context_, field_types);
            llvm::AllocaInst* ctx = builder_.CreateAlloca(ctx_ty, nullptr, "par.ctx");
            for (std::size_t i = 0; i < captures.size(); ++i) {
                builder_.CreateStore(captures[i], builder_.CreateStructGEP(ctx_ty, ctx, static_cast<unsigned>(i)));
            }

            auto* thunk_ty = llvm::FunctionType::get(void_ty, {i64, i64, ptr}, false);
            auto* thunk = llvm::Function::Create(thunk_ty, llvm::Function::InternalLinkage,
                                                 "yukt_par_thunk_" + std::to_string(next_thunk_++), module_);
            llvm::BasicBlock* thunk_entry = llvm::BasicBlock::Create(context_, "entry", thunk);
            {
                llvm::IRBuilder<> thunk_builder(thunk_entry);
                std::vector<llvm::Value*> call_args;
                call_args.push_back(thunk->getArg(0));
                call_args.push_back(thunk->getArg(1));
                const std::size_t capture_count = reducing ? field_types.size() - 1 : field_types.size();
                for (std::size_t i = 0; i < capture_count; ++i) {
                    llvm::Value* slot =
                        thunk_builder.CreateStructGEP(ctx_ty, thunk->getArg(2), static_cast<unsigned>(i));
                    call_args.push_back(thunk_builder.CreateLoad(field_types[i], slot));
                }
                auto* outlined = functions_.at(inst.name);
                llvm::Value* partial =
                    thunk_builder.CreateCall(outlined->getFunctionType(), outlined, call_args);
                if (reducing) {
                    llvm::Value* slot = thunk_builder.CreateStructGEP(
                        ctx_ty, thunk->getArg(2), static_cast<unsigned>(field_types.size() - 1));
                    llvm::Value* acc_ptr = thunk_builder.CreateLoad(ptr, slot);
                    llvm::Value* op = llvm::ConstantInt::get(
                        i64, inst.reduce_bin == IrBinOp::Mul ? 1 : 0, true);
                    if (outlined->getReturnType()->isDoubleTy()) {
                        thunk_builder.CreateCall(runtime("yukt_reduce_f64"), {acc_ptr, partial, op});
                    } else {
                        thunk_builder.CreateCall(runtime("yukt_reduce_i64"), {acc_ptr, partial, op});
                    }
                }
                thunk_builder.CreateRetVoid();
            }
            llvm::Value* work =
                llvm::ConstantInt::get(i64, static_cast<uint64_t>(inst.work < 1 ? 1 : inst.work), true);
            builder_.CreateCall(runtime("yukt_parallel_for"), {start, end, thunk, ctx, work});
            return nullptr;
        }
        case IrOp::Say: {
            const IrValue& value = inst.args[0];
            llvm::Value* materialized = materialize(value);
            if (value.type.kind == IrTypeKind::Record || type_has_record(value.type)) {
                emit_print_value(materialized, value.type);
                print_cstr("\n");
            } else if (value.type.kind == IrTypeKind::I64) {
                builder_.CreateCall(runtime("yukt_say_i64"), {materialized});
            } else if (value.type.kind == IrTypeKind::F64) {
                builder_.CreateCall(runtime("yukt_say_f64"), {materialized});
            } else if (value.type.kind == IrTypeKind::Bool) {
                builder_.CreateCall(runtime("yukt_say_bool"),
                                    {builder_.CreateZExt(materialized, llvm::Type::getInt8Ty(context_))});
            } else if (value.type.kind == IrTypeKind::String) {
                builder_.CreateCall(runtime("yukt_say_cstr"), {materialized});
            } else {
                builder_.CreateCall(runtime("yukt_say_list"), {materialized});
            }
            return nullptr;
        }
        case IrOp::RecordNew: {
            const int record_id = inst.dest.type.record_id;
            const IrRecord* record = find_record(record_id);
            llvm::Type* rec_ty = type_of(inst.dest.type);
            llvm::Value* rec = llvm::UndefValue::get(rec_ty);
            if (record != nullptr) {
                const std::size_t count = std::min(inst.args.size(), record->fields.size());
                for (std::size_t i = 0; i < count; ++i) {
                    llvm::Value* stored = to_stored_field(materialize(inst.args[i]), record->fields[i].type);
                    rec = builder_.CreateInsertValue(rec, stored, {record_llvm_index(record_id, static_cast<int>(i))});
                }
            }
            return rec;
        }
        case IrOp::FieldGet: {
            const IrType object_type = inst.args[0].type;
            llvm::Value* object = materialize(inst.args[0]);
            llvm::Value* loaded =
                builder_.CreateExtractValue(object, {record_llvm_index(object_type.record_id, inst.field_index)});
            const IrRecord* record = find_record(object_type.record_id);
            IrType field_type = inst.dest.type;
            if (record != nullptr && inst.field_index >= 0 &&
                static_cast<std::size_t>(inst.field_index) < record->fields.size()) {
                field_type = record->fields[static_cast<std::size_t>(inst.field_index)].type;
            }
            return from_stored_field(loaded, field_type);
        }
        case IrOp::FieldSet: {
            const IrType object_type = inst.args[0].type;
            llvm::Value* object = materialize(inst.args[0]);
            const IrRecord* record = find_record(object_type.record_id);
            IrType field_type = inst.args.size() > 1 ? inst.args[1].type : ir_i64();
            if (record != nullptr && inst.field_index >= 0 &&
                static_cast<std::size_t>(inst.field_index) < record->fields.size()) {
                field_type = record->fields[static_cast<std::size_t>(inst.field_index)].type;
            }
            llvm::Value* stored = to_stored_field(materialize(inst.args[1]), field_type);
            return builder_.CreateInsertValue(object, stored,
                                              {record_llvm_index(object_type.record_id, inst.field_index)});
        }
        case IrOp::Drop:
            if (!inst.args.empty() && inst.args[0].type.kind == IrTypeKind::Record) {
                // Record bytes are not a tracked allocation. Heap fields are
                // dropped by IR (FieldGet then Drop of each owned pointer).
                return nullptr;
            }
            builder_.CreateCall(runtime("yukt_release"), {materialize(inst.args[0])});
            return nullptr;
    }
    return nullptr;
}

llvm::Value* LlvmEmitter::emit_stdlib_call(const IrInst& inst) {
    std::vector<llvm::Value*> args;
    args.reserve(inst.args.size());
    for (const auto& arg : inst.args) {
        args.push_back(materialize(arg));
    }

    auto call_runtime = [&](const std::string& name, const std::vector<llvm::Value*>& call_args) -> llvm::Value* {
        llvm::Value* call = builder_.CreateCall(runtime(name), call_args);
        if (inst.dest.type.kind == IrTypeKind::Void) {
            return nullptr;
        }
        if (inst.dest.type.kind == IrTypeKind::Bool) {
            return builder_.CreateTrunc(call, llvm::Type::getInt1Ty(context_));
        }
        return call;
    };

    const std::string& name = inst.name;
    if (name == "std$str$len") {
        return call_runtime("yukt_str_len", {args.at(0)});
    }
    if (name == "std$str$at") {
        return call_runtime("yukt_str_at", {args.at(0), args.at(1)});
    }
    if (name == "std$str$slice") {
        return call_runtime("yukt_str_slice", {args.at(0), args.at(1), args.at(2)});
    }
    if (name == "std$str$find") {
        return call_runtime("yukt_str_find", {args.at(0), args.at(1)});
    }
    if (name == "std$str$contains") {
        return call_runtime("yukt_str_contains", {args.at(0), args.at(1)});
    }
    if (name == "std$str$starts") {
        return call_runtime("yukt_str_starts", {args.at(0), args.at(1)});
    }
    if (name == "std$str$ends") {
        return call_runtime("yukt_str_ends", {args.at(0), args.at(1)});
    }
    if (name == "std$str$repeat") {
        return call_runtime("yukt_str_repeat", {args.at(0), args.at(1)});
    }
    if (name == "std$str$join") {
        return call_runtime("yukt_str_join", {args.at(0), args.at(1)});
    }
    if (name == "std$str$split") {
        return call_runtime("yukt_str_split", {args.at(0), args.at(1)});
    }
    if (name == "std$list$len") {
        return call_runtime("yukt_list_len", {args.at(0)});
    }
    if (name == "std$list$copy") {
        return call_runtime("yukt_list_copy", {args.at(0)});
    }
    if (name == "std$list$fill") {
        const IrType value_type = inst.args.size() > 1 ? inst.args[1].type : ir_i64();
        if (value_type.kind == IrTypeKind::F64) {
            return call_runtime("yukt_list_fill_f64", {args.at(0), args.at(1)});
        }
        if (value_type.kind == IrTypeKind::Bool) {
            llvm::Value* packed = builder_.CreateZExt(args.at(1), llvm::Type::getInt8Ty(context_));
            return call_runtime("yukt_list_fill_bool", {args.at(0), packed});
        }
        if (value_type.kind == IrTypeKind::String) {
            return call_runtime("yukt_list_fill_cstr", {args.at(0), args.at(1)});
        }
        if (value_type.kind == IrTypeKind::List) {
            return call_runtime("yukt_list_fill_list", {args.at(0), args.at(1)});
        }
        return call_runtime("yukt_list_fill_i64", {args.at(0), args.at(1)});
    }
    if (name == "std$list$reverse") {
        return call_runtime("yukt_list_reverse", {args.at(0)});
    }
    if (name == "std$list$slice") {
        return call_runtime("yukt_list_slice", {args.at(0), args.at(1), args.at(2)});
    }
    if (name == "std$sort$asc") {
        return call_runtime("yukt_list_sort_asc", {args.at(0)});
    }
    if (name == "std$sort$desc") {
        return call_runtime("yukt_list_sort_desc", {args.at(0)});
    }
    if (name == "std$sort$sorted") {
        return call_runtime("yukt_list_sorted", {args.at(0)});
    }
    if (name == "std$sort$search") {
        llvm::Value* ivalue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0, true);
        llvm::Value* fvalue = llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), 0.0);
        if (inst.args.size() > 1 && inst.args[1].type.kind == IrTypeKind::F64) {
            fvalue = args.at(1);
        } else if (inst.args.size() > 1) {
            ivalue = args.at(1);
        }
        return call_runtime("yukt_list_search", {args.at(0), ivalue, fvalue});
    }
    if (name == "std$math$abs_i") {
        return call_runtime("yukt_math_abs_i", {args.at(0)});
    }
    if (name == "std$math$abs_f") {
        return call_runtime("yukt_math_abs_f", {args.at(0)});
    }
    if (name == "std$math$min_i") {
        return call_runtime("yukt_math_min_i", {args.at(0), args.at(1)});
    }
    if (name == "std$math$max_i") {
        return call_runtime("yukt_math_max_i", {args.at(0), args.at(1)});
    }
    if (name == "std$math$min_f") {
        return call_runtime("yukt_math_min_f", {args.at(0), args.at(1)});
    }
    if (name == "std$math$max_f") {
        return call_runtime("yukt_math_max_f", {args.at(0), args.at(1)});
    }
    if (name == "std$math$floor") {
        return call_runtime("yukt_math_floor", {args.at(0)});
    }
    if (name == "std$math$ceil") {
        return call_runtime("yukt_math_ceil", {args.at(0)});
    }
    if (name == "std$math$sqrt") {
        return call_runtime("yukt_math_sqrt", {args.at(0)});
    }
    if (name == "std$math$pow") {
        return call_runtime("yukt_math_pow", {args.at(0), args.at(1)});
    }
    if (name == "std$math$sin") {
        return call_runtime("yukt_math_sin", {args.at(0)});
    }
    if (name == "std$math$cos") {
        return call_runtime("yukt_math_cos", {args.at(0)});
    }
    if (name == "std$math$tan") {
        return call_runtime("yukt_math_tan", {args.at(0)});
    }
    if (name == "std$math$exp") {
        return call_runtime("yukt_math_exp", {args.at(0)});
    }
    if (name == "std$math$log") {
        return call_runtime("yukt_math_log", {args.at(0)});
    }
    if (name == "std$stat$min_i") {
        return call_runtime("yukt_stat_min_i", {args.at(0)});
    }
    if (name == "std$stat$max_i") {
        return call_runtime("yukt_stat_max_i", {args.at(0)});
    }
    if (name == "std$stat$min_f") {
        return call_runtime("yukt_stat_min_f", {args.at(0)});
    }
    if (name == "std$stat$max_f") {
        return call_runtime("yukt_stat_max_f", {args.at(0)});
    }
    if (name == "std$stat$mean_f") {
        return call_runtime("yukt_stat_mean_f", {args.at(0)});
    }
    return llvm::PoisonValue::get(type_of(inst.dest.type));
}

llvm::FunctionCallee LlvmEmitter::runtime(const std::string& name) const {
    return runtime_.at(name);
}

llvm::StructType* LlvmEmitter::list_struct_type() {
    return llvm::StructType::get(context_,
                                 {llvm::Type::getInt64Ty(context_), llvm::Type::getInt64Ty(context_),
                                  llvm::PointerType::getUnqual(context_), llvm::Type::getInt64Ty(context_)});
}

llvm::Type* LlvmEmitter::type_of(const IrType& type) {
    if (type.kind == IrTypeKind::Record) {
        return record_type(type.record_id);
    }
    return llvm_type(context_, type);
}

const IrRecord* LlvmEmitter::find_record(int id) const {
    for (const auto& record : ir_.records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

std::int64_t LlvmEmitter::record_byte_size(int id) const {
    const IrRecord* record = find_record(id);
    return record != nullptr && record->size > 0 ? record->size : 0;
}

bool LlvmEmitter::type_has_record(const IrType& type) const {
    if (type.kind == IrTypeKind::Record) {
        return true;
    }
    if (type.kind == IrTypeKind::List && type.element != nullptr) {
        return type_has_record(*type.element);
    }
    return false;
}

llvm::StructType* LlvmEmitter::record_type(int id) {
    if (const auto found = record_types_.find(id); found != record_types_.end()) {
        return found->second;
    }
    const IrRecord* record = find_record(id);
    const std::string name = record != nullptr
                                 ? (record->identity.empty() ? record->name : record->identity)
                                 : ("anon" + std::to_string(id));
    auto* st = llvm::StructType::create(context_, "yukt.rec." + name);
    record_types_[id] = st;
    std::vector<llvm::Type*> elems;
    std::vector<unsigned> indices;
    if (record != nullptr) {
        elems.reserve(record->fields.size());
        indices.reserve(record->fields.size());
        for (std::size_t i = 0; i < record->fields.size(); ++i) {
            indices.push_back(static_cast<unsigned>(elems.size()));
            elems.push_back(stored_field_type(record->fields[i].type));
        }
    }
    st->setBody(elems, false);
    record_llvm_indices_[id] = std::move(indices);
    return st;
}

unsigned LlvmEmitter::record_llvm_index(int record_id, int field_index) {
    record_type(record_id);
    const auto found = record_llvm_indices_.find(record_id);
    if (found == record_llvm_indices_.end() || field_index < 0 ||
        static_cast<std::size_t>(field_index) >= found->second.size()) {
        return field_index < 0 ? 0 : static_cast<unsigned>(field_index);
    }
    return found->second[static_cast<std::size_t>(field_index)];
}

llvm::Type* LlvmEmitter::stored_field_type(const IrType& type) {
    if (type.kind == IrTypeKind::Bool) {
        return llvm::Type::getInt8Ty(context_);
    }
    return type_of(type);
}

llvm::Value* LlvmEmitter::to_stored_field(llvm::Value* value, const IrType& type) {
    if (type.kind == IrTypeKind::Bool) {
        return builder_.CreateZExt(value, llvm::Type::getInt8Ty(context_));
    }
    return value;
}

llvm::Value* LlvmEmitter::from_stored_field(llvm::Value* value, const IrType& type) {
    if (type.kind == IrTypeKind::Bool) {
        return builder_.CreateTrunc(value, llvm::Type::getInt1Ty(context_));
    }
    return value;
}

llvm::Value* LlvmEmitter::emit_record_eq(llvm::Value* left, llvm::Value* right, int record_id) {
    const IrRecord* record = find_record(record_id);
    llvm::Value* acc = llvm::ConstantInt::getTrue(context_);
    if (record == nullptr) {
        return acc;
    }
    for (int i = 0; i < static_cast<int>(record->fields.size()); ++i) {
        const IrType& field = record->fields[static_cast<std::size_t>(i)].type;
        const unsigned index = record_llvm_index(record_id, i);
        llvm::Value* l = builder_.CreateExtractValue(left, {index});
        llvm::Value* r = builder_.CreateExtractValue(right, {index});
        llvm::Value* cmp = nullptr;
        if (field.kind == IrTypeKind::Record) {
            cmp = emit_record_eq(l, r, field.record_id);
        } else if (field.kind == IrTypeKind::F64) {
            cmp = builder_.CreateFCmpOEQ(l, r);
        } else {
            cmp = builder_.CreateICmpEQ(l, r);
        }
        acc = builder_.CreateAnd(acc, cmp);
    }
    return acc;
}

llvm::Value* LlvmEmitter::cstr_ptr(const std::string& text) {
    llvm::GlobalVariable* text_global = builder_.CreateGlobalString(text);
    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
    return builder_.CreateInBoundsGEP(text_global->getValueType(), text_global, {zero, zero});
}

void LlvmEmitter::print_cstr(const std::string& text) {
    builder_.CreateCall(runtime("yukt_print_cstr"), {cstr_ptr(text)});
}

llvm::Value* LlvmEmitter::list_get_value(llvm::Value* list, llvm::Value* index, const IrType& elem) {
    if (elem.kind == IrTypeKind::I64 || elem.kind == IrTypeKind::F64) {
        llvm::Type* elem_ty = type_of(elem);
        llvm::Value* data = list_data_pointer(list);
        llvm::Value* slot = builder_.CreateInBoundsGEP(elem_ty, data, index);
        return builder_.CreateLoad(elem_ty, slot);
    }
    if (elem.kind == IrTypeKind::Bool) {
        llvm::Value* raw = builder_.CreateCall(runtime("yukt_list_get_bool"), {list, index});
        return builder_.CreateTrunc(raw, llvm::Type::getInt1Ty(context_));
    }
    if (elem.kind == IrTypeKind::String) {
        return builder_.CreateCall(runtime("yukt_list_get_cstr"), {list, index});
    }
    if (elem.kind == IrTypeKind::Record) {
        llvm::Type* rec_ty = type_of(elem);
        llvm::Value* tmp = builder_.CreateAlloca(rec_ty);
        llvm::Value* stride = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                                     static_cast<uint64_t>(record_byte_size(elem.record_id)));
        builder_.CreateCall(runtime("yukt_list_get_rec"), {list, index, tmp, stride});
        return builder_.CreateLoad(rec_ty, tmp);
    }
    return builder_.CreateCall(runtime("yukt_list_get_list"), {list, index});
}

void LlvmEmitter::emit_print_value(llvm::Value* value, const IrType& type) {
    switch (type.kind) {
        case IrTypeKind::I64:
            builder_.CreateCall(runtime("yukt_print_i64"), {value});
            return;
        case IrTypeKind::F64:
            builder_.CreateCall(runtime("yukt_print_f64"), {value});
            return;
        case IrTypeKind::Bool:
            builder_.CreateCall(runtime("yukt_print_bool"),
                                {builder_.CreateZExt(value, llvm::Type::getInt8Ty(context_))});
            return;
        case IrTypeKind::String:
            builder_.CreateCall(runtime("yukt_print_cstr"), {value});
            return;
        case IrTypeKind::Record:
            emit_print_record(value, type.record_id);
            return;
        case IrTypeKind::List:
            emit_print_list(value, type);
            return;
        case IrTypeKind::Void:
            return;
    }
}

void LlvmEmitter::emit_print_record(llvm::Value* value, int record_id) {
    const IrRecord* record = find_record(record_id);
    print_cstr(record != nullptr ? record->name : "record");
    print_cstr("(");
    if (record != nullptr) {
        for (int i = 0; i < static_cast<int>(record->fields.size()); ++i) {
            if (i != 0) {
                print_cstr(", ");
            }
            llvm::Value* field =
                builder_.CreateExtractValue(value, {record_llvm_index(record_id, i)});
            emit_print_value(from_stored_field(field, record->fields[static_cast<std::size_t>(i)].type),
                             record->fields[static_cast<std::size_t>(i)].type);
        }
    }
    print_cstr(")");
}

void LlvmEmitter::emit_print_list(llvm::Value* list, const IrType& type) {
    print_cstr("[");
    auto* fn = current_fn_;
    if (fn == nullptr) {
        print_cstr("]");
        return;
    }
    llvm::Value* is_null =
        builder_.CreateICmpEQ(list, llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context_)));
    auto* nonempty = llvm::BasicBlock::Create(context_, "rec.list.nonempty", fn);
    auto* loop = llvm::BasicBlock::Create(context_, "rec.list.loop", fn);
    auto* body = llvm::BasicBlock::Create(context_, "rec.list.body", fn);
    auto* comma = llvm::BasicBlock::Create(context_, "rec.list.comma", fn);
    auto* elem = llvm::BasicBlock::Create(context_, "rec.list.elem", fn);
    auto* done = llvm::BasicBlock::Create(context_, "rec.list.done", fn);
    builder_.CreateCondBr(is_null, done, nonempty);

    builder_.SetInsertPoint(nonempty);
    llvm::Value* length_ptr = builder_.CreateStructGEP(list_struct_type(), list, 0);
    llvm::Value* length = builder_.CreateLoad(llvm::Type::getInt64Ty(context_), length_ptr);
    llvm::Value* index_ptr = builder_.CreateAlloca(llvm::Type::getInt64Ty(context_));
    builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0), index_ptr);
    builder_.CreateBr(loop);

    builder_.SetInsertPoint(loop);
    llvm::Value* index = builder_.CreateLoad(llvm::Type::getInt64Ty(context_), index_ptr);
    llvm::Value* more = builder_.CreateICmpSLT(index, length);
    builder_.CreateCondBr(more, body, done);

    builder_.SetInsertPoint(body);
    llvm::Value* first = builder_.CreateICmpEQ(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0));
    builder_.CreateCondBr(first, elem, comma);

    builder_.SetInsertPoint(comma);
    print_cstr(", ");
    builder_.CreateBr(elem);

    builder_.SetInsertPoint(elem);
    const IrType element = type.element != nullptr ? *type.element : ir_i64();
    llvm::Value* item = list_get_value(list, index, element);
    emit_print_value(item, element);
    builder_.CreateStore(builder_.CreateAdd(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 1)),
                         index_ptr);
    builder_.CreateBr(loop);

    builder_.SetInsertPoint(done);
    print_cstr("]");
}

llvm::Value* LlvmEmitter::list_data_pointer(llvm::Value* list) {
    llvm::Value* field = builder_.CreateStructGEP(list_struct_type(), list, 2);
    auto* loaded = builder_.CreateLoad(llvm::PointerType::getUnqual(context_), field);
    loaded->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(context_, {}));
    loaded->setMetadata(llvm::LLVMContext::MD_noundef, llvm::MDNode::get(context_, {}));
    builder_.CreateAlignmentAssumption(module_.getDataLayout(), loaded, 16);
    return loaded;
}

void LlvmEmitter::attach_loop_metadata(llvm::Instruction* latch, const std::string& iterator) {
    llvm::Metadata* mustprogress[] = {llvm::MDString::get(context_, "llvm.loop.mustprogress")};
    llvm::SmallVector<llvm::Metadata*, 4> ops;
    ops.push_back(nullptr);
    ops.push_back(llvm::MDNode::get(context_, mustprogress));
    if (!iterator.empty()) {
        llvm::Metadata* named[] = {llvm::MDString::get(context_, "yukt.iterator"),
                                   llvm::MDString::get(context_, iterator)};
        ops.push_back(llvm::MDNode::get(context_, named));
    }
    auto* loop_id = llvm::MDNode::getDistinct(context_, ops);
    loop_id->replaceOperandWith(0, loop_id);
    latch->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
}

}  // namespace

bool emit_llvm_ir(const IrModule& module, std::string& llvm_ir, std::string& error) {
    LlvmEmitter emitter(module);
    return emitter.emit_text(llvm_ir, error);
}

bool emit_object_file(const IrModule& module, const std::string& object_path, std::string& error) {
    LlvmEmitter emitter(module);
    return emitter.emit_object(object_path, error);
}

bool link_executable(const std::string& object_path, const std::string& output_path, const std::string& runtime_path,
                     std::string& error) {
    const std::string clang = find_clang();
    std::string command = quote(clang) + " -o " + quote(output_path) + " " + quote(object_path) + " " +
                          quote(runtime_path);
    const std::string sysroot = macos_sysroot();
    if (!sysroot.empty()) {
        command += " -isysroot " + quote(sysroot);
    }
#if !defined(__APPLE__)
    command += " -pthread";
#endif
    command += " -lm";
    const int status = std::system(command.c_str());
    if (status != 0) {
        error = "linker failed: " + command;
        return false;
    }
    return true;
}

int run_executable(const std::string& executable_path, std::string& output, std::string& error) {
    const std::string command = quote(executable_path) + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        error = "could not execute `" + executable_path + "`";
        return 1;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = pclose(pipe);
    if (status == -1) {
        error = "failed to finish `" + executable_path + "`";
        return 1;
    }
#if defined(WIFEXITED)
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        error = "`" + executable_path + "` terminated by a signal";
        return 1;
    }
#endif
    return status;
}

}  // namespace yukt

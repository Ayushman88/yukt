#include "backend/llvm_backend.hpp"

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
#include <llvm/IR/Constants.h>
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
    llvm::FunctionCallee runtime(const std::string& name) const;
    llvm::StructType* list_struct_type();
    llvm::Value* list_data_pointer(llvm::Value* list);
    void attach_loop_metadata(llvm::Instruction* latch, const std::string& iterator);

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
            params.push_back(llvm_type(context_, param.second));
        }
        auto* type = llvm::FunctionType::get(llvm_type(context_, function.return_type), params, false);
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
        locals_[local.first] = builder_.CreateAlloca(llvm_type(context_, local.second), nullptr, local.first);
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
            return builder_.CreateLoad(llvm_type(context_, value.type), locals_.at(value.name));
        case IrValue::Kind::ConstI64:
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), static_cast<uint64_t>(value.i64), true);
        case IrValue::Kind::ConstF64:
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), value.f64);
        case IrValue::Kind::ConstBool:
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context_), value.boolean ? 1 : 0);
        case IrValue::Kind::ConstString: {
            llvm::GlobalVariable* text = builder_.CreateGlobalString(value.text);
            llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
            return builder_.CreateInBoundsGEP(text->getValueType(), text, {zero, zero});
        }
        case IrValue::Kind::Undef:
            return llvm::PoisonValue::get(llvm_type(context_, value.type));
    }
    return llvm::PoisonValue::get(llvm::Type::getInt64Ty(context_));
}

llvm::Value* LlvmEmitter::emit_inst(const IrInst& inst) {
    switch (inst.op) {
        case IrOp::Store:
            builder_.CreateStore(materialize(inst.args[0]), locals_.at(inst.name));
            return nullptr;
        case IrOp::Load:
            return builder_.CreateLoad(llvm_type(context_, inst.dest.type), locals_.at(inst.name));
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
                llvm::Type* elem_ty = llvm_type(context_, elem);
                llvm::Value* data = list_data_pointer(list);
                llvm::Value* slot = builder_.CreateInBoundsGEP(elem_ty, data, index);
                builder_.CreateStore(value, slot);
            } else if (elem.kind == IrTypeKind::Bool) {
                builder_.CreateCall(runtime("yukt_list_set_bool"),
                                    {list, index, builder_.CreateZExt(value, llvm::Type::getInt8Ty(context_))});
            } else if (elem.kind == IrTypeKind::String) {
                builder_.CreateCall(runtime("yukt_list_set_cstr"), {list, index, value});
            } else {
                builder_.CreateCall(runtime("yukt_list_set_list"), {list, index, value});
            }
            return nullptr;
        }
        case IrOp::ListGet: {
            llvm::Value* list = materialize(inst.args[0]);
            llvm::Value* index = materialize(inst.args[1]);
            const IrType elem = inst.dest.type;
            if (elem.kind == IrTypeKind::I64 || elem.kind == IrTypeKind::F64) {
                llvm::Type* elem_ty = llvm_type(context_, elem);
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
            return builder_.CreateCall(runtime("yukt_list_get_list"), {list, index});
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
                field_types.push_back(llvm_type(context_, inst.args[i].type));
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
            if (value.type.kind == IrTypeKind::I64) {
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
    }
    return nullptr;
}

llvm::FunctionCallee LlvmEmitter::runtime(const std::string& name) const {
    return runtime_.at(name);
}

llvm::StructType* LlvmEmitter::list_struct_type() {
    return llvm::StructType::get(context_,
                                 {llvm::Type::getInt64Ty(context_), llvm::Type::getInt64Ty(context_),
                                  llvm::PointerType::getUnqual(context_)});
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

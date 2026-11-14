// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The splitters' calls that LLVM's C API lacks, through its C++ API.
//
// split.c's bitcode output (split_write_bitcode). Clang's -emit-llvm keeps the module's use-list order, and code
// generation follows it (a block's predecessors, a value's users): a partition written without it compiles to different
// instructions. LLVM's C API writes bitcode without it, so this call takes the choice as an argument. split.c's parts
// also erase the aliases they don't emit (split_erase_alias), and its start order moves a function within the module's
// list (split_move_function), which LLVM's C API can't.
//
// presplit.c's parts (split_clone_part, split_write_part): a module's clone with chosen definitions alone, and its
// bitcode without the use-list order, which the optimizer that follows each part doesn't need (the part's object then
// depends on its bitcode's bytes alone).

#include <llvm-c/Core.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <cstdint>
#include <memory>

static bool split_write(LLVMModuleRef module, const char* path, bool order)
{
    std::error_code error;
    llvm::raw_fd_ostream output(path, error, llvm::sys::fs::OF_None);
    bool written = false;
    if (!error)
    {
        llvm::WriteBitcodeToFile(*llvm::unwrap(module), output, order);
        output.close();
        written = !output.has_error();
    }
    return written;
}

extern "C" bool split_write_bitcode(LLVMModuleRef module, const char* path)
{
    return split_write(module, path, true);
}

extern "C" bool split_write_part(LLVMModuleRef module, const char* path)
{
    return split_write(module, path, false);
}

// split.c's aliases of another part's definitions (split_part), which LLVM's C API can't erase: `alias` goes once its
// dead constant users (the initializers and bodies the part dropped left them) are gone, and only if nothing else uses it.
extern "C" bool split_erase_alias(LLVMValueRef alias)
{
    llvm::GlobalAlias* value = llvm::cast<llvm::GlobalAlias>(llvm::unwrap(alias));
    value->removeDeadConstantUsers();
    const bool unused = value->use_empty();
    if (unused)
    {
        value->eraseFromParent();
    }
    return unused;
}

// split.c's start order (split_order_first): `function` goes right before `before` in their module's list of functions,
// the order the code generator emits them in. The written bitcode keeps the use-list order whatever the functions'
// order, so each function's code stays what it was.
extern "C" void split_move_function(LLVMValueRef function, LLVMValueRef before)
{
    llvm::Function* moved = llvm::cast<llvm::Function>(llvm::unwrap(function));
    llvm::Function* next = llvm::cast<llvm::Function>(llvm::unwrap(before));
    llvm::Module::FunctionListType& list = moved->getParent()->getFunctionList();
    list.splice(next->getIterator(), list, moved->getIterator());
}

// `module`'s clone in the same context: every global value declared, and the `count` in `keep` defined as they are.
// A body the part keeps but doesn't own (`owned` names the `owned_count` it emits) loses its debug information: the
// part's bitcode then holds no line number of another part's function, which an edit above that function would move.
// The clone's debug information is its own definitions': the compile unit's list of global variables keeps the records
// of the variables it owns (and, with `lists`, the records no variable holds: the constants'), and its lists of
// enumerations, retained types, imports and macros stay only with `lists`, which the data part asks for. Its struct
// types lose their names, which clang numbers in the order it met them (struct.anon.N), so that an anonymous struct an
// edit adds renames no other part's types; a name changes no code. Changes `module` and its context: the caller is a
// child process that owns its copy.
extern "C" LLVMModuleRef split_clone_part(LLVMModuleRef module, LLVMValueRef* keep, uint64_t count, LLVMValueRef* owned, uint64_t owned_count,
                                          bool lists)
{
    llvm::Module& source = *llvm::unwrap(module);
    llvm::DenseSet<const llvm::GlobalValue*> kept;
    for (uint64_t i = 0; i < count; i += 1)
    {
        kept.insert(llvm::cast<llvm::GlobalValue>(llvm::unwrap(keep[i])));
    }
    llvm::DenseSet<const llvm::GlobalValue*> own;
    for (uint64_t i = 0; i < owned_count; i += 1)
    {
        own.insert(llvm::cast<llvm::GlobalValue>(llvm::unwrap(owned[i])));
    }
    for (llvm::Function& function : source)
    {
        if (!function.isDeclaration() && kept.count(&function) != 0 && own.count(&function) == 0)
        {
            llvm::stripDebugInfo(function);
        }
    }
    llvm::DenseSet<const llvm::Metadata*> attached;
    llvm::SmallVector<llvm::Metadata*, 0> expressions;
    for (llvm::GlobalVariable& global : source.globals())
    {
        llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> records;
        global.getDebugInfo(records);
        for (llvm::DIGlobalVariableExpression* record : records)
        {
            attached.insert(record);
            if (own.count(&global) != 0)
            {
                expressions.push_back(record);
            }
        }
        if (own.count(&global) == 0)
        {
            global.eraseMetadata(llvm::LLVMContext::MD_dbg);
        }
    }
    llvm::LLVMContext& context = source.getContext();
    llvm::DenseSet<const llvm::Metadata*> mine(expressions.begin(), expressions.end());
    for (llvm::DICompileUnit* unit : source.debug_compile_units())
    {
        // The unit's own list in its order: a function's static variable's record is its function's, not the unit's.
        llvm::SmallVector<llvm::Metadata*, 0> globals;
        for (llvm::DIGlobalVariableExpression* record : unit->getGlobalVariables())
        {
            if (mine.count(record) != 0 || (lists && attached.count(record) == 0))
            {
                globals.push_back(record);
            }
        }
        if (!lists)
        {
            // Absent lists, not empty ones: an empty macro list still makes the unit name a macro section.
            unit->replaceEnumTypes(llvm::DICompositeTypeArray());
            unit->replaceRetainedTypes(llvm::DITypeArray());
            unit->replaceImportedEntities(llvm::DIImportedEntityArray());
            unit->replaceMacros(llvm::DIMacroNodeArray());
        }
        unit->replaceGlobalVariables(llvm::MDTuple::get(context, globals));
    }
    llvm::ValueToValueMapTy map;
    std::unique_ptr<llvm::Module> clone = llvm::CloneModule(source, map, [&](const llvm::GlobalValue* value) { return kept.count(value) != 0; });
    for (llvm::StructType* type : clone->getIdentifiedStructTypes())
    {
        type->setName("");
    }
    return llvm::wrap(clone.release());
}

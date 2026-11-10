// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// split.c's bitcode output. Clang's -emit-llvm keeps the module's use-list order, and code generation follows it (a
// block's predecessors, a value's users): a partition written without it compiles to different instructions. LLVM's C
// API writes bitcode without it, so this one call goes through the C++ API, which takes the choice as an argument.

#include <llvm-c/Core.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

extern "C" bool split_write_bitcode(LLVMModuleRef module, const char* path)
{
    std::error_code error;
    llvm::raw_fd_ostream output(path, error, llvm::sys::fs::OF_None);
    bool written = false;
    if (!error)
    {
        llvm::WriteBitcodeToFile(*llvm::unwrap(module), output, true);
        output.close();
        written = !output.has_error();
    }
    return written;
}

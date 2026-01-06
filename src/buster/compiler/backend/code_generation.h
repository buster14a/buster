#pragma once

#include <buster/lib.h>
#include <buster/compiler/ir/ir.h>

STRUCT(CodeGeneration)
{
    Arena* code_arena;
    Arena* data_arena;
};

STRUCT(CodeGenerationOptions)
{
};

BUSTER_DECL CodeGeneration module_generation_initialize();
BUSTER_DECL bool function_generate(CodeGeneration* generation, Arena* arena, IrModule* module, IrFunction* function, CodeGenerationOptions options);

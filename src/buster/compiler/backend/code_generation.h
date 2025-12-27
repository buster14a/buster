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

STRUCT(InternalModule)
{
    String8* section_contents;
    String8* section_names;
    u16 section_count;
};

STRUCT(ExternalModuleFile)
{
    OsString path;
};

STRUCT(ExternalModuleMemory)
{
    OsString path;
    String8 content;
};

BUSTER_DECL CodeGeneration module_generation_initialize();
BUSTER_DECL bool function_generate(CodeGeneration* generation, Arena* arena, IrModule* module, IrFunction* function, CodeGenerationOptions options);

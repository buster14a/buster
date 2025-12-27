#pragma once

#include <buster/lib.h>
#include <buster/compiler/backend/code_generation.h>

STRUCT(LinkUnit)
{
    InternalModule* internal_modules;
    u64 internal_module_count;
    ExternalModuleFile* external_module_files;
    u64 external_module_file_count;
    ExternalModuleMemory* external_module_memories;
    u64 external_module_memory_count;
};

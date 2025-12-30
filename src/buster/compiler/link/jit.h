#pragma once

#include <buster/base.h>
#include <buster/compiler/link/link.h>

STRUCT(JitModule)
{
    u32 v;
};
BUSTER_DECL JitModule module_jit(Arena* arena, LinkArguments arguments);
BUSTER_DECL int jit_module_run(JitModule module);

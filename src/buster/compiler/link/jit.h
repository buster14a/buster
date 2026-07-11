#pragma once

#include <buster/base.h>
#include <buster/compiler/link/link.h>

typedef struct JitModule JitModule;
struct JitModule
{
    u32 v;
};

BUSTER_F_DECL JitModule module_jit(Arena* arena, LinkArguments arguments);
BUSTER_F_DECL int jit_module_run(JitModule module);

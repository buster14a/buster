#pragma once

#include <buster/lib.h>
#include <buster/compiler/link/link.h>

STRUCT(JitModule)
{
};
BUSTER_DECL JitModule module_jit(Arena* arena, LinkArguments arguments);
BUSTER_DECL int jit_module_run(JitModule module);

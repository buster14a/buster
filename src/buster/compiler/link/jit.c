#pragma once

#include <buster/compiler/link/jit.h>
BUSTER_IMPL JitModule module_jit(Arena* arena, LinkUnit unit)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(unit);
    return (JitModule){};
}

BUSTER_IMPL int jit_module_run(JitModule module)
{
    BUSTER_UNUSED(module);
    return 0;
}

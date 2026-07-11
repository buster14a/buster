#include <buster/compiler/link/jit.h>

JitModule module_jit(Arena* arena, LinkArguments arguments)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    return (JitModule){0};
}

int jit_module_run(JitModule module)
{
    BUSTER_UNUSED(module);
    return 0;
}

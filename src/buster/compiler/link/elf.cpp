#pragma once

#include <buster/compiler/link/elf.h>

BUSTER_IMPL ElfResult module_link_elf(Arena* arena, LinkArguments arguments)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    return (ElfResult){};
}

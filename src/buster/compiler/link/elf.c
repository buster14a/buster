#pragma once

#include <buster/compiler/link/elf.h>

BUSTER_IMPL ElfResult module_link_elf(Arena* arena, LinkUnit unit)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(unit);
    return (ElfResult){};
}

#pragma once

#include <buster/base.h>
#include <buster/compiler/link/link.h>

typedef struct ElfResult ElfResult;
struct ElfResult
{
    u32 v;
};

BUSTER_F_DECL ElfResult module_link_elf(Arena* arena, LinkArguments arguments);

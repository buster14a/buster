#pragma once
// Private driver classification and runtime-selection seams shared with tests.
#include <buster/lib/compiler/driver/driver.h>

BUSTER_F_DECL bool compiler_driver_language_is_native(CompilerDriverLanguage language);

// Select the x86-64 Linux DSO runtime only for its supported shared image;
// other ELF runtime targets retain the ordinary libc stubs.
BUSTER_F_DECL ObjectFile compiler_driver_elf_libc_runtime_object(Arena* arena, CompilerDriverInvocation invocation);

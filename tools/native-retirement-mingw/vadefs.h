#ifndef BUSTER_RETIREMENT_MINGW_VADEFS_H
#define BUSTER_RETIREMENT_MINGW_VADEFS_H

// MinGW's non-GNU vadefs.h only describes MSVC x86 and x64. Buster owns
// va_list and the four builtins on both Windows ABIs, including ARM64.
// Keep those definitions rather than selecting MinGW's GCC-only inline asm.
#include <stdarg.h>
#define _crt_va_start(list, last) __builtin_va_start(list, last)
#define _crt_va_arg(list, type) __builtin_va_arg(list, type)
#define _crt_va_end(list) __builtin_va_end(list)
#define _crt_va_copy(destination, source) __builtin_va_copy(destination, source)

#endif

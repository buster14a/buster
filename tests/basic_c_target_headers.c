#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdalign.h>
#include <string.h>
#include <stdlib.h>
// Darwin does not provide the optional C Unicode conversion header.
#if !defined(__APPLE__)
#include <uchar.h>
#endif

size_t target_header_size(uintptr_t value)
{
    return (size_t)value;
}

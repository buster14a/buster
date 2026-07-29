#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdalign.h>
#include <string.h>
#include <stdlib.h>
#include <uchar.h>

size_t target_header_size(uintptr_t value)
{
    return (size_t)value;
}

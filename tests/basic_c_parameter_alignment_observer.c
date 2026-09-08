#include "basic_c_parameter_alignment.h"

int parameter_alignment_observe(void const* pointer, unsigned alignment, unsigned long long first, unsigned long long last)
{
    unsigned long long const* values = pointer;
    return ((unsigned long long)pointer & (alignment - 1)) != 0 || values[0] != first || values[alignment / sizeof(*values) - 1] != last;
}

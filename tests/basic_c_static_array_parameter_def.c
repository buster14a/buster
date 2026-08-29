// The definition half of basic_c_static_array_parameter.c. Every function
// here is declared with a C99 `static` array bound on a parameter, which is
// a bound qualifier and not a storage class: none of them may become an
// internal symbol, or the caller unit fails to link against this object.
// The uncalled-in-this-unit shape is the one that used to disappear
// entirely -- an internal definition nothing in its own unit calls is
// dropped before it reaches the object.

#include <stddef.h>

// The musl shape: an attribute list ahead of the specifiers, a `sizeof` in
// the bound expression, and the parameter spelled as a pointer where the
// body is.
__attribute__((visibility("hidden"))) void fill_hidden(char buffer[static 15 + 3 * sizeof(int)], unsigned value);

void fill_hidden(char* buffer, unsigned value)
{
    buffer[0] = (char)value;
}

// The bound on the definition rather than on a prototype.
int sum_static(int values[static 4])
{
    return values[0] + values[1] + values[2] + values[3];
}

// The bound on both the prototype and the definition.
int head_static(int values[static 2]);

int head_static(int values[static 2])
{
    return values[0];
}

// A `static` bound behind a qualifier, and one on a parameter that is not
// the first.
long tail_static(int count, long values[const static 3])
{
    return values[count];
}

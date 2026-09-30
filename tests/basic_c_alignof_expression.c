// GNU's `__alignof__` takes an expression where `_Alignof` takes only a type
// name, and every answer here is the alignment of the operand's own type --
// the array's, not the pointer it would decay to in any other context.
//
// The lowering used to accept an expression operand only for a compound
// literal and refuse everything else with "could not lower logical expression
// core", which is what libc-test's `tls_align_dso.c` hit the moment
// `__attribute__((constructor))` stopped dropping the function that holds it:
// it fills a table with `__alignof__(x)` over four thread-local objects.
// Predicting the operand's type instead would decay `arr` and answer 8.
//
// Every value below was compared against clang for this target.

struct pair
{
    char c;
    double d;
};

union either
{
    char c;
    long long l;
};

typedef int quad __attribute__((vector_size(16)));

static struct pair value;
static struct pair values[3];
static long long wide;
static char bytes[7];
static char* pointer;
static __thread int thread_local_int;
static quad vector;

int main(void)
{
    // An aggregate's alignment is its widest member's.
    if (__alignof__(value) != 8)
    {
        return 1;
    }
    // An array's is its element's, and neither the array nor a subscript of it
    // decays here.
    if (__alignof__(values) != 8)
    {
        return 2;
    }
    if (__alignof__(values[0]) != 8)
    {
        return 3;
    }
    if (__alignof__(bytes) != 1)
    {
        return 4;
    }
    // A member, reached through the object.
    if (__alignof__(value.d) != 8)
    {
        return 5;
    }
    if (__alignof__(wide) != 8)
    {
        return 6;
    }
    // A pointer object against what it points at.
    if (__alignof__(pointer) != 8)
    {
        return 7;
    }
    if (__alignof__(*pointer) != 1)
    {
        return 8;
    }
    // Thread-local storage does not change an object's alignment.
    if (__alignof__(thread_local_int) != 4)
    {
        return 9;
    }
    if (__alignof__(vector) != 16)
    {
        return 10;
    }
    // The type-name spellings still answer the same, and a compound literal
    // operand -- the one expression shape that already worked -- still does.
    if (__alignof__(union either) != 8)
    {
        return 11;
    }
    if (__alignof__((struct pair){0}) != 8)
    {
        return 12;
    }
    if (_Alignof(struct pair) != 8)
    {
        return 13;
    }
    // `sizeof` over the same operands is unchanged: the array measures its
    // own extent rather than a pointer's.
    if (sizeof bytes != 7)
    {
        return 14;
    }
    if (sizeof values != 3 * sizeof(struct pair))
    {
        return 15;
    }
    return 0;
}

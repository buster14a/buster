// A block-local typedef shadows an outer one of the same spelling.  The C
// fuzzer of LZ4 declares two sibling blocks that each typedef `shct` to a
// different anonymous struct; resolving the second one at file scope silently
// gives the first block's layout.
typedef struct { int file_scope_field; } shadowed;

_Static_assert(sizeof(shadowed) == sizeof(int), "file scope typedef");

static int sibling_blocks(void)
{
    int total = 0;
    {
        typedef struct { int first; int second; int third; } local;
        local value;
        _Static_assert(sizeof(local) == 3 * sizeof(int), "first sibling");
        value.first = 1;
        value.second = 2;
        value.third = 3;
        total += value.first + value.second + value.third;
    }
    {
        typedef struct { int only; } local;
        local value;
        _Static_assert(sizeof(local) == sizeof(int), "second sibling");
        value.only = 10;
        total += value.only;
    }
    return total;
}

static int nested_shadow(void)
{
    typedef struct { int outer_first; int outer_second; } shadowed;
    shadowed outer;
    outer.outer_first = 1;
    outer.outer_second = 2;
    {
        typedef struct { int inner; } shadowed;
        shadowed inner;
        _Static_assert(sizeof(shadowed) == sizeof(int), "inner shadow");
        inner.inner = 100;
        return outer.outer_first + outer.outer_second + inner.inner;
    }
}

int main(void)
{
    if (sibling_blocks() != 16) return 1;
    if (nested_shadow() != 103) return 2;
    {
        shadowed file_scope;
        file_scope.file_scope_field = 5;
        if (file_scope.file_scope_field != 5) return 3;
    }
    return 0;
}

// A block-scope tag definition is its own type.  The aggregate table used to
// resolve tags by (kind, tag) alone -- "oldest matching" across the whole
// translation unit -- so two sibling functions defining one tag silently
// shared the first definition's layout, and a body against the already
// completed tag was refused as a redefinition, sending the whole statement
// back through the expression parser.  clang's xmmintrin.h defines
// `struct __mm_storeh_pi_struct` inside two intrinsics, which is where
// CPython's build found both halves.
//
// Sizes are asserted in every direction: two sibling definitions, an inner
// definition shadowing a file-scope one, and the file scope's own answer
// after the shadowing function closed.
struct outer
{
    char a;
    char b;
};

int sibling_first(void)
{
    struct wrap
    {
        char a;
    };
    return (int)sizeof(struct wrap);
}

int sibling_second(void)
{
    struct wrap
    {
        char a;
        char b;
        char c;
        char d;
        char e;
    };
    struct wrap object;
    object.e = 5;
    return (int)sizeof(struct wrap) + (object.e - 5);
}

int inner_shadow(void)
{
    struct outer
    {
        char a[7];
    };
    struct outer object;
    object.a[6] = 1;
    return (int)sizeof(struct outer) + (object.a[6] - 1);
}

struct outer file_scope_object;

int main(void)
{
    if (sibling_first() != 1)
    {
        return 1;
    }
    if (sibling_second() != 5)
    {
        return 2;
    }
    if (inner_shadow() != 7)
    {
        return 3;
    }
    if (sizeof(file_scope_object) != 2 || sizeof(struct outer) != 2)
    {
        return 4;
    }
    return 0;
}

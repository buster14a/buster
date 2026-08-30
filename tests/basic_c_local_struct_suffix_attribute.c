// A block-scope tag definition's suffix attribute list:
// `struct S { ... } __attribute__((__packed__));` after the closing brace,
// with and without a declarator following the list.  The file-scope parsers
// learned this position long ago; the block-scope declarator walk read
// `__attribute__` as the declarator name and its parenthesized list as a
// function suffix, refusing with "use of undeclared identifier '__packed__'".
// clang's own xmmintrin.h is the shape that matters: every
// _mm_loadh_pi-style intrinsic wraps its unaligned load in exactly such a
// local packed struct, so any translation unit including <immintrin.h>
// through these headers -- CPython's mimalloc does -- stopped compiling.
// The sizes are asserted so a parse that skips the list without applying it
// fails here too.
int definition_only(void)
{
    struct wrapped
    {
        unsigned short value;
        unsigned char extra;
    } __attribute__((__packed__, __may_alias__));
    struct wrapped w;
    w.value = 3;
    w.extra = 9;
    return sizeof(struct wrapped) == 3 && w.value == 3 && w.extra == 9 ? 0 : 1;
}

int with_declarator(void)
{
    struct declared
    {
        unsigned short value;
        unsigned char extra;
    } __attribute__((__packed__)) object;
    object.value = 7;
    object.extra = 2;
    return sizeof object == 3 && object.value == 7 && object.extra == 2 ? 0 : 1;
}

int main(void)
{
    if (definition_only() != 0)
    {
        return 1;
    }
    if (with_declarator() != 0)
    {
        return 2;
    }
    return 0;
}

// A named object's own `_Alignas` or `aligned` raises GNU `__alignof__` and
// `_Alignof` over that object, and the raised answer is an integer constant
// expression: GCC and Clang fold `_Alignas(64) int g; _Alignof(g)` to 64 in a
// static assertion, a static initializer and a function body alike (#1704).
//
// Every value below was compared against clang for this target.

static _Alignas(64) int aligned_object;
static int attribute_aligned_object __attribute__((aligned(32)));
extern int redeclared_object;
_Alignas(16) int redeclared_object;
_Static_assert(_Alignof(aligned_object) == 64, "alignof an _Alignas object");
_Static_assert(__alignof__(attribute_aligned_object) == 32, "alignof an aligned object");
_Static_assert(__alignof__(redeclared_object) == 16, "alignof a redeclared object");
static unsigned long folded_alignment = __alignof__(aligned_object);

int main(void)
{
    // A declared alignment raises the type's, wherever the answer is read.
    _Alignas(128) int aligned_local = 0;
    _Static_assert(_Alignof(aligned_local) == 128, "alignof an _Alignas local");
    if (__alignof__(aligned_object) != 64 || _Alignof((aligned_object)) != 64)
    {
        return 1;
    }
    if (__alignof__(attribute_aligned_object) != 32 || __alignof__(redeclared_object) != 16)
    {
        return 2;
    }
    if (_Alignof(aligned_local) != 128 || ((unsigned long)&aligned_local & 127) != 0)
    {
        return 3;
    }
    if (folded_alignment != 64)
    {
        return 4;
    }
    return 0;
}

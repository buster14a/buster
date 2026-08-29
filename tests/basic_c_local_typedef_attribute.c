// A GNU attribute in specifier position, after a typedef name, on a
// *block-scope* declaration.  Nothing consumed it there: the walk that says
// where a declaration's declarator starts skipped attributes only after it had
// already consumed a qualifier or an alignment specifier, so an attribute
// following the typedef name directly ended the specifier run and became the
// declared name.
//
// It is not an obscure shape.  musl writes
//
//     typedef size_t __attribute__((__may_alias__)) word;
//
// inside strlen, memcpy, memset, memchr, stpcpy, stpncpy, strchrnul, strlcpy,
// memccpy, calloc, libc_calloc and mbsrtowcs -- twelve translation units,
// every one of them behind `#ifdef __GNUC__`, which is why the gap only
// surfaced once __GNUC__ was predefined in every dialect (see
// tests/basic_c_type_generic_math.c).
//
// The `aligned` half is the reason this cannot be fixed by dropping the
// attribute run on the floor: the specifier range the declaration hands to the
// alignment pass has to still contain it, or a local alias silently loses the
// alignment its file-scope twin keeps.

typedef int base_type;
typedef unsigned long size_type;

// The file-scope forms, which already worked, as the reference the block-scope
// ones are compared against.
typedef base_type __attribute__((aligned(16))) file_aligned;
typedef size_type __attribute__((__may_alias__)) file_word;

int main(void)
{
    typedef size_type __attribute__((__may_alias__)) word;
    typedef base_type __attribute__((aligned(16))) local_aligned;
    // A qualifier ahead of the attribute took the older path and has to keep
    // working.
    typedef const size_type __attribute__((__may_alias__)) const_word;

    word w = 6;
    const_word c = 1;
    if (w + c != 7)
    {
        return 1;
    }
    if (_Alignof(local_aligned) != 16 || _Alignof(file_aligned) != 16)
    {
        return 2;
    }
    // Unattributed, so the alias must not have picked the alignment up from
    // somewhere else.
    if (_Alignof(base_type) != _Alignof(int))
    {
        return 3;
    }
    if (sizeof(word) != sizeof(size_type) || sizeof(file_word) != sizeof(size_type))
    {
        return 4;
    }
    return 0;
}

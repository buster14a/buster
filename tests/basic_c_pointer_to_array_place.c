// Dereferencing a pointer to an array (C11 6.5.3.2p4).  `*p` designates the
// array object itself, exactly the way a named array does; an array lvalue is
// never loaded, so what follows either decays it or indexes it in place.
// Lowering `*p` as an rvalue load copied the whole array into a frame
// temporary, and everything downstream then addressed the copy: a subscript
// store landed in the temporary and was dropped, a decay handed a callee the
// temporary's address, and a returned pointer outlived it.
//
// This was not a hypothetical.  musl's strftime is written around
//
//     const char *__strftime_fmt_1(char (*s)[100], size_t *l, ...)
//     {
//         *l = snprintf(*s, sizeof *s, "%0*lld", width, val);
//         return *s;
//     }
//
// and `__strftime_l` calls it with `&buf` of its own `char buf[100]`.  With
// `*s` loaded, snprintf formatted into a copy nobody could read and the
// returned pointer named that copy, so libc-test's `functional/strftime`
// disagreed with the reference on every format it checked.
//
// The three store spellings and the layout arithmetic are pinned in
// tests/basic_c_packed_layout.c beside the packed declarations that first
// tripped over them.  What is here is the shape a libc actually writes: the
// pointee crossing a call boundary by decay, and the place surviving the
// return.

typedef unsigned long size_type;

// Out of line, so the write is a real store through the pointer the caller
// handed over rather than something folded at the call site.
static void fill(char* target, size_type count, char first)
{
    for (size_type index = 0; index < count; index += 1)
    {
        target[index] = (char)(first + (char)index);
    }
}

// musl's shape: a parameter of pointer-to-array type, a callee writing through
// the decayed pointee, `sizeof *s` as the bound, and the pointee handed back.
static const char* format_into(char (*s)[8])
{
    fill(*s, sizeof *s, 'a');
    return *s;
}

static int rows[2][3];
static int (*row_pointer)[3] = &rows[1];

static int cube[2][2][3];

int main(void)
{
    // The call boundary: the callee's stores have to land in this frame's
    // array, and the pointer it returns has to name it.
    char buffer[8];
    for (size_type index = 0; index < sizeof buffer; index += 1)
    {
        buffer[index] = 0;
    }
    const char* returned = format_into(&buffer);
    if (returned != buffer)
    {
        return 1;
    }
    for (size_type index = 0; index < sizeof buffer; index += 1)
    {
        if (buffer[index] != (char)('a' + (char)index))
        {
            return 2;
        }
    }

    // `sizeof *p` is the array's size, not the pointer's: the operand is not
    // read, so nothing here depends on the fix, and it is what tells the
    // callee above how much room it has.
    char (*buffer_pointer)[8] = &buffer;
    if (sizeof *buffer_pointer != 8 || sizeof buffer_pointer != sizeof(char*))
    {
        return 3;
    }

    // The address the decay produces, without a call in the way.
    if ((char*)*buffer_pointer != buffer || &(*buffer_pointer)[3] != buffer + 3)
    {
        return 4;
    }

    // The store, in each of the three spellings that reach the same object.
    (*row_pointer)[0] = 11;
    *(*row_pointer + 1) = 12;
    (*(row_pointer + 0))[2] = 13;
    if (rows[1][0] != 11 || rows[1][1] != 12 || rows[1][2] != 13)
    {
        return 5;
    }
    // Row zero must be untouched: the pointer was taken to row one.
    if (rows[0][0] != 0 || rows[0][1] != 0 || rows[0][2] != 0)
    {
        return 6;
    }
    // Read back through the dereference, which was always right -- the copy
    // held the right bytes -- and has to stay right now that there is none.
    if ((*row_pointer)[0] != 11 || row_pointer[0][1] != 12 || *(*row_pointer + 2) != 13)
    {
        return 7;
    }

    // Compound assignment and increment name the object twice.
    (*row_pointer)[0] += 5;
    (*row_pointer)[1] -= 2;
    (*row_pointer)[2] += 1;
    if (rows[1][0] != 16 || rows[1][1] != 10 || rows[1][2] != 14)
    {
        return 8;
    }

    // A pointer to a two-dimensional array: the dereference yields an array of
    // arrays, and the subscript that follows it decays in place.
    int (*plane_pointer)[2][3] = &cube[1];
    (*plane_pointer)[1][2] = 21;
    *((*plane_pointer)[0] + 1) = 22;
    if (cube[1][1][2] != 21 || cube[1][0][1] != 22)
    {
        return 9;
    }
    if (cube[0][1][2] != 0 || cube[0][0][1] != 0)
    {
        return 10;
    }
    if (sizeof *plane_pointer != sizeof(int) * 6 || sizeof(*plane_pointer)[0] != sizeof(int) * 3)
    {
        return 11;
    }

    // Arithmetic on the pointer itself, so the place the dereference hands
    // back is the one the arithmetic selected and not the one it started at.
    int (*walking)[3] = rows;
    (*walking)[1] = 31;
    walking += 1;
    (*walking)[1] = 32;
    if (rows[0][1] != 31 || rows[1][1] != 32)
    {
        return 12;
    }
    if ((*walking)[1] != 32 || (*(walking - 1))[1] != 31)
    {
        return 13;
    }

    return 0;
}

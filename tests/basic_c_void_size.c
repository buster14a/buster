// GNU gives `void` a size of one, and the reason is the line below it:
// arithmetic on a `void *` steps by bytes.  clang and gcc both fold
// `sizeof(void)`, `sizeof(const void)` and `_Alignof(void)` to 1, and both
// step a `void *` one byte per unit.  This compiler folded the size to 0, so
// `p + 3` scaled the index by nothing and did not move the pointer at all --
// a silently wrong address rather than a diagnostic (#743).  Arithmetic on a
// `void *` is ordinary GNU C: it is how a program walks a buffer it has no
// element type for, and every answer it produced was the base.
//
// The oracle is clang, with gcc recorded beside it; measured 2026-08-30 on
// x86-64 Linux, clang 21 and gcc 15 agree on every probe here:
//
//     sizeof(void)          1      p + 3          steps 3
//     sizeof(const void)    1      3 + p          steps 3
//     sizeof(volatile void) 1      q - 2          steps -2
//     sizeof(void alias)    1      q - p          answers the byte distance
//     _Alignof(void)        1      ++p, p += n    step by bytes
//     sizeof(*p)            1      p++, --p, -=   step by bytes
//
// The size is an extension for that arithmetic and for `sizeof`, never a
// licence to declare a `void` object: `void v;`, `void a[4];` and a `void`
// member stay refused, which is what the negative half of this rule is, and
// what c_ir_type_is_void_object in c_gen.c answers.  A fixture cannot assert a
// refusal, so those live in the frontend module test
// (c_test_void_object_refusals); what is here is every answer that has to come
// out right at run time.
//
// Every probe reads the stepped pointer back through a live object rather than
// only comparing addresses: a compiler that folded the arithmetic correctly
// and lowered it wrongly would still hand back the right number from a
// subtraction.  The fixture exits non-zero at its first wrong answer, naming
// the case, and the driver test runs it under all four register allocators
// because the index the arithmetic becomes is materialized differently by
// each.

typedef unsigned long size_type;
typedef long difference_type;
typedef void void_alias;

static unsigned char buffer[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// Out of line so the pointer is a real argument rather than a constant the
// fold could carry: the constant-folding path and the emitted arithmetic are
// two separate answers and both have to step.
static unsigned char read_at(void* p)
{
    return *(unsigned char*)p;
}

static void* step(void* p, int n)
{
    return p + n;
}

int main(void)
{
    // The size itself, in every spelling that reaches a different resolver:
    // the keyword, the two qualified copies, an alias, and the size of what a
    // `void *` points at.
    if (sizeof(void) != 1)
    {
        return 1;
    }
    if (sizeof(const void) != 1 || sizeof(volatile void) != 1 || sizeof(const volatile void) != 1)
    {
        return 2;
    }
    if (sizeof(void_alias) != 1)
    {
        return 3;
    }
    if (_Alignof(void) != 1 || _Alignof(const void) != 1)
    {
        return 4;
    }
    void* p = buffer;
    if (sizeof(*p) != 1)
    {
        return 5;
    }
    // A size that folds inside a constant expression the parser evaluates
    // rather than the lowering: an array bound is the shape that has to agree
    // with the fold above.
    {
        char bound[sizeof(void) + sizeof(const void)];
        if (sizeof(bound) != 2)
        {
            return 6;
        }
    }

    // Addition, both orders, read back through the object.
    if (read_at(p + 3) != 3)
    {
        return 7;
    }
    if (read_at(3 + p) != 3)
    {
        return 8;
    }
    if ((unsigned char*)(p + 3) - buffer != 3)
    {
        return 9;
    }
    // Through a call, so the operand is not a constant.
    if (read_at(step(p, 6)) != 6)
    {
        return 10;
    }

    // Subtraction of an integer.
    void* q = buffer + 10;
    if (read_at(q - 2) != 8)
    {
        return 11;
    }
    if ((unsigned char*)(q - 2) - buffer != 8)
    {
        return 12;
    }

    // Subtraction of two `void *`, which is the byte distance: the divide by
    // the element size that a pointer difference normally carries is a divide
    // by one here, and was a divide by zero this compiler refused outright.
    if (q - p != 10)
    {
        return 13;
    }
    if (p - q != -10)
    {
        return 14;
    }
    {
        difference_type distance = (unsigned char*)q - (unsigned char*)p;
        if (q - p != distance)
        {
            return 15;
        }
    }

    // The mutating forms.  Each one reads the object back, so an update that
    // computed the right value into the wrong place still fails.
    p = buffer;
    p += 5;
    if (read_at(p) != 5)
    {
        return 16;
    }
    ++p;
    if (read_at(p) != 6)
    {
        return 17;
    }
    if (read_at(p++) != 6 || read_at(p) != 7)
    {
        return 18;
    }
    --p;
    if (read_at(p) != 6)
    {
        return 19;
    }
    if (read_at(p--) != 6 || read_at(p) != 5)
    {
        return 20;
    }
    p -= 3;
    if (read_at(p) != 2)
    {
        return 21;
    }
    if (p - (void*)buffer != 2)
    {
        return 22;
    }

    // The qualified pointees step the same way; `const void *` is what a
    // program that only reads a buffer writes.
    const void* cp = buffer;
    if (*(const unsigned char*)(cp + 4) != 4)
    {
        return 23;
    }
    cp += 9;
    if (*(const unsigned char*)cp != 9)
    {
        return 24;
    }
    volatile void* vp = buffer;
    if (*(volatile unsigned char*)(vp + 6) != 6)
    {
        return 25;
    }
    vp += 1;
    if (*(volatile unsigned char*)vp != 1)
    {
        return 26;
    }
    void_alias* ap = buffer;
    if (read_at(ap + 12) != 12)
    {
        return 27;
    }

    // A step written on a `void *` stored in an object rather than in a local,
    // so the arithmetic runs on a value loaded from memory.
    static void* stored;
    stored = buffer;
    stored += 14;
    if (read_at(stored) != 14)
    {
        return 28;
    }

    // The step is a byte, not the pointer's own width: a scale of 8 would pass
    // every probe above that only checks a difference, and none of these.
    if (read_at(p + 1) != 3 || read_at(p - 1) != 1)
    {
        return 29;
    }
    // A negative and a zero index, and an index whose type is narrower than a
    // pointer, so the widening the index takes does not change the scale.
    {
        unsigned char narrow = 4;
        if (read_at(buffer + 8 + (int)0) != 8)
        {
            return 30;
        }
        void* mid = buffer + 8;
        if (read_at(mid + narrow) != 12 || read_at(mid - narrow) != 4)
        {
            return 31;
        }
    }

    // Writing through the stepped pointer, which is the half a comparison of
    // addresses cannot see at all.
    void* write = buffer;
    write += 15;
    *(unsigned char*)write = 200;
    if (buffer[15] != 200)
    {
        return 32;
    }

    return 0;
}

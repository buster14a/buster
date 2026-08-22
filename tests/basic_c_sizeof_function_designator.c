// GNU sizeof over a function designator is 1 (clang and gcc agree; both
// warn under -Wpointer-arith), and dereferencing a function pointer yields
// a designator again. The IR function type's layout carries pointer size
// for its other consumers, so the sizeof exits used to fold 8. A function
// pointer object itself still measures pointer size.
static int bare(void)
{
    return 3;
}

static int (*fn_ptr)(void) = bare;

static int folded_static = (int)sizeof(bare);

int main(void)
{
    if (sizeof(bare) != 1 || sizeof bare != 1)
    {
        return 1;
    }
    if (sizeof(*fn_ptr) != 1)
    {
        return 2;
    }
    if (sizeof(fn_ptr) != sizeof(void*))
    {
        return 3;
    }
    if (folded_static != 1)
    {
        return 4;
    }
    // The designator folds inside an array bound and inside arithmetic.
    char bounded[sizeof(bare) + 1];
    bounded[1] = 2;
    if (sizeof(bounded) != 2 || bounded[1] != 2)
    {
        return 5;
    }
    if (10 - (int)sizeof(bare) != 9)
    {
        return 6;
    }
    // A call through the designator still sizes the return type.
    if (sizeof(bare()) != sizeof(int))
    {
        return 7;
    }
    if (fn_ptr() != 3)
    {
        return 8;
    }
    return 0;
}

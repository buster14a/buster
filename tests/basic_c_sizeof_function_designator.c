// GNU sizeof over a function designator is 1 (clang and gcc agree; both
// warn under -Wpointer-arith), and dereferencing a function pointer yields
// a designator again. The IR function type's layout carries pointer size
// for its other consumers, so the sizeof exits used to fold 8. A function
// pointer object itself still measures pointer size.
//
// A spelled function type name measures the same 1. `int(void)` names a
// function type, not the `int` its prefix starts with: the type-name
// resolution used to accept only the pointer declarator `int (*)(void)`, so a
// bare one left the whole type name unresolved and sizeof fell back to the
// prediction's int guess -- 4.
//
// _Alignof over a function has no cross-compiler answer (gcc folds 1, clang
// 4), so the checks below encode gcc's 1, which is what goes with the size
// fold. This fixture is compiled by `ide cc` only; it is not clang-comparable.
static int bare(void)
{
    return 3;
}

static int (*fn_ptr)(void) = bare;

typedef int NamedFunction(void);

static int folded_static = (int)sizeof(bare);

static int folded_type_name = (int)sizeof(int(void)) * 10;

static char spelled_bound[sizeof(int(void)) + 1];

static int apply(int (*call)(void))
{
    return call();
}

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
    // The spelled function type name measures 1 whatever its return type and
    // parameter list are, including the unprototyped and variadic spellings.
    if (sizeof(int(void)) != 1 || sizeof(int()) != 1)
    {
        return 9;
    }
    if (sizeof(double(void)) != 1 || sizeof(void(void)) != 1)
    {
        return 10;
    }
    if (sizeof(int(int, char)) != 1 || sizeof(int(int, ...)) != 1)
    {
        return 11;
    }
    if (sizeof(NamedFunction) != 1 || sizeof(NamedFunction*) != sizeof(void*))
    {
        return 12;
    }
    // Pointer declarators around the same parameter list still measure a
    // pointer, however many stars they carry.
    if (sizeof(int (*)(void)) != sizeof(void*) || sizeof(int(**)(void)) != sizeof(void*))
    {
        return 13;
    }
    // A parameter is a type name too: a function-typed one is adjusted to
    // pointer-to-function, and either spelling leaves the enclosing type name
    // resolvable.
    if (sizeof(int (*)(int(void))) != sizeof(void*) || sizeof(int (*)(int (*)(void))) != sizeof(void*))
    {
        return 14;
    }
    if (sizeof(int(int (*)(void))) != 1 || sizeof(int(char, int(void))) != 1)
    {
        return 15;
    }
    // The spelled name folds in a static initializer, in a file-scope array
    // bound, in a block-scope array bound, and inside arithmetic.
    if (folded_type_name != 10 || sizeof(spelled_bound) != 2)
    {
        return 16;
    }
    char spelled_local[sizeof(int(void)) + 1];
    spelled_local[1] = 5;
    if (sizeof(spelled_local) != 2 || spelled_local[1] != 5)
    {
        return 17;
    }
    if (10 - (int)sizeof(int(void)) != 9)
    {
        return 18;
    }
    // gcc's alignment for both spellings of the same function type.
    if (_Alignof(int(void)) != 1 || __alignof__(int(void)) != 1)
    {
        return 19;
    }
    // A call through a pointer to one of these types still runs.
    if (apply(bare) != 3 || ((int (*)(void))bare)() != 3)
    {
        return 20;
    }
    return 0;
}

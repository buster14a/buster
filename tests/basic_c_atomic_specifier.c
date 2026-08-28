// `quickjs-libc.c` reaches an atomic through a cast:
// `atomic_fetch_add((_Atomic(uint32_t) *)ptr, v)`.  The declaration path
// already parsed `_Atomic ( type-name )`, but the type-name reader a cast
// goes through did not, so the cast was lowered as an expression and
// `_Atomic` reached lowering as an unbound identifier.  The atomicity also
// has to reach the IR type: an atomic builtin reads it off the place its
// pointer argument dereferences to.
static int atomic_add(int *pointer, int value)
{
    return __c11_atomic_fetch_add((_Atomic(unsigned) *)pointer, value, 5) + value;
}

static unsigned atomic_load(const int *pointer)
{
    return __c11_atomic_load((const _Atomic(unsigned) *)pointer, 5);
}

static _Atomic(unsigned) declared_atomic = 3;

int main(void)
{
    int counter = 4;
    if (atomic_add(&counter, 6) != 10) return 1;
    if (counter != 10) return 2;
    if (atomic_load(&counter) != 10u) return 3;
    // The specifier form is also a type name on its own, and it carries
    // pointer and qualifier suffixes.
    if (sizeof(_Atomic(unsigned)) != sizeof(unsigned)) return 4;
    if (sizeof(_Atomic(unsigned) *) != sizeof(void *)) return 5;
    if (__c11_atomic_load(&declared_atomic, 5) != 3u) return 6;
    __c11_atomic_store(&declared_atomic, 8u, 5);
    if (__c11_atomic_load(&declared_atomic, 5) != 8u) return 7;
    return 0;
}

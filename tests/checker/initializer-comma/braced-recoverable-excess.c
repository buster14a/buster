int *file_integer[1] = { 1, 2 };

void recoverable_values(int *pointer, void (*function)(void), _Atomic(int) atomic)
{
    int *integer[1] = { 1, 2 };
    long address[1] = { pointer, 2 };
    int *incompatible[1] = { function, 0 };
    int *atomic_lvalue[1] = { atomic, 0 };
    int *atomic_rvalue[1] = { (_Atomic int)1, 0 };
}

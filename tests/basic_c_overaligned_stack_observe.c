// A separate translation unit prevents a callee's ABI alignment assumptions
// from folding its parameter-address check to zero before it executes.
int stack_observe_alignment(void const* pointer, unsigned long long alignment)
{
    return ((unsigned long long)pointer & (alignment - 1)) != 0;
}

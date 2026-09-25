// A fetch-or keeps bits already set in both the object and operand.
int main(void)
{
    _Atomic(unsigned int) value = 1;
    unsigned int previous = __c11_atomic_fetch_or(&value, 1, __ATOMIC_RELAXED);
    unsigned int after = __c11_atomic_load(&value, __ATOMIC_RELAXED);
    return (previous != 1) | ((after != 1) << 1);
}

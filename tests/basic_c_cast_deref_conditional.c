// ctypes' SET macro writes `(signed char)*(signed char*)ptr & mask` inside
// a conditional arm: the star after the cast is a dereference, and an
// operator scan that reads it as multiplication combines the mask against
// the pointer and predicts the arm as pointer-typed.  Both bit-field
// updates are executed and checked.
static signed char set_bits(void* ptr, long val, long size)
{
    return (((size) >> 16)
        ? (((signed char)*(signed char*)ptr & ~((((((signed char)1 << (((size) >> 16) - 1)) - 1) << 1) + 1) << ((size) & 0xFFFF)))
           | (((signed char)val & (((((signed char)1 << (((size) >> 16) - 1)) - 1) << 1) + 1)) << ((size) & 0xFFFF)))
        : (signed char)val);
}
int main(void)
{
    char storage = 0;
    if (set_bits(&storage, 5, 0) != 5) { return 1; }
    if (set_bits(&storage, 3, (3L << 16) | 0) != 3) { return 2; }
    return 0;
}

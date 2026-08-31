// Driver options follow last-option-wins: an allocator named after the -O
// flag decides the emitter (the reverse order deliberately restores the
// default, which is why harnesses that ride autoconf carry the flag in
// CFLAGS, after the project's own -O3).  The driver test compiles this
// fixture under two allocators spelled after -O2 and asserts the objects
// differ; the runtime body only proves each object still computes.

__attribute__((noinline)) static int accumulate(const int* values, int count)
{
    int total = 0;
    for (int index = 0; index < count; index += 1)
    {
        total += values[index] * (index + 1);
    }
    return total;
}

int main(void)
{
    int values[5] = {3, 1, 4, 1, 5};
    if (accumulate(values, 5) != 3 + 2 + 12 + 4 + 25)
    {
        return 1;
    }
    return 0;
}

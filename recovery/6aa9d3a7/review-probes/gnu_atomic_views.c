static volatile char small;
static int values[4];
static int *volatile pointer;

int main(void)
{
    __atomic_store_n(&small, 2, __ATOMIC_RELEASE);
    if (__atomic_add_fetch(&small, 3, __ATOMIC_SEQ_CST) != 5)
    {
        return 1;
    }
    __atomic_store_n(&pointer, values, __ATOMIC_RELEASE);
    if (__atomic_add_fetch(&pointer, 1, __ATOMIC_SEQ_CST) != (int *)((char *)values + 1))
    {
        return 2;
    }
    return 0;
}

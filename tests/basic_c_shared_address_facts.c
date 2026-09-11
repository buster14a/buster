// Shared selector address facts must retain subobjects and pointer widths,
// preserve pure address arithmetic, and stop at loaded/atomic pointer values.
struct AddressInner
{
    int prefix;
    int elements[8];
};

struct AddressOuter
{
    char padding[32769];
    struct AddressInner inner[4];
};

static struct AddressOuter address_global;
static int *address_relocation = &address_global.inner[2].elements[3];
static const struct AddressInner address_read_only = {5, {11, 13}};
static volatile struct AddressInner address_volatile;
static int *address_atomic_pointer;

static int *address_nested(struct AddressOuter *outer, int row, signed char column)
{
    return &outer->inner[row].elements[column];
}

static int *address_negative(int *pointer, short offset)
{
    return &pointer[offset];
}

static unsigned long long address_integer_wrap(unsigned long long bits)
{
    void *pointer = (void *)bits;
    return (unsigned long long)pointer + 16;
}

int main(void)
{
    int failed = 0;
    struct AddressInner local = {3, {1, 2, 3, 4, 5, 6, 7, 8}};
    int *middle = &local.elements[4];
    failed |= address_negative(middle, -4) != &local.elements[0];
    failed |= address_negative(middle, 4) != &local.elements[8];
    failed |= &*(&local.elements[2]) != &local.elements[2];
    failed |= address_nested(&address_global, 2, 3) != address_relocation;
    *address_relocation = 31;
    failed |= *address_nested(&address_global, 2, 3) != 31;
    failed |= address_read_only.prefix != 5 || address_read_only.elements[1] != 13;
    address_volatile.elements[3] = 19;
    address_volatile.elements[3] += 2;
    failed |= address_volatile.elements[3] != 21;
    __atomic_store_n(&address_atomic_pointer, &local.elements[2], __ATOMIC_RELAXED);
    int *loaded = __atomic_load_n(&address_atomic_pointer, __ATOMIC_RELAXED);
    failed |= loaded != &local.elements[2];
    failed |= __atomic_add_fetch(loaded, 7, __ATOMIC_SEQ_CST) != 10;
    failed |= address_integer_wrap(~0ULL - 3) != 12;
    void *first = &&address_first;
    void *second = &&address_second;
    failed |= first == second;
    goto *first;
address_first:
    failed |= local.elements[2] != 10;
    goto *second;
address_second:
    return failed;
}

long wasm64_global = 41;
long* wasm64_global_pointer = &wasm64_global;

static int wasm64_twice(int value)
{
    return value + value;
}

int wasm64_add(int left, int right)
{
    return left + right;
}

unsigned long wasm64_pointer_bits(void)
{
    return sizeof(void*) * 8;
}

long wasm64_read_global(void)
{
    return wasm64_global;
}

long wasm64_read_pointer_global(void)
{
    return *wasm64_global_pointer;
}

int wasm64_negate(int value)
{
    return -value;
}

int wasm64_direct_call(int value)
{
    return wasm64_twice(value);
}

long wasm64_switch(long value)
{
    switch (value)
    {
    case 3: return 30;
    case 7: return 70;
    default: return 90;
    }
}

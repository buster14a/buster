// Compile provider and consumer separately, then exchange them with Clang/GCC.
// The reference answer is a scalar lane calculation with adjacent sentinels.
typedef unsigned int WideAbiVector __attribute__((vector_size(32)));
typedef union WideAbiStorage { WideAbiVector vector; unsigned int lanes[8]; } WideAbiStorage;

WideAbiVector wide_abi_make(unsigned int seed);
WideAbiVector wide_abi_mix(WideAbiVector first, unsigned int tag, WideAbiVector second);
WideAbiVector wide_abi_ninth(WideAbiVector, WideAbiVector, WideAbiVector, WideAbiVector,
                           WideAbiVector, WideAbiVector, WideAbiVector, WideAbiVector, WideAbiVector);

#if defined(WIDE_ABI_PROVIDER)
WideAbiVector wide_abi_make(unsigned int seed)
{
    WideAbiStorage result;
    for (unsigned int lane = 0; lane < 8; lane += 1) { result.lanes[lane] = seed + lane * 17u; }
    return result.vector;
}

WideAbiVector wide_abi_mix(WideAbiVector first, unsigned int tag, WideAbiVector second)
{
    WideAbiVector result = first + second;
    for (unsigned int lane = 0; lane < 8; lane += 1) { result[lane] ^= tag + lane; }
    return result;
}

WideAbiVector wide_abi_ninth(WideAbiVector a, WideAbiVector b, WideAbiVector c, WideAbiVector d,
    WideAbiVector e, WideAbiVector f, WideAbiVector g, WideAbiVector h, WideAbiVector i)
{
    return a + b + c + d + e + f + g + h + i;
}
#else
int main(void)
{
    struct { unsigned int before[4]; WideAbiStorage value; unsigned int after[4]; } destination;
    WideAbiStorage first;
    WideAbiStorage second;
    for (unsigned int lane = 0; lane < 4; lane += 1)
    {
        destination.before[lane] = 0x51c0ffeeu + lane;
        destination.after[lane] = 0xdecaf00du + lane;
    }
    for (unsigned int lane = 0; lane < 8; lane += 1)
    {
        first.lanes[lane] = 10u + lane * 17u;
        second.lanes[lane] = 100u + lane * 23u;
    }
    int result = 0;
    destination.value.vector = wide_abi_make(10u);
    for (unsigned int lane = 0; lane < 8; lane += 1)
    {
        if (destination.value.lanes[lane] != first.lanes[lane]) { result = 1; }
    }
    WideAbiVector (*volatile indirect)(WideAbiVector, unsigned int, WideAbiVector) = wide_abi_mix;
    destination.value.vector = indirect(first.vector, 0x1234u, second.vector);
    for (unsigned int lane = 0; lane < 8; lane += 1)
    {
        if (destination.value.lanes[lane] != ((first.lanes[lane] + second.lanes[lane]) ^ (0x1234u + lane))) { result = 2; }
    }
    destination.value.vector = wide_abi_ninth(first.vector, second.vector, first.vector, second.vector,
        first.vector, second.vector, first.vector, second.vector, first.vector);
    for (unsigned int lane = 0; lane < 8; lane += 1)
    {
        if (destination.value.lanes[lane] != 5u * first.lanes[lane] + 4u * second.lanes[lane]) { result = 3; }
    }
    for (unsigned int lane = 0; lane < 4; lane += 1)
    {
        if (destination.before[lane] != 0x51c0ffeeu + lane || destination.after[lane] != 0xdecaf00du + lane) { result = 4; }
    }
    return result;
}
#endif

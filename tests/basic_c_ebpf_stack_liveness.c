// Issue #102: a dead branch must not change eBPF stack-limit acceptance.
unsigned probe(unsigned input)
{
    unsigned x = input;
    unsigned y = 0u;
#if EBPF_DEAD_BRANCH
    if (0u) { x = input + 19u; }
#endif
    if (x < 0u) { x = x + y; } else { x = x - y; }
    for (unsigned i = 0; i < 3u; i += 1u)
    {
        unsigned saved = x; x = y; y = saved + y + 0u;
    }
    { unsigned slots[2]; slots[0] = x; slots[1] = y; x = slots[0] + slots[1] + 0u; }
    { unsigned slots[2]; slots[0] = x; slots[1] = y; x = slots[0] + slots[1] + 0u; }
    if (x < 0u) { x = x + y; } else { x = x - y; }
    { unsigned slots[2]; slots[0] = x; slots[1] = y; x = slots[0] + slots[1] + 0u; }
    x = x + (unsigned)sizeof (++y);
    return x + y;
}

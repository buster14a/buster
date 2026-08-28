// A `goto` into the middle of a loop body.  LZ4_decompress_generic enters its
// copy loop that way, and a stack checkpoint taken at the body's entry does
// not dominate the label: the end-of-iteration restore then loaded a stale
// frame slot into RSP and the next call faulted.  A body that allocates
// nothing must emit no checkpoint at all.
static int visits;

static void visit(int value) { visits += value; }

static int jump_into_body(int limit, int *accumulator)
{
    int index = 0;
    if (limit > 3) goto inside;
    while (index < limit)
    {
        visit(index);
    inside:
        *accumulator += index;
        index += 1;
    }
    return index;
}

// The counterpart the checkpoint exists for: a variably modified declaration
// inside a loop body must be reclaimed once per iteration, including on the
// `continue` path, so the addresses the loop hands out stay within one
// allocation instead of walking down the stack.
static unsigned long long checksum;

static void fill(char *storage, int length)
{
    storage[0] = (char)length;
    storage[length - 1] = (char)(length + 1);
    checksum += (unsigned long long)(unsigned char)storage[0] + (unsigned long long)(unsigned char)storage[length - 1];
}

static int variable_length_loop(void)
{
    unsigned long long lowest = ~0ull;
    unsigned long long highest = 0;
    for (int index = 1; index <= 20000; index += 1)
    {
        int length = (index % 64) + 8;
        char storage[length];
        fill(storage, length);
        unsigned long long address = (unsigned long long)(unsigned long)(void *)storage;
        lowest = address < lowest ? address : lowest;
        highest = address > highest ? address : highest;
        if (index % 3 == 0) { continue; }
    }
    return checksum != 0 && highest - lowest < 4096;
}

int main(void)
{
    int accumulator = 0;
    if (jump_into_body(5, &accumulator) != 5) return 1;
    if (accumulator != 10) return 2;
    if (visits != 10) return 3;
    visits = 0;
    accumulator = 0;
    if (jump_into_body(3, &accumulator) != 3) return 4;
    if (accumulator != 3 || visits != 3) return 5;
    if (!variable_length_loop()) return 6;
    return 0;
}

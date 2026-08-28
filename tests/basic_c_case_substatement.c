// A case label standing where a control statement expects its substatement.
// LZ4F_decompress uses the shape to let one stage fall into the next while
// still being reachable directly from the switch dispatch, so the label
// belongs to the enclosing statement rather than opening a new case range.
static int stage(int entry, int *out)
{
    switch (entry)
    {
    case 0:
        *out += 1;
        if (entry == 0)
    case 1:
        {
            *out += 10;
        }
        *out += 100;
        break;
    case 2:
        *out += 1000;
        break;
    }
    return *out;
}

// The same shape under a loop header, where the label is entered both by the
// dispatch and by the iteration.
static int looped(int entry)
{
    int total = 0;
    int count = 0;
    switch (entry)
    {
    case 0:
        while (count < 3)
    case 1:
            {
                total += 1;
                count += 1;
            }
        break;
    default:
        total = -1;
        break;
    }
    return total;
}

int main(void)
{
    int value = 0;
    if (stage(0, &value) != 111) return 1;
    value = 0;
    if (stage(1, &value) != 110) return 2;
    value = 0;
    if (stage(2, &value) != 1000) return 3;
    if (looped(0) != 3) return 4;
    if (looped(1) != 3) return 5;
    if (looped(7) != -1) return 6;
    return 0;
}

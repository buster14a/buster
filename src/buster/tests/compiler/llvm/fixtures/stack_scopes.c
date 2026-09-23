// The independent caller observes only live VLA elements. Every scope exit
// must preserve the outer VLA while discarding allocations made after it.
extern int observe_bytes(volatile unsigned char* bytes, int count, int first, int last);

int scoped_vlas(int n, int repetitions)
{
    volatile unsigned char outer[n + 1];
    outer[0] = 71;
    outer[n] = 83;
    int result = 0;
    for (int i = 0; i < repetitions; i += 1)
    {
        volatile unsigned char bytes[n];
        bytes[0] = (unsigned char)i;
        bytes[n - 1] = (unsigned char)(n + i);
        result += observe_bytes(bytes, n, (unsigned char)i, (unsigned char)(n + i));
        if (i % 3 == 0)
        {
            volatile unsigned char nested[n + 3];
            nested[0] = (unsigned char)(i + 3);
            nested[n + 2] = 47;
            result += observe_bytes(nested, n + 3, (unsigned char)(i + 3), 47);
        }
    }
    return result + outer[0] + outer[n];
}

int scoped_exits(int n, int mode)
{
    volatile unsigned char outer[n + 3];
    outer[0] = 19;
    outer[n + 2] = 23;
    int result = 0;
    for (int i = 0; i < 4; i += 1)
    {
        volatile unsigned char inner[n];
        inner[0] = (unsigned char)i;
        inner[n - 1] = 29;
        if (mode == 1 && i == 0)
        {
            continue;
        }
        if (mode == 2 && i == 1)
        {
            break;
        }
        if (mode == 3 && i == 2)
        {
            goto outer_exit;
        }
        if (mode == 4 && i == 0)
        {
            return outer[0] + outer[n + 2];
        }
        result += observe_bytes(inner, n, (unsigned char)i, 29);
    }
outer_exit:
    return result + outer[0] + outer[n + 2];
}

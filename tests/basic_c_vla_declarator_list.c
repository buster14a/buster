// A declaration may carry more than one declarator, and any of them may be a
// variable-length array: musl's res_msend writes
// `int qpos[nqueries], apos[nqueries];`.  A list is split at its top-level
// commas and each declarator is lowered on its own, so the VLA machinery has
// to be reachable from that path as well as from the single-declarator one --
// and the stack save the allocations move the stack pointer past is taken once
// for the whole list, before the split, because only the statement walker
// holds the loop task it belongs to.

static int check_two(int count)
{
    int first[count], second[count];
    for (int index = 0; index < count; index += 1)
    {
        first[index] = index;
        second[index] = index * 10;
    }
    if (sizeof(first) != (unsigned long long)count * sizeof(int) || sizeof(second) != sizeof(first))
    {
        return 1;
    }
    if (first[count - 1] != count - 1 || second[count - 1] != (count - 1) * 10)
    {
        return 2;
    }
    return (char *)second == (char *)first ? 3 : 0;
}

// A scalar either side of the array in the same list: the scalar's storage is
// an ordinary frame slot and the array's is not, so the two must not be
// confused for one another when the segments are lowered in order.
static int check_mixed(int count)
{
    int leading = 5, values[count], trailing = 7;
    values[0] = 11;
    values[count - 1] = 13;
    if (leading != 5 || trailing != 7)
    {
        return 1;
    }
    return values[0] + values[count - 1] == 24 ? 0 : 2;
}

// Two dimensions in a list, and a second declarator whose bounds differ from
// the first's: each declarator owns its own layout rather than inheriting the
// one the specifiers began.
static int check_nested(int rows, int columns)
{
    int narrow[rows][columns], wide[rows][columns + 2];
    narrow[rows - 1][columns - 1] = 17;
    wide[rows - 1][columns + 1] = 19;
    if (sizeof(narrow[0]) != (unsigned long long)columns * sizeof(int))
    {
        return 1;
    }
    if (sizeof(wide[0]) != (unsigned long long)(columns + 2) * sizeof(int))
    {
        return 2;
    }
    return narrow[rows - 1][columns - 1] + wide[rows - 1][columns + 1] == 36 ? 0 : 3;
}

// The checkpoint the list takes has to be the one `continue` restores, or the
// next iteration allocates below what this one left behind and the frame grows
// without bound.
static int check_loop_lifetime(int count)
{
    int total = 0;
    for (int index = 0; index < 256; index += 1)
    {
        int first[count], second[count];
        first[count - 1] = index;
        second[count - 1] = index;
        if ((index & 1) != 0)
        {
            continue;
        }
        total += first[count - 1] + second[count - 1];
    }
    return total == 32512 ? 0 : 1;
}

int main(void)
{
    int two = check_two(9);
    if (two != 0)
    {
        return 10 + two;
    }
    int mixed = check_mixed(6);
    if (mixed != 0)
    {
        return 20 + mixed;
    }
    int nested = check_nested(4, 5);
    if (nested != 0)
    {
        return 30 + nested;
    }
    int lifetime = check_loop_lifetime(1024);
    if (lifetime != 0)
    {
        return 40 + lifetime;
    }
    return 0;
}

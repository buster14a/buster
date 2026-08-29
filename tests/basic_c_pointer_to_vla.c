// `char (*p)[width]` is a pointer to a variably modified array, which is the
// type `char p[][width]` adjusts to, and musl's lsearch and lfind walk their
// tables with one.  The local is an ordinary pointer holding an address the
// initializer computes -- no storage is allocated for it -- but indexing it
// scales by the runtime row size, so it carries the same dimension table an
// array parameter does.
//
// Indexing either shape with fewer subscripts than it has dimensions leaves an
// array, and an array is the address of its first element everywhere but
// `sizeof` and `&`.  A variably modified array has no IR array type for the
// ordinary decay to recognise, so `p[i]` used to load one element's worth of
// bytes where the row's address was meant -- which is what `compar(key, p[i])`
// hands its comparison function.

typedef unsigned long ulong;

static ulong row_offset(void *base, ulong width, ulong index)
{
    char (*rows)[width] = base;
    return (ulong)((char *)rows[index] - (char *)base);
}

static ulong parameter_row_offset(ulong width, char rows[][width], ulong index)
{
    return (ulong)((char *)rows[index] - (char *)rows);
}

static ulong local_row_offset(ulong rows, ulong columns, ulong index)
{
    char cells[rows][columns];
    return (ulong)((char *)cells[index] - (char *)cells);
}

static int compare_four(const void *left, const void *right)
{
    const char *a = left;
    const char *b = right;
    for (int index = 0; index < 4; index += 1)
    {
        if (a[index] != b[index])
        {
            return 1;
        }
    }
    return 0;
}

// musl's lsearch, spelled the way musl spells it.
static void *table_search(const void *key, void *base, ulong *count, ulong width, int (*compar)(const void *, const void *))
{
    char (*p)[width] = base;
    ulong n = *count;
    for (ulong index = 0; index < n; index += 1)
    {
        if (compar(key, p[index]) == 0)
        {
            return p[index];
        }
    }
    *count = n + 1;
    for (ulong byte = 0; byte < width; byte += 1)
    {
        p[n][byte] = ((const char *)key)[byte];
    }
    return p[n];
}

int main(void)
{
    char table[8][4] = {0};
    if (row_offset(table, 4, 3) != 12 || row_offset(table, 7, 5) != 35 || row_offset(table, 1, 0) != 0)
    {
        return 1;
    }
    if (parameter_row_offset(4, table, 3) != 12)
    {
        return 2;
    }
    if (local_row_offset(6, 5, 4) != 20)
    {
        return 3;
    }
    {
        char (*rows)[4] = table;
        if (sizeof(rows) != sizeof(char *) || sizeof(*rows) != 4 || sizeof(rows[0]) != 4)
        {
            return 4;
        }
        rows[2][1] = 'x';
        if (table[2][1] != 'x')
        {
            return 5;
        }
    }
    {
        table[0][0] = 'a';
        table[0][1] = 'a';
        table[0][2] = 'a';
        table[0][3] = 0;
        ulong count = 1;
        char *found = table_search("bbb", table, &count, 4, compare_four);
        if (count != 2 || found != table[1] || found[0] != 'b')
        {
            return 6;
        }
        found = table_search("aaa", table, &count, 4, compare_four);
        if (count != 2 || found != table[0])
        {
            return 7;
        }
    }
    return 0;
}

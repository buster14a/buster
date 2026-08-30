// C11 6.7.6.2p4: `[*]` declares a variable-length array of unspecified size,
// valid only in a declaration with function prototype scope that is not a
// definition -- it is the standard way to forward-declare the definition that
// spells the same parameter `[n]`. Its bound is one `*` token, and the type
// compatibility check compared that spelling against the definition's
// expression, failed to evaluate `*` as a constant, and reported the pair as
// conflicting declarations (issue #825). Both parameter types are pointers
// after adjustment anyway.
//
// The controls are the spellings that were already compatible and must stay so:
// `[n]` against `[n]`, `[]` against a pointer, and a 2-D `[*][*]` whose inner
// bound the definition spells with a runtime expression.

static int sum(int n, int a[*]);
static int sum(int n, int a[n])
{
    int total = 0;
    for (int index = 0; index < n; index += 1)
    {
        total += a[index];
    }
    return total;
}

static int corner(int rows, int columns, int grid[*][*]);
static int corner(int rows, int columns, int grid[rows][columns])
{
    return grid[rows - 1][columns - 1];
}

static int qualified(int n, int a[const *]);
static int qualified(int n, int a[const n])
{
    return a[n - 1];
}

static int unbounded(int n, int a[]);
static int unbounded(int n, int* a)
{
    return a[n - 1];
}

int main(void)
{
    int flat[3] = {1, 2, 3};
    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    if (sum(3, flat) != 6)
    {
        return 1;
    }
    if (corner(2, 3, grid) != 6)
    {
        return 2;
    }
    if (qualified(3, flat) != 3)
    {
        return 3;
    }
    if (unbounded(3, flat) != 3)
    {
        return 4;
    }
    return 0;
}

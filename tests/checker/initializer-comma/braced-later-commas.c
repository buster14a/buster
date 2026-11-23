struct Source { int value; } source;

void local_values(struct Source value)
{
    int *values[3][1] = { { (1, value), (2, value) }, { (3, (int *)0), (4, 0) }, { (5, value), (6, 0) } };
    int *designated[3][1] = { [0] = { (7, value), (8, 0) }, [1] = { (9, (int *)0), (10, 0) }, [2] = { (11, value), (12, 0) } };
}

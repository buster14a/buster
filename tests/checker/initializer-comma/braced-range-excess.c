struct Source { int value; } source;

void local_values(struct Source value)
{
    int *rejected_range[3][1] = { [0 ... 1] = { value, 0 }, [2] = { 0, 0 } };
    int *valid_range[3][1] = { [0 ... 1] = { 0, 0 }, [2] = { value, 0 } };
}

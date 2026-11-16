struct Source { int value; } source;
struct Pair { int *first; int *second; };
struct Pair file_values[4] = { [0 ... 2] = { (1, source), (2, source) }, [3] = { (3, source), (4, source) } };

void local_values(void)
{
    int *values[5] = { [0 ... 2] = (5, source), [4] = (6, source) };
}

struct Source { int value; } source;
int *file_values[4] = { (1, (int *)0), (2, source), (3, (int *)0), (4, source) };

void local_values(void)
{
    int *values[4] = { (5, (int *)0), (6, source), (7, (int *)0), (8, source) };
}

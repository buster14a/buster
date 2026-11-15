struct Source { int value; } source;
int *file_values[3] = { (1, source), 0, (2, source) };

void local_values(void)
{
    int *values[4] = { (3, source), 0, (4, source), (5, source) };
}

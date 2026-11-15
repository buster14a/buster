struct Source { int value; } source;
int *file_values[3] = { source, 0, source };

void local_values(void)
{
    int *values[4] = { source, 0, source, source };
}

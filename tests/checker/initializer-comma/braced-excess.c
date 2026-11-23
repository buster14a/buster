struct Source { int value; } source;
int *file_earlier[1] = { 0, 0 };
int *file_rejected[2] = { source, source, 0 };
int *file_nested[2][1] = { { 0, 0 }, { source, 0 } };

void local_values(struct Source value)
{
    int *earlier[1] = { 0, 0 };
    int *nested[2][1] = { { 0, 0 }, { value, 0 } };
    int *rejected[2] = { (1, value), (2, value), 0 };
    int *ancestor[1][1] = { { value, 0 }, { 0, 0 } };
    int *later[1] = { 0, 0 };
}

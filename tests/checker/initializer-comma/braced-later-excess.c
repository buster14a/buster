struct Source { int value; } source;

int *file_values[3][1] = { { source, 0 }, { 0, 0 }, { source, 0 } };

void local_values(struct Source value)
{
    int *nested[3][1] = { { value, 0 }, { 0, 0 }, { value, 0 } };
    int *ancestor[2][1] = { { value, 0 }, { 0, 0 }, { 0, 0 } };
    int *implicit[2][1] = { value, { 0, 0 } };
    int *designated[3][1] = { [0] = { value, 0 }, [1] = { 0, 0 }, [2] = { 0, 0 } };
    int *deep[2][2][1] = { { { value, 0 }, { 0, 0 } }, { { 0, 0 }, { 0, 0 } } };
    struct Pair { int *a[1]; int *b[1]; } record = { .a = { value, 0 }, .b = { 0, 0 } };
    struct Text { int *p; char text[1]; } string = { value, "abc" };
    struct Text string_braces = { value, { "abc" } };
}

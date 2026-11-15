struct Source { int value; } source;
// Explicit warning policy isolates earlier controls from the existing global -Werror excess-warning limitation.
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wexcess-initializers"
int *file_earlier[1] = { 0, 0 };
#pragma clang diagnostic pop
int *file_rejected[2] = { source, source, 0 };
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wexcess-initializers"
int *file_nested[2][1] = { { 0, 0 }, { source, 0 } };
#pragma clang diagnostic pop

void local_values(struct Source value)
{
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wexcess-initializers"
    int *earlier[1] = { 0, 0 };
    int *nested[2][1] = { { 0, 0 }, { value, 0 } };
#pragma clang diagnostic pop
    int *rejected[2] = { (1, value), (2, value), 0 };
    int *ancestor[1][1] = { { value, 0 }, { 0, 0 } };
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wexcess-initializers"
    int *later[1] = { 0, 0 };
#pragma clang diagnostic pop
}

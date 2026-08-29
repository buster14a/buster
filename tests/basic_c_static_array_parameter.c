// A C99 `static` array bound on a parameter must not change the linkage of
// the function that declares it. The definitions live in
// basic_c_static_array_parameter_def.c and are reachable only across the
// unit boundary, so a definition the frontend made internal -- or dropped
// for being internal and uncalled -- fails this fixture at link time rather
// than silently producing an object nobody can call.

#include <stddef.h>

void fill_hidden(char buffer[static 15 + 3 * sizeof(int)], unsigned value);
int sum_static(int values[static 4]);
int head_static(int values[static 2]);
long tail_static(int count, long values[const static 3]);

int main(void)
{
    char buffer[32];
    int values[4] = {1, 2, 3, 4};
    long longs[3] = {10, 20, 30};

    buffer[0] = 0;
    fill_hidden(buffer, 7);
    if (buffer[0] != 7)
        return 1;
    if (sum_static(values) != 10)
        return 2;
    if (head_static(values) != 1)
        return 3;
    if (tail_static(2, longs) != 30)
        return 4;
    return 0;
}

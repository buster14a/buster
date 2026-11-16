struct Source { int value; } source;
struct Pair { int *first; int *second; };
struct Container { struct Pair pair[2]; int *last; };
struct Container file_value = {
    .pair[1].second = (1, source),
    .pair[0].first = (2, source),
    .pair[1].first = 0,
    .last = (3, source),
};

void local_values(void)
{
    int *values[5] = { [3] = (4, source), [1] = (5, source), 0, [4] = (6, source) };
}

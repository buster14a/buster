int item;
struct Pair { int *first; int *second; };
struct Container { struct Pair pair[2]; int *last; };
struct Container file_value = { { { &item, 0 }, { 0, &item } }, &item };

int valid_values(void)
{
    struct Container value = { .pair[1].second = &item, .pair[0].first = &item, .last = &item };
    int *values[4] = { [3] = &item, [1] = &item, 0 };
    return value.pair[1].second == values[3] && file_value.last == value.last;
}

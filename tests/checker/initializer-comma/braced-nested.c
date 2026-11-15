struct Source { int value; } source;
struct Pair { int *first; int *second; };
struct Container { struct Pair pair[2]; int *last; };
struct Container file_value = { { { (1, source), 0 }, { 0, (2, source) } }, (3, source) };

void local_values(void)
{
    struct Container value = { { { (4, source), 0 }, { 0, (5, source) } }, (6, source) };
}

struct Source { int value; } source;
struct Pair { int *first; int *second; };
struct Pair file_value = (struct Pair){ (1, source), (2, source) };

void local_values(void)
{
    struct Pair value = (struct Pair){ (3, source), (4, source) };
}

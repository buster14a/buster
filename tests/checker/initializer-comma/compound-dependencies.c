struct Source { int value; } source;
struct Pair { int *first; int *second; };

void compound_dependencies(void)
{
    int *values[2] = { (int){ source }, source };
    struct Pair pairs[2] = {
        (struct Pair){ (1, source), (2, source) },
        { (3, source), (4, source) },
    };
    int *comma[2] = { ((int){ source }, source), (5, source) };
    int *member[2] = { (struct Pair){ source, source }.first, source };
    int *nested[2] = { (int){ (int){ source } }, source };
    int *earlier[2] = { (6, source), (int){ source } };
    int *later = source;
}

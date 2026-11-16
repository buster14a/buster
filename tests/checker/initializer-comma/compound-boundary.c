struct Source { int value; } source;
struct Pair { int *first; int *second; };
void compound_boundary(void)
{
    struct Pair values[2] = {
        (struct Pair){ source, source },
        { source, source },
    };
}

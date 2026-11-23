struct Source { int value; };

void compound_excess_dependencies(struct Source source)
{
    int first[1] = { source, (int){ (0, 1) } };
    int nested[1] = { source, (int){ (1, (int){ (2, 3) }) } };
    int unevaluated[1] = { source, sizeof((int){ (3, 4) }) };
    int *later[1] = { (int){ (4, 5) } };
}

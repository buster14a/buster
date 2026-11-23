struct Source { int value; } source;

void compound_expressions(int choose)
{
    int *scalar = (int){ source };
    int *record = (struct Source){ source };
    int *comma = ((struct Source){ source }, source);
    int *binary = (int){ source } + source;
    int *conditional = choose ? (int){ source } : source;
    int *member = (struct Source){ source }.missing;
    int *parenthesized = (((int){ source }));
    int *independent = source;
}

struct Source { int value; } source;
int consume(int first, int second);

void compound_call_branches(void)
{
    int *unselected = 0 ? (int){ source } : source;
    int *argument = consume((int){ source }, source);
    int *independent_argument = consume((int){ source }, source + 1);
}

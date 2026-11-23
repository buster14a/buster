struct Source { int value; } source;
struct Pair { int *first; int *second; };

void compound_empty_elements(void)
{
    int *direct[2] = { ({ (int){ (0, source) }; }), (1, source) };
    int *parenthesized[2] = { (({ ((int){ source }); })), source };
    struct Pair fields = { .second = ({ (int){ source }; }), .first = source };
    int *nested[2][2] = { { ({ (int){ source }; }), source }, { source, source } };
    int *atomic[2] = { ({ (_Atomic(int)){ 1 }; }), source };
    int scalar = { ({ (int){ source }; }) };
}

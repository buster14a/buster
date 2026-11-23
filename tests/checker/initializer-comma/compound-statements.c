struct Source { int value; } source;

void compound_statements(void)
{
    int *empty = ({ (int){ source }; });
    int *parenthesized = (({ ((int){ source }); }));
    int *earlier_statement = ({ (void)0; (int){ source }; });
    int *later_statement = ({ (int){ source }; source; });
    int *later_comma = ({ (int){ source }; (1, source); });
    int *later_declaration = ({ int rejected = (int){ source }; (2, source); });
    int *independent = source;
}

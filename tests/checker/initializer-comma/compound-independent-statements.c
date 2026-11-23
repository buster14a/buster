struct Source { int value; } source;

void compound_independent_statements(void)
{
    int *declaration = (int){ ({ int *invalid = source; 1; }) };
    int *literal = (int){ ({ (int){ source }; 1; }) };
    int *operator = (int){ ({ source + 1; 1; }) };
    int *final_invalid = (int){ ({ int *invalid = source; (int){ source }; }) };
    int *final_valid = (int){ ({ int *invalid = source; (int){ 1 }; }) };
}

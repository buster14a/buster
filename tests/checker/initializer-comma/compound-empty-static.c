struct Source { int value; };

void static_empty_elements(struct Source source)
{
    static int *elements[2] = { ({ (int){ source }; }), source };
    static int *single = ({ (int){ source }; });
    static int integers[2] = { ({ (int){ source }; }), source };
}

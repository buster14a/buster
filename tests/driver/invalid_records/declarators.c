struct Negative
{
    int first[-1];
    int later;
};
int first_tail = missing_after_negative;

struct NestedArray
{
    int matrix[2][-3];
    int last;
};
int second_tail = missing_after_nested;

struct IncompleteElement;
struct ArrayElement
{
    struct IncompleteElement values[2];
    int last;
};
int third_tail = missing_after_element;

int invalid_uses(struct Negative *a, struct NestedArray *b, struct ArrayElement *c)
{
    return a->first[0] + b->matrix[0][0] + c->values[0].anything + missing_in_body;
}

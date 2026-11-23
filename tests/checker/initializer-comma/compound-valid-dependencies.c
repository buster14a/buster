struct Value { int value; };
struct Pair { int *first; int *second; };

void valid_compound_dependencies(int *pointer)
{
    struct Pair good[2] = { (struct Pair){ pointer, 0 }, { 0, pointer } };
    struct Pair nested = (struct Pair){ ({ (void)0; pointer; }), 0 };
    int value = (int){ 7 };
    int *wrong[3] = { (struct Value){ 1 }, (struct Value){ 2 }, (struct Value){ 3 } };
    int *later = (struct Value){ 4 };
    (void)good;
    (void)nested;
    (void)value;
}

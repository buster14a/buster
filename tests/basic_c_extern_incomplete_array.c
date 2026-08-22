// An extern declaration with an incomplete array type must not stop the
// entity from lowering: a later declaration completes it (spelled bound,
// braced-initializer inference, or string-literal inference), and one with
// no completing declaration in the unit stays an import whose definition
// lives in basic_c_extern_incomplete_array_def.c.

extern char spelled[];
char spelled[5] = {10, 20, 30, 40, 50};

extern int braced[];
int braced[] = {1, 2, 3};

extern char text[];
char text[] = "abcd";

extern const char qualified[];
const char qualified[3] = {7, 8, 9};

char reversed[5] = {1, 2, 3, 4, 5};
extern char reversed[];

extern char external_pad[];

int main(void)
{
    if (spelled[0] != 10 || spelled[4] != 50)
        return 1;
    if (sizeof(spelled) != 5)
        return 2;
    if (braced[0] != 1 || braced[2] != 3)
        return 3;
    if (sizeof(braced) / sizeof(braced[0]) != 3)
        return 4;
    if (text[0] != 'a' || text[3] != 'd')
        return 5;
    if (sizeof(text) != 5)
        return 6;
    if (qualified[0] != 7 || qualified[2] != 9)
        return 7;
    if (reversed[0] != 1 || reversed[4] != 5)
        return 8;
    if (sizeof(reversed) != 5)
        return 9;
    if (external_pad[0] != 60 || external_pad[3] != 63)
        return 10;
    return 0;
}

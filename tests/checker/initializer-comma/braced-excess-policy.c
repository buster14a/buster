int array[1] = { 1, 2 };
char text[2] = "abcd";
struct Pair { int first; } record = { 1, 2 };
union Choice { int first; } choice = { 1, 2 };
int scalar = { 1, 2 };

void local_values(void)
{
    int nested[2][1] = { { 1, 2 }, { 3, 4 } };
    char local_text[2] = "abcd";
    (void)nested;
    (void)local_text;
}

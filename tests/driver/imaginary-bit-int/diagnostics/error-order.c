// A literal's suffix is diagnosed before the conversion or operator that consumes it.
int* real_pointer = 5wb;
#define IMAGINARY 5wbi
int* imaginary_pointer = IMAGINARY
#warning after literal
;
struct T { int member; };
void f(int n)
{
    struct T s = 5uwbi;
    int r = n << 3wbi;
    int* p = &5wbi;
    (void)s;
    (void)r;
    (void)p;
}

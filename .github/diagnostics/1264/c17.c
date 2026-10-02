typedef struct { char c[17]; } C17;
typedef struct { char c[18]; } C18;
typedef struct { char c[20]; } C20;
_Static_assert(sizeof(C17) == 17 && sizeof(C18) == 18 && sizeof(C20) == 20, "MEMORY tail sizes");
#ifdef C17_HOST_CALLER
int c17_probe(int, ...);
int main(void)
{
    C17 v17 = {{0}};
    C18 v18 = {{0}};
    C20 v20 = {{0}};
    for (int i = 0; i < 17; i += 1) { v17.c[i] = (char)(11 + i); }
    for (int i = 0; i < 18; i += 1) { v18.c[i] = (char)(41 + i); }
    for (int i = 0; i < 20; i += 1) { v20.c[i] = (char)(71 + i); }
    return c17_probe(17, v17, v18, v20, 91ll);
}
#else
#include <stdarg.h>
int c17_probe(int marker, ...)
{
    va_list ap;
    va_start(ap, marker);
    C17 v17 = va_arg(ap, C17);
    C18 v18 = va_arg(ap, C18);
    C20 v20 = va_arg(ap, C20);
    long long tail = va_arg(ap, long long);
    va_end(ap);
    int bad = marker != 17 || tail != 91;
    for (int i = 0; i < 17; i += 1) { bad |= v17.c[i] != 11 + i; }
    for (int i = 0; i < 18; i += 1) { bad |= v18.c[i] != 41 + i; }
    for (int i = 0; i < 20; i += 1) { bad |= v20.c[i] != 71 + i; }
    return bad;
}
#endif

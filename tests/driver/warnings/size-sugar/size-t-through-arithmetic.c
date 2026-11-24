typedef struct _IO_FILE FILE;
void f(const char *s, char *d, unsigned long n, int i)
{
    char *e = sizeof(int) + 1;
    char *g = 1 + sizeof(int);
    char *h = n + sizeof(int);
    char *k = sizeof(int) + n;
    char *l = i ? sizeof(int) : 2;
    char *o = -sizeof(int);
    char *v = sizeof(int) << 2;
    char *w = strlen(s) - n;
    char *x = (unsigned long)strlen(s);
    char *y = i + strlen(s);
}

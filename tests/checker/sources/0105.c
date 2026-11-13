#define CHECKED(x) ({ __label__ out; int r = 0; if (x) goto out; r = 1; out: r; })
int f(int a) {
    return CHECKED(a) + CHECKED(a > 1);
}

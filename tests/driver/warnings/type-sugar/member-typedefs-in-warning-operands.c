int printf(const char *, ...);
typedef unsigned long ul;
typedef enum { A1, A2 } EA;
typedef enum { B1 } EB;
typedef double D;
struct s { ul n; EA ea; EB eb; D d; ul arr[2]; struct s *next; };
int f(struct s x, struct s *px, int i)
{
    int r = 0;
    r += x.n < i;
    r += px->next->arr[0] < i;
    r += x.ea == x.eb;
    r += x.ea + px->eb;
    r += (x.ea < px->eb);
    printf("%d %s %f\n", x.n, px->d, x.arr[1]);
    printf("%ld\n", x.d);
    return r + (i ? x.ea : x.eb);
}

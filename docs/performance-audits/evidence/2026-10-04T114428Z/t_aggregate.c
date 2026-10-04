struct Inner { int x; int y; };
struct Outer { struct Inner in; int arr[4]; char name[8]; };
int f(int k)
{
    struct Outer o = {.in = {.y = k, .x = 1}, .arr = {[2] = k, 3}, .name = "ab"};
    int table[3][2] = {{1, 2}, {k}, [2][1] = 5};
    struct Inner* p = &(struct Inner){.x = k, .y = 2};
    return o.in.x + o.arr[2] + o.arr[3] + table[1][0] + table[2][1] + p->y + o.name[1];
}

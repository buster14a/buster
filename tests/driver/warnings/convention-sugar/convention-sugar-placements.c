#define MS __attribute__((ms_abi))
#define PM __attribute__((preserve_most))
#define AS2(x) __attribute__((x))
void (*const c1)(void) MS = 0;
void (* const MS c2)(void) = 0;
void (* MS *pq)(void);
void (*arr[3])(void) PM;
void (*(*fpf)(int))(void) MS;
void (AS2(ms_abi) *m2)(void);
void (__attribute__((sysv_abi)) *sg)(void);
__attribute__((vectorcall)) void (*vc)(void);
[[gnu::ms_abi]] void (*c23)(void);
void (*c23b)(void) [[gnu::ms_abi]];
void h(void)
{
    void (*local)(void) MS;
    int a = local, c = pq, d = *pq, e = arr, f = arr[1], g = &arr, i = fpf, k = m2, l = sg;
    int m = vc, n = c23, o = c23b, r = c1, s = c2;
    local = 1;
    pq = 2;
    *pq = 3;
}

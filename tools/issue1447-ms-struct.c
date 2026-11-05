// Original transient Clang/Buster layout probes; keep off product branches.
extern int printf(const char *, ...);
#define SAMPLE s.a=1;s.b=2
#define OFFSET __builtin_offsetof(S,tail)
#if CASE == 1
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 2
#pragma ms_struct on
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 3
#pragma ms_struct on
typedef struct {unsigned char a:3; unsigned b:5; char tail;} S;
#elif CASE == 4
#pragma ms_struct on
typedef struct {unsigned a:31,b:2; char tail;} S;
#elif CASE == 5
#pragma ms_struct on
typedef struct {unsigned :3,a:3,b:5; char tail;} S;
#elif CASE == 6
#pragma ms_struct on
typedef struct {char head; unsigned :0; char tail;} S;
#undef SAMPLE
#define SAMPLE s.head=3;s.tail=4
#elif CASE == 7
#pragma ms_struct on
typedef struct {unsigned char a:3; unsigned :0; unsigned char b:3; char tail;} S;
#elif CASE == 8
#pragma ms_struct on
typedef struct {unsigned char a:3; unsigned :0; unsigned long long :0; unsigned char b:3; char tail;} S;
#elif CASE == 9
#pragma ms_struct on
typedef struct {unsigned :0;} S;
#undef SAMPLE
#define SAMPLE
#undef OFFSET
#define OFFSET 0
#elif CASE == 10
#pragma ms_struct on
typedef union {unsigned a:3; unsigned b:5; char tail;} S;
#elif CASE == 11
#pragma ms_struct on
typedef union {unsigned :0;} S;
#undef SAMPLE
#define SAMPLE
#undef OFFSET
#define OFFSET 0
#elif CASE == 12
#pragma ms_struct on
typedef struct __attribute__((packed)) {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 13
#pragma ms_struct on
#pragma pack(1)
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 14
#pragma ms_struct on
#pragma pack(2)
typedef struct {char head; unsigned char a:3; unsigned b:5; char tail;} S;
#elif CASE == 15
#pragma ms_struct on
typedef struct {unsigned a:3; unsigned b:5 __attribute__((aligned(16))); char tail;} S;
#elif CASE == 16
#pragma ms_struct on
typedef unsigned __attribute__((aligned(1))) Small;
typedef struct {char head; Small a:3,b:5; char tail;} S;
#elif CASE == 17
#pragma ms_struct on
struct Forward;
#pragma ms_struct off
struct Forward {char head; unsigned a:3,b:5; char tail;};
typedef struct Forward S;
#elif CASE == 18
struct Forward;
#pragma ms_struct on
struct Forward {char head; unsigned a:3,b:5; char tail;};
typedef struct Forward S;
#elif CASE == 19
typedef struct __attribute__((ms_struct)) {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 20
#pragma ms_struct on
typedef struct __attribute__((gcc_struct)) {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 21
#pragma ms_struct on
enum __attribute__((packed)) E {ZERO, ONE, TWO};
typedef struct {enum E a:2,b:2; char tail;} S;
#elif CASE == 22
#pragma ms_struct on
typedef struct {char head; unsigned __int128 a:70,b:30; char tail;} S;
#elif CASE == 23
#pragma ms_struct on
typedef struct {char head; unsigned _BitInt(65) a:40,b:20; char tail;} S;
#elif CASE == 24
#pragma ms_struct on
typedef struct {unsigned a:3,b:5; char tail; int flexible[];} S;
#elif CASE == 25
#pragma ms_struct on
typedef struct {} S;
#undef SAMPLE
#define SAMPLE
#undef OFFSET
#define OFFSET 0
#elif CASE == 26
void earlier(void) {
#pragma ms_struct on
}
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 27
typedef struct Outer {
    unsigned a:3;
#pragma ms_struct on
    unsigned b:5;
    struct {unsigned a:3,b:5; char tail;} nested;
    char tail;
} S;
#elif CASE == 28
#pragma ms_struct on
typedef struct Outer {
    unsigned a:3;
#pragma ms_struct off
    unsigned b:5;
    struct {unsigned a:3,b:5; char tail;} nested;
    char tail;
} S;
#elif CASE == 29
#pragma ms_struct on
#pragma ms_struct nonsense
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 30
#pragma ms_struct on extra
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 31
#define MODE on
#pragma ms_struct MODE
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 32
#pragma ms_struct on
#pragma ms_struct reset
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#elif CASE == 33
#pragma ms_struct on
typedef struct __attribute__((packed)) {unsigned char a:3; unsigned :0; unsigned char b:3; char tail;} S;
#elif CASE == 34
#pragma ms_struct on
#pragma pack(1)
typedef struct {unsigned char a:3; unsigned :0; unsigned char b:3; char tail;} S;
#elif CASE == 35
#pragma ms_struct on
typedef int Small __attribute__((aligned(1)));
typedef struct {char head; Small a,b; char tail;} S;
#elif CASE == 36
#pragma ms_struct on
typedef int Small __attribute__((aligned(1)));
typedef struct {char head; Small a[3]; unsigned b:2; char tail;} S;
#undef SAMPLE
#define SAMPLE s.a[0]=1;s.b=2
#elif CASE == 37
#pragma ms_struct on
typedef struct {char head; _Complex double a; unsigned b:2; char tail;} S;
#elif CASE == 38
#pragma ms_struct on
typedef union {unsigned a:3 __attribute__((aligned(16))); unsigned b:5; char tail;} S;
#elif CASE == 39
#pragma ms_struct on
typedef struct {unsigned a:3; unsigned :0 __attribute__((aligned(16))); unsigned b:3; char tail;} S;
#elif CASE == 40
#pragma ms_struct on
typedef struct {unsigned a:3; unsigned long long b:5; char tail;} S;
#elif CASE == 41
typedef struct __attribute__((ms_struct)) Forward S;
struct Forward {char head; unsigned a:3,b:5; char tail;};
#elif CASE == 42
#pragma ms_struct on
typedef struct {unsigned char a:3,b:5; char tail;} S;
#elif CASE == 43
#pragma ms_struct on
typedef struct {unsigned a:3,b:5; char tail; _Complex int complex;} S;
#elif CASE == 44
#pragma ms_struct on
#pragma ms_struct
typedef struct {char head; unsigned a:3,b:5; char tail;} S;
#else
#error unknown case
#endif
int main(void)
{
    S s;
    __builtin_memset(&s,0,sizeof(s));
    SAMPLE;
    printf("layout %lu %lu %lu :",(unsigned long)sizeof(S),(unsigned long)_Alignof(S),(unsigned long)OFFSET);
    for (unsigned long i=0;i<sizeof(S);i++)
        printf(" %02x",((unsigned char *)&s)[i]);
    printf("\n");
    return 0;
}

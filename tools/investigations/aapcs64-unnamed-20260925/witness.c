/* AAPCS64 10.1.8 unnamed-container alignment witness.
 * C17 only: int/unsigned int bit-fields, literal widths, and named char offsets.
 * Expected tuples are hand-derived from the target contract, not subject output.
 * Compile SUBJECT and OBSERVER separately to prevent shared frontend facts.
 */
#define CASES(X) \
    X(0, plain, 2,1,1, 2,1,1) \
    X(1, named_one, 4,4,2, 4,4,2) \
    X(2, zero_middle, 8,4,4, 5,1,4) \
    X(3, zero_head, 4,4,1, 2,1,1) \
    X(4, zero_tail, 4,4,1, 4,1,1) \
    X(5, width_one, 4,4,2, 3,1,2) \
    X(6, width_seven, 4,4,2, 3,1,2) \
    X(7, width_eight, 4,4,2, 3,1,2) \
    X(8, width_sixteen, 4,4,3, 4,1,3) \
    X(9, width_twentyfour, 8,4,4, 5,1,4) \
    X(10, width_twentyfive, 12,4,8, 9,1,8) \
    X(11, width_thirtyone, 12,4,8, 9,1,8) \
    X(12, width_thirtytwo, 12,4,8, 9,1,8) \
    X(13, consecutive_zero, 8,4,4, 5,1,4) \
    X(14, union_zero, 4,4,0, 1,1,0) \
    X(15, union_one, 4,4,0, 1,1,0) \
    X(16, nested_zero, 16,4,12, 7,1,6) \
    X(17, array_zero, 20,4,16, 11,1,10) \
    X(18, aligned_control, 8,4,4, 8,4,4) \
    X(19, named_full, 12,4,8, 12,4,8) \
    X(20, ordinary_control, 12,4,8, 12,4,8)

#if defined(SUBJECT)
typedef struct { char a; char b; } plain;
typedef struct { char a; unsigned int f:1; char b; } named_one;
typedef struct { char a; unsigned int:0; char b; } zero_middle;
typedef struct { unsigned int:0; char a; char b; } zero_head;
typedef struct { char a; char b; unsigned int:0; } zero_tail;
typedef struct { char a; unsigned int:1; char b; } width_one;
typedef struct { char a; unsigned int:7; char b; } width_seven;
typedef struct { char a; unsigned int:8; char b; } width_eight;
typedef struct { char a; unsigned int:16; char b; } width_sixteen;
typedef struct { char a; unsigned int:24; char b; } width_twentyfour;
typedef struct { char a; unsigned int:25; char b; } width_twentyfive;
typedef struct { char a; unsigned int:31; char b; } width_thirtyone;
typedef struct { char a; unsigned int:32; char b; } width_thirtytwo;
typedef struct { char a; unsigned int:0; unsigned int:0; char b; } consecutive_zero;
typedef union { char b; unsigned int:0; } union_zero;
typedef union { char b; unsigned int:1; } union_one;
typedef struct { char a; zero_middle member; char b; } nested_zero;
typedef struct { zero_middle members[2]; char b; } array_zero;
typedef struct { _Alignas(4) char a; unsigned int:0; char b; } aligned_control;
typedef struct { char a; unsigned int f:32; char b; } named_full;
typedef struct { char a; unsigned int f; char b; } ordinary_control;
#define ENUM_ROW(i,t,as,aa,ao,xs,xa,xo) enum { t##_size = sizeof(t), t##_align = _Alignof(t), t##_offset = __builtin_offsetof(t,b) };
CASES(ENUM_ROW)
#undef ENUM_ROW
/* These real objects let ELF/LLVM independently expose canonical array stride. */
#define OBJECT_ROW(i,t,as,aa,ao,xs,xa,xo) t witness_object_##t[2];
CASES(OBJECT_ROW)
#undef OBJECT_ROW
unsigned witness_value(unsigned row, unsigned column)
{
    unsigned result = 0;
    switch (row)
    {
#define VALUE_ROW(i,t,as,aa,ao,xs,xa,xo) \
        case i: \
            switch (column) \
            { \
                case 0: result = t##_size; break; \
                case 1: result = t##_align; break; \
                case 2: result = t##_offset; break; \
                case 3: result = sizeof(t); break; \
                case 4: result = _Alignof(t); break; \
                case 5: result = __builtin_offsetof(t,b); break; \
                case 6: result = sizeof(t[2]); break; \
                default: result = 0; break; \
            } \
            break;
CASES(VALUE_ROW)
#undef VALUE_ROW
        default: result = 0; break;
    }
    return result;
}
#else
#include <stdio.h>
#include <limits.h>
extern unsigned witness_value(unsigned row, unsigned column);
_Static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4, "target precondition");
#if defined(__aarch64__)
#define EXPECTED_ROW(i,t,as,aa,ao,xs,xa,xo) {as,aa,ao,as,aa,ao,2*as},
#elif defined(__x86_64__)
#define EXPECTED_ROW(i,t,as,aa,ao,xs,xa,xo) {xs,xa,xo,xs,xa,xo,2*xs},
#else
#error unsupported witness target
#endif
static unsigned const expected[][7] = { CASES(EXPECTED_ROW) };
#undef EXPECTED_ROW
#define NAME_ROW(i,t,as,aa,ao,xs,xa,xo) #t,
static char const* const names[] = { CASES(NAME_ROW) };
#undef NAME_ROW
int main(void)
{
    unsigned mismatches = 0;
    unsigned internal_disagreements = 0;
    for (unsigned row = 0; row < sizeof(expected)/sizeof(expected[0]); row += 1)
    {
        unsigned actual[7];
        printf("%s", names[row]);
        for (unsigned col = 0; col < 7; col += 1)
        {
            actual[col] = witness_value(row, col);
            printf(" %u/%u", actual[col], expected[row][col]);
            mismatches += actual[col] != expected[row][col];
        }
        internal_disagreements += actual[0] != actual[3];
        internal_disagreements += actual[1] != actual[4];
        internal_disagreements += actual[2] != actual[5];
        internal_disagreements += actual[6] != 2*actual[3];
        putchar('\n');
    }
    printf("SUMMARY mismatches=%u internal_disagreements=%u cells=147\n", mismatches, internal_disagreements);
    return mismatches != 0;
}
#endif

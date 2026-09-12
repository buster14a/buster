#define A long
#define LONG_IDENTIFIER x
#define INNER signed
#define OUTER INNER
#define S /
#define D .
#define P L
#define E 1e
#define N 123

bool flag;
bool  spaced_flag;
A   macro_name;
LONG_IDENTIFIER shorter_name;
OUTER nested_name;
P"text";
E +2;
N  number_name;
D.. ellipsis_tokens;
S/ line_comment_tokens;
S* block_comment_tokens;

#include "basic_c_preprocess_boundaries.h"
#line 200 "logical-boundaries.c"
int main(void)
{
    return (1 + 2);
}

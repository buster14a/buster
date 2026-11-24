#pragma clang fp exceptions(strict) contract(off)
int clear_fp(void);int fp_state(void);
int tested(int n){clear_fp();int a=0;{
#pragma clang fp exceptions(ignore)
1.0/3.0;a=fp_state();}clear_fp();1.0/3.0;return a*100+fp_state()+n;}
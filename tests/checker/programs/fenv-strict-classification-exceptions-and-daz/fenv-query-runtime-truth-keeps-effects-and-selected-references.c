#pragma STDC FENV_ACCESS ON
int absent(void);int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();int a=__builtin_constant_p(1.0/3.0)?absent():n;int f=fp_state();return a+f;}
#pragma STDC FENV_ACCESS ON
#pragma clang fp eval_method(double)
int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);volatile float a=1.0f;clear_fp();double b=a+0x1p-24f;int wide=fp_state();clear_fp();float stored=b;int narrow=fp_state();change_rounding(0);return (b>1.0)*4096+(stored>1.0f)*256+wide*32+narrow;}
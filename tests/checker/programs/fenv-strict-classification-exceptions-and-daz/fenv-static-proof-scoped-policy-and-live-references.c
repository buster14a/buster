#pragma STDC FENV_ACCESS ON
int change_rounding(int);int clear_fp(void);int fp_state(void);int external_helper(int);extern int external_data;int tested(int n){static double fixed=1.0+0x1p-53;change_rounding(n);clear_fp();double a=1.0+0x1p-53;int f=fp_state();double off;change_rounding(0);{
#pragma STDC FENV_ACCESS OFF
 off=1.0+0x1p-53;}change_rounding(n);double b=1.0+0x1p-53;int selected=(1.0+0x1p-53>1.0)?external_helper(n):external_data;change_rounding(0);return (a>fixed)*1000+(b>off)*100+f+selected;}
int clear_fp(void);int fp_state(void);
#pragma float_control(except, on, push)
int strict_part(void){clear_fp();1.0/3.0;return fp_state();}
#pragma float_control(pop)
int ordinary_part(void){clear_fp();1.0/3.0;return fp_state();}
int tested(int n){return strict_part()*100+ordinary_part()+n;}
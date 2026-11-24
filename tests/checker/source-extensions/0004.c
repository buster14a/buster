typedef float T __attribute__((available_only_in_default_eval_method));typedef float float_t;
#pragma clang fp eval_method(double)
T value;float_t ordinary;int f(void){T x=1;return sizeof(T)+sizeof((T)1)+sizeof(__typeof__(x+x))+sizeof(ordinary);}

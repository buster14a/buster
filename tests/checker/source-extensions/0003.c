#define TYPE(e,t) _Static_assert(_Generic((e),t:1,default:0),"scope type")
#define METHOD double
#pragma clang fp eval_method(METHOD)
float x;TYPE(+x,double);int f(void){
#pragma clang fp eval_method(extended)
TYPE(+x,long double);{
#pragma clang fp eval_method(source)
TYPE(+x,float);}TYPE(+x,long double);return 0;}TYPE(+x,double);
#pragma float_control(push)
#pragma clang fp eval_method(extended)
TYPE(+x,long double);
#pragma float_control(pop)
TYPE(+x,double);

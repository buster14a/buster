typedef __builtin_va_list V; int f(int n,...){V x,y;__builtin_va_start(x,n);__builtin_va_copy(y,x);int r=__builtin_va_arg(y,int);__builtin_va_end(y);__builtin_va_end(x);return r;}

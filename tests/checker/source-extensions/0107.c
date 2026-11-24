extern _Thread_local int x; _Thread_local int x=3; int f(void){static _Thread_local int y; return x+y;}

/* Original conversion-order and inactive-operand probes for issue #1447. */
#if CASE == 0
void f(short,short); short g(short); void h(void){f(g(65536),65537);}
#elif CASE == 1
void f(int,int); int g(int); void h(void){f(2.5,g(1.5));}
#elif CASE == 2
void f(int,int); int g(int); void h(void){f(g(1.5),2.5);}
#elif CASE == 3
int f(void){int a=0?1.5:2.5;return a;}
#elif CASE == 4
void f(int);void h(void){if(0)f(1.5);f(2.5);}
#elif CASE == 5
int f(void){if(0)return 1.5;if(0){int a=2.5;}return 0;}
#elif CASE == 6
void f(int);int h(void){return 0&& (f(1.5),1);}
#elif CASE == 7
void f(int);int h(void){return 1?0:(f(1.5),1);}
#elif CASE == 8
int f(void){return sizeof((int){1.5});}
#elif CASE == 9
int f(void){return _Generic(0,int:0,default:(int){1.5});}
#elif CASE == 10
void f(short);void h(void){if(0)f(65536);f(65537);}
#elif CASE == 11
short f(void){if(0)return 65536;if(0){short a=65537;}return 0;}
#elif CASE == 12
void f(short);int h(void){return 0&& (f(65536),1);}
#elif CASE == 13
void f(short);int h(void){return 1?0:(f(65536),1);}
#elif CASE == 14
int f(void){return sizeof((short){65536});}
#elif CASE == 15
int f(void){return _Generic(0,int:0,default:(short){65536});}
#elif CASE == 16
int f(void){int a=1?1.5:2.5;return a;}
#elif CASE == 17
short f(void){short a=0?65536:65537;return a;}
#elif CASE == 18
int f(int);int h(void){return f(1.5)+f(2.5);}
#elif CASE == 19
int h(void){int a,b;a=1.5,b=2.5;return a+b;}
#elif CASE == 20
int f(int);void h(void){int a[1];(f(1.5)+a[2]),(f(2.5)+a[3]);}
#elif CASE == 21
void f(int);void h(void){int a[1];(a[2],f(1.5)),(a[3],f(2.5));}
#elif CASE == 22
short f(short);void h(void){f(65536),f(65537);}
#elif CASE == 23
int h(void){return 0;short a=65536;int b=1.5;(void)a;(void)b;}
#else
#error Select one of the conversion-order cases.
#endif

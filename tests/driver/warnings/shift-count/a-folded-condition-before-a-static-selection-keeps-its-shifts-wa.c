int f1(int x){if((1<<40)!=0)return _Generic(x,int:0,long:0);return 0;}
int f2(int x){if((1<<41)!=0)return __builtin_choose_expr(1,0,0);return 0;}
int f3(int x){if((1<<42)!=0){}_Generic(x,int:0,long:0);return 0;}
int f4(int x){switch(1<<43){default:return _Generic(x,int:0,long:0);}return 0;}
int f5(int x){if((-1<<2)<0)return _Generic(x,int:0,long:0);return 0;}
int f6(int x){return x;if((1<<44)!=0)return _Generic(x,int:0,long:0);return 0;}

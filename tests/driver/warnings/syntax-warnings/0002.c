int a(int x){if(x==1)return 1;return 0;}
int b(int x){if((1==x))return 1;return 0;}
int c(int x){if(((int)x==1))return 1;return 0;}
int d(const int x){if((x==1))return 1;return 0;}
int e(int x){if((x!=1))return 1;return 0;}
int f(int x){if((x<1))return 1;return 0;}
int g(int x){return (x==1);}
int h(int x){return (x==1)?2:3;}

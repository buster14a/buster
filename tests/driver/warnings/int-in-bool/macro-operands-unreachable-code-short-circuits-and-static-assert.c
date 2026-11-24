#define SH(x) ((x)<<1)
#define CO(y) ((y)?2:3)
int f(int x,int y){if(SH(x))return 1;if(CO(y))return 2;return 0;if(x<<3)return 3;y=0&&(x<<4);y=1||(y?6:7);_Static_assert(!(1<<3)||1,"");return y;}

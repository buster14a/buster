int f(int x){
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshift-negative-value"
x=-1<<2;
#pragma clang diagnostic pop
#pragma clang diagnostic error "-Wshift-count-overflow"
x=1<<40;x=-2<<1;x=0x7fffffff<<4;if(x)return 0;return x;x=1<<41;}

int a(int x){return x;x=1<<32;x=-1<<1;x=3<<31;x=-1<<2;return x;}
int b(int x){if(x)return 1;else return 2;x=1<<33;}
int c(int x){if(0){x=1<<40;x=-1<<2;x=0x7fffffff<<3;}while(0){x=1<<42;}for(;;){}x=-1<<4;}
void d(int x){goto e;x=-1<<9;e:x=-1<<10;while(1){}x=-1<<11;}

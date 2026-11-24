_Bool b;void take(_Bool);_Bool r(int x){return x<<1;}
int f(int x,int y){if(x<<2)return 1;while(x<<3)x--;for(;x<<4;)x--;do x--;while(x<<5);y=!(x<<6);y=(x<<7)&&y;b=x<<8;take(x<<9);b=(y,x<<10);b=y?(x<<11):0;return y;}

int f(int x){x<<32;(void)_Generic(x,int:0,default:({x<<32;}));x>>32;return 0;}

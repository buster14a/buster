struct T{int x;};
int tested(int n){int k=n>0?n:1;return __is_destructible(int)+__is_destructible(void)*2+__is_destructible(int[])*4+__is_destructible(int[k])*8+
__is_nothrow_destructible(struct T)*16+__is_bitwise_cloneable(int[])*32+__is_bitwise_cloneable(void(void))*64+__is_bitwise_cloneable(void)*128+
__is_bitwise_cloneable(int[k][2])*256+n*1000;}

int abs(int);long labs(long);double fabs(double);float fabsf(float);
int f(double d,long l,unsigned u){int x=abs(d);x+=abs(l);x+=abs(u);x+=fabs(1);x+=fabsf(d);x+=__builtin_abs(d);return x;}
int g(double d){if(0)return abs(d);return sizeof(abs(d));}

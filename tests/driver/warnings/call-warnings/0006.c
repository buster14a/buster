typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);void*memset(void*,int,size_t);
void f(int*p,int*q,char*s){memset(p,0,sizeof(p));memcpy(p,q,sizeof(p));memcpy(p,q,sizeof(q));memset(p,0,sizeof(*p));memset(s,0,sizeof(s));memset((void*)p,0,sizeof(p));if(0)memset(p,0,sizeof(p));(void)sizeof(memset(p,0,sizeof(p)));}

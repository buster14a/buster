int f(const char*,...) __attribute__((nothrow,leaf,format(printf,1,2),nonnull(1))); int g(void){return f("%d",3);}

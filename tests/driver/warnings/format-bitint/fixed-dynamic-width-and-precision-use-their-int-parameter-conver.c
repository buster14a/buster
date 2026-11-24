void log_value(const char*,int,int,const char*) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value("%*.*s",x,x,"x");}

void log_value(const char*,_BitInt(32)*) __attribute__((format(printf,1,2)));void f(void*x){log_value("%n",x);}

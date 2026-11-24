static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}
int tested(int n){h=(unsigned)n;
#line 20 "/"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 30 "/foo/"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 40 "a/"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 50 "//"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 60 "//net"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 70 ""
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 80 "../"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);
#line 90 "/dir/name.c"
mixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);mixs(__builtin_FILE());
return (int)(h^(h>>32));}

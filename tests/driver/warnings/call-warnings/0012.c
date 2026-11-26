typedef __SIZE_TYPE__ size_t;void*memcpy(void*,const void*,size_t);void f(void){char b[4];memcpy(b,"123456789",9);__builtin_memcpy(b,"123456789",9);}

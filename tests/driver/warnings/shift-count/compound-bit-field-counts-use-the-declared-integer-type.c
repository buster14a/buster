struct S{unsigned long n:7;};void f(struct S*x){x->n<<=7;x->n>>=64;}

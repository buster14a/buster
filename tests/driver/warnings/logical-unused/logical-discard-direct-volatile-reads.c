struct S{volatile int value;};void f(int a,volatile int*p,struct S*s){a&&*p;*p||a;a&&s->value;}

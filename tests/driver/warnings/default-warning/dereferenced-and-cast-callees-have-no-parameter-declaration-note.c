void f(int *parameter);void g(const int *p){(*f)(p);((void(*)(int *))f)(p);}

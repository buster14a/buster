typedef void (*F)(int *parameter);F f;void g(const int *p){f(p);}
void h(void (*callback)(int *parameter),const int *p){callback(p);}

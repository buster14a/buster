struct S { int a; } s;
void g(int x){ long l = (_Atomic long)(_Atomic int)x; (void)(_Atomic int)s; (void)(_Atomic(int *))s;
(void)(int)(_Atomic int)x; (void)l; }

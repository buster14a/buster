typedef float F; typedef typeof(nullptr) N; typedef N M; typedef int I; typedef int *IP; enum E { A };
void f(N n, const N cn, M m, I i, IP p, const int ci, int a[2]) {
(void)(float)nullptr; (void)(_Complex float)nullptr; (void)(F)n; (void)(enum E)m; (void)(long double)cn; (void)(_BitInt(8))nullptr;
(void)(_Atomic float)n; (void)(N)1; (void)(N)i; (void)(N)p; (void)(M)ci; (void)(N)a; (void)(N)(int*)0; (void)(N)1.5; (void)(const N)p;
(void)(I)n; (void)(N)"x";
(void)(_Bool)nullptr; (void)(void*)n; (void)(int*)m; (void)(void)n; (void)(N)n; (void)(N)0; (void)(N)(void *)0; }

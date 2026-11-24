typedef typeof(nullptr) N; enum E { A };
N g = (N)0; N h = (N)(void *)0; static N k = (N)false; N u; void *v = (N)0; struct P { int a; N n; } ps = { 1, (N)0 };
int f(void) { N a = (N)0; N b = (N)(void*)0; N c = (N)(1 - 1); N d = (N)A; N e = (N)'\0'; N q = (N)(char)0; N r = (N)((void*)0);
return (a == nullptr) + (b == nullptr) + (c == nullptr) + (d == nullptr) + (e == nullptr) + (q == nullptr) + (r == nullptr) + (g == nullptr) + (h == nullptr) + (k == nullptr); }

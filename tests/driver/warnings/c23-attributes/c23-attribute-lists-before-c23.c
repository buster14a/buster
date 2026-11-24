struct [[deprecated]] S { int a [[maybe_unused]]; [[maybe_unused]] int b; };
[[nodiscard]] [[maybe_unused]] int f(int x [[maybe_unused]]);
[[]] int g(int x) { switch (x) { case 1: x++; [[fallthrough]]; case 2: break; } return x; }
typedef int T [[deprecated]];
enum [[deprecated]] E { A [[deprecated]] };
int h(void) { [[]]; [[gnu::unused]] int y = 0; return y; }
void k(int *[[gnu::unused]] p);

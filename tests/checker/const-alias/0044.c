struct P{_Alignas(16) int x;int a[];};
const struct P p={1};
enum{E=p.a[0]};

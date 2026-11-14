struct P{_Alignas(16) int x;int a[];};
const struct P p={1,{2,3}};
enum{E=p.a[0]};

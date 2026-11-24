__attribute__((pure)) int p(int);
__attribute__((const)) int c(int);
__attribute__((warn_unused_result)) int w(int);
struct [[nodiscard]] R { int x; };
struct R r(int);
#define ID(x) x
#define STMT(x) do { x; } while (0)
#define CALL(f) f(1)
#define TWO(x) ID(x)
#define ARG(x) p(x)
void g(int n) {
    ID(p(n)); ID(c(n)); ID(w(n)); ID(r(n));
    STMT(p(n)); STMT(w(n));
    CALL(p); CALL(w);
    TWO(p(n)); TWO(w(n));
    ARG(n);
    ID((p)(n));
    ID(n ? p(n) : c(n));
}

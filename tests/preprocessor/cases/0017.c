#define log_all(...) logf(level, __VA_ARGS__)
#define names(...) show(#__VA_ARGS__)
#define check(c, ...) ((c) ? show(#c) : \
 emit(__VA_ARGS__))
log_all("start");
log_all("n = %d\n", n);
names(red, green and blue);
check(a < b, "a is %d, b is %d", a, b);

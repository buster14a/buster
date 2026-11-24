int * __attribute__((objc_gc(weak))) w;
int * __attribute__((objc_gc(strong))) s;
__attribute__((objc_gc(weak))) int *moved;
int * __attribute__((objc_gc(weak))) *inner;
int * __attribute__((objc_gc(weak))) array[2];
struct M { int * __attribute__((objc_gc(weak))) m; };
int **plain;
void f(void) {
  int * __attribute__((objc_gc(weak))) local;
  int * __attribute__((objc_gc(strong))) kept;
  plain = &w; inner = plain;
}

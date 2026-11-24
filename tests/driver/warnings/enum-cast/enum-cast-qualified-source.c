typedef const struct { int x; } CS, CS2;
typedef volatile enum { WA } WAnon;
typedef enum { XA } __attribute__((aligned(8))) XAnon;
enum G { GA };
void h(CS s, CS2 t){ int *p = s; int *q = t; (void)p; (void)q; }
void k(WAnon w, XAnon x){ enum G g = w; enum G g2 = x; enum G g3 = WA; enum G g4 = XA; int *p = w; (void)g; (void)g2; (void)g3; (void)g4; (void)p; }

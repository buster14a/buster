typedef enum { VA } VAnon, VAlias;
typedef VAnon VAlias2;
typedef const enum { WA } WAnon, WAlias;
typedef struct { int x; } SAnon, SAlias;
enum G { GA };
void f(enum G g) { VAlias a = g; VAlias2 b = g; VAnon c = g; WAlias w = g; (void)a; (void)b; (void)c; (void)w; }
void h(SAlias s) { int *p = s; (void)p; }
void k(void) { VAlias2 v = GA; enum G g = v; enum G g2 = VA; (void)g; (void)g2; }

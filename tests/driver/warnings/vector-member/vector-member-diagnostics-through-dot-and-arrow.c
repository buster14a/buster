typedef float v4 __attribute__((ext_vector_type(4)));
typedef float g4 __attribute__((vector_size(16)));
float a(g4 *p) { return (*p).y; }
float b(v4 *p) { return (*p).q; }
void c(v4 *p) { (*p).xx = (*p).zw; }
float *d(v4 *p) { return &(*p).x; }
float e(g4 *p) { return p->y; }
float f(v4 *p) { return p->q; }
void g(v4 *p) { p->xx = p->zw; }
float *h(v4 *p) { return &p->x; }

typedef void *VP;
void f(void *p, VP v, const void *c, void *const k) {
  (void)*p; (void)*v; (void)*c; (void)*k; void *q = &*p; q = &(*p); q = & *p;
  (void)sizeof(*p); (void)_Alignof(*p); (void)__alignof__ *p; (void)(1 ? *p : *p);
  if (0) (void)*p; (void)**(void **)p; (void)q;
}

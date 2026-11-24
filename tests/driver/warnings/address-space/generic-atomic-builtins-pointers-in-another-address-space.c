void a(int __seg_gs *p, int *v, int *o) { __atomic_exchange(p, v, o, 5); }
void b(int __seg_gs *p, int *r) { __atomic_load(p, r, 5); }
void c(int __seg_gs *p, int *v) { __atomic_store(p, v, 5); }
_Bool d(int __seg_gs *p, int *e, int *w) { return __atomic_compare_exchange(p, e, w, 0, 5, 5); }
void a2(int __seg_gs *p, int __seg_gs *v, int *o) { __atomic_exchange(p, v, o, 5); }
void a3(int *p, int __seg_gs *v, int *o) { __atomic_exchange(p, v, o, 5); }
void b3(int *p, int __seg_gs *r) { __atomic_load(p, r, 5); }
void c3(int *p, int __seg_gs *v) { __atomic_store(p, v, 5); }
_Bool d3(int *p, int __seg_gs *e, int __seg_gs *w) { return __atomic_compare_exchange(p, e, w, 0, 5, 5); }

typedef ELEMENT V __attribute__((ATTRIBUTE));
V preinc(V *p) { return ++*p; }
V predec(V *p) { return --*p; }
V postinc(V *p) { return (*p)++; }
V postdec(V *p) { return (*p)--; }

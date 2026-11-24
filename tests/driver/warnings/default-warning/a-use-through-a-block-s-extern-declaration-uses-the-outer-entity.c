static void sf(void);
void o(void) { extern void sf(void); sf(); }
static void sf(void) {}
static int t;
void p(void) { { extern int t; } }
static int u;
void q(void) { { extern int u; { extern int u; (void)u; } } }

// A name that names nothing (#2376): the name highlighted, and where a macro made it, the invocation it maps to and,
// in each "expanded from" note, the name in the macro's body.
int direct(void) { return undeclared_a; }
#define BODY(x) x + undeclared_b
int body(void) { return BODY(1); }
#define ID(x) x
int argument(void) { return ID(  undeclared_c ); }
#define OBJECT undeclared_d
int object(void) { return OBJECT; }
int nested(void) { return BODY(BODY(2)); }
#define TWICE(x) ID(x) + ID(x)
int twice(void) { return TWICE(undeclared_e); }
#define WRAP(x) (ID(x))
int wrapped(void) { return WRAP(undeclared_f); }
#define LONG_NAME_MACRO(x) x
int split(void) { return LONG_NAME_MACRO(
    undeclared_g); }
	int tabbed(void) { return ID(	undeclared_h); }
int wide(void) { return /* é中 */ undeclared_i; }

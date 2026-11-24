struct s {};
typedef struct {} T;
  union u {};
struct b { int : 3; };
struct n { struct {}; };
void f(void) { struct l {} x; (void)x; }
struct o { struct m {} q; };
struct c { int : 0; };
struct d { int a : 2; };

enum P : unsigned const long volatile x;
void f(void) { enum S : volatile char s; s = 1; (void)s; }
enum Q : short q1, q2;
enum Q { QA = 1 };
_Static_assert(sizeof(q1) == 2, "");
enum E : int;
void g(void) { enum E : short y; (void)y; }
enum F { FA };
enum F : int z;
typedef enum T : char TT;
_Static_assert(sizeof(TT) == 1, "");

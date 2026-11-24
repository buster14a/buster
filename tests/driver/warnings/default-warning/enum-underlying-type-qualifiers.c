enum H : const int { D };
enum I : const volatile const int { E };
enum J : int const { F };
enum K : volatile unsigned long { G };
typedef const int CI;
enum L : CI { H1 };
enum M : const CI { M1 };
enum N : const int;
const enum O : const int { O1 };
enum Q : const int { Q1 } q;
void f(void) { enum R : const short { R1 }; enum R r = R1; (void)r; }
struct T { enum U : const int { U1 } u; };

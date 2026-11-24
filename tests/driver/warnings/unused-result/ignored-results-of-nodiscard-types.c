struct [[nodiscard]] S { int a; };
struct [[nodiscard("keep it")]] M { int a; };
union __attribute__((warn_unused_result)) U { int a; };
enum [[nodiscard]] E { A };
typedef int __attribute__((warn_unused_result)) WI;
typedef WI WJ;
typedef struct S TS;
struct S mk(void);
struct M mm(void);
union U mu(void);
enum E me(void);
WI mwi(void);
WJ mwj(void);
TS mts(void);
struct S *ps(void);
[[nodiscard]] struct S both(void);
void f(void) {
  mk(); mm(); mu(); me(); mwi(); mwj(); mts(); ps(); both(); (void)mk();
}

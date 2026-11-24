union U{int x;float f;};const union U u={.x=7};
enum { E=u.x };
_Static_assert(E==7,"active union value");

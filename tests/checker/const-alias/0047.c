union U{int x,y;};const union U u={.x=1};const int k=3;
const int* const p=(&u.y,&k);
enum{E=*p};
_Static_assert(E==3,"discarded address");

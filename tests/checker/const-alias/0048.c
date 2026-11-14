union U{int x,y;};const union U u={.x=1};const int a[2]={3,4};
enum{E=a[(&u.y,1)]};
_Static_assert(E==4,"discarded index address");

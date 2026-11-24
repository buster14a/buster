union U{int x,y;};const union U u={.x=3};const int* const p=&u.y;
const int* q=p;

struct I{int x,y;};struct O{struct I a,b;};const struct O p={{1},{2}};const struct I q=p.b;_Static_assert(q.x==2 && q.y==0,"copy");

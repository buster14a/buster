enum{E=-1<<2};_Static_assert((-1<<1)==-2,"");int f(int x){switch(x){case -1<<3:return 1;}int y=sizeof(-1<<4);y+=x?(-1<<5):0;y+=1?0:(-1<<6);return y;}

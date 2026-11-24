struct s { int overloadable; };
int __overloadable__;
int get(struct s* p){int overloadable = p->overloadable; return overloadable + __overloadable__;}
int run(void){struct s v = {3}; return get(&v);}

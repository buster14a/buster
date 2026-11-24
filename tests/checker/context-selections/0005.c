union U { int i; unsigned u; }; static const union U v={.i=7}; static const unsigned *p=&v.u;

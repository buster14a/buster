struct P{int x;};
const struct P p={};
const int* const q=(const int*)&p;
const int* r=q;

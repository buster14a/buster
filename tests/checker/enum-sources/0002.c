enum { P=4, N=P+__builtin_offsetof(struct { char p[P]; int y; },y) }; _Static_assert(N==8,"ordered enum record uses");

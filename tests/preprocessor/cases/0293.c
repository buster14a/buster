#define G(...) __VA_ARGS__ ## __VA_ARGS__
G(,)
#define D(c, e...) e ## e
D( , , )

#define c(d, e...) e ## __VA_OPT__( d ## e)
c(,,)
#define h(d, g, ...) , ## __VA_OPT__(d ## g ## __VA_ARGS__)
h(,,y)

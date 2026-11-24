#define vs(...) #__VA_ARGS__
#define vt(...) vs(__VA_ARGS__)
#define vm(p, q, ...) __VA_OPT__(,) ## __VA_OPT__( q ## __VA_ARGS__ )
vt(vm( ,,u ))
vt(vm( ,, u ))

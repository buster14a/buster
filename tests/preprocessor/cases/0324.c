#define Z() a ## __VA_OPT__(b) # __VA_OPT__(c)
Z()
#define Y() __VA_OPT__ ( )
Y()

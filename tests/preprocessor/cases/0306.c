#define F(__VA_ARGS__) [__VA_ARGS__]
F(1)
#define G(__VA_ARGS__, ...) [__VA_ARGS__]
G(1, 2)

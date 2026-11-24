#define HI __has_include
#if HI(<R"bad"x>)
bad
#endif
after

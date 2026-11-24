#define L changed
#define S_(...) #__VA_ARGS__
#define S(...) S_(__VA_ARGS__)
S($L\
"y") $L\
"z"

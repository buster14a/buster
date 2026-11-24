#define SYSMAC user_token
#define SYSFN(x) [x]
#define CALLS USERMAC
#define CAT(a, b) a ## b
#define STR(x) #x
int header = SYSMAC + USERMAC + CALLS + CAT(1, 2);

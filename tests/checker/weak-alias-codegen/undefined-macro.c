#define ALIAS_PRAGMA _Pragma("weak alias = target")
int target(void);
ALIAS_PRAGMA

enum { N=sizeof(struct { char x; int y; }) }; _Static_assert(N==8,"record query");

int a[3]; enum { N=sizeof(__typeof__(a)) }; _Static_assert(N==12, "typeof array");

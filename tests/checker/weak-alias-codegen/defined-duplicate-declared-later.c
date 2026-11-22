extern int alias;
#pragma weak alias = late
int early = 7;
#pragma weak alias = early
extern int late;

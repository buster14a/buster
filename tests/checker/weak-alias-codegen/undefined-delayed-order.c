#pragma weak second = late
int early(void);
#pragma weak first = early
int late(void);

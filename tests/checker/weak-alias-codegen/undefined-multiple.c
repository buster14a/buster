int first_target(void);
extern int second_target;
#pragma weak first = first_target
int ordinary(void) __attribute__((alias("missing_ordinary")));
#pragma weak second = second_target

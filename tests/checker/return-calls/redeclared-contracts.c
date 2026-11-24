// Redeclarations accumulate nonnull indices independently of return-value and control-flow attributes.
void *stop(int *, int *) __attribute__((analyzer_noreturn, nonnull(1)));
void *stop(int *, int *) __attribute__((returns_nonnull, nonnull(2)));
void *stop(int *, int *);

int stops(void) {
    stop(0, 0);
}

void *make(int *) __attribute__((returns_nonnull));
void *make(int *) __attribute__((nonnull(1)));

int truth(int *p) { return !!make(p); }
int falls(int *p) { make(p); }

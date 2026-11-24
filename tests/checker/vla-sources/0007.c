unsigned long f(int n) { __typeof__(({ int (*p)[n]; p; })) q; return sizeof *q; }

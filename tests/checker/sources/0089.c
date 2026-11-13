enum N : char { P = sizeof(struct { enum N n; }) } __attribute__((aligned(8)));

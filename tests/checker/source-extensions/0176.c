int a[2][3]; int (*p)[3] = &a[1]; _Static_assert(sizeof(*p) == 12, "row");

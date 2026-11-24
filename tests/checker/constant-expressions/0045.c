struct Buffer { char spare[12 * sizeof(int) - 5 * sizeof(void *)]; }; _Static_assert(sizeof(struct Buffer)==8, "bound");

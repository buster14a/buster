typedef union {
    int narrow;
    long long wide;
} mismatch __attribute__((transparent_union));

static long long read_wide(mismatch value)
{
    return value.wide;
}

int main(void)
{
    return read_wide(7) == 7 ? 0 : 1;
}

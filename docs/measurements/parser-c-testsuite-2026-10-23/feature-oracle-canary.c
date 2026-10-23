#if !__has_attribute(always_inline)
#error Clang feature answers are required for this qualification.
#endif
static inline __attribute__((always_inline)) int oracle_canary(void)
{
    return 0;
}
int main(void)
{
    return oracle_canary();
}

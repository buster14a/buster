static inline __attribute__((always_inline)) int oracle_canary(void)
{
return 0;
}
int main(void)
{
return oracle_canary();
}

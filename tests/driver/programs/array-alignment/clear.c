extern char big[64];
extern _Thread_local char tls[32];
char* hidden_address(void);
void clear_all(void)
{
    char* hidden = __builtin_assume_aligned(hidden_address(), 16);
    for (int i = 0; i < 64; i += 1) big[i] = 0;
    for (int i = 0; i < 48; i += 1) hidden[i] = 0;
    for (int i = 0; i < 32; i += 1) tls[i] = 0;
}

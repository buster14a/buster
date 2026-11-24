char pad1 = 1;
char big[64];
char pad2 = 2;
static char pad3 = 3;
static char hidden[48];
_Thread_local char pad4 = 4;
_Thread_local char tls[32];
char* hidden_address(void) { return hidden; }
void clear_all(void);
int main(void)
{
    big[0] = 1;
    hidden[47] = 2;
    tls[31] = 3;
    clear_all();
    return big[0] + hidden[47] + tls[31] + pad1 + pad2 + pad3 + pad4 - 10;
}

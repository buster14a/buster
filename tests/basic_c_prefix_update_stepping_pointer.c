// `++*s++`: the prefix update's operand carries its own postfix step --
// dtoa's digit-strip bumps a digit and moves past it in one expression.
// The pointer's increment runs first and hands the deref its previous
// value; both the updated digit and the stepped pointer are asserted.
int main(void)
{
    char buffer[4] = {'1', '2', '3', 0};
    char* s = buffer;
    ++*s++;
    if (buffer[0] != '2') { return 1; }
    if (s != buffer + 1) { return 2; }
    *s++ += 1;
    if (buffer[1] != '3') { return 3; }
    return 0;
}

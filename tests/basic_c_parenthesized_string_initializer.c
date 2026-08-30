// A string initializer in redundant parentheses: CPython's
// _PyRuntimeState_INIT writes ("<dictcomp>") into its static identifier
// table, and the reference compilers accept the parenthesized spelling
// everywhere the bare one initializes an array.  The decoded bytes are read
// back beside an unparenthesized sibling.
struct ascii { long state; char data[12]; };
static const struct ascii table[] = {
    { 1, ("<dictcomp>") },
    { 2, "<setcomp>" },
};
int main(void)
{
    return table[0].data[1] == 'd' && table[1].data[1] == 's' ? 0 : 1;
}

static int association(void) { return 2; }
static int operand(void) { return 3; }
int main(void) { return _Generic(1, long: association, default: operand)(); }

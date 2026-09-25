static const int zero = 0;
static int folded = zero ? 3 : 4;
int main(void) { return folded != 4; }

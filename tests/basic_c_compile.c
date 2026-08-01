static int multiply(int left, int right);
static int multiplier = 7;
static const int expected = 42;
static const char* message = "ok";

int main(void)
{
    int value = 6;
    return (multiply(value + 0, multiplier) == expected) - 1;
}

static int multiply(int left, int right)
{
    int value = left * right;
    return value;
}

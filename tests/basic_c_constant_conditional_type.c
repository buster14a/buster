// The selected arm is converted before any enclosing operator or initializer.
static int comparison_true = (1 ? -1 : 0U) < 0;
static int comparison_false = (0 ? 0U : -1) < 0;
static unsigned long long widened_true = 1 ? -1 : 0U;
static unsigned long long widened_false = 0 ? 0U : -1;
static double mixed_true = (1 ? 3 : 2.0) / 2;
static double mixed_false = (0 ? 2.0 : 3) / 2;
static double promoted_narrow = (1 ? (unsigned char)255 : (short)-1) / 2;
static int nested = ((1 ? (0 ? 4.0 : 3) : 2.0) / 2) == 1.5;
static int items[3] = {11, 22, 33};
static int *selected_pointer = 1 ? items + 1 : 0;
static int *selected_null = 0 ? items : 0;
static void *void_pointer = 0 ? (void *)items : items + 2;

_Static_assert(((1 ? -1 : 0U) < 0) == 0, "conditional integer conversion");
_Static_assert(((0 ? 0U : -1) < 0) == 0, "conditional false arm conversion");

static unsigned long long runtime_choice(int condition)
{
    return condition ? -1 : 0U;
}

int main(void)
{
    int result = 0;
    result |= comparison_true != 0 || comparison_false != 0;
    result |= widened_true != 4294967295ULL || widened_false != 4294967295ULL;
    result |= mixed_true != 1.5 || mixed_false != 1.5;
    result |= promoted_narrow != 127.0;
    result |= nested != 1;
    result |= selected_pointer != items + 1 || *selected_pointer != 22;
    result |= selected_null != 0 || void_pointer != items + 2;
    result |= runtime_choice(1) != 4294967295ULL || runtime_choice(0) != 0;
    return result;
}

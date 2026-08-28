// QuickJS spells one of its debug helpers
// `static void __attribute((unused)) dump_token(JSParseState *s, ...)`.
// `__attribute` without the trailing underscores was missing from the
// declaration-keyword table, so the declaration scan took it for the declared
// name and `(unused)` for the parameter list: the real parameters were never
// bound and the body could not name them.
static int __attribute((unused)) doubled(int value)
{
    return value * 2;
}

static int __attribute__((unused)) tripled(int value)
{
    return value * 3;
}

// The short spelling also has to be skipped where an attribute may appear
// without introducing a declaration of its own.
static int __attribute((always_inline)) __attribute((unused)) summed(int left, int right)
{
    return left + right;
}

int main(void)
{
    if (doubled(21) != 42) return 1;
    if (tripled(4) != 12) return 2;
    if (summed(20, 22) != 42) return 3;
    return 0;
}

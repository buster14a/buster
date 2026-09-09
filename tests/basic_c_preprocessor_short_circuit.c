// GitHub #147 / #258. Every arithmetic fault below is in an unevaluated
// operand. Syntax and the common type of conditional arms still matter.
#define BAD_DIV (1 / 0)
#define BAD_REM (1 % 0)
#define BAD_UNSIGNED (1u / 0)
#define BAD_SIGNED_DIV ((-9223372036854775807LL - 1) / -1)
#define BAD_SIGNED_REM ((-9223372036854775807LL - 1) % -1)

#if 0 && BAD_DIV
#error false logical-and selected its right operand
#endif
#if !(1 || BAD_REM)
#error true logical-or lost its left operand
#endif
#if (1 ? 17 : BAD_DIV) != 17
#error conditional selected the wrong arm
#endif
#if (0 ? BAD_REM : 23) != 23
#error conditional selected the wrong arm
#endif
#if 0 && BAD_SIGNED_DIV
#error discarded signed division was evaluated
#endif
#if !(1 || BAD_SIGNED_REM)
#error discarded signed remainder was evaluated
#endif
#if ~(1 ? 1 : BAD_UNSIGNED) <= 0
#error unselected unsigned arm must still determine the common type
#endif
#if (1 ? -1 : BAD_DIV) >= 0
#error signed conditional unexpectedly became unsigned
#endif
#if 0
#error inactive directive
#elif 1 || (BAD_DIV && BAD_REM)
#define SELECTED_VALUE 42
#else
#error elif short circuit failed
#endif
#if (1 ? (0 ? BAD_DIV : 1) : BAD_REM) != 1
#error nested conditional associativity changed
#endif

int main(void)
{
    return SELECTED_VALUE != 42;
}

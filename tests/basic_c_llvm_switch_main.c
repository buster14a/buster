// Compile this oracle separately with Clang so the writer cannot rotate both
// the tested branches and the code that checks their results.
int switch_distinct(int value);
int switch_grouped(int value);
int switch_no_default(int value);
int switch_default_only(int value);
int switch_empty(int value);
int switch_nested(int outer, int inner);
int switch_signed(long long value);
int switch_unsigned(unsigned long long value);

int main(void)
{
    int failures = 0;
    failures += switch_distinct(0) != 11;
    failures += switch_distinct(1) != 23;
    failures += switch_distinct(99) != 37;
    failures += switch_grouped(-3) != 10;
    failures += switch_grouped(2) != 10;
    failures += switch_grouped(4) != 27;
    failures += switch_grouped(5) != 16;
    failures += switch_grouped(99) != 20;
    failures += switch_no_default(2) != 43;
    failures += switch_no_default(7) != 47;
    failures += switch_no_default(-1) != 41;
    failures += switch_default_only(-1) != 53;
    failures += switch_default_only(0) != 53;
    failures += switch_empty(-1) != 0;
    failures += switch_empty(10) != 11;
    failures += switch_nested(-1, -2) != 59;
    failures += switch_nested(-1, 3) != 61;
    failures += switch_nested(-1, 0) != 67;
    failures += switch_nested(4, -2) != 71;
    failures += switch_nested(0, 3) != 73;
    failures += switch_signed(-1099511627776LL) != 79;
    failures += switch_signed(-1) != 83;
    failures += switch_signed(1099511627776LL) != 89;
    failures += switch_signed(0) != 97;
    failures += switch_unsigned(0) != 101;
    failures += switch_unsigned(9223372036854775808ULL) != 103;
    failures += switch_unsigned(18446744073709551615ULL) != 107;
    failures += switch_unsigned(1099511627776ULL) != 109;
    return failures;
}

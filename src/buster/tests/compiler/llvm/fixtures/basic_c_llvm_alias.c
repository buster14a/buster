// __attribute__((alias)) through LLVM bitcode (#1338): each name below must
// survive as an LLVM alias of its definition, not as an unresolved
// declaration. llvm_bitcode_test_alias reads the records and links and runs
// this unit through Clang; main returns 0 only when every alias is the same
// function or object as its target.
int target(void) { return 1; }
int other(void) __attribute__((alias("target")));

// Used before the alias is declared, with a static target nothing else names.
int used_early(void);
int early(void) { return used_early(); }
static int local_target(void) { return 30; }
int used_early(void) __attribute__((alias("local_target")));

int counter = 5;
extern int counter_alias __attribute__((alias("counter")));
int table[3] = {1, 2, 3};
extern int table_alias[3] __attribute__((alias("table")));

int weak_other(void) __attribute__((weak, alias("target")));
__attribute__((visibility("hidden"))) int hidden_other(void) __attribute__((alias("target")));
static int local_other(void) __attribute__((alias("target")));

int (*saved)(void) = other;
int* counter_address = &counter_alias;

int main(void)
{
    counter_alias += 4;
    table_alias[1] = 7;
    int same_function = saved == target && &weak_other != 0;
    int same_data = counter_address == &counter && table_alias == table;
    int calls = other() + early() + weak_other() + hidden_other() + local_other();
    return same_function && same_data && calls == 34 && counter == 9 && table[1] == 7 ? 0 : 1;
}

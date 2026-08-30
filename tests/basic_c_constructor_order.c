// `__attribute__((constructor))` priority across translation units (issue
// 789).  GNU runs every prioritized constructor before every unprioritized
// one, ascending, over the *whole program*: the two units here are written so
// that reproducing that order requires interleaving them, and reproducing
// only each unit's own order does not.  This is the half of the contract
// `tests/basic_c_constructor.c` cannot see, because it is one file.
//
// The five constructors are numbered by the position they must run in:
//
//   1  with_earliest_priority   constructor(101)   second unit
//   2  with_middle_priority     constructor(120)   this unit
//   3  with_latest_priority     constructor(150)   second unit
//   4  without_priority         constructor        this unit
//   5  second_without_priority  constructor        second unit
//
// The two that named no priority run last in link order, which is why this
// unit has to be given to the linker first.  Nothing here calls a library
// function, for the reason the single-file fixture gives: it is linked and
// run for every hosted target, and a libc call would make it a test of the
// import machinery instead.

// Both defined by the second translation unit, which is also where the
// recorder lives: a constructor in either unit reaches the same array.
extern int constructor_order[8];
extern int constructor_order_count;
void record_constructor(int identifier);

__attribute__((constructor)) static void without_priority(void)
{
    record_constructor(4);
}

__attribute__((constructor(120))) static void with_middle_priority(void)
{
    record_constructor(2);
}

int main(void)
{
    if (constructor_order_count != 5)
    {
        return 1;
    }
    for (int index = 0; index < 5; index += 1)
    {
        if (constructor_order[index] != index + 1)
        {
            // 2 through 6: the position that ran out of order names itself.
            return index + 2;
        }
    }
    return 0;
}

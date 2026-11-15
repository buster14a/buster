int object;
_Atomic int atomic_object;
const int constant = 1;
const int *const fixed = &object;
volatile int *volatile changing;

_Atomic __auto_type *plain = &object;
_Atomic __auto_type *atomic_source = &atomic_object;
const _Atomic __auto_type *qualified = &constant;
_Atomic const volatile __auto_type **nested = (int **)0;
_Atomic __auto_type *const fixed_pointer = &object;
_Atomic __auto_type *restrict restricted_pointer = &object;
_Atomic __auto_type *volatile volatile_pointer = &object;
_Atomic __auto_type *qualified_source = fixed;
_Atomic __auto_type *volatile_source = changing;
_Atomic __auto_type ********depth = (int ********)0;

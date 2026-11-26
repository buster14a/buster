// Missing parentheses around a type use the exact end found by the ordinary abstract-type reader (#2376).
typedef unsigned long Alias;
struct Record { int member; };
int simple(void) { return sizeof int; }
int qualified_pointer(void) { return sizeof const unsigned long *; }
int named_type(void) { return sizeof Alias; }
int record_type(void) { return sizeof struct Record; }
int array_type(void) { return sizeof int [3]; }
int function_pointer(void) { return sizeof int (*)(int); }
int local_type(void) { typedef int Local; return sizeof Local; }
int typeof_type(int value) { return sizeof __typeof__(value); }
int aligned_type(void) { return _Alignof int; }

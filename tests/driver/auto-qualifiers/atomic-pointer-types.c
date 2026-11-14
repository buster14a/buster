typedef int Integer;
typedef int *Pointer;
Integer value;
Pointer pointer;
int array[2];
int function(void);

_Atomic __auto_type *alias_pointer = pointer;
_Atomic __auto_type *alias_address = &value;
_Atomic __auto_type *alias_group = ((pointer));
_Atomic __auto_type *alias_cast = (Pointer)0;
_Atomic __auto_type *array_value = array;
_Atomic __auto_type *function_value = function;
_Atomic __auto_type *integer_value = 0;
_Atomic __auto_type *float_value = 1.0f;
_Atomic __auto_type *void_pointer = (void *)0;
_Atomic __auto_type *function_pointer = (int (*)(void))0;

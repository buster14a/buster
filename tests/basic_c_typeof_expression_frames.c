// The same type checks run with Clang/GCC as independent GNU C oracles.
long object;
const volatile long qualified;
int numbers[3];
struct Node { struct Node *next; long value; } node;
long callback(int);
typedef __typeof__((0, (0, object))) comma_scalar;
typedef __typeof__(((numbers))) bare_array;
typedef __typeof__(((callback))) bare_function;
typedef __typeof__((0, &qualified)) qualified_pointer;
typedef __typeof__(*&(qualified)) qualified_scalar;
typedef __typeof__((0, (0, &node))->next->value) member_value;
typedef __typeof__((*(&(node))).next->value) prefixed_member;
typedef __typeof__(((struct Node *)&node)->value) cast_member;
typedef __typeof__((0, numbers)) comma_array;
typedef __typeof__((0, callback)) comma_function;
__typeof__((0, qualified)) writable;
long callback(int argument) { return argument + 11; }
int main(void)
{
    comma_array pointer = numbers;
    comma_function function_pointer = callback;
    member_value value = 19;
    qualified_pointer cv_pointer = &qualified;
    pointer[1] = 7;
    writable = 23;
    int failed = value != 19 || pointer[1] != 7 || function_pointer(5) != 16 || writable != 23 || cv_pointer != &qualified;
    failed |= !__builtin_types_compatible_p(comma_scalar, long);
    failed |= !__builtin_types_compatible_p(bare_array, int [3]);
    failed |= !__builtin_types_compatible_p(bare_function, long (int));
    failed |= !__builtin_types_compatible_p(qualified_pointer, const volatile long *);
    failed |= !__builtin_types_compatible_p(qualified_scalar, const volatile long);
    failed |= !__builtin_types_compatible_p(member_value, long);
    failed |= !__builtin_types_compatible_p(prefixed_member, long);
    failed |= !__builtin_types_compatible_p(cast_member, long);
    failed |= !__builtin_types_compatible_p(comma_array, int *);
    failed |= !__builtin_types_compatible_p(comma_function, long (*)(int));
    return failed;
}

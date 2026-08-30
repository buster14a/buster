/* The shapes an atomic aggregate reaches the LLVM bitcode writer through.

   `_Atomic T` is a type built from T rather than a qualified T: it is padded up
   to the next power of two so that one lock-free access covers it (#731), so
   its LLVM type has to be the operand's wrapped with that padding -- what Clang
   writes as `{ %struct.three, [1 x i8] }` -- and not the operand's own, which
   would size the object short of what the native object already gives it
   (#767).

   Declarations only, in the three positions the writer builds a type for: a
   file-scope object, an aggregate member, and a block-scope object. An atomic
   aggregate can be loaded and stored as of #762, but the type is the same
   either way and keeping the accesses out leaves this fixture readable by a
   compiler that only has the declaration half. */

typedef struct
{
    signed char a, b, c;
} three;
typedef _Atomic three atomic_three;

typedef struct
{
    int a, b, c;
} twelve;

typedef union
{
    signed char bytes[3];
} narrow_union;

atomic_three file_scope_object;
_Atomic twelve file_scope_twelve;
_Atomic narrow_union file_scope_union;

/* No padding: the operand's size already covers the object, so this one stays
   its operand's LLVM type. */
typedef struct
{
    long long a;
} eight;
_Atomic eight file_scope_eight;

struct holder
{
    atomic_three member;
    int tail;
};
struct holder file_scope_holder;

int atomic_bitcode_shapes(void)
{
    atomic_three block_scope_object;
    _Atomic twelve block_scope_twelve;
    signed char* address = (signed char*)&block_scope_object;
    signed char* wide_address = (signed char*)&block_scope_twelve;
    return (int)(address != wide_address) + (int)sizeof(atomic_three) + (int)sizeof(struct holder);
}

int main(void)
{
    return !(sizeof(atomic_three) == 4 && sizeof(file_scope_twelve) == 16 && sizeof(file_scope_union) == 4 && sizeof(file_scope_eight) == 8 &&
             sizeof(struct holder) == 8 && atomic_bitcode_shapes() == 13);
}

// The shapes the atomic-argument pair passes across the compiler boundary.
// Both halves include this file and each half may be built by a different
// compiler, so a disagreement about which register -- or whether a register at
// all -- carries a record holding an atomic member becomes a wrong answer
// rather than a wrong-looking disassembly.
//
// This pair pins a decision rather than a bug (#763).  A qualifier decides
// nothing about how a value is passed here: `_Atomic T` and `volatile T` are
// classified as the T they are built from, so every record below rides exactly
// the registers its unqualified spelling rides.  GCC 16.2.1 answers the same
// on x86-64.  Clang 22.1.8 does not, in two separate places: on System V
// x86-64 it sends any record containing an atomic member -- and any atomic
// record -- to memory, and on AArch64 it declines to call an aggregate with an
// atomic floating member homogeneous.  It does neither on Win64 or Darwin
// AArch64, and does neither for `volatile`, which is why both are read as
// fall-throughs in its walks rather than as ABI positions.  See
// ir_abi_unqualified_type in ir.c for the whole reasoning and the measurements.
//
// Every shape here has the same size and alignment under Clang, GCC and this
// compiler (measured 2026-08-30), so only the argument class is under test: a
// half that laid one out differently would fail for a reason this pair is not
// about.  That is why the promoted-layout shapes -- `_Atomic` of a three-byte
// record and friends, where Clang pads and GCC does not (#731) -- stay in
// tests/basic_c_packed_layout.c and out of any cross-linked header.
#ifndef BASIC_C_ATOMIC_ABI_SHAPES_H
#define BASIC_C_ATOMIC_ABI_SHAPES_H

// One INTEGER eightbyte: eight bytes holding a `char` and an `_Atomic int`.
// The `_Atomic int` alone is what Clang's System V walk trips over, so this is
// the smallest shape the decision is visible in -- it rides `rdi`/`x0` here
// and GCC's `edi`, and arrives on the stack from a Clang-built System V caller.
struct atomic_scalar_member
{
    char c;
    _Atomic int v;
};

// Two INTEGER eightbytes, so the pair also pins the second half of the
// register cursor: a memory classification moves both halves at once, and a
// half that classified only the atomic eightbyte would place the second one
// against a different register.
struct atomic_pair_member
{
    _Atomic long long a;
    long long b;
};

// An atomic *aggregate* member rather than an atomic scalar one, so the walk
// has to go through the atomic type into the record it is built from: this is
// two INTEGER eightbytes because `struct atomic_int_pair` is, and Clang sends
// the whole thing to memory on System V for the same reason it sends the
// scalar shape above.  Reading the member needs an atomic load of the
// aggregate, which is the lowering #762 added -- a half without it does not
// compile this at all.
//
// A *bare* atomic record passed by value is the same rule and is not here: an
// atomic aggregate parameter fails code generation in every spelling today
// (#786), and the one spelling that does compile -- `_Atomic` written in front
// of the tag -- compiles because it is not reaching the type at all (#761), so
// a fixture written that way would pass while testing nothing.  It belongs in
// this pair once either lands.
struct atomic_int_pair
{
    int a;
    int b;
};

struct atomic_record_member
{
    char c;
    _Atomic(struct atomic_int_pair) v;
};

// A `volatile` floating member, which neither reference lets disturb the
// class: this is the two-register homogeneous float aggregate on AArch64 and
// the single SSE eightbyte on System V that `struct { float a, b; }` is.  It
// is in the pair because it is the same element-identity question the atomic
// spelling below asks and the one both references answer our way, so it pins
// the mechanism against a compiler that is not this one.
struct volatile_float_member
{
    float a;
    volatile float b;
};

#ifndef ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA
// The same shape with `_Atomic` in place of `volatile`, which is the shape the
// decision is about on the floating side.  GCC classifies it SSE on x86-64
// exactly as the plain spelling; Clang declines the homogeneous aggregate on
// AArch64, so the lane that links against Clang there defines the macro above
// and leaves this shape out.  Every other lane carries it.
struct atomic_float_member
{
    float a;
    _Atomic float b;
};
#endif

// The by-value directions.  Each shape crosses first as an argument in the
// leading register, then after enough scalars to exhaust the cheap end of the
// register file -- five integers leave one general-purpose argument register
// on both conventions, and five floats leave the sixth vector register -- so a
// half that sent the record to memory instead disagrees about the last
// argument's home rather than about the first one's.  The `make` direction
// pins the result class the same way; there is no atomic *result* to pin
// beside it, because a function's return type drops its qualifiers.
extern int atomic_abi_scalar_member_sum(struct atomic_scalar_member x);
extern int atomic_abi_scalar_member_after_five(int a, int b, int c, int d, int e, struct atomic_scalar_member x);
extern struct atomic_scalar_member atomic_abi_make_scalar_member(char c, int v);

extern long long atomic_abi_pair_member_sum(struct atomic_pair_member x);
extern long long atomic_abi_pair_member_after_five(int a, int b, int c, int d, int e, struct atomic_pair_member x);
extern struct atomic_pair_member atomic_abi_make_pair_member(long long a, long long b);

extern int atomic_abi_record_member_sum(struct atomic_record_member x);
extern int atomic_abi_record_member_after_five(int a, int b, int c, int d, int e, struct atomic_record_member x);
extern struct atomic_record_member atomic_abi_make_record_member(char c, int a, int b);

extern float atomic_abi_volatile_float_second(struct volatile_float_member x);
extern float atomic_abi_volatile_float_after_five(float a, float b, float c, float d, float e, struct volatile_float_member x);
extern struct volatile_float_member atomic_abi_make_volatile_float(float a, float b);

#ifndef ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA
extern float atomic_abi_atomic_float_second(struct atomic_float_member x);
extern float atomic_abi_atomic_float_after_five(float a, float b, float c, float d, float e, struct atomic_float_member x);
extern struct atomic_float_member atomic_abi_make_atomic_float(float a, float b);
#endif

#endif

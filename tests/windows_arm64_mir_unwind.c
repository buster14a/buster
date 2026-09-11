// Native Windows ARM64 oracle for MIR frames. Expected register values come
// from instruction effects and caller sentinels, never from unwind opcodes.
// The SDK ARM64 CONTEXT offsets are 0x100 SP, 0x108 PC, 0x110 V, size 0x390.
typedef unsigned long long U64;
typedef unsigned int U32;
typedef struct RuntimeContext RuntimeContext;
struct RuntimeContext
{
    U32 flags;
    U32 cpsr;
    U64 x[31];
    U64 sp;
    U64 pc;
    U64 v[64];
    U32 fpcr;
    U32 fpsr;
    U32 bcr[8];
    U64 bvr[8];
    U32 wcr[2];
    U64 wvr[2];
};
_Static_assert(sizeof(RuntimeContext) == 0x390, "ARM64 CONTEXT size");
_Static_assert(__builtin_offsetof(RuntimeContext, sp) == 0x100, "ARM64 CONTEXT SP");
_Static_assert(__builtin_offsetof(RuntimeContext, pc) == 0x108, "ARM64 CONTEXT PC");
typedef struct RuntimeFunction RuntimeFunction;
struct RuntimeFunction { U32 begin_address; U32 unwind_data; };
extern RuntimeFunction* RtlLookupFunctionEntry(U64 pc, U64* image_base, void* history);
extern void* RtlVirtualUnwind(U32 handler_type, U64 image_base, U64 pc, RuntimeFunction* function,
                              RuntimeContext* context, void** handler_data, U64* establisher_frame, void* pointers);
extern void* GetStdHandle(int standard_handle);
extern int WriteFile(void* handle, void* buffer, U32 size, U32* written, void* overlapped);

typedef struct RuntimeReport RuntimeReport;
struct RuntimeReport { U64 success; U64 boundaries; U64 saved_mask; U64 failure; };
static RuntimeReport report;
static U64 fake_stack[131080];
static volatile U64 opaque_value = 3;
int main(void);

static U64 opaque(U64 value)
{
    return value + opaque_value;
}

// More than eight integer arguments exercise the incoming-SP displacement.
static U64 small_frame(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g, U64 h, U64 i, U64 j)
{
    return a + b + c + d + e + f + g + h + i + j + opaque(a);
}

static U64 large_frame(U64 seed)
{
    volatile unsigned char bytes[524304];
    bytes[0] = 13;
    bytes[4095] = 17;
    bytes[4096] = 19;
    bytes[524303] = 23;
    U64 a = seed + 1, b = seed + 2, c = seed + 3, d = seed + 4;
    U64 e = seed + 5, f = seed + 6, g = seed + 7, h = seed + 8;
    U64 called = opaque(seed);
    return a + b + c + d + e + f + g + h + called + bytes[0] + bytes[4095] + bytes[4096] + bytes[524303];
}

static U64 dynamic_frame(U64 size)
{
    volatile unsigned char bytes[size];
    bytes[0] = 41;
    bytes[size - 1] = 43;
    U64 called = opaque(size);
    return called + bytes[0] + bytes[size - 1];
}

static U64 caller_value(U32 reg)
{
    return reg == 30 ? (U64)(void*)main + 4 : 0x1122334455660000ULL + (U64)reg * 0x101;
}

static int check_boundary(RuntimeContext* state, RuntimeFunction* function, U64 image_base, U64 entry_sp)
{
    U64 storage[116];
    RuntimeContext* context = (RuntimeContext*)(((U64)storage + 15) & ~15ULL);
    *context = *state;
    void* handler_data = 0;
    U64 establisher_frame = 0;
    RtlVirtualUnwind(0, image_base, state->pc, function, context, &handler_data, &establisher_frame, 0);
    int valid = context->sp == entry_sp && context->pc == caller_value(30);
    for (U32 reg = 19; reg <= 30; reg += 1)
    {
        valid = valid && context->x[reg] == caller_value(reg);
    }
    report.boundaries += 1;
    if (!valid && !report.failure)
    {
        report.failure = state->pc - image_base;
    }
    return valid;
}

static void set_body_state(RuntimeContext* state, U64 saved_mask, U64 fixed_sp)
{
    for (U32 reg = 19; reg < 28; reg += 1)
    {
        if (saved_mask & (1ULL << reg))
        {
            state->x[reg] = 0x8877665544330000ULL + reg;
        }
    }
    state->x[28] = fixed_sp;
    state->x[30] = state->pc;
    // An interrupted VLA body can have SP below the fixed local allocation.
    state->sp = fixed_sp - 272;
}

static int check_function(void* address, int require_large)
{
    U64 image_base = 0;
    RuntimeFunction* function = RtlLookupFunctionEntry((U64)address, &image_base, 0);
    int valid = function && image_base && image_base + function->begin_address == (U64)address && !(function->unwind_data & 3);
    U32 length = valid ? (*(U32*)(image_base + function->unwind_data) & 0x3ffffU) * 4 : 0;
    valid = valid && length >= 32 && length < 1048576;
    U64 storage[116];
    RuntimeContext* state = (RuntimeContext*)(((U64)storage + 15) & ~15ULL);
    *state = (RuntimeContext){0};
    U64 entry_sp = ((U64)(fake_stack + 131078)) & ~15ULL;
    state->flags = 0x00400003;
    state->sp = entry_sp;
    for (U32 reg = 19; reg <= 30; reg += 1)
    {
        state->x[reg] = caller_value(reg);
    }
    U32* code = (U32*)address;
    U32 prolog_words = 0;
    U64 saved_mask = 0;
    int reached_body = 0;
    int saw_large_probe = 0;
    while (valid && !reached_body && prolog_words < length / 4 && prolog_words < 64)
    {
        state->pc = (U64)(code + prolog_words);
        valid = check_boundary(state, function, image_base, entry_sp);
        U32 word = code[prolog_words];
        if ((word & 0xffc07fffU) == 0xa9807bfdU)
        {
            int units = (int)((word >> 15) & 127);
            if (units >= 64) { units -= 128; }
            valid = valid && prolog_words == 0 && units <= -4 && units >= -64;
            state->sp += (U64)(units * 8);
            ((U64*)state->sp)[0] = state->x[29];
            ((U64*)state->sp)[1] = state->x[30];
        }
        else if ((word & 0xffc003e0U) == 0xf90003e0U && (word & 31) >= 19 && (word & 31) <= 28)
        {
            U32 reg = word & 31;
            U32 offset = ((word >> 10) & 4095) * 8;
            valid = valid && offset >= 16 && state->sp + offset + 8 <= entry_sp;
            if (valid) { *(U64*)(state->sp + offset) = state->x[reg]; }
            saved_mask |= 1ULL << reg;
        }
        else if (word == 0x910003fdU)
        {
            state->x[29] = state->sp;
        }
        else if ((word & 0xffc003ffU) == 0xd10003ffU)
        {
            state->sp -= (word >> 10) & 4095;
        }
        else if ((word & 0xff80001fU) == 0xd280000fU)
        {
            state->x[15] = (U64)((word >> 5) & 65535) << (((word >> 21) & 3) * 16);
            saw_large_probe = 1;
        }
        else if ((word & 0xff80001fU) == 0xf280000fU)
        {
            U32 shift = ((word >> 21) & 3) * 16;
            state->x[15] = (state->x[15] & ~(65535ULL << shift)) | ((U64)((word >> 5) & 65535) << shift);
        }
        else if (word == 0xcb2f73ffU)
        {
            valid = valid && state->x[15] <= 65536;
            state->sp -= state->x[15] * 16;
        }
        else if (word == 0x910003fcU)
        {
            state->x[28] = state->sp;
            reached_body = 1;
        }
        else
        {
            // The guard loop only changes X16/X17/flags and touches pages.
            // SP and the nonvolatile file do not change at these boundaries.
            valid = valid && (word == 0xf90003ffU || word == 0x910003f0U || word == 0xcb0f1210U ||
                               word == 0x910003f1U || word == 0xd1400631U || word == 0xeb10023fU ||
                               word == 0x54000069U || word == 0xf900023fU || word == 0x17fffffcU || word == 0xf900021fU);
        }
        prolog_words += 1;
    }
    valid = valid && reached_body && (!require_large || saw_large_probe) && (saved_mask & (1ULL << 28));
    U64 fixed_sp = state->sp;
    U64 chain_sp = state->x[29];
    state->pc = (U64)(code + prolog_words);
    if (valid)
    {
        set_body_state(state, saved_mask, fixed_sp);
        valid = check_boundary(state, function, image_base, entry_sp);
    }
    U32 epilogs = 0;
    for (U32 index = prolog_words; valid && index < length / 4; index += 1)
    {
        if (code[index] == 0x910003bfU)
        {
            state->x[29] = chain_sp;
            state->pc = (U64)(code + index);
            set_body_state(state, saved_mask, fixed_sp);
            int returned = 0;
            U32 cursor = index;
            while (valid && !returned && cursor < length / 4 && cursor - index < 16)
            {
                state->pc = (U64)(code + cursor);
                valid = check_boundary(state, function, image_base, entry_sp);
                U32 word = code[cursor];
                if (word == 0x910003bfU)
                {
                    state->sp = state->x[29];
                }
                else if ((word & 0xffc003e0U) == 0xf94003e0U && (word & 31) >= 19 && (word & 31) <= 28)
                {
                    state->x[word & 31] = *(U64*)(state->sp + ((word >> 10) & 4095) * 8);
                }
                else if ((word & 0xffc07fffU) == 0xa8c07bfdU)
                {
                    state->x[29] = ((U64*)state->sp)[0];
                    state->x[30] = ((U64*)state->sp)[1];
                    state->sp += ((word >> 15) & 127) * 8;
                }
                else if (word == 0xd65f03c0U)
                {
                    returned = 1;
                }
                else { valid = 0; }
                cursor += 1;
            }
            valid = valid && returned;
            epilogs += 1;
            index = cursor - 1;
        }
    }
    valid = valid && epilogs > 0;
    report.saved_mask |= saved_mask;
    if (!valid && !report.failure) { report.failure = (U64)address - image_base; }
    return valid;
}

int main(void)
{
    int valid = small_frame(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 59 && large_frame(1) == 120 && dynamic_frame(257) == 344;
    valid = check_function((void*)small_frame, 0) && valid;
    valid = check_function((void*)large_frame, 1) && valid;
    valid = check_function((void*)dynamic_frame, 0) && valid;
    report.success = valid;
    U32 written = 0;
    WriteFile(GetStdHandle(-11), &report, sizeof(report), &written, 0);
    return valid && written == sizeof(report) ? 0 : 1;
}

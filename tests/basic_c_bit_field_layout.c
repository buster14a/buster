// QuickJS's JSClosureVar packs an enum-typed bit-field against three
// `uint8_t` ones:
//
//   JSClosureTypeEnum closure_type : 3;
//   uint8_t is_lexical : 1;
//   uint8_t is_const : 1;
//   uint8_t var_kind : 4;
//   uint16_t var_idx;
//
// Two contracts meet in that declaration.  Bit-fields are placed in bits, and
// a declared type only decides when a field must move on to the next storage
// unit of its own type -- not that a differently typed neighbour starts a new
// one.  And a bit-field of enumerated type is read with the enum's underlying
// signedness, which is unsigned when no enumerator is negative: read as a
// signed 3-bit field, the enumerator 5 answers -3 and the interpreter's
// switch falls to `default: abort()`.
typedef enum
{
    CLOSURE_LOCAL,
    CLOSURE_ARG,
    CLOSURE_REF,
    CLOSURE_GLOBAL_REF,
    CLOSURE_GLOBAL_DECL,
    CLOSURE_GLOBAL,
    CLOSURE_MODULE_DECL,
    CLOSURE_MODULE_IMPORT,
} ClosureTypeEnum;

typedef enum
{
    SIGNED_LOW = -2,
    SIGNED_HIGH = 1,
} SignedEnum;

struct closure_var
{
    ClosureTypeEnum closure_type : 3;
    unsigned char is_lexical : 1;
    unsigned char is_const : 1;
    unsigned char var_kind : 4;
    unsigned short var_idx;
};

struct mixed
{
    int wide : 30;
    unsigned char narrow : 5;
};

struct crossing
{
    char lead[5];
    int trailing : 3;
};

struct three_units
{
    short first : 9;
    char second : 7;
    short third : 9;
};

struct zero_width
{
    char before;
    long long : 0;
    char after;
};

struct signed_fields
{
    short narrow : 9;
    SignedEnum negative : 3;
};

int main(void)
{
    // Every size below is what the platform C compiler computes for the same
    // declaration.
    if (sizeof(struct closure_var) != 4) return 1;
    if (sizeof(struct mixed) != 8) return 2;
    if (sizeof(struct crossing) != 8) return 3;
    if (sizeof(struct three_units) != 4) return 4;
    if (sizeof(struct zero_width) != 9 || _Alignof(struct zero_width) != 1) return 5;

    struct closure_var closure;
    closure.closure_type = CLOSURE_GLOBAL;
    closure.is_lexical = 1;
    closure.is_const = 0;
    closure.var_kind = 9;
    closure.var_idx = 0xbeef;
    if (closure.closure_type != CLOSURE_GLOBAL) return 6;
    if ((int)closure.closure_type != 5) return 7;
    if (closure.is_lexical != 1 || closure.is_const != 0) return 8;
    if (closure.var_kind != 9) return 9;
    if (closure.var_idx != 0xbeef) return 10;
    switch (closure.closure_type)
    {
    case CLOSURE_GLOBAL: break;
    default: return 11;
    }

    struct three_units units;
    units.first = -200;
    units.second = 60;
    units.third = 255;
    if (units.first != -200) return 12;
    if (units.second != 60) return 13;
    if (units.third != 255) return 14;

    struct signed_fields fields;
    fields.narrow = -200;
    fields.negative = SIGNED_LOW;
    if (fields.narrow != -200) return 15;
    if (fields.negative != SIGNED_LOW) return 16;

    struct mixed mixed_fields;
    mixed_fields.wide = -3;
    mixed_fields.narrow = 17;
    if (mixed_fields.wide != -3) return 17;
    if (mixed_fields.narrow != 17) return 18;

    struct crossing crossing_fields;
    crossing_fields.lead[4] = 5;
    crossing_fields.trailing = -2;
    if (crossing_fields.trailing != -2) return 19;
    if (crossing_fields.lead[4] != 5) return 20;
    return 0;
}

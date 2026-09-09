// Allocation-lifetime regression: small member paths, a breadth > 64,
// depth > 32, and a SysV aggregate frontier that grows while a floating-point
// sibling remains pending. All results must survive later function lowering.
// Define BUSTER_ALLOCATION_NO_MAIN / BUSTER_ALLOCATION_ONLY_MAIN to cross-link
// this same ABI boundary against an independent C compiler.
struct Wide
{
    struct { int field_0; };
    struct { int field_1; };
    struct { int field_2; };
    struct { int field_3; };
    struct { int field_4; };
    struct { int field_5; };
    struct { int field_6; };
    struct { int field_7; };
    struct { int field_8; };
    struct { int field_9; };
    struct { int field_10; };
    struct { int field_11; };
    struct { int field_12; };
    struct { int field_13; };
    struct { int field_14; };
    struct { int field_15; };
    struct { int field_16; };
    struct { int field_17; };
    struct { int field_18; };
    struct { int field_19; };
    struct { int field_20; };
    struct { int field_21; };
    struct { int field_22; };
    struct { int field_23; };
    struct { int field_24; };
    struct { int field_25; };
    struct { int field_26; };
    struct { int field_27; };
    struct { int field_28; };
    struct { int field_29; };
    struct { int field_30; };
    struct { int field_31; };
    struct { int field_32; };
    struct { int field_33; };
    struct { int field_34; };
    struct { int field_35; };
    struct { int field_36; };
    struct { int field_37; };
    struct { int field_38; };
    struct { int field_39; };
    struct { int field_40; };
    struct { int field_41; };
    struct { int field_42; };
    struct { int field_43; };
    struct { int field_44; };
    struct { int field_45; };
    struct { int field_46; };
    struct { int field_47; };
    struct { int field_48; };
    struct { int field_49; };
    struct { int field_50; };
    struct { int field_51; };
    struct { int field_52; };
    struct { int field_53; };
    struct { int field_54; };
    struct { int field_55; };
    struct { int field_56; };
    struct { int field_57; };
    struct { int field_58; };
    struct { int field_59; };
    struct { int field_60; };
    struct { int field_61; };
    struct { int field_62; };
    struct { int field_63; };
    struct { int field_64; };
    struct { int field_65; };
    struct { int field_66; };
    struct { int field_67; };
    struct { int field_68; };
    struct { int field_69; };
    struct { int field_70; };
    struct { int field_71; };
    struct { int field_72; };
    struct { int field_73; };
    struct { int field_74; };
    struct { int field_75; };
    struct { int field_76; };
    struct { int field_77; };
    struct { int field_78; };
    struct { int field_79; };
};
struct Deep
{
    struct
    {
        struct
        {
            struct
            {
                struct
                {
                    struct
                    {
                        struct
                        {
                            struct
                            {
                                struct
                                {
                                    struct
                                    {
                                        struct
                                        {
                                            struct
                                            {
                                                struct
                                                {
                                                    struct
                                                    {
                                                        struct
                                                        {
                                                            struct
                                                            {
                                                                struct
                                                                {
                                                                    struct
                                                                    {
                                                                        struct
                                                                        {
                                                                            struct
                                                                            {
                                                                                struct
                                                                                {
                                                                                    struct
                                                                                    {
                                                                                        struct
                                                                                        {
                                                                                            struct
                                                                                            {
                                                                                                struct
                                                                                                {
                                                                                                    struct
                                                                                                    {
                                                                                                        struct
                                                                                                        {
                                                                                                            struct
                                                                                                            {
                                                                                                                struct
                                                                                                                {
                                                                                                                    struct
                                                                                                                    {
                                                                                                                        struct
                                                                                                                        {
                                                                                                                            struct
                                                                                                                            {
                                                                                                                                struct
                                                                                                                                {
                                                                                                                                    struct
                                                                                                                                    {
                                                                                                                                        struct
                                                                                                                                        {
                                                                                                                                            struct
                                                                                                                                            {
                                                                                                                                                struct
                                                                                                                                                {
                                                                                                                                                    struct
                                                                                                                                                    {
                                                                                                                                                        struct
                                                                                                                                                        {
                                                                                                                                                            struct
                                                                                                                                                            {
                                                                                                                                                                struct
                                                                                                                                                                {
                                                                                                                                                                    int value;
                                                                                                                                                                };
                                                                                                                                                            };
                                                                                                                                                        };
                                                                                                                                                    };
                                                                                                                                                };
                                                                                                                                            };
                                                                                                                                        };
                                                                                                                                    };
                                                                                                                                };
                                                                                                                            };
                                                                                                                        };
                                                                                                                    };
                                                                                                                };
                                                                                                            };
                                                                                                        };
                                                                                                    };
                                                                                                };
                                                                                            };
                                                                                        };
                                                                                    };
                                                                                };
                                                                            };
                                                                        };
                                                                    };
                                                                };
                                                            };
                                                        };
                                                    };
                                                };
                                            };
                                        };
                                    };
                                };
                            };
                        };
                    };
                };
            };
        };
    };
};
union Leaf
{
    long long field_0;
    long long field_1;
    long long field_2;
    long long field_3;
    long long field_4;
    long long field_5;
    long long field_6;
    long long field_7;
    long long field_8;
    long long field_9;
    long long field_10;
    long long field_11;
    long long field_12;
    long long field_13;
    long long field_14;
    long long field_15;
    long long field_16;
    long long field_17;
    long long field_18;
    long long field_19;
    long long field_20;
    long long field_21;
    long long field_22;
    long long field_23;
    long long field_24;
    long long field_25;
    long long field_26;
    long long field_27;
    long long field_28;
    long long field_29;
    long long field_30;
    long long field_31;
    long long field_32;
    long long field_33;
    long long field_34;
    long long field_35;
    long long field_36;
    long long field_37;
    long long field_38;
    long long field_39;
};
union Branch
{
    union Leaf field_0;
    union Leaf field_1;
    union Leaf field_2;
    union Leaf field_3;
    union Leaf field_4;
    union Leaf field_5;
    union Leaf field_6;
    union Leaf field_7;
    union Leaf field_8;
    union Leaf field_9;
    union Leaf field_10;
    union Leaf field_11;
    union Leaf field_12;
    union Leaf field_13;
    union Leaf field_14;
    union Leaf field_15;
    union Leaf field_16;
    union Leaf field_17;
    union Leaf field_18;
    union Leaf field_19;
    union Leaf field_20;
    union Leaf field_21;
    union Leaf field_22;
    union Leaf field_23;
    union Leaf field_24;
    union Leaf field_25;
    union Leaf field_26;
    union Leaf field_27;
    union Leaf field_28;
    union Leaf field_29;
    union Leaf field_30;
    union Leaf field_31;
    union Leaf field_32;
    union Leaf field_33;
    union Leaf field_34;
    union Leaf field_35;
    union Leaf field_36;
    union Leaf field_37;
    union Leaf field_38;
    union Leaf field_39;
};
struct Mixed { double real; union Branch integer; };

int allocation_wide(struct Wide* value);
int allocation_deep(struct Deep* value);
int allocation_qualified(struct Wide const volatile* value);
struct Mixed allocation_mixed(struct Mixed value);

#ifndef BUSTER_ALLOCATION_ONLY_MAIN
int allocation_wide(struct Wide* value)
{
    value->field_0 += 1;
    value->field_39 += 2;
    value->field_79 += 3;
    return value->field_0 + value->field_39 + value->field_79;
}

int allocation_deep(struct Deep* value)
{
    value->value += 7;
    return value->value;
}

int allocation_qualified(struct Wide const volatile* value)
{
    return value->field_0 + value->field_39 + value->field_79;
}

struct Mixed allocation_mixed(struct Mixed value)
{
    value.real += 1.25;
    value.integer.field_39.field_39 += 9;
    return value;
}
#endif

#ifndef BUSTER_ALLOCATION_NO_MAIN
int main(void)
{
    struct Wide wide;
    struct Deep deep;
    struct Mixed input;
    wide.field_0 = 10;
    wide.field_39 = 20;
    wide.field_79 = 30;
    deep.value = 100;
    input.real = 2.5;
    input.integer.field_39.field_39 = 33;
    struct Mixed output = allocation_mixed(input);
    int failed = allocation_wide(&wide) != 66;
    failed |= allocation_deep(&deep) != 107;
    failed |= allocation_qualified(&wide) != 66;
    failed |= output.real != 3.75 || output.integer.field_39.field_39 != 42;
    failed |= input.real != 2.5 || input.integer.field_39.field_39 != 33;
    return failed;
}
#endif

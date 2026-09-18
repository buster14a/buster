// Independent GNU as / LLVM bytes: tests/x86_64_got_encoding_oracle.s.
// The oracle below is built from the psABI conversion table (B.2) and the
// SDM's encodings for each instruction it names, not from the metadata
// module's derivation, so the two agree only if both are right.
#include <buster/tests/compiler/assembly/x86_64_got_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
static u8 const x86_got_got_oracle[16][7] = {
    {0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x3d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x3d, 0x00, 0x00, 0x00, 0x00},
};
static u8 const x86_got_address_oracle[16][7] = {
    {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00},
};

// The GOT-load operand shapes the psABI names, spelled as the SDM spells
// them: which operand is the rip-relative one, and the ModRM.reg extension a
// shape without a register operand uses.
enum
{
    X86_GOT_ORACLE_DESTINATION,
    X86_GOT_ORACLE_SOURCE,
    X86_GOT_ORACLE_SINGLE,
};
// Replacement opcodes are either a ModRM form addressing the same register,
// a rip-relative memory form (LEA alone), or an operand-free form whose
// immediate or displacement follows the opcode.
enum
{
    X86_GOT_ORACLE_DIRECT_REGISTER,
    X86_GOT_ORACLE_DIRECT_MEMORY,
    X86_GOT_ORACLE_DIRECT_BARE,
};
// The single-byte NOP the SDM defines; a replacement shorter than the shape
// it fills is padded in front so the relocated field never moves.
enum
{
    X86_GOT_ORACLE_NOP = 0x90,
    X86_GOT_ORACLE_FIELD_WIDTH = 4,
    X86_GOT_ORACLE_REGISTERS = 16,
};

typedef struct X86GotOracleClass X86GotOracleClass;
struct X86GotOracleClass
{
    u8 shape;
    u8 width;
    u8 source_opcode;
    u8 source_extension;
    u8 direct_form;
    u8 direct_opcode;
    u8 direct_extension;
    // The accumulator-or-shorter SDM encoding of the same replacement, or
    // zero where the SDM defines none. MOV's B8+rd exists for every register;
    // every other short form here is the eAX/rAX one.
    u8 short_opcode;
    bool short_every_register;
    u8 patch;
    u8 site;
};

// R_X86_64_GOTPCREL carries no relaxability promise, so the first row is the
// only conversion it may take. Both X spellings take every row whose prefix
// width matches the prefix they promise.
static X86GotOracleClass const x86_got_oracle_classes[] = {
    {X86_GOT_ORACLE_DESTINATION, 64, 0x8b, 0, X86_GOT_ORACLE_DIRECT_MEMORY, 0x8d, 0, 0, false, BUSTER_X86_METADATA_GOT_PATCH_PC32,
     BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x8b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0xc7, 0, 0xb8, true, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x03, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 0, 0x05, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x03, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 0, 0x05, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x0b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 1, 0x0d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x0b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 1, 0x0d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x13, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 2, 0x15, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x13, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 2, 0x15, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x1b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 3, 0x1d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x1b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 3, 0x1d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x23, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 4, 0x25, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x23, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 4, 0x25, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x2b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 5, 0x2d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x2b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 5, 0x2d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x33, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 6, 0x35, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x33, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 6, 0x35, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 32, 0x3b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 7, 0x3d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_DESTINATION, 64, 0x3b, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0x81, 7, 0x3d, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_SOURCE, 32, 0x85, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0xf7, 0, 0xa9, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_SOURCE, 64, 0x85, 0, X86_GOT_ORACLE_DIRECT_REGISTER, 0xf7, 0, 0xa9, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
    {X86_GOT_ORACLE_SINGLE, 64, 0xff, 2, X86_GOT_ORACLE_DIRECT_BARE, 0xe8, 0, 0, false, BUSTER_X86_METADATA_GOT_PATCH_PC32, 0},
    {X86_GOT_ORACLE_SINGLE, 64, 0xff, 4, X86_GOT_ORACLE_DIRECT_BARE, 0xe9, 0, 0, false, BUSTER_X86_METADATA_GOT_PATCH_PC32, 0},
    {X86_GOT_ORACLE_SINGLE, 64, 0xff, 6, X86_GOT_ORACLE_DIRECT_BARE, 0x68, 0, 0, false, BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32, 0},
};

// The bytes in front of the relocated field for one GOT-load shape. A
// register operand lives in ModRM.reg, so REX.R extends it; the rip-relative
// operand is always mod 00 with r/m 101 and no SIB.
static u32 x86_got_oracle_source(X86GotOracleClass entry, u32 reg, u8* output)
{
    u32 length = 0;
    if (entry.shape == X86_GOT_ORACLE_SINGLE)
    {
        output[length] = entry.source_opcode;
        length += 1;
        output[length] = (u8)(0x05u | (u32)entry.source_extension << 3);
        length += 1;
    }
    else
    {
        if (entry.width == 64)
        {
            output[length] = (u8)(0x48u | (reg >= 8 ? 0x04u : 0u));
            length += 1;
        }
        else if (reg >= 8)
        {
            output[length] = 0x44;
            length += 1;
        }
        output[length] = entry.source_opcode;
        length += 1;
        output[length] = (u8)(0x05u | (reg & 7u) << 3);
        length += 1;
    }
    return length;
}

// One SDM encoding of the replacement: the ModRM form, or the shorter form
// where the SDM defines one. A register the shorter form cannot name answers
// with no bytes at all.
static u32 x86_got_oracle_direct(X86GotOracleClass entry, u32 reg, bool shorter, u8* output)
{
    u32 length = 0;
    bool present = !shorter || (entry.short_opcode && (entry.short_every_register || !reg));
    if (present && entry.direct_form == X86_GOT_ORACLE_DIRECT_BARE)
    {
        output[length] = entry.direct_opcode;
        length += 1;
    }
    else if (present && entry.direct_form == X86_GOT_ORACLE_DIRECT_MEMORY)
    {
        output[length] = (u8)(0x48u | (reg >= 8 ? 0x04u : 0u));
        length += 1;
        output[length] = entry.direct_opcode;
        length += 1;
        output[length] = (u8)(0x05u | (reg & 7u) << 3);
        length += 1;
    }
    else if (present)
    {
        // The replacement names the same register through ModRM.r/m, so the
        // REX bit that extends it is B where the load's was R.
        if (entry.width == 64)
        {
            output[length] = (u8)(0x48u | (reg >= 8 ? 0x01u : 0u));
            length += 1;
        }
        else if (reg >= 8)
        {
            output[length] = 0x41;
            length += 1;
        }
        if (shorter)
        {
            output[length] = (u8)(entry.short_opcode + (entry.short_every_register ? (reg & 7u) : 0u));
            length += 1;
        }
        else
        {
            output[length] = entry.direct_opcode;
            length += 1;
            output[length] = (u8)(0xc0u | (u32)entry.direct_extension << 3 | (reg & 7u));
            length += 1;
        }
    }
    return length;
}

// The replacement as it must appear in the image: front-padded with NOPs to
// the shape's own width, so the four relocated bytes stay where the
// relocation already points.
static bool x86_got_oracle_expected(X86GotOracleClass entry, u32 reg, bool shorter, u32 prefix, u8* output)
{
    u8 direct[8] = {0};
    u32 length = x86_got_oracle_direct(entry, reg, shorter, direct);
    bool result = length && length <= prefix;
    if (result)
    {
        memset(output, X86_GOT_ORACLE_NOP, prefix - length);
        memcpy(output + prefix - length, direct, length);
    }
    return result;
}

static BusterX86MetadataGotSite x86_got_oracle_site(X86GotOracleClass entry, u32 prefix)
{
    return entry.site ? (BusterX86MetadataGotSite)entry.site
           : prefix == 2 ? BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX
                         : BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX;
}

UnitTestResult x86_64_got_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, 0, 3, 7, -4));
    u8 bytes[128];
    u8 expected[128];
    static u32 const values[] = {0, 1, 127, 128, 0x7fffffff, 0x80000000, 0xffffff80, 0xffffffff};
    // Plain type 9 names only a field. None of the eight REX-looking
    // bytes that previously matched may be consumed from the preceding
    // instruction, and failure must be atomic.
    for (u32 preceding = 0x40; preceding <= 0x4f; preceding += 1)
    {
        u8 ambiguous[] = {(u8)preceding, 0x8b, 0x05, 0x11, 0x22, 0x33, 0x44};
        u8 unchanged[sizeof(ambiguous)];
        memcpy(unchanged, ambiguous, sizeof(ambiguous));
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, ambiguous, 3, sizeof(ambiguous), -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_NONE);
        BUSTER_TEST(arguments, memcmp(ambiguous, unchanged, sizeof(ambiguous)) == 0);
    }
    // Type 43 promises a four-byte REX2/opcode/ModRM prefix. Encoding one
    // EGPR source and replacement through the checked metadata front door
    // both forces preparation of the complete APX vocabulary and verifies the
    // relocation field stays at the promised offset.
    {
        u8 apx_source[16] = {0};
        u8 apx_direct[16] = {0};
        String8 apx_features[1] = {S8("APX_F")};
        BusterX86MetadataPhysicalOperand apx_operands[2] = {
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
             .reg = {.index = 16, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 64,
             .memory = {.has_symbol = true, .symbol = S8("target"), .rip_relative = true,
                        .has_displacement = true, .address_size = 64, .scale = 1}},
        };
        for (u32 output = 0; output < 2; output += 1)
        {
            BusterX86MetadataRelocation field = {0};
            BusterX86MetadataEmitResult encoded = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = {.mnemonic = output ? S8("LEA") : S8("MOV"), .operands = apx_operands, .operand_count = 2,
                             .features = {.names = apx_features, .count = 1},
                             .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64},
                .output = output ? apx_direct : apx_source, .output_capacity = sizeof(apx_source),
                .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, encoded.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && encoded.byte_count == 8 &&
                                       encoded.relocation_count == 1 && field.offset == 4 && field.width == 4 &&
                                       field.kind == BUSTER_X86_METADATA_RELOCATION_PC32 && field.addend == -4 &&
                                       (output ? apx_direct[0] : apx_source[0]) == 0xd5);
        }
        memcpy(bytes, apx_source, 8);
        bytes[4] = 0x11;
        bytes[5] = 0x22;
        bytes[6] = 0x33;
        bytes[7] = 0x44;
        memcpy(expected, bytes, 8);
        memcpy(expected, apx_direct, 4);
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_CODE_4_GOTPCRELX, bytes, 4, 8, -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_PC32);
        BUSTER_TEST(arguments, memcmp(bytes, expected, 8) == 0);
    }
    // The programmatic oracle and the hand-written one agree on the family
    // R_X86_64_GOTPCREL has always converted, which is what lets the rest of
    // the vocabulary rest on the programmatic one alone.
    for (u32 reg = 0; reg < X86_GOT_ORACLE_REGISTERS; reg += 1)
    {
        u8 source[8] = {0};
        u8 address[8] = {0};
        u32 prefix = x86_got_oracle_source(x86_got_oracle_classes[0], reg, source);
        BUSTER_TEST(arguments, prefix == 3 && memcmp(source, x86_got_got_oracle[reg], prefix) == 0);
        BUSTER_TEST(arguments, x86_got_oracle_expected(x86_got_oracle_classes[0], reg, false, prefix, address));
        BUSTER_TEST(arguments, memcmp(address, x86_got_address_oracle[reg], prefix) == 0);
    }
    // Every row of the psABI table, every register, every field content and
    // every redundant REX bit a rip-relative form ignores.
    for (u32 entry_index = 0; entry_index < BUSTER_ARRAY_LENGTH(x86_got_oracle_classes); entry_index += 1)
    {
        X86GotOracleClass entry = x86_got_oracle_classes[entry_index];
        u32 registers = entry.shape == X86_GOT_ORACLE_SINGLE ? 1 : X86_GOT_ORACLE_REGISTERS;
        for (u32 reg = 0; reg < registers; reg += 1)
        {
            u8 source[8] = {0};
            u8 modrm_form[8] = {0};
            u8 short_form[8] = {0};
            u32 prefix = x86_got_oracle_source(entry, reg, source);
            bool has_modrm_form = x86_got_oracle_expected(entry, reg, false, prefix, modrm_form);
            bool has_short_form = x86_got_oracle_expected(entry, reg, true, prefix, short_form);
            BusterX86MetadataGotSite site = x86_got_oracle_site(entry, prefix);
            BUSTER_TEST(arguments, prefix >= 2 && prefix <= 3 && has_modrm_form);
            u32 variants = entry.width == 64 || entry.shape == X86_GOT_ORACLE_SINGLE ? 4 : 1;
            for (u32 variant = 0; variant < variants; variant += 1)
            {
                for (u32 value = 0; value < BUSTER_ARRAY_LENGTH(values); value += 1)
                {
                    u64 place = 11;
                    u64 field = place + prefix;
                    memset(bytes, 0xa5, sizeof(bytes));
                    memcpy(bytes + place, source, prefix);
                    // REX.X and REX.B name no operand here, so a producer's
                    // redundant bits still have to be recognized.
                    if (entry.width == 64 || (entry.shape != X86_GOT_ORACLE_SINGLE && reg >= 8))
                    {
                        bytes[place] |= (u8)variant;
                    }
                    memcpy(bytes + field, &values[value], X86_GOT_ORACLE_FIELD_WIDTH);
                    memcpy(expected, bytes, sizeof(bytes));
                    BusterX86MetadataGotPatch patch =
                        buster_x86_metadata_relax_got_reference(site, bytes, field, field + X86_GOT_ORACLE_FIELD_WIDTH, -4);
                    BUSTER_TEST(arguments, patch == entry.patch);
                    bool matched_modrm = memcmp(bytes + place, modrm_form, prefix) == 0;
                    bool matched_short = has_short_form && memcmp(bytes + place, short_form, prefix) == 0;
                    // Either SDM encoding of the same instruction is correct;
                    // nothing outside the shape's own bytes may change.
                    BUSTER_TEST(arguments, matched_modrm || matched_short);
                    memcpy(expected + place, bytes + place, prefix);
                    BUSTER_TEST(arguments, memcmp(bytes, expected, sizeof(bytes)) == 0);
                }
            }
            // An absolute replacement answers with the address itself, so a
            // site whose addend was never the -4 that aimed a rip-relative
            // field at a slot is refused outright.
            for (u32 addend = 0; addend < 3; addend += 1)
            {
                static s64 const addends[] = {0, -5, 4};
                u64 place = 11;
                u64 field = place + prefix;
                memset(bytes, 0xa5, sizeof(bytes));
                memcpy(bytes + place, source, prefix);
                memcpy(expected, bytes, sizeof(bytes));
                BusterX86MetadataGotPatch patch =
                    buster_x86_metadata_relax_got_reference(site, bytes, field, field + X86_GOT_ORACLE_FIELD_WIDTH, addends[addend]);
                bool relaxed = entry.patch == BUSTER_X86_METADATA_GOT_PATCH_PC32;
                BUSTER_TEST(arguments, patch == (relaxed ? entry.patch : BUSTER_X86_METADATA_GOT_PATCH_NONE));
                if (relaxed)
                {
                    memcpy(expected + place, bytes + place, prefix);
                }
                BUSTER_TEST(arguments, memcmp(bytes, expected, sizeof(bytes)) == 0);
            }
            // A shape is only inside the section when all of it is.
            for (u32 truncation = 0; truncation < prefix; truncation += 1)
            {
                u64 field = prefix;
                memset(bytes, 0xa5, sizeof(bytes));
                memcpy(bytes, source, prefix);
                memcpy(expected, bytes, sizeof(bytes));
                BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(site, bytes + truncation + 1, field - truncation - 1,
                                                                               field + X86_GOT_ORACLE_FIELD_WIDTH - truncation - 1, -4));
                BUSTER_TEST(arguments, memcmp(bytes, expected, sizeof(bytes)) == 0);
            }
        }
    }
    // The byte in front of a site without a REX prefix belongs to whatever
    // instruction precedes it. Reading `8b 05` as `48 8b 05` would rewrite
    // both; the relocation's own spelling is what rules that reading out.
    {
        u8 source[8] = {0};
        u32 prefix = x86_got_oracle_source(x86_got_oracle_classes[1], 0, source);
        u64 place = 11;
        u64 field = place + prefix;
        BUSTER_TEST(arguments, prefix == 2);
        memset(bytes, 0xa5, sizeof(bytes));
        bytes[place - 1] = 0x48;
        memcpy(bytes + place, source, prefix);
        memcpy(expected, bytes, sizeof(bytes));
        BUSTER_TEST(arguments, buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCRELX, bytes, field,
                                                                      field + X86_GOT_ORACLE_FIELD_WIDTH, -4) ==
                                   BUSTER_X86_METADATA_GOT_PATCH_ABSOLUTE32);
        memcpy(expected + place, bytes + place, prefix);
        BUSTER_TEST(arguments, memcmp(bytes, expected, sizeof(bytes)) == 0);
        BUSTER_TEST(arguments, bytes[place - 1] == 0x48);
        // The same bytes under the promise the producer did not make stay a
        // MOV-r64 load, which is the reading that rewrites the wrong
        // instruction, so plain GOTPCREL is the only site that takes it.
        memset(bytes, 0xa5, sizeof(bytes));
        bytes[place - 1] = 0x48;
        memcpy(bytes + place, source, prefix);
        BUSTER_TEST(arguments, buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, field,
                                                                      field + X86_GOT_ORACLE_FIELD_WIDTH, -4) ==
                                   BUSTER_X86_METADATA_GOT_PATCH_PC32);
        BUSTER_TEST(arguments, bytes[place - 1] == 0x48 && memcmp(bytes + place - 1, x86_got_address_oracle[0], 3) == 0);
    }
    for (u32 reg = 0; reg < 16; reg += 1)
    {
        BusterX86MetadataPhysicalOperand operands[2] = {
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
             .reg = {.index = (u16)reg, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 64,
             .memory = {.has_symbol = true, .symbol = S8("target"), .rip_relative = true,
                        .address_size = 64, .scale = 1}},
        };
        for (u32 output = 0; output < 2; output += 1)
        {
            BusterX86MetadataPhysicalQuery query = {
                .mnemonic = output ? S8("LEA") : S8("MOV"), .operands = operands, .operand_count = 2,
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataRelocation field = {0};
            BusterX86MetadataEmitResult checked = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = query, .output = bytes, .output_capacity = sizeof(bytes), .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, checked.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && checked.byte_count == 7 && checked.relocation_count == 1);
            BUSTER_TEST(arguments, field.offset == 3 && field.width == 4 && field.addend == -4 && field.kind == BUSTER_X86_METADATA_RELOCATION_PC32);
            BUSTER_TEST(arguments, memcmp(bytes, output ? x86_got_address_oracle[reg] : x86_got_got_oracle[reg], 7) == 0);
            BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
            BusterX86MetadataEmitResult exact = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = selected.form_id, .output = expected, .output_capacity = sizeof(expected),
                .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, exact.status == checked.status && exact.byte_count == checked.byte_count && memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 alignment = 0; alignment < 32; alignment += 1)
    {
        for (u32 size = 0; size <= 32; size += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            memcpy(bytes + alignment, x86_got_got_oracle[0], 7);
            memcpy(expected, bytes, sizeof(bytes));
            bool success = buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, alignment + 3, size, -4) ==
                           BUSTER_X86_METADATA_GOT_PATCH_PC32;
            bool valid = size >= alignment + 7;
            if (valid) memcpy(expected + alignment, x86_got_address_oracle[0], 3);
            BUSTER_TEST(arguments, success == valid && memcmp(bytes, expected, sizeof(bytes)) == 0);
        }
    }
    // Plain type 9 describes only the four-byte field. Exhaust every possible
    // byte in the three positions before it and prove the implementation
    // neither recognizes nor writes any of them.
    for (u32 rex = 0; rex < 256; rex += 1)
    {
        for (u32 modrm = 0; modrm < 256; modrm += 1)
        {
            memcpy(bytes, x86_got_got_oracle[0], 7);
            bytes[0] = (u8)rex;
            bytes[2] = (u8)modrm;
            memcpy(expected, bytes, 7);
            BUSTER_TEST(arguments,
                        buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, bytes, 3, 7, -4) ==
                            BUSTER_X86_METADATA_GOT_PATCH_NONE);
            BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 opcode = 0; opcode < 256; opcode += 1)
    {
        memcpy(bytes, x86_got_got_oracle[0], 7);
        bytes[1] = (u8)opcode;
        memcpy(expected, bytes, 7);
        BUSTER_TEST(arguments,
                    buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_GOTPCREL, bytes, 3, 7, -4) ==
                        BUSTER_X86_METADATA_GOT_PATCH_NONE);
        BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
    }
    memcpy(bytes, x86_got_got_oracle[0], 7);
    memcpy(expected, bytes, 7);
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, 2, 7, -4));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, UINT64_MAX, 7, -4));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_REX_GOTPCRELX, bytes, UINT64_MAX, UINT64_MAX, -4));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_reference(BUSTER_X86_METADATA_GOT_SITE_COUNT, bytes, 3, 7, -4));
    BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
    return result;
}
#endif

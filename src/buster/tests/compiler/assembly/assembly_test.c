#include <buster/tests/compiler/assembly/assembly_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/tests/compiler/assembly/generated/aarch64_scalar_integer_corpus.generated.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_registers.h>
#include <buster/lib/compiler/assembly/aarch64_syntax.h>

BUSTER_GLOBAL_LOCAL bool assembly_test_bytes_equal(ByteSlice actual, u8 const* expected, u32 expected_count)
{
    return actual.length == expected_count && (!expected_count || (actual.pointer && expected &&
                                                                     memcmp(actual.pointer, expected, expected_count) == 0));
}

BUSTER_GLOBAL_LOCAL bool assembly_test_source_has_half_precision(String8 source)
{
    for (u64 index = 0; index < source.length; index += 1)
    {
        if (source.pointer[index] == 'h' || source.pointer[index] == 'H')
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_test_source_has_x_register(String8 source)
{
    for (u64 index = 0; index + 1 < source.length; index += 1)
    {
        if ((source.pointer[index] == 'x' || source.pointer[index] == 'X') &&
            source.pointer[index + 1] >= '0' && source.pointer[index + 1] <= '9')
        {
            return true;
        }
    }
    return false;
}

typedef struct AssemblyA64DirectSIMDNeonCase AssemblyA64DirectSIMDNeonCase;
struct AssemblyA64DirectSIMDNeonCase
{
    String8 representative;
    u8 representative_bytes[4];
    String8 boundary;
    u8 boundary_bytes[4];
};

// One legal spelling per FP16 row (and both 4H/8H selectors for transform rows).
// Bytes are independent llvm-mc 22.1.8 encodings, not buster output.
typedef struct AssemblyA64DirectSIMDEncodingCase AssemblyA64DirectSIMDEncodingCase;
struct AssemblyA64DirectSIMDEncodingCase
{
    String8 source;
    u8 bytes[4];
};

typedef struct AssemblyA64DirectSIMDSpellingExpectation AssemblyA64DirectSIMDSpellingExpectation;
struct AssemblyA64DirectSIMDSpellingExpectation
{
    String8 semantic_id;
    u64 source_digest;
    u8 operand_count;
    u8 arrangements[4];
    u8 fixed_field_kind;
    u8 fixed_field_value;
};

/* FCVTL/FCVTN use the generated fixed literal `2` as a public suffix.  The
 * no-suffix and `{2}` spellings share one canonical row; a spelling-owned
 * COUNT override selects Q=0 or Q=1 respectively. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcvt_suffix_cases[] = {
    {S8_INITIALIZER("fcvtl v0.4s, v1.4h\n"), {0x20, 0x78, 0x21, 0x0e}},
    {S8_INITIALIZER("fcvtl2 v0.4s, v1.8h\n"), {0x20, 0x78, 0x21, 0x4e}},
    {S8_INITIALIZER("fcvtl v0.2d, v1.2s\n"), {0x20, 0x78, 0x61, 0x0e}},
    {S8_INITIALIZER("fcvtl2 v0.2d, v1.4s\n"), {0x20, 0x78, 0x61, 0x4e}},
    {S8_INITIALIZER("fcvtn v0.4h, v1.4s\n"), {0x20, 0x68, 0x21, 0x0e}},
    {S8_INITIALIZER("fcvtn2 v0.8h, v1.4s\n"), {0x20, 0x68, 0x21, 0x4e}},
    {S8_INITIALIZER("fcvtn v0.2s, v1.2d\n"), {0x20, 0x68, 0x61, 0x0e}},
    {S8_INITIALIZER("fcvtn2 v0.4s, v1.2d\n"), {0x20, 0x68, 0x61, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcvt_suffix_boundary_cases[] = {
    {S8_INITIALIZER("FCVTL V31.4S, V30.4H\n"), {0xdf, 0x7b, 0x21, 0x0e}},
    {S8_INITIALIZER("FCVTL2 V31.4S, V30.8H\n"), {0xdf, 0x7b, 0x21, 0x4e}},
    {S8_INITIALIZER("FCVTL V31.2D, V30.2S\n"), {0xdf, 0x7b, 0x61, 0x0e}},
    {S8_INITIALIZER("FCVTL2 V31.2D, V30.4S\n"), {0xdf, 0x7b, 0x61, 0x4e}},
    {S8_INITIALIZER("FCVTN V31.4H, V30.4S\n"), {0xdf, 0x6b, 0x21, 0x0e}},
    {S8_INITIALIZER("FCVTN2 V31.8H, V30.4S\n"), {0xdf, 0x6b, 0x21, 0x4e}},
    {S8_INITIALIZER("FCVTN V31.2S, V30.2D\n"), {0xdf, 0x6b, 0x61, 0x0e}},
    {S8_INITIALIZER("FCVTN2 V31.4S, V30.2D\n"), {0xdf, 0x6b, 0x61, 0x4e}},
};

static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_fcvt_suffix_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:FCVTL_asimdmisc_L"), UINT64_C(0x3605be7c9c5b5cdf), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID},
     BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT, 0},
    {S8_INITIALIZER("arm-a64@2026-06:FCVTL_asimdmisc_L"), UINT64_C(0x3605be7c9c5b5cdf), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID},
     BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT, 1},
    {S8_INITIALIZER("arm-a64@2026-06:FCVTN_asimdmisc_N"), UINT64_C(0x3fe51307652789ca), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID},
     BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT, 0},
    {S8_INITIALIZER("arm-a64@2026-06:FCVTN_asimdmisc_N"), UINT64_C(0x3fe51307652789ca), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID},
     BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT, 1},
};

/* SHA-1 AdvSIMD spellings are selected by the generated rows' HasSHA2
 * predicate in LLVM's AArch64 decoder.  These bytes come independently from
 * llvm-mc 22.1.8; the direct table deliberately reuses the existing SHA2
 * requirement mask rather than introducing a SHA1-only target feature. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_sha1_cases[] = {
    {S8_INITIALIZER("sha1c q0, s1, v2.4s\n"), {0x20, 0x00, 0x02, 0x5e}},
    {S8_INITIALIZER("sha1h s0, s1\n"), {0x20, 0x08, 0x28, 0x5e}},
    {S8_INITIALIZER("sha1m q0, s1, v2.4s\n"), {0x20, 0x20, 0x02, 0x5e}},
    {S8_INITIALIZER("sha1p q0, s1, v2.4s\n"), {0x20, 0x10, 0x02, 0x5e}},
    {S8_INITIALIZER("sha1su0 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x30, 0x02, 0x5e}},
    {S8_INITIALIZER("sha1su1 v0.4s, v1.4s\n"), {0x20, 0x18, 0x28, 0x5e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_sha1_boundary_cases[] = {
    {S8_INITIALIZER("SHA1C Q31, S30, V29.4S\n"), {0xdf, 0x03, 0x1d, 0x5e}},
    {S8_INITIALIZER("SHA1H S31, S30\n"), {0xdf, 0x0b, 0x28, 0x5e}},
    {S8_INITIALIZER("SHA1M Q31, S30, V29.4S\n"), {0xdf, 0x23, 0x1d, 0x5e}},
    {S8_INITIALIZER("SHA1P Q31, S30, V29.4S\n"), {0xdf, 0x13, 0x1d, 0x5e}},
    {S8_INITIALIZER("SHA1SU0 V31.4S, V30.4S, V29.4S\n"), {0xdf, 0x33, 0x1d, 0x5e}},
    {S8_INITIALIZER("SHA1SU1 V31.4S, V30.4S\n"), {0xdf, 0x1b, 0x28, 0x5e}},
};

static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_sha1_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SHA1C_QSV_cryptosha3"), UINT64_C(0xc8bf61c1fbc4c68b), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SHA1H_SS_cryptosha2"), UINT64_C(0x527117bcf01c589e), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SHA1M_QSV_cryptosha3"), UINT64_C(0xfe21fb23ee0af9be), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SHA1P_QSV_cryptosha3"), UINT64_C(0x6df8cac602cb80f6), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SHA1SU0_VVV_cryptosha3"), UINT64_C(0xe03e17b03ab0e566), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SHA1SU1_VV_cryptosha2"), UINT64_C(0x91788958671acea8), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

/* TBL/TBX expose a brace-delimited table list and a byte-vector index.  The
 * 16 low-register forms cover both Q-selected destination/index arrangements
 * for every list length; bytes are independent llvm-mc 22.1.8 encodings. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_tbl_tbx_cases[] = {
    {S8_INITIALIZER("tbl v0.8b, {v1.16b}, v9.8b\n"), {0x20, 0x00, 0x09, 0x0e}},
    {S8_INITIALIZER("tbl v0.16b, {v1.16b}, v9.16b\n"), {0x20, 0x00, 0x09, 0x4e}},
    {S8_INITIALIZER("tbl v0.8b, {v1.16b, v2.16b}, v9.8b\n"), {0x20, 0x20, 0x09, 0x0e}},
    {S8_INITIALIZER("tbl v0.16b, {v1.16b, v2.16b}, v9.16b\n"), {0x20, 0x20, 0x09, 0x4e}},
    {S8_INITIALIZER("tbl v0.8b, {v1.16b, v2.16b, v3.16b}, v9.8b\n"), {0x20, 0x40, 0x09, 0x0e}},
    {S8_INITIALIZER("tbl v0.16b, {v1.16b, v2.16b, v3.16b}, v9.16b\n"), {0x20, 0x40, 0x09, 0x4e}},
    {S8_INITIALIZER("tbl v0.8b, {v1.16b, v2.16b, v3.16b, v4.16b}, v9.8b\n"), {0x20, 0x60, 0x09, 0x0e}},
    {S8_INITIALIZER("tbl v0.16b, {v1.16b, v2.16b, v3.16b, v4.16b}, v9.16b\n"), {0x20, 0x60, 0x09, 0x4e}},
    {S8_INITIALIZER("tbx v0.8b, {v1.16b}, v9.8b\n"), {0x20, 0x10, 0x09, 0x0e}},
    {S8_INITIALIZER("tbx v0.16b, {v1.16b}, v9.16b\n"), {0x20, 0x10, 0x09, 0x4e}},
    {S8_INITIALIZER("tbx v0.8b, {v1.16b, v2.16b}, v9.8b\n"), {0x20, 0x30, 0x09, 0x0e}},
    {S8_INITIALIZER("tbx v0.16b, {v1.16b, v2.16b}, v9.16b\n"), {0x20, 0x30, 0x09, 0x4e}},
    {S8_INITIALIZER("tbx v0.8b, {v1.16b, v2.16b, v3.16b}, v9.8b\n"), {0x20, 0x50, 0x09, 0x0e}},
    {S8_INITIALIZER("tbx v0.16b, {v1.16b, v2.16b, v3.16b}, v9.16b\n"), {0x20, 0x50, 0x09, 0x4e}},
    {S8_INITIALIZER("tbx v0.8b, {v1.16b, v2.16b, v3.16b, v4.16b}, v9.8b\n"), {0x20, 0x70, 0x09, 0x0e}},
    {S8_INITIALIZER("tbx v0.16b, {v1.16b, v2.16b, v3.16b, v4.16b}, v9.16b\n"), {0x20, 0x70, 0x09, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_tbl_tbx_boundary_cases[] = {
    {S8_INITIALIZER("TBL V31.16B, {V28.16B}, V27.16B\n"), {0x9f, 0x03, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBL V31.16B, {V28.16B, V29.16B}, V27.16B\n"), {0x9f, 0x23, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBL V31.16B, {V28.16B, V29.16B, V30.16B}, V27.16B\n"), {0x9f, 0x43, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBL V31.16B, {V28.16B, V29.16B, V30.16B, V31.16B}, V27.16B\n"), {0x9f, 0x63, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBX V31.16B, {V28.16B}, V27.16B\n"), {0x9f, 0x13, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBX V31.16B, {V28.16B, V29.16B}, V27.16B\n"), {0x9f, 0x33, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBX V31.16B, {V28.16B, V29.16B, V30.16B}, V27.16B\n"), {0x9f, 0x53, 0x1b, 0x4e}},
    {S8_INITIALIZER("TBX V31.16B, {V28.16B, V29.16B, V30.16B, V31.16B}, V27.16B\n"), {0x9f, 0x73, 0x1b, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_tbl_tbx_wrap_cases[] = {
    {S8_INITIALIZER("tbl v0.16b, {v31.16b, v0.16b}, v1.16b\n"), {0xe0, 0x23, 0x01, 0x4e}},
    {S8_INITIALIZER("tbl v0.16b, {v30.16b, v31.16b, v0.16b}, v1.16b\n"), {0xc0, 0x43, 0x01, 0x4e}},
    {S8_INITIALIZER("tbx v0.16b, {v29.16b, v30.16b, v31.16b, v0.16b}, v1.16b\n"), {0xa0, 0x73, 0x01, 0x4e}},
};

static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_tbl_tbx_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:TBL_asimdtbl_L1_1"), UINT64_C(0x7217285bada5df77), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBL_asimdtbl_L2_2"), UINT64_C(0xb78a5bd0b06eeb02), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBL_asimdtbl_L3_3"), UINT64_C(0x1d93f8d9ed2b6dd8), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBL_asimdtbl_L4_4"), UINT64_C(0x0f24e509b0a9d450), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBX_asimdtbl_L1_1"), UINT64_C(0x804d68c24266e3c4), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBX_asimdtbl_L2_2"), UINT64_C(0xe298fbaad2ea6fd7), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBX_asimdtbl_L3_3"), UINT64_C(0x9f7c68cc37209c0f), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:TBX_asimdtbl_L4_4"), UINT64_C(0x2050b49463e72e5a), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

/* Final canonical direct-SIMD rows.  DUP exercises the mixed vector/GPR
 * transform path; NOT and ORR remain canonical spellings even though LLVM's
 * disassembler prefers the MVN/MOV aliases for some encodings. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_final3_cases[] = {
    {S8_INITIALIZER("dup v0.8b, w1\n"), {0x20, 0x0c, 0x01, 0x0e}},
    {S8_INITIALIZER("dup v0.16b, w1\n"), {0x20, 0x0c, 0x01, 0x4e}},
    {S8_INITIALIZER("dup v0.4h, w1\n"), {0x20, 0x0c, 0x02, 0x0e}},
    {S8_INITIALIZER("dup v0.8h, w1\n"), {0x20, 0x0c, 0x02, 0x4e}},
    {S8_INITIALIZER("dup v0.2s, w1\n"), {0x20, 0x0c, 0x04, 0x0e}},
    {S8_INITIALIZER("dup v0.4s, w1\n"), {0x20, 0x0c, 0x04, 0x4e}},
    {S8_INITIALIZER("dup v0.2d, x1\n"), {0x20, 0x0c, 0x08, 0x4e}},
    {S8_INITIALIZER("not v0.8b, v1.8b\n"), {0x20, 0x58, 0x20, 0x2e}},
    {S8_INITIALIZER("not v0.16b, v1.16b\n"), {0x20, 0x58, 0x20, 0x6e}},
    {S8_INITIALIZER("orr v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0xa2, 0x0e}},
    {S8_INITIALIZER("orr v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0xa2, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_final3_boundary_cases[] = {
    {S8_INITIALIZER("DUP V31.2D, XZR\n"), {0xff, 0x0f, 0x08, 0x4e}},
    {S8_INITIALIZER("NOT V31.16B, V30.16B\n"), {0xdf, 0x5b, 0x20, 0x6e}},
    {S8_INITIALIZER("ORR V31.16B, V30.16B, V29.16B\n"), {0xdf, 0x1f, 0xbd, 0x4e}},
};

static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_final3_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:DUP_asimdins_DR_r"), UINT64_C(0xe6799413838aec2f), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:NOT_asimdmisc_R"), UINT64_C(0xb4fad81197ef56dd), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:ORR_asimdsame_only"), UINT64_C(0xc6e29afad4e09fb3), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

/* FHM AdvSIMD rows require LLVM's HasNEON && HasFP16FML predicate.  The
 * compact direct-SIMD requirement additionally relies on target validation to
 * reject FP16FML without its FULLFP16/NEON dependencies. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_fhm_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:FMLAL2_asimdsame_F"), UINT64_C(0x7cd436e3f3d6fb58), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FMLAL_asimdsame_F"), UINT64_C(0x424770611f6d4571), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FMLSL2_asimdsame_F"), UINT64_C(0x3a930821d594548d), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FMLSL_asimdsame_F"), UINT64_C(0x7c64660496445a9d), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

/* Independent llvm-mc 22.1.8 encodings (little-endian bytes), exhaustive for
 * the 2S/4S destination arrangements exposed by each FHM row. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fhm_cases[] = {
    {S8_INITIALIZER("fmlal2 v0.2s, v1.2h, v2.2h\n"), {0x20, 0xcc, 0x22, 0x2e}},
    {S8_INITIALIZER("fmlal2 v0.4s, v1.4h, v2.4h\n"), {0x20, 0xcc, 0x22, 0x6e}},
    {S8_INITIALIZER("fmlal v0.2s, v1.2h, v2.2h\n"), {0x20, 0xec, 0x22, 0x0e}},
    {S8_INITIALIZER("fmlal v0.4s, v1.4h, v2.4h\n"), {0x20, 0xec, 0x22, 0x4e}},
    {S8_INITIALIZER("fmlsl2 v0.2s, v1.2h, v2.2h\n"), {0x20, 0xcc, 0xa2, 0x2e}},
    {S8_INITIALIZER("fmlsl2 v0.4s, v1.4h, v2.4h\n"), {0x20, 0xcc, 0xa2, 0x6e}},
    {S8_INITIALIZER("fmlsl v0.2s, v1.2h, v2.2h\n"), {0x20, 0xec, 0xa2, 0x0e}},
    {S8_INITIALIZER("fmlsl v0.4s, v1.4h, v2.4h\n"), {0x20, 0xec, 0xa2, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fhm_boundary_cases[] = {
    {S8_INITIALIZER("FMLAL2 V31.4S, V30.4H, V29.4H\n"), {0xdf, 0xcf, 0x3d, 0x6e}},
    {S8_INITIALIZER("FMLAL V31.4S, V30.4H, V29.4H\n"), {0xdf, 0xef, 0x3d, 0x4e}},
    {S8_INITIALIZER("FMLSL2 V31.4S, V30.4H, V29.4H\n"), {0xdf, 0xcf, 0xbd, 0x6e}},
    {S8_INITIALIZER("FMLSL V31.4S, V30.4H, V29.4H\n"), {0xdf, 0xef, 0xbd, 0x4e}},
};

/* FRINT32X/Z and FRINT64X/Z AdvSIMD rows require LLVM's HasFRInt3264
 * predicate, which maps to the local FPTOINT target feature.  These are the
 * complete 2S/4S/2D selector set for each row, encoded independently with
 * llvm-mc 22.1.8. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_frintts_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:FRINT32X_asimdmisc_R"), UINT64_C(0x418e0ab118cb28ba), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FRINT32Z_asimdmisc_R"), UINT64_C(0x8dede1d564313c59), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FRINT64X_asimdmisc_R"), UINT64_C(0x956ff9b673405173), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FRINT64Z_asimdmisc_R"), UINT64_C(0x89795480cfdf8cbf), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_frintts_cases[] = {
    {S8_INITIALIZER("frint32x v0.2s, v1.2s\n"), {0x20, 0xe8, 0x21, 0x2e}},
    {S8_INITIALIZER("frint32x v0.4s, v1.4s\n"), {0x20, 0xe8, 0x21, 0x6e}},
    {S8_INITIALIZER("frint32x v0.2d, v1.2d\n"), {0x20, 0xe8, 0x61, 0x6e}},
    {S8_INITIALIZER("frint32z v0.2s, v1.2s\n"), {0x20, 0xe8, 0x21, 0x0e}},
    {S8_INITIALIZER("frint32z v0.4s, v1.4s\n"), {0x20, 0xe8, 0x21, 0x4e}},
    {S8_INITIALIZER("frint32z v0.2d, v1.2d\n"), {0x20, 0xe8, 0x61, 0x4e}},
    {S8_INITIALIZER("frint64x v0.2s, v1.2s\n"), {0x20, 0xf8, 0x21, 0x2e}},
    {S8_INITIALIZER("frint64x v0.4s, v1.4s\n"), {0x20, 0xf8, 0x21, 0x6e}},
    {S8_INITIALIZER("frint64x v0.2d, v1.2d\n"), {0x20, 0xf8, 0x61, 0x6e}},
    {S8_INITIALIZER("frint64z v0.2s, v1.2s\n"), {0x20, 0xf8, 0x21, 0x0e}},
    {S8_INITIALIZER("frint64z v0.4s, v1.4s\n"), {0x20, 0xf8, 0x21, 0x4e}},
    {S8_INITIALIZER("frint64z v0.2d, v1.2d\n"), {0x20, 0xf8, 0x61, 0x4e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_frintts_boundary_cases[] = {
    {S8_INITIALIZER("FRINT32X V31.2D, V30.2D\n"), {0xdf, 0xeb, 0x61, 0x6e}},
    {S8_INITIALIZER("FRINT32Z V31.2D, V30.2D\n"), {0xdf, 0xeb, 0x61, 0x4e}},
    {S8_INITIALIZER("FRINT64X V31.2D, V30.2D\n"), {0xdf, 0xfb, 0x61, 0x6e}},
    {S8_INITIALIZER("FRINT64Z V31.2D, V30.2D\n"), {0xdf, 0xfb, 0x61, 0x4e}},
};

/* SDOT/UDOT AdvSIMD rows require LLVM's HasDotProd predicate.  These bytes
 * are independent llvm-mc 22.1.8 encodings and cover both legal destination
 * and source arrangements for each direct row. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_dotprod_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SDOT_asimdsame2_D"), UINT64_C(0x1aab66d054562284), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:UDOT_asimdsame2_D"), UINT64_C(0xd40495bc9d9aacdf), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_dotprod_cases[] = {
    {S8_INITIALIZER("sdot v0.2s, v1.8b, v2.8b\n"), {0x20, 0x94, 0x82, 0x0e}},
    {S8_INITIALIZER("sdot v0.4s, v1.16b, v2.16b\n"), {0x20, 0x94, 0x82, 0x4e}},
    {S8_INITIALIZER("udot v0.2s, v1.8b, v2.8b\n"), {0x20, 0x94, 0x82, 0x2e}},
    {S8_INITIALIZER("udot v0.4s, v1.16b, v2.16b\n"), {0x20, 0x94, 0x82, 0x6e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_dotprod_boundary_cases[] = {
    {S8_INITIALIZER("SDOT V31.4S, V30.16B, V29.16B\n"), {0xdf, 0x97, 0x9d, 0x4e}},
    {S8_INITIALIZER("UDOT V31.4S, V30.16B, V29.16B\n"), {0xdf, 0x97, 0x9d, 0x6e}},
};

/* SQRDMLAH/SQRDMLSH AdvSIMD rows require LLVM's HasRDM predicate.  The
 * direct spellings expose all four legal same-shape arrangements (4H/8H and
 * 2S/4S); these bytes are independent llvm-mc 22.1.8 encodings. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_rdm_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SQRDMLAH_asimdsame2_only"), UINT64_C(0x3557225a108dda38), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SQRDMLSH_asimdsame2_only"), UINT64_C(0x03b0c5137f14528a), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_rdm_scalar_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SQRDMLAH_asisdsame2_only"), UINT64_C(0x26cc7e6837d3c6f), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SQRDMLSH_asisdsame2_only"), UINT64_C(0x8f3f8e995448cec5), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_rdm_cases[] = {
    {S8_INITIALIZER("sqrdmlah v0.4h, v1.4h, v2.4h\n"), {0x20, 0x84, 0x42, 0x2e}},
    {S8_INITIALIZER("sqrdmlah v0.8h, v1.8h, v2.8h\n"), {0x20, 0x84, 0x42, 0x6e}},
    {S8_INITIALIZER("sqrdmlah v0.2s, v1.2s, v2.2s\n"), {0x20, 0x84, 0x82, 0x2e}},
    {S8_INITIALIZER("sqrdmlah v0.4s, v1.4s, v2.4s\n"), {0x20, 0x84, 0x82, 0x6e}},
    {S8_INITIALIZER("sqrdmlsh v0.4h, v1.4h, v2.4h\n"), {0x20, 0x8c, 0x42, 0x2e}},
    {S8_INITIALIZER("sqrdmlsh v0.8h, v1.8h, v2.8h\n"), {0x20, 0x8c, 0x42, 0x6e}},
    {S8_INITIALIZER("sqrdmlsh v0.2s, v1.2s, v2.2s\n"), {0x20, 0x8c, 0x82, 0x2e}},
    {S8_INITIALIZER("sqrdmlsh v0.4s, v1.4s, v2.4s\n"), {0x20, 0x8c, 0x82, 0x6e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_rdm_boundary_cases[] = {
    {S8_INITIALIZER("SQRDMLAH V31.4H, V30.4H, V29.4H\n"), {0xdf, 0x87, 0x5d, 0x2e}},
    {S8_INITIALIZER("SQRDMLAH V31.8H, V30.8H, V29.8H\n"), {0xdf, 0x87, 0x5d, 0x6e}},
    {S8_INITIALIZER("SQRDMLAH V31.2S, V30.2S, V29.2S\n"), {0xdf, 0x87, 0x9d, 0x2e}},
    {S8_INITIALIZER("SQRDMLAH V31.4S, V30.4S, V29.4S\n"), {0xdf, 0x87, 0x9d, 0x6e}},
    {S8_INITIALIZER("SQRDMLSH V31.4H, V30.4H, V29.4H\n"), {0xdf, 0x8f, 0x5d, 0x2e}},
    {S8_INITIALIZER("SQRDMLSH V31.8H, V30.8H, V29.8H\n"), {0xdf, 0x8f, 0x5d, 0x6e}},
    {S8_INITIALIZER("SQRDMLSH V31.2S, V30.2S, V29.2S\n"), {0xdf, 0x8f, 0x9d, 0x2e}},
    {S8_INITIALIZER("SQRDMLSH V31.4S, V30.4S, V29.4S\n"), {0xdf, 0x8f, 0x9d, 0x6e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_rdm_scalar_cases[] = {
    {S8_INITIALIZER("sqrdmlah h0, h1, h2\n"), {0x20, 0x84, 0x42, 0x7e}},
    {S8_INITIALIZER("sqrdmlah s0, s1, s2\n"), {0x20, 0x84, 0x82, 0x7e}},
    {S8_INITIALIZER("sqrdmlsh h0, h1, h2\n"), {0x20, 0x8c, 0x42, 0x7e}},
    {S8_INITIALIZER("sqrdmlsh s0, s1, s2\n"), {0x20, 0x8c, 0x82, 0x7e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_rdm_scalar_boundary_cases[] = {
    {S8_INITIALIZER("SQRDMLAH H31, H30, H29\n"), {0xdf, 0x87, 0x5d, 0x7e}},
    {S8_INITIALIZER("SQRDMLAH S31, S30, S29\n"), {0xdf, 0x87, 0x9d, 0x7e}},
    {S8_INITIALIZER("SQRDMLSH H31, H30, H29\n"), {0xdf, 0x8f, 0x5d, 0x7e}},
    {S8_INITIALIZER("SQRDMLSH S31, S30, S29\n"), {0xdf, 0x8f, 0x9d, 0x7e}},
};

/* Scalar S/D AdvSIMD rows use the shared arrangement+width-selector grammar.
 * These bytes are independent llvm-mc 22.1.8 encodings and cover both legal
 * selectors for every row. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_scalar_selector_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:FRECPE_asisdmisc_R"), UINT64_C(0x374ec904f3062ae6), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FRECPX_asisdmisc_R"), UINT64_C(0x8562c49063307536), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:FRSQRTE_asisdmisc_R"), UINT64_C(0xa8946616cf3a7d68), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SCVTF_asisdmisc_R"), UINT64_C(0x8fbf3a3a185ff928), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:UCVTF_asisdmisc_R"), UINT64_C(0x8a8e6b7c5213fb44), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_selector_cases[] = {
    {S8_INITIALIZER("frecpe s0, s1\n"), {0x20, 0xd8, 0xa1, 0x5e}},
    {S8_INITIALIZER("frecpe d0, d1\n"), {0x20, 0xd8, 0xe1, 0x5e}},
    {S8_INITIALIZER("frecpx s0, s1\n"), {0x20, 0xf8, 0xa1, 0x5e}},
    {S8_INITIALIZER("frecpx d0, d1\n"), {0x20, 0xf8, 0xe1, 0x5e}},
    {S8_INITIALIZER("frsqrte s0, s1\n"), {0x20, 0xd8, 0xa1, 0x7e}},
    {S8_INITIALIZER("frsqrte d0, d1\n"), {0x20, 0xd8, 0xe1, 0x7e}},
    {S8_INITIALIZER("scvtf s0, s1\n"), {0x20, 0xd8, 0x21, 0x5e}},
    {S8_INITIALIZER("scvtf d0, d1\n"), {0x20, 0xd8, 0x61, 0x5e}},
    {S8_INITIALIZER("ucvtf s0, s1\n"), {0x20, 0xd8, 0x21, 0x7e}},
    {S8_INITIALIZER("ucvtf d0, d1\n"), {0x20, 0xd8, 0x61, 0x7e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_selector_boundary_cases[] = {
    {S8_INITIALIZER("FRECPE S31, S30\n"), {0xdf, 0xdb, 0xa1, 0x5e}},
    {S8_INITIALIZER("FRECPE D31, D30\n"), {0xdf, 0xdb, 0xe1, 0x5e}},
    {S8_INITIALIZER("FRECPX S31, S30\n"), {0xdf, 0xfb, 0xa1, 0x5e}},
    {S8_INITIALIZER("FRECPX D31, D30\n"), {0xdf, 0xfb, 0xe1, 0x5e}},
    {S8_INITIALIZER("FRSQRTE S31, S30\n"), {0xdf, 0xdb, 0xa1, 0x7e}},
    {S8_INITIALIZER("FRSQRTE D31, D30\n"), {0xdf, 0xdb, 0xe1, 0x7e}},
    {S8_INITIALIZER("SCVTF S31, S30\n"), {0xdf, 0xdb, 0x21, 0x5e}},
    {S8_INITIALIZER("SCVTF D31, D30\n"), {0xdf, 0xdb, 0x61, 0x5e}},
    {S8_INITIALIZER("UCVTF S31, S30\n"), {0xdf, 0xdb, 0x21, 0x7e}},
    {S8_INITIALIZER("UCVTF D31, D30\n"), {0xdf, 0xdb, 0x61, 0x7e}},
};

/* Scalar narrowing rows use the generated size relation B/H, H/S, S/D.
 * These IDs, digests, and bytes are pinned independently to the canonical
 * metadata and llvm-mc 22.1.8. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_scalar_narrow_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SQXTN_asisdmisc_N"), UINT64_C(0xaba6357972b5fd03), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SQXTUN_asisdmisc_N"), UINT64_C(0x947ecbd12b6c0cf8), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:UQXTN_asisdmisc_N"), UINT64_C(0xe02de395147ccab2), 2,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_narrow_cases[] = {
    {S8_INITIALIZER("sqxtn b0, h1\n"), {0x20, 0x48, 0x21, 0x5e}},
    {S8_INITIALIZER("sqxtn h0, s1\n"), {0x20, 0x48, 0x61, 0x5e}},
    {S8_INITIALIZER("sqxtn s0, d1\n"), {0x20, 0x48, 0xa1, 0x5e}},
    {S8_INITIALIZER("sqxtun b0, h1\n"), {0x20, 0x28, 0x21, 0x7e}},
    {S8_INITIALIZER("sqxtun h0, s1\n"), {0x20, 0x28, 0x61, 0x7e}},
    {S8_INITIALIZER("sqxtun s0, d1\n"), {0x20, 0x28, 0xa1, 0x7e}},
    {S8_INITIALIZER("uqxtn b0, h1\n"), {0x20, 0x48, 0x21, 0x7e}},
    {S8_INITIALIZER("uqxtn h0, s1\n"), {0x20, 0x48, 0x61, 0x7e}},
    {S8_INITIALIZER("uqxtn s0, d1\n"), {0x20, 0x48, 0xa1, 0x7e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_narrow_boundary_cases[] = {
    {S8_INITIALIZER("SQXTN B31, H30\n"), {0xdf, 0x4b, 0x21, 0x5e}},
    {S8_INITIALIZER("SQXTN H31, S30\n"), {0xdf, 0x4b, 0x61, 0x5e}},
    {S8_INITIALIZER("SQXTN S31, D30\n"), {0xdf, 0x4b, 0xa1, 0x5e}},
    {S8_INITIALIZER("SQXTUN B31, H30\n"), {0xdf, 0x2b, 0x21, 0x7e}},
    {S8_INITIALIZER("SQXTUN H31, S30\n"), {0xdf, 0x2b, 0x61, 0x7e}},
    {S8_INITIALIZER("SQXTUN S31, D30\n"), {0xdf, 0x2b, 0xa1, 0x7e}},
    {S8_INITIALIZER("UQXTN B31, H30\n"), {0xdf, 0x4b, 0x21, 0x7e}},
    {S8_INITIALIZER("UQXTN H31, S30\n"), {0xdf, 0x4b, 0x61, 0x7e}},
    {S8_INITIALIZER("UQXTN S31, D30\n"), {0xdf, 0x4b, 0xa1, 0x7e}},
};

/* Scalar widening multiply-accumulate rows use the generated size relation
 * S <- H,H and D <- S,S.  IDs, digests, and bytes are pinned independently
 * to the canonical metadata and llvm-mc 22.1.8. */
static AssemblyA64DirectSIMDSpellingExpectation const assembly_a64_direct_simd_scalar_widen_spellings[] = {
    {S8_INITIALIZER("arm-a64@2026-06:SQDMLAL_asisddiff_only"), UINT64_C(0xa880474b2d693e03), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SQDMLSL_asisddiff_only"), UINT64_C(0xf39ad3663eebb936), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
    {S8_INITIALIZER("arm-a64@2026-06:SQDMULL_asisddiff_only"), UINT64_C(0x5eaf349cfe4c58fa), 3,
     {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
      BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_widen_cases[] = {
    {S8_INITIALIZER("sqdmlal s0, h1, h2\n"), {0x20, 0x90, 0x62, 0x5e}},
    {S8_INITIALIZER("sqdmlal d0, s1, s2\n"), {0x20, 0x90, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqdmlsl s0, h1, h2\n"), {0x20, 0xb0, 0x62, 0x5e}},
    {S8_INITIALIZER("sqdmlsl d0, s1, s2\n"), {0x20, 0xb0, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqdmull s0, h1, h2\n"), {0x20, 0xd0, 0x62, 0x5e}},
    {S8_INITIALIZER("sqdmull d0, s1, s2\n"), {0x20, 0xd0, 0xa2, 0x5e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_widen_boundary_cases[] = {
    {S8_INITIALIZER("SQDMLAL S31, H30, H29\n"), {0xdf, 0x93, 0x7d, 0x5e}},
    {S8_INITIALIZER("SQDMLAL D31, S30, S29\n"), {0xdf, 0x93, 0xbd, 0x5e}},
    {S8_INITIALIZER("SQDMLSL S31, H30, H29\n"), {0xdf, 0xb3, 0x7d, 0x5e}},
    {S8_INITIALIZER("SQDMLSL D31, S30, S29\n"), {0xdf, 0xb3, 0xbd, 0x5e}},
    {S8_INITIALIZER("SQDMULL S31, H30, H29\n"), {0xdf, 0xd3, 0x7d, 0x5e}},
    {S8_INITIALIZER("SQDMULL D31, S30, S29\n"), {0xdf, 0xd3, 0xbd, 0x5e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fp16_cases[] = {
    {S8_INITIALIZER("fabd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0xc2, 0x2e}},
    {S8_INITIALIZER("fabd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0xc2, 0x6e}},
    {S8_INITIALIZER("fabd h0, h1, h2\n"), {0x20, 0x14, 0xc2, 0x7e}},
    {S8_INITIALIZER("fabs v0.4h, v1.4h\n"), {0x20, 0xf8, 0xf8, 0x0e}},
    {S8_INITIALIZER("fabs v0.8h, v1.8h\n"), {0x20, 0xf8, 0xf8, 0x4e}},
    {S8_INITIALIZER("facge v0.4h, v1.4h, v2.4h\n"), {0x20, 0x2c, 0x42, 0x2e}},
    {S8_INITIALIZER("facge v0.8h, v1.8h, v2.8h\n"), {0x20, 0x2c, 0x42, 0x6e}},
    {S8_INITIALIZER("facge h0, h1, h2\n"), {0x20, 0x2c, 0x42, 0x7e}},
    {S8_INITIALIZER("facgt v0.4h, v1.4h, v2.4h\n"), {0x20, 0x2c, 0xc2, 0x2e}},
    {S8_INITIALIZER("facgt v0.8h, v1.8h, v2.8h\n"), {0x20, 0x2c, 0xc2, 0x6e}},
    {S8_INITIALIZER("facgt h0, h1, h2\n"), {0x20, 0x2c, 0xc2, 0x7e}},
    {S8_INITIALIZER("faddp v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0x42, 0x2e}},
    {S8_INITIALIZER("faddp v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0x42, 0x6e}},
    {S8_INITIALIZER("faddp h0, v1.2h\n"), {0x20, 0xd8, 0x30, 0x5e}},
    {S8_INITIALIZER("fadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0x42, 0x0e}},
    {S8_INITIALIZER("fadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0x42, 0x4e}},
    {S8_INITIALIZER("fcmeq v0.4h, v1.4h, v2.4h\n"), {0x20, 0x24, 0x42, 0x0e}},
    {S8_INITIALIZER("fcmeq v0.8h, v1.8h, v2.8h\n"), {0x20, 0x24, 0x42, 0x4e}},
    {S8_INITIALIZER("fcmeq h0, h1, h2\n"), {0x20, 0x24, 0x42, 0x5e}},
    {S8_INITIALIZER("fcmge v0.4h, v1.4h, v2.4h\n"), {0x20, 0x24, 0x42, 0x2e}},
    {S8_INITIALIZER("fcmge v0.8h, v1.8h, v2.8h\n"), {0x20, 0x24, 0x42, 0x6e}},
    {S8_INITIALIZER("fcmge h0, h1, h2\n"), {0x20, 0x24, 0x42, 0x7e}},
    {S8_INITIALIZER("fcmgt v0.4h, v1.4h, v2.4h\n"), {0x20, 0x24, 0xc2, 0x2e}},
    {S8_INITIALIZER("fcmgt v0.8h, v1.8h, v2.8h\n"), {0x20, 0x24, 0xc2, 0x6e}},
    {S8_INITIALIZER("fcmgt h0, h1, h2\n"), {0x20, 0x24, 0xc2, 0x7e}},
    {S8_INITIALIZER("fcvtas v0.4h, v1.4h\n"), {0x20, 0xc8, 0x79, 0x0e}},
    {S8_INITIALIZER("fcvtas v0.8h, v1.8h\n"), {0x20, 0xc8, 0x79, 0x4e}},
    {S8_INITIALIZER("fcvtas h0, h1\n"), {0x20, 0xc8, 0x79, 0x5e}},
    {S8_INITIALIZER("fcvtau v0.4h, v1.4h\n"), {0x20, 0xc8, 0x79, 0x2e}},
    {S8_INITIALIZER("fcvtau v0.8h, v1.8h\n"), {0x20, 0xc8, 0x79, 0x6e}},
    {S8_INITIALIZER("fcvtau h0, h1\n"), {0x20, 0xc8, 0x79, 0x7e}},
    {S8_INITIALIZER("fcvtms v0.4h, v1.4h\n"), {0x20, 0xb8, 0x79, 0x0e}},
    {S8_INITIALIZER("fcvtms v0.8h, v1.8h\n"), {0x20, 0xb8, 0x79, 0x4e}},
    {S8_INITIALIZER("fcvtms h0, h1\n"), {0x20, 0xb8, 0x79, 0x5e}},
    {S8_INITIALIZER("fcvtmu v0.4h, v1.4h\n"), {0x20, 0xb8, 0x79, 0x2e}},
    {S8_INITIALIZER("fcvtmu v0.8h, v1.8h\n"), {0x20, 0xb8, 0x79, 0x6e}},
    {S8_INITIALIZER("fcvtmu h0, h1\n"), {0x20, 0xb8, 0x79, 0x7e}},
    {S8_INITIALIZER("fcvtns v0.4h, v1.4h\n"), {0x20, 0xa8, 0x79, 0x0e}},
    {S8_INITIALIZER("fcvtns v0.8h, v1.8h\n"), {0x20, 0xa8, 0x79, 0x4e}},
    {S8_INITIALIZER("fcvtns h0, h1\n"), {0x20, 0xa8, 0x79, 0x5e}},
    {S8_INITIALIZER("fcvtnu v0.4h, v1.4h\n"), {0x20, 0xa8, 0x79, 0x2e}},
    {S8_INITIALIZER("fcvtnu v0.8h, v1.8h\n"), {0x20, 0xa8, 0x79, 0x6e}},
    {S8_INITIALIZER("fcvtnu h0, h1\n"), {0x20, 0xa8, 0x79, 0x7e}},
    {S8_INITIALIZER("fcvtps v0.4h, v1.4h\n"), {0x20, 0xa8, 0xf9, 0x0e}},
    {S8_INITIALIZER("fcvtps v0.8h, v1.8h\n"), {0x20, 0xa8, 0xf9, 0x4e}},
    {S8_INITIALIZER("fcvtps h0, h1\n"), {0x20, 0xa8, 0xf9, 0x5e}},
    {S8_INITIALIZER("fcvtpu v0.4h, v1.4h\n"), {0x20, 0xa8, 0xf9, 0x2e}},
    {S8_INITIALIZER("fcvtpu v0.8h, v1.8h\n"), {0x20, 0xa8, 0xf9, 0x6e}},
    {S8_INITIALIZER("fcvtpu h0, h1\n"), {0x20, 0xa8, 0xf9, 0x7e}},
    {S8_INITIALIZER("fcvtzs v0.4h, v1.4h\n"), {0x20, 0xb8, 0xf9, 0x0e}},
    {S8_INITIALIZER("fcvtzs v0.8h, v1.8h\n"), {0x20, 0xb8, 0xf9, 0x4e}},
    {S8_INITIALIZER("fcvtzs h0, h1\n"), {0x20, 0xb8, 0xf9, 0x5e}},
    {S8_INITIALIZER("fcvtzu v0.4h, v1.4h\n"), {0x20, 0xb8, 0xf9, 0x2e}},
    {S8_INITIALIZER("fcvtzu v0.8h, v1.8h\n"), {0x20, 0xb8, 0xf9, 0x6e}},
    {S8_INITIALIZER("fcvtzu h0, h1\n"), {0x20, 0xb8, 0xf9, 0x7e}},
    {S8_INITIALIZER("fdiv v0.4h, v1.4h, v2.4h\n"), {0x20, 0x3c, 0x42, 0x2e}},
    {S8_INITIALIZER("fdiv v0.8h, v1.8h, v2.8h\n"), {0x20, 0x3c, 0x42, 0x6e}},
    {S8_INITIALIZER("fmaxnmp v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0x42, 0x2e}},
    {S8_INITIALIZER("fmaxnmp v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0x42, 0x6e}},
    {S8_INITIALIZER("fmaxnmp h0, v1.2h\n"), {0x20, 0xc8, 0x30, 0x5e}},
    {S8_INITIALIZER("fmaxnmv h0, v1.4h\n"), {0x20, 0xc8, 0x30, 0x0e}},
    {S8_INITIALIZER("fmaxnmv h0, v1.8h\n"), {0x20, 0xc8, 0x30, 0x4e}},
    {S8_INITIALIZER("fmaxnm v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0x42, 0x0e}},
    {S8_INITIALIZER("fmaxnm v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0x42, 0x4e}},
    {S8_INITIALIZER("fmaxp v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0x42, 0x2e}},
    {S8_INITIALIZER("fmaxp v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0x42, 0x6e}},
    {S8_INITIALIZER("fmaxp h0, v1.2h\n"), {0x20, 0xf8, 0x30, 0x5e}},
    {S8_INITIALIZER("fmaxv h0, v1.4h\n"), {0x20, 0xf8, 0x30, 0x0e}},
    {S8_INITIALIZER("fmaxv h0, v1.8h\n"), {0x20, 0xf8, 0x30, 0x4e}},
    {S8_INITIALIZER("fmax v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0x42, 0x0e}},
    {S8_INITIALIZER("fmax v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0x42, 0x4e}},
    {S8_INITIALIZER("fminnmp v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0xc2, 0x2e}},
    {S8_INITIALIZER("fminnmp v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0xc2, 0x6e}},
    {S8_INITIALIZER("fminnmp h0, v1.2h\n"), {0x20, 0xc8, 0xb0, 0x5e}},
    {S8_INITIALIZER("fminnmv h0, v1.4h\n"), {0x20, 0xc8, 0xb0, 0x0e}},
    {S8_INITIALIZER("fminnmv h0, v1.8h\n"), {0x20, 0xc8, 0xb0, 0x4e}},
    {S8_INITIALIZER("fminnm v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0xc2, 0x0e}},
    {S8_INITIALIZER("fminnm v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0xc2, 0x4e}},
    {S8_INITIALIZER("fminp v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0xc2, 0x2e}},
    {S8_INITIALIZER("fminp v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0xc2, 0x6e}},
    {S8_INITIALIZER("fminp h0, v1.2h\n"), {0x20, 0xf8, 0xb0, 0x5e}},
    {S8_INITIALIZER("fminv h0, v1.4h\n"), {0x20, 0xf8, 0xb0, 0x0e}},
    {S8_INITIALIZER("fminv h0, v1.8h\n"), {0x20, 0xf8, 0xb0, 0x4e}},
    {S8_INITIALIZER("fmin v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0xc2, 0x0e}},
    {S8_INITIALIZER("fmin v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0xc2, 0x4e}},
    {S8_INITIALIZER("fmla v0.4h, v1.4h, v2.4h\n"), {0x20, 0x0c, 0x42, 0x0e}},
    {S8_INITIALIZER("fmla v0.8h, v1.8h, v2.8h\n"), {0x20, 0x0c, 0x42, 0x4e}},
    {S8_INITIALIZER("fmls v0.4h, v1.4h, v2.4h\n"), {0x20, 0x0c, 0xc2, 0x0e}},
    {S8_INITIALIZER("fmls v0.8h, v1.8h, v2.8h\n"), {0x20, 0x0c, 0xc2, 0x4e}},
    {S8_INITIALIZER("fmulx v0.4h, v1.4h, v2.4h\n"), {0x20, 0x1c, 0x42, 0x0e}},
    {S8_INITIALIZER("fmulx v0.8h, v1.8h, v2.8h\n"), {0x20, 0x1c, 0x42, 0x4e}},
    {S8_INITIALIZER("fmulx h0, h1, h2\n"), {0x20, 0x1c, 0x42, 0x5e}},
    {S8_INITIALIZER("fmul v0.4h, v1.4h, v2.4h\n"), {0x20, 0x1c, 0x42, 0x2e}},
    {S8_INITIALIZER("fmul v0.8h, v1.8h, v2.8h\n"), {0x20, 0x1c, 0x42, 0x6e}},
    {S8_INITIALIZER("fneg v0.4h, v1.4h\n"), {0x20, 0xf8, 0xf8, 0x2e}},
    {S8_INITIALIZER("fneg v0.8h, v1.8h\n"), {0x20, 0xf8, 0xf8, 0x6e}},
    {S8_INITIALIZER("frecpe v0.4h, v1.4h\n"), {0x20, 0xd8, 0xf9, 0x0e}},
    {S8_INITIALIZER("frecpe v0.8h, v1.8h\n"), {0x20, 0xd8, 0xf9, 0x4e}},
    {S8_INITIALIZER("frecpe h0, h1\n"), {0x20, 0xd8, 0xf9, 0x5e}},
    {S8_INITIALIZER("frecps v0.4h, v1.4h, v2.4h\n"), {0x20, 0x3c, 0x42, 0x0e}},
    {S8_INITIALIZER("frecps v0.8h, v1.8h, v2.8h\n"), {0x20, 0x3c, 0x42, 0x4e}},
    {S8_INITIALIZER("frecps h0, h1, h2\n"), {0x20, 0x3c, 0x42, 0x5e}},
    {S8_INITIALIZER("frecpx h0, h1\n"), {0x20, 0xf8, 0xf9, 0x5e}},
    {S8_INITIALIZER("frinta v0.4h, v1.4h\n"), {0x20, 0x88, 0x79, 0x2e}},
    {S8_INITIALIZER("frinta v0.8h, v1.8h\n"), {0x20, 0x88, 0x79, 0x6e}},
    {S8_INITIALIZER("frinti v0.4h, v1.4h\n"), {0x20, 0x98, 0xf9, 0x2e}},
    {S8_INITIALIZER("frinti v0.8h, v1.8h\n"), {0x20, 0x98, 0xf9, 0x6e}},
    {S8_INITIALIZER("frintm v0.4h, v1.4h\n"), {0x20, 0x98, 0x79, 0x0e}},
    {S8_INITIALIZER("frintm v0.8h, v1.8h\n"), {0x20, 0x98, 0x79, 0x4e}},
    {S8_INITIALIZER("frintn v0.4h, v1.4h\n"), {0x20, 0x88, 0x79, 0x0e}},
    {S8_INITIALIZER("frintn v0.8h, v1.8h\n"), {0x20, 0x88, 0x79, 0x4e}},
    {S8_INITIALIZER("frintp v0.4h, v1.4h\n"), {0x20, 0x88, 0xf9, 0x0e}},
    {S8_INITIALIZER("frintp v0.8h, v1.8h\n"), {0x20, 0x88, 0xf9, 0x4e}},
    {S8_INITIALIZER("frintx v0.4h, v1.4h\n"), {0x20, 0x98, 0x79, 0x2e}},
    {S8_INITIALIZER("frintx v0.8h, v1.8h\n"), {0x20, 0x98, 0x79, 0x6e}},
    {S8_INITIALIZER("frintz v0.4h, v1.4h\n"), {0x20, 0x98, 0xf9, 0x0e}},
    {S8_INITIALIZER("frintz v0.8h, v1.8h\n"), {0x20, 0x98, 0xf9, 0x4e}},
    {S8_INITIALIZER("frsqrte v0.4h, v1.4h\n"), {0x20, 0xd8, 0xf9, 0x2e}},
    {S8_INITIALIZER("frsqrte v0.8h, v1.8h\n"), {0x20, 0xd8, 0xf9, 0x6e}},
    {S8_INITIALIZER("frsqrte h0, h1\n"), {0x20, 0xd8, 0xf9, 0x7e}},
    {S8_INITIALIZER("frsqrts v0.4h, v1.4h, v2.4h\n"), {0x20, 0x3c, 0xc2, 0x0e}},
    {S8_INITIALIZER("frsqrts v0.8h, v1.8h, v2.8h\n"), {0x20, 0x3c, 0xc2, 0x4e}},
    {S8_INITIALIZER("frsqrts h0, h1, h2\n"), {0x20, 0x3c, 0xc2, 0x5e}},
    {S8_INITIALIZER("fsqrt v0.4h, v1.4h\n"), {0x20, 0xf8, 0xf9, 0x2e}},
    {S8_INITIALIZER("fsqrt v0.8h, v1.8h\n"), {0x20, 0xf8, 0xf9, 0x6e}},
    {S8_INITIALIZER("fsub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0xc2, 0x0e}},
    {S8_INITIALIZER("fsub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0xc2, 0x4e}},
    {S8_INITIALIZER("scvtf v0.4h, v1.4h\n"), {0x20, 0xd8, 0x79, 0x0e}},
    {S8_INITIALIZER("scvtf v0.8h, v1.8h\n"), {0x20, 0xd8, 0x79, 0x4e}},
    {S8_INITIALIZER("scvtf h0, h1\n"), {0x20, 0xd8, 0x79, 0x5e}},
    {S8_INITIALIZER("ucvtf v0.4h, v1.4h\n"), {0x20, 0xd8, 0x79, 0x2e}},
    {S8_INITIALIZER("ucvtf v0.8h, v1.8h\n"), {0x20, 0xd8, 0x79, 0x6e}},
    {S8_INITIALIZER("ucvtf h0, h1\n"), {0x20, 0xd8, 0x79, 0x7e}},
};

/* Scalar FP/FP16 direct rows. Bytes are independent llvm-mc 22.1.8
 * encodings; each of the twelve fixed generated rows has one legal spelling
 * and the boundary table below exercises the high register encodings. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fp_scalar_cases[] = {
    {S8_INITIALIZER("fabs d0, d1\n"), {0x20, 0xc0, 0x60, 0x1e}},
    {S8_INITIALIZER("fabs h0, h1\n"), {0x20, 0xc0, 0xe0, 0x1e}},
    {S8_INITIALIZER("fabs s0, s1\n"), {0x20, 0xc0, 0x20, 0x1e}},
    {S8_INITIALIZER("fadd d0, d1, d2\n"), {0x20, 0x28, 0x62, 0x1e}},
    {S8_INITIALIZER("fadd h0, h1, h2\n"), {0x20, 0x28, 0xe2, 0x1e}},
    {S8_INITIALIZER("fadd s0, s1, s2\n"), {0x20, 0x28, 0x22, 0x1e}},
    {S8_INITIALIZER("fcmp d0, d1\n"), {0x00, 0x20, 0x61, 0x1e}},
    {S8_INITIALIZER("fcmp h0, h1\n"), {0x00, 0x20, 0xe1, 0x1e}},
    {S8_INITIALIZER("fcmp s0, s1\n"), {0x00, 0x20, 0x21, 0x1e}},
    {S8_INITIALIZER("fcmpe d0, d1\n"), {0x10, 0x20, 0x61, 0x1e}},
    {S8_INITIALIZER("fcmpe h0, h1\n"), {0x10, 0x20, 0xe1, 0x1e}},
    {S8_INITIALIZER("fcmpe s0, s1\n"), {0x10, 0x20, 0x21, 0x1e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fp_scalar_boundary_cases[] = {
    {S8_INITIALIZER("FABS D31, D30\n"), {0xdf, 0xc3, 0x60, 0x1e}},
    {S8_INITIALIZER("FABS H31, H30\n"), {0xdf, 0xc3, 0xe0, 0x1e}},
    {S8_INITIALIZER("FABS S31, S30\n"), {0xdf, 0xc3, 0x20, 0x1e}},
    {S8_INITIALIZER("FADD D31, D30, D29\n"), {0xdf, 0x2b, 0x7d, 0x1e}},
    {S8_INITIALIZER("FADD H31, H30, H29\n"), {0xdf, 0x2b, 0xfd, 0x1e}},
    {S8_INITIALIZER("FADD S31, S30, S29\n"), {0xdf, 0x2b, 0x3d, 0x1e}},
    {S8_INITIALIZER("FCMP D31, D30\n"), {0xe0, 0x23, 0x7e, 0x1e}},
    {S8_INITIALIZER("FCMP H31, H30\n"), {0xe0, 0x23, 0xfe, 0x1e}},
    {S8_INITIALIZER("FCMP S31, S30\n"), {0xe0, 0x23, 0x3e, 0x1e}},
    {S8_INITIALIZER("FCMPE D31, D30\n"), {0xf0, 0x23, 0x7e, 0x1e}},
    {S8_INITIALIZER("FCMPE H31, H30\n"), {0xf0, 0x23, 0xfe, 0x1e}},
    {S8_INITIALIZER("FCMPE S31, S30\n"), {0xf0, 0x23, 0x3e, 0x1e}},
};

/* FCSEL condition_field coverage. LLVM accepts all sixteen architectural
 * condition values, including AL/NV, plus the HS/LO aliases; each byte is an
 * independent llvm-mc 22.1.8 encoding. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcsel_cases[] = {
    {S8_INITIALIZER("fcsel d0, d1, d2, eq\n"), {0x20, 0x0c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, ne\n"), {0x20, 0x1c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, cs\n"), {0x20, 0x2c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, hs\n"), {0x20, 0x2c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, cc\n"), {0x20, 0x3c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, lo\n"), {0x20, 0x3c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, mi\n"), {0x20, 0x4c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, pl\n"), {0x20, 0x5c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, vs\n"), {0x20, 0x6c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, vc\n"), {0x20, 0x7c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, hi\n"), {0x20, 0x8c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, ls\n"), {0x20, 0x9c, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, ge\n"), {0x20, 0xac, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, lt\n"), {0x20, 0xbc, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, gt\n"), {0x20, 0xcc, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, le\n"), {0x20, 0xdc, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, al\n"), {0x20, 0xec, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel d0, d1, d2, nv\n"), {0x20, 0xfc, 0x62, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, eq\n"), {0x20, 0x0c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, ne\n"), {0x20, 0x1c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, cs\n"), {0x20, 0x2c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, hs\n"), {0x20, 0x2c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, cc\n"), {0x20, 0x3c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, lo\n"), {0x20, 0x3c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, mi\n"), {0x20, 0x4c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, pl\n"), {0x20, 0x5c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, vs\n"), {0x20, 0x6c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, vc\n"), {0x20, 0x7c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, hi\n"), {0x20, 0x8c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, ls\n"), {0x20, 0x9c, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, ge\n"), {0x20, 0xac, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, lt\n"), {0x20, 0xbc, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, gt\n"), {0x20, 0xcc, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, le\n"), {0x20, 0xdc, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, al\n"), {0x20, 0xec, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel h0, h1, h2, nv\n"), {0x20, 0xfc, 0xe2, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, eq\n"), {0x20, 0x0c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, ne\n"), {0x20, 0x1c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, cs\n"), {0x20, 0x2c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, hs\n"), {0x20, 0x2c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, cc\n"), {0x20, 0x3c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, lo\n"), {0x20, 0x3c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, mi\n"), {0x20, 0x4c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, pl\n"), {0x20, 0x5c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, vs\n"), {0x20, 0x6c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, vc\n"), {0x20, 0x7c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, hi\n"), {0x20, 0x8c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, ls\n"), {0x20, 0x9c, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, ge\n"), {0x20, 0xac, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, lt\n"), {0x20, 0xbc, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, gt\n"), {0x20, 0xcc, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, le\n"), {0x20, 0xdc, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, al\n"), {0x20, 0xec, 0x22, 0x1e}},
    {S8_INITIALIZER("fcsel s0, s1, s2, nv\n"), {0x20, 0xfc, 0x22, 0x1e}},
};

/* Scalar same-shape AdvSIMD rows added after the FP16 cohort.  Every byte is
 * an independent llvm-mc 22.1.8 encoding; the table intentionally covers
 * every legal width of every row (nine FP rows at S/D and eight integer rows
 * at B/H/S/D). */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_scalar_same_cases[] = {
    {S8_INITIALIZER("fabd s0, s1, s2\n"), {0x20, 0xd4, 0xa2, 0x7e}},
    {S8_INITIALIZER("fabd d0, d1, d2\n"), {0x20, 0xd4, 0xe2, 0x7e}},
    {S8_INITIALIZER("facge s0, s1, s2\n"), {0x20, 0xec, 0x22, 0x7e}},
    {S8_INITIALIZER("facge d0, d1, d2\n"), {0x20, 0xec, 0x62, 0x7e}},
    {S8_INITIALIZER("facgt s0, s1, s2\n"), {0x20, 0xec, 0xa2, 0x7e}},
    {S8_INITIALIZER("facgt d0, d1, d2\n"), {0x20, 0xec, 0xe2, 0x7e}},
    {S8_INITIALIZER("fcmeq s0, s1, s2\n"), {0x20, 0xe4, 0x22, 0x5e}},
    {S8_INITIALIZER("fcmeq d0, d1, d2\n"), {0x20, 0xe4, 0x62, 0x5e}},
    {S8_INITIALIZER("fcmge s0, s1, s2\n"), {0x20, 0xe4, 0x22, 0x7e}},
    {S8_INITIALIZER("fcmge d0, d1, d2\n"), {0x20, 0xe4, 0x62, 0x7e}},
    {S8_INITIALIZER("fcmgt s0, s1, s2\n"), {0x20, 0xe4, 0xa2, 0x7e}},
    {S8_INITIALIZER("fcmgt d0, d1, d2\n"), {0x20, 0xe4, 0xe2, 0x7e}},
    {S8_INITIALIZER("fmulx s0, s1, s2\n"), {0x20, 0xdc, 0x22, 0x5e}},
    {S8_INITIALIZER("fmulx d0, d1, d2\n"), {0x20, 0xdc, 0x62, 0x5e}},
    {S8_INITIALIZER("frecps s0, s1, s2\n"), {0x20, 0xfc, 0x22, 0x5e}},
    {S8_INITIALIZER("frecps d0, d1, d2\n"), {0x20, 0xfc, 0x62, 0x5e}},
    {S8_INITIALIZER("frsqrts s0, s1, s2\n"), {0x20, 0xfc, 0xa2, 0x5e}},
    {S8_INITIALIZER("frsqrts d0, d1, d2\n"), {0x20, 0xfc, 0xe2, 0x5e}},
    {S8_INITIALIZER("sqadd b0, b1, b2\n"), {0x20, 0x0c, 0x22, 0x5e}},
    {S8_INITIALIZER("sqadd h0, h1, h2\n"), {0x20, 0x0c, 0x62, 0x5e}},
    {S8_INITIALIZER("sqadd s0, s1, s2\n"), {0x20, 0x0c, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqadd d0, d1, d2\n"), {0x20, 0x0c, 0xe2, 0x5e}},
    {S8_INITIALIZER("sqrshl b0, b1, b2\n"), {0x20, 0x5c, 0x22, 0x5e}},
    {S8_INITIALIZER("sqrshl h0, h1, h2\n"), {0x20, 0x5c, 0x62, 0x5e}},
    {S8_INITIALIZER("sqrshl s0, s1, s2\n"), {0x20, 0x5c, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqrshl d0, d1, d2\n"), {0x20, 0x5c, 0xe2, 0x5e}},
    {S8_INITIALIZER("sqshl b0, b1, b2\n"), {0x20, 0x4c, 0x22, 0x5e}},
    {S8_INITIALIZER("sqshl h0, h1, h2\n"), {0x20, 0x4c, 0x62, 0x5e}},
    {S8_INITIALIZER("sqshl s0, s1, s2\n"), {0x20, 0x4c, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqshl d0, d1, d2\n"), {0x20, 0x4c, 0xe2, 0x5e}},
    {S8_INITIALIZER("sqsub b0, b1, b2\n"), {0x20, 0x2c, 0x22, 0x5e}},
    {S8_INITIALIZER("sqsub h0, h1, h2\n"), {0x20, 0x2c, 0x62, 0x5e}},
    {S8_INITIALIZER("sqsub s0, s1, s2\n"), {0x20, 0x2c, 0xa2, 0x5e}},
    {S8_INITIALIZER("sqsub d0, d1, d2\n"), {0x20, 0x2c, 0xe2, 0x5e}},
    {S8_INITIALIZER("uqadd b0, b1, b2\n"), {0x20, 0x0c, 0x22, 0x7e}},
    {S8_INITIALIZER("uqadd h0, h1, h2\n"), {0x20, 0x0c, 0x62, 0x7e}},
    {S8_INITIALIZER("uqadd s0, s1, s2\n"), {0x20, 0x0c, 0xa2, 0x7e}},
    {S8_INITIALIZER("uqadd d0, d1, d2\n"), {0x20, 0x0c, 0xe2, 0x7e}},
    {S8_INITIALIZER("uqrshl b0, b1, b2\n"), {0x20, 0x5c, 0x22, 0x7e}},
    {S8_INITIALIZER("uqrshl h0, h1, h2\n"), {0x20, 0x5c, 0x62, 0x7e}},
    {S8_INITIALIZER("uqrshl s0, s1, s2\n"), {0x20, 0x5c, 0xa2, 0x7e}},
    {S8_INITIALIZER("uqrshl d0, d1, d2\n"), {0x20, 0x5c, 0xe2, 0x7e}},
    {S8_INITIALIZER("uqshl b0, b1, b2\n"), {0x20, 0x4c, 0x22, 0x7e}},
    {S8_INITIALIZER("uqshl h0, h1, h2\n"), {0x20, 0x4c, 0x62, 0x7e}},
    {S8_INITIALIZER("uqshl s0, s1, s2\n"), {0x20, 0x4c, 0xa2, 0x7e}},
    {S8_INITIALIZER("uqshl d0, d1, d2\n"), {0x20, 0x4c, 0xe2, 0x7e}},
    {S8_INITIALIZER("uqsub b0, b1, b2\n"), {0x20, 0x2c, 0x22, 0x7e}},
    {S8_INITIALIZER("uqsub h0, h1, h2\n"), {0x20, 0x2c, 0x62, 0x7e}},
    {S8_INITIALIZER("uqsub s0, s1, s2\n"), {0x20, 0x2c, 0xa2, 0x7e}},
    {S8_INITIALIZER("uqsub d0, d1, d2\n"), {0x20, 0x2c, 0xe2, 0x7e}},
};

/* Exhaustive legal arrangements for the widening absolute-difference and
 * pair-long AdvSIMD rows.  Bytes are independent llvm-mc 22.1.8 literals. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_widening_cases[] = {
    {S8_INITIALIZER("saba v0.8b, v1.8b, v31.8b\n"), {0x20, 0x7c, 0x3f, 0x0e}},
    {S8_INITIALIZER("saba v0.16b, v1.16b, v31.16b\n"), {0x20, 0x7c, 0x3f, 0x4e}},
    {S8_INITIALIZER("saba v0.4h, v1.4h, v31.4h\n"), {0x20, 0x7c, 0x7f, 0x0e}},
    {S8_INITIALIZER("saba v0.8h, v1.8h, v31.8h\n"), {0x20, 0x7c, 0x7f, 0x4e}},
    {S8_INITIALIZER("saba v0.2s, v1.2s, v31.2s\n"), {0x20, 0x7c, 0xbf, 0x0e}},
    {S8_INITIALIZER("saba v0.4s, v1.4s, v31.4s\n"), {0x20, 0x7c, 0xbf, 0x4e}},
    {S8_INITIALIZER("sabd v0.8b, v1.8b, v31.8b\n"), {0x20, 0x74, 0x3f, 0x0e}},
    {S8_INITIALIZER("sabd v0.16b, v1.16b, v31.16b\n"), {0x20, 0x74, 0x3f, 0x4e}},
    {S8_INITIALIZER("sabd v0.4h, v1.4h, v31.4h\n"), {0x20, 0x74, 0x7f, 0x0e}},
    {S8_INITIALIZER("sabd v0.8h, v1.8h, v31.8h\n"), {0x20, 0x74, 0x7f, 0x4e}},
    {S8_INITIALIZER("sabd v0.2s, v1.2s, v31.2s\n"), {0x20, 0x74, 0xbf, 0x0e}},
    {S8_INITIALIZER("sabd v0.4s, v1.4s, v31.4s\n"), {0x20, 0x74, 0xbf, 0x4e}},
    {S8_INITIALIZER("uaba v0.8b, v1.8b, v31.8b\n"), {0x20, 0x7c, 0x3f, 0x2e}},
    {S8_INITIALIZER("uaba v0.16b, v1.16b, v31.16b\n"), {0x20, 0x7c, 0x3f, 0x6e}},
    {S8_INITIALIZER("uaba v0.4h, v1.4h, v31.4h\n"), {0x20, 0x7c, 0x7f, 0x2e}},
    {S8_INITIALIZER("uaba v0.8h, v1.8h, v31.8h\n"), {0x20, 0x7c, 0x7f, 0x6e}},
    {S8_INITIALIZER("uaba v0.2s, v1.2s, v31.2s\n"), {0x20, 0x7c, 0xbf, 0x2e}},
    {S8_INITIALIZER("uaba v0.4s, v1.4s, v31.4s\n"), {0x20, 0x7c, 0xbf, 0x6e}},
    {S8_INITIALIZER("uabd v0.8b, v1.8b, v31.8b\n"), {0x20, 0x74, 0x3f, 0x2e}},
    {S8_INITIALIZER("uabd v0.16b, v1.16b, v31.16b\n"), {0x20, 0x74, 0x3f, 0x6e}},
    {S8_INITIALIZER("uabd v0.4h, v1.4h, v31.4h\n"), {0x20, 0x74, 0x7f, 0x2e}},
    {S8_INITIALIZER("uabd v0.8h, v1.8h, v31.8h\n"), {0x20, 0x74, 0x7f, 0x6e}},
    {S8_INITIALIZER("uabd v0.2s, v1.2s, v31.2s\n"), {0x20, 0x74, 0xbf, 0x2e}},
    {S8_INITIALIZER("uabd v0.4s, v1.4s, v31.4s\n"), {0x20, 0x74, 0xbf, 0x6e}},
    {S8_INITIALIZER("sadalp v31.4h, v30.8b\n"), {0xdf, 0x6b, 0x20, 0x0e}},
    {S8_INITIALIZER("sadalp v31.8h, v30.16b\n"), {0xdf, 0x6b, 0x20, 0x4e}},
    {S8_INITIALIZER("sadalp v31.2s, v30.4h\n"), {0xdf, 0x6b, 0x60, 0x0e}},
    {S8_INITIALIZER("sadalp v31.4s, v30.8h\n"), {0xdf, 0x6b, 0x60, 0x4e}},
    {S8_INITIALIZER("sadalp v31.1d, v30.2s\n"), {0xdf, 0x6b, 0xa0, 0x0e}},
    {S8_INITIALIZER("sadalp v31.2d, v30.4s\n"), {0xdf, 0x6b, 0xa0, 0x4e}},
    {S8_INITIALIZER("saddlp v31.4h, v30.8b\n"), {0xdf, 0x2b, 0x20, 0x0e}},
    {S8_INITIALIZER("saddlp v31.8h, v30.16b\n"), {0xdf, 0x2b, 0x20, 0x4e}},
    {S8_INITIALIZER("saddlp v31.2s, v30.4h\n"), {0xdf, 0x2b, 0x60, 0x0e}},
    {S8_INITIALIZER("saddlp v31.4s, v30.8h\n"), {0xdf, 0x2b, 0x60, 0x4e}},
    {S8_INITIALIZER("saddlp v31.1d, v30.2s\n"), {0xdf, 0x2b, 0xa0, 0x0e}},
    {S8_INITIALIZER("saddlp v31.2d, v30.4s\n"), {0xdf, 0x2b, 0xa0, 0x4e}},
    {S8_INITIALIZER("uadalp v31.4h, v30.8b\n"), {0xdf, 0x6b, 0x20, 0x2e}},
    {S8_INITIALIZER("uadalp v31.8h, v30.16b\n"), {0xdf, 0x6b, 0x20, 0x6e}},
    {S8_INITIALIZER("uadalp v31.2s, v30.4h\n"), {0xdf, 0x6b, 0x60, 0x2e}},
    {S8_INITIALIZER("uadalp v31.4s, v30.8h\n"), {0xdf, 0x6b, 0x60, 0x6e}},
    {S8_INITIALIZER("uadalp v31.1d, v30.2s\n"), {0xdf, 0x6b, 0xa0, 0x2e}},
    {S8_INITIALIZER("uadalp v31.2d, v30.4s\n"), {0xdf, 0x6b, 0xa0, 0x6e}},
    {S8_INITIALIZER("uaddlp v31.4h, v30.8b\n"), {0xdf, 0x2b, 0x20, 0x2e}},
    {S8_INITIALIZER("uaddlp v31.8h, v30.16b\n"), {0xdf, 0x2b, 0x20, 0x6e}},
    {S8_INITIALIZER("uaddlp v31.2s, v30.4h\n"), {0xdf, 0x2b, 0x60, 0x2e}},
    {S8_INITIALIZER("uaddlp v31.4s, v30.8h\n"), {0xdf, 0x2b, 0x60, 0x6e}},
    {S8_INITIALIZER("uaddlp v31.1d, v30.2s\n"), {0xdf, 0x2b, 0xa0, 0x2e}},
    {S8_INITIALIZER("uaddlp v31.2d, v30.4s\n"), {0xdf, 0x2b, 0xa0, 0x6e}},
};

/* Exhaustive legal across-vector integer reductions.  Bytes are independent
 * llvm-mc 22.1.8 literals; 2S/2D encodings are reserved and tested below. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_reduction_cases[] = {
    {S8_INITIALIZER("addv b0, v1.8b\n"), {0x20, 0xb8, 0x31, 0x0e}},
    {S8_INITIALIZER("addv b0, v1.16b\n"), {0x20, 0xb8, 0x31, 0x4e}},
    {S8_INITIALIZER("addv h0, v1.4h\n"), {0x20, 0xb8, 0x71, 0x0e}},
    {S8_INITIALIZER("addv h0, v1.8h\n"), {0x20, 0xb8, 0x71, 0x4e}},
    {S8_INITIALIZER("addv s0, v1.4s\n"), {0x20, 0xb8, 0xb1, 0x4e}},
    {S8_INITIALIZER("saddlv h0, v1.8b\n"), {0x20, 0x38, 0x30, 0x0e}},
    {S8_INITIALIZER("saddlv h0, v1.16b\n"), {0x20, 0x38, 0x30, 0x4e}},
    {S8_INITIALIZER("saddlv s0, v1.4h\n"), {0x20, 0x38, 0x70, 0x0e}},
    {S8_INITIALIZER("saddlv s0, v1.8h\n"), {0x20, 0x38, 0x70, 0x4e}},
    {S8_INITIALIZER("saddlv d0, v1.4s\n"), {0x20, 0x38, 0xb0, 0x4e}},
    {S8_INITIALIZER("uaddlv h0, v1.8b\n"), {0x20, 0x38, 0x30, 0x2e}},
    {S8_INITIALIZER("uaddlv h0, v1.16b\n"), {0x20, 0x38, 0x30, 0x6e}},
    {S8_INITIALIZER("uaddlv s0, v1.4h\n"), {0x20, 0x38, 0x70, 0x2e}},
    {S8_INITIALIZER("uaddlv s0, v1.8h\n"), {0x20, 0x38, 0x70, 0x6e}},
    {S8_INITIALIZER("uaddlv d0, v1.4s\n"), {0x20, 0x38, 0xb0, 0x6e}},
    {S8_INITIALIZER("smaxv b0, v1.8b\n"), {0x20, 0xa8, 0x30, 0x0e}},
    {S8_INITIALIZER("smaxv b0, v1.16b\n"), {0x20, 0xa8, 0x30, 0x4e}},
    {S8_INITIALIZER("smaxv h0, v1.4h\n"), {0x20, 0xa8, 0x70, 0x0e}},
    {S8_INITIALIZER("smaxv h0, v1.8h\n"), {0x20, 0xa8, 0x70, 0x4e}},
    {S8_INITIALIZER("smaxv s0, v1.4s\n"), {0x20, 0xa8, 0xb0, 0x4e}},
    {S8_INITIALIZER("sminv b0, v1.8b\n"), {0x20, 0xa8, 0x31, 0x0e}},
    {S8_INITIALIZER("sminv b0, v1.16b\n"), {0x20, 0xa8, 0x31, 0x4e}},
    {S8_INITIALIZER("sminv h0, v1.4h\n"), {0x20, 0xa8, 0x71, 0x0e}},
    {S8_INITIALIZER("sminv h0, v1.8h\n"), {0x20, 0xa8, 0x71, 0x4e}},
    {S8_INITIALIZER("sminv s0, v1.4s\n"), {0x20, 0xa8, 0xb1, 0x4e}},
    {S8_INITIALIZER("umaxv b0, v1.8b\n"), {0x20, 0xa8, 0x30, 0x2e}},
    {S8_INITIALIZER("umaxv b0, v1.16b\n"), {0x20, 0xa8, 0x30, 0x6e}},
    {S8_INITIALIZER("umaxv h0, v1.4h\n"), {0x20, 0xa8, 0x70, 0x2e}},
    {S8_INITIALIZER("umaxv h0, v1.8h\n"), {0x20, 0xa8, 0x70, 0x6e}},
    {S8_INITIALIZER("umaxv s0, v1.4s\n"), {0x20, 0xa8, 0xb0, 0x6e}},
    {S8_INITIALIZER("uminv b0, v1.8b\n"), {0x20, 0xa8, 0x31, 0x2e}},
    {S8_INITIALIZER("uminv b0, v1.16b\n"), {0x20, 0xa8, 0x31, 0x6e}},
    {S8_INITIALIZER("uminv h0, v1.4h\n"), {0x20, 0xa8, 0x71, 0x2e}},
    {S8_INITIALIZER("uminv h0, v1.8h\n"), {0x20, 0xa8, 0x71, 0x6e}},
    {S8_INITIALIZER("uminv s0, v1.4s\n"), {0x20, 0xa8, 0xb1, 0x6e}},
};

/* Exhaustive legal vector ADDP arrangements. Bytes are independent
 * llvm-mc 22.1.8 literals, not buster output. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_addp_cases[] = {
    {S8_INITIALIZER("addp v0.8b, v1.8b, v31.8b\n"), {0x20, 0xbc, 0x3f, 0x0e}},
    {S8_INITIALIZER("addp v0.16b, v1.16b, v31.16b\n"), {0x20, 0xbc, 0x3f, 0x4e}},
    {S8_INITIALIZER("addp v0.4h, v1.4h, v31.4h\n"), {0x20, 0xbc, 0x7f, 0x0e}},
    {S8_INITIALIZER("addp v0.8h, v1.8h, v31.8h\n"), {0x20, 0xbc, 0x7f, 0x4e}},
    {S8_INITIALIZER("addp v0.2s, v1.2s, v31.2s\n"), {0x20, 0xbc, 0xbf, 0x0e}},
    {S8_INITIALIZER("addp v0.4s, v1.4s, v31.4s\n"), {0x20, 0xbc, 0xbf, 0x4e}},
    {S8_INITIALIZER("addp v0.2d, v1.2d, v31.2d\n"), {0x20, 0xbc, 0xff, 0x4e}},
};

/* Exhaustive legal scalar pair reductions. Bytes are independent llvm-mc
 * 22.1.8 literals; each row accepts S/2S and D/2D only. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_pair_fp_cases[] = {
    {S8_INITIALIZER("faddp s0, v1.2s\n"), {0x20, 0xd8, 0x30, 0x7e}},
    {S8_INITIALIZER("faddp d0, v1.2d\n"), {0x20, 0xd8, 0x70, 0x7e}},
    {S8_INITIALIZER("fmaxnmp s0, v1.2s\n"), {0x20, 0xc8, 0x30, 0x7e}},
    {S8_INITIALIZER("fmaxnmp d0, v1.2d\n"), {0x20, 0xc8, 0x70, 0x7e}},
    {S8_INITIALIZER("fmaxp s0, v1.2s\n"), {0x20, 0xf8, 0x30, 0x7e}},
    {S8_INITIALIZER("fmaxp d0, v1.2d\n"), {0x20, 0xf8, 0x70, 0x7e}},
    {S8_INITIALIZER("fminnmp s0, v1.2s\n"), {0x20, 0xc8, 0xb0, 0x7e}},
    {S8_INITIALIZER("fminnmp d0, v1.2d\n"), {0x20, 0xc8, 0xf0, 0x7e}},
    {S8_INITIALIZER("fminp s0, v1.2s\n"), {0x20, 0xf8, 0xb0, 0x7e}},
    {S8_INITIALIZER("fminp d0, v1.2d\n"), {0x20, 0xf8, 0xf0, 0x7e}},
};

/* Exhaustive legal vector unary forms. Bytes are independent llvm-mc 22.1.8
 * literals, not buster output. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_unary_cases[] = {
    {S8_INITIALIZER("rev64 v0.8b, v1.8b\n"), {0x20, 0x08, 0x20, 0x0e}},
    {S8_INITIALIZER("rev64 v0.16b, v1.16b\n"), {0x20, 0x08, 0x20, 0x4e}},
    {S8_INITIALIZER("rev64 v0.4h, v1.4h\n"), {0x20, 0x08, 0x60, 0x0e}},
    {S8_INITIALIZER("rev64 v0.8h, v1.8h\n"), {0x20, 0x08, 0x60, 0x4e}},
    {S8_INITIALIZER("rev64 v0.2s, v1.2s\n"), {0x20, 0x08, 0xa0, 0x0e}},
    {S8_INITIALIZER("rev64 v0.4s, v1.4s\n"), {0x20, 0x08, 0xa0, 0x4e}},
    {S8_INITIALIZER("urecpe v0.2s, v1.2s\n"), {0x20, 0xc8, 0xa1, 0x0e}},
    {S8_INITIALIZER("urecpe v0.4s, v1.4s\n"), {0x20, 0xc8, 0xa1, 0x4e}},
    {S8_INITIALIZER("ursqrte v0.2s, v1.2s\n"), {0x20, 0xc8, 0xa1, 0x2e}},
    {S8_INITIALIZER("ursqrte v0.4s, v1.4s\n"), {0x20, 0xc8, 0xa1, 0x6e}},
};

/* Scalar S/D FCVT forms are independent llvm-mc 22.1.8 literals. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcvt_scalar_cases[] = {
    {S8_INITIALIZER("fcvtas s0, s1\n"), {0x20, 0xc8, 0x21, 0x5e}},
    {S8_INITIALIZER("fcvtas d0, d1\n"), {0x20, 0xc8, 0x61, 0x5e}},
    {S8_INITIALIZER("fcvtau s0, s1\n"), {0x20, 0xc8, 0x21, 0x7e}},
    {S8_INITIALIZER("fcvtau d0, d1\n"), {0x20, 0xc8, 0x61, 0x7e}},
    {S8_INITIALIZER("fcvtms s0, s1\n"), {0x20, 0xb8, 0x21, 0x5e}},
    {S8_INITIALIZER("fcvtms d0, d1\n"), {0x20, 0xb8, 0x61, 0x5e}},
    {S8_INITIALIZER("fcvtmu s0, s1\n"), {0x20, 0xb8, 0x21, 0x7e}},
    {S8_INITIALIZER("fcvtmu d0, d1\n"), {0x20, 0xb8, 0x61, 0x7e}},
    {S8_INITIALIZER("fcvtns s0, s1\n"), {0x20, 0xa8, 0x21, 0x5e}},
    {S8_INITIALIZER("fcvtns d0, d1\n"), {0x20, 0xa8, 0x61, 0x5e}},
    {S8_INITIALIZER("fcvtnu s0, s1\n"), {0x20, 0xa8, 0x21, 0x7e}},
    {S8_INITIALIZER("fcvtnu d0, d1\n"), {0x20, 0xa8, 0x61, 0x7e}},
    {S8_INITIALIZER("fcvtps s0, s1\n"), {0x20, 0xa8, 0xa1, 0x5e}},
    {S8_INITIALIZER("fcvtps d0, d1\n"), {0x20, 0xa8, 0xe1, 0x5e}},
    {S8_INITIALIZER("fcvtpu s0, s1\n"), {0x20, 0xa8, 0xa1, 0x7e}},
    {S8_INITIALIZER("fcvtpu d0, d1\n"), {0x20, 0xa8, 0xe1, 0x7e}},
    {S8_INITIALIZER("fcvtzs s0, s1\n"), {0x20, 0xb8, 0xa1, 0x5e}},
    {S8_INITIALIZER("fcvtzs d0, d1\n"), {0x20, 0xb8, 0xe1, 0x5e}},
    {S8_INITIALIZER("fcvtzu s0, s1\n"), {0x20, 0xb8, 0xa1, 0x7e}},
    {S8_INITIALIZER("fcvtzu d0, d1\n"), {0x20, 0xb8, 0xe1, 0x7e}},
};

/* Mixed GPR/scalar-SIMD FCVT rows.  The instruction bytes come from an
 * independent llvm-mc 22.1.8 run; the ID/digest pair pins each fixture to
 * its canonical generated row rather than merely exercising a mnemonic. */
typedef struct AssemblyA64DirectSIMDFcvtGprCase AssemblyA64DirectSIMDFcvtGprCase;
struct AssemblyA64DirectSIMDFcvtGprCase
{
    String8 source;
    u8 bytes[4];
    String8 semantic_id;
    u64 source_digest;
};

static AssemblyA64DirectSIMDFcvtGprCase const assembly_a64_direct_simd_fcvt_gpr_cases[] = {
    {S8_INITIALIZER("fcvtas w0, d1\n"), {0x20, 0x00, 0x64, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_32D_float2int"), UINT64_C(0xbd156ab773e3db2d)},
    {S8_INITIALIZER("fcvtas w0, h1\n"), {0x20, 0x00, 0xe4, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_32H_float2int"), UINT64_C(0x913328683e893c88)},
    {S8_INITIALIZER("fcvtas w0, s1\n"), {0x20, 0x00, 0x24, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_32S_float2int"), UINT64_C(0xac161d00b51920b9)},
    {S8_INITIALIZER("fcvtas x0, d1\n"), {0x20, 0x00, 0x64, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_64D_float2int"), UINT64_C(0x5eda5c8f175dd96e)},
    {S8_INITIALIZER("fcvtas x0, h1\n"), {0x20, 0x00, 0xe4, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_64H_float2int"), UINT64_C(0xedf3d0506bbe827a)},
    {S8_INITIALIZER("fcvtas x0, s1\n"), {0x20, 0x00, 0x24, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAS_64S_float2int"), UINT64_C(0x27af20ba2edd15e5)},
    {S8_INITIALIZER("fcvtau w0, d1\n"), {0x20, 0x00, 0x65, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_32D_float2int"), UINT64_C(0x3e813e6b1e499209)},
    {S8_INITIALIZER("fcvtau w0, h1\n"), {0x20, 0x00, 0xe5, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_32H_float2int"), UINT64_C(0x4de0c9e8dfb514c1)},
    {S8_INITIALIZER("fcvtau w0, s1\n"), {0x20, 0x00, 0x25, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_32S_float2int"), UINT64_C(0x68d8a09a27183810)},
    {S8_INITIALIZER("fcvtau x0, d1\n"), {0x20, 0x00, 0x65, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_64D_float2int"), UINT64_C(0x7dede17ec2c4de3b)},
    {S8_INITIALIZER("fcvtau x0, h1\n"), {0x20, 0x00, 0xe5, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_64H_float2int"), UINT64_C(0xe474e9242969ced3)},
    {S8_INITIALIZER("fcvtau x0, s1\n"), {0x20, 0x00, 0x25, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTAU_64S_float2int"), UINT64_C(0x232bd56f2551067c)},
    {S8_INITIALIZER("fcvtms w0, d1\n"), {0x20, 0x00, 0x70, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_32D_float2int"), UINT64_C(0xdb999f823dcbeadf)},
    {S8_INITIALIZER("fcvtms w0, h1\n"), {0x20, 0x00, 0xf0, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_32H_float2int"), UINT64_C(0x5f900df11447d8eb)},
    {S8_INITIALIZER("fcvtms w0, s1\n"), {0x20, 0x00, 0x30, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_32S_float2int"), UINT64_C(0x826eae4d5959097f)},
    {S8_INITIALIZER("fcvtms x0, d1\n"), {0x20, 0x00, 0x70, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_64D_float2int"), UINT64_C(0x754d122a7c6bc2a0)},
    {S8_INITIALIZER("fcvtms x0, h1\n"), {0x20, 0x00, 0xf0, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_64H_float2int"), UINT64_C(0x0698365a178984a2)},
    {S8_INITIALIZER("fcvtms x0, s1\n"), {0x20, 0x00, 0x30, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMS_64S_float2int"), UINT64_C(0xf7f4b327317ab2ae)},
    {S8_INITIALIZER("fcvtmu w0, d1\n"), {0x20, 0x00, 0x71, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_32D_float2int"), UINT64_C(0xb09a743c1e19bdaa)},
    {S8_INITIALIZER("fcvtmu w0, h1\n"), {0x20, 0x00, 0xf1, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_32H_float2int"), UINT64_C(0x9b695075d37a1cc9)},
    {S8_INITIALIZER("fcvtmu w0, s1\n"), {0x20, 0x00, 0x31, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_32S_float2int"), UINT64_C(0xe28fc4f972b3218c)},
    {S8_INITIALIZER("fcvtmu x0, d1\n"), {0x20, 0x00, 0x71, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_64D_float2int"), UINT64_C(0xecddac8715a29697)},
    {S8_INITIALIZER("fcvtmu x0, h1\n"), {0x20, 0x00, 0xf1, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_64H_float2int"), UINT64_C(0xa12cdfe13344872e)},
    {S8_INITIALIZER("fcvtmu x0, s1\n"), {0x20, 0x00, 0x31, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTMU_64S_float2int"), UINT64_C(0xe650588e5f4c16c2)},
    {S8_INITIALIZER("fcvtns w0, d1\n"), {0x20, 0x00, 0x60, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_32D_float2int"), UINT64_C(0xc6547e16a34ec087)},
    {S8_INITIALIZER("fcvtns w0, h1\n"), {0x20, 0x00, 0xe0, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_32H_float2int"), UINT64_C(0xebb8428412bf2a40)},
    {S8_INITIALIZER("fcvtns w0, s1\n"), {0x20, 0x00, 0x20, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_32S_float2int"), UINT64_C(0x4a8d8be66b5be011)},
    {S8_INITIALIZER("fcvtns x0, d1\n"), {0x20, 0x00, 0x60, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_64D_float2int"), UINT64_C(0x4b41ddf39c44a620)},
    {S8_INITIALIZER("fcvtns x0, h1\n"), {0x20, 0x00, 0xe0, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_64H_float2int"), UINT64_C(0xad2efe69a1bda08b)},
    {S8_INITIALIZER("fcvtns x0, s1\n"), {0x20, 0x00, 0x20, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNS_64S_float2int"), UINT64_C(0x534c61c03a514586)},
    {S8_INITIALIZER("fcvtnu w0, d1\n"), {0x20, 0x00, 0x61, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_32D_float2int"), UINT64_C(0x920564d643ec86be)},
    {S8_INITIALIZER("fcvtnu w0, h1\n"), {0x20, 0x00, 0xe1, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_32H_float2int"), UINT64_C(0xc1d9e16f602eaa9f)},
    {S8_INITIALIZER("fcvtnu w0, s1\n"), {0x20, 0x00, 0x21, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_32S_float2int"), UINT64_C(0x54458195fb9c7e24)},
    {S8_INITIALIZER("fcvtnu x0, d1\n"), {0x20, 0x00, 0x61, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_64D_float2int"), UINT64_C(0xc955d5b1ad9e93d5)},
    {S8_INITIALIZER("fcvtnu x0, h1\n"), {0x20, 0x00, 0xe1, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_64H_float2int"), UINT64_C(0xaf4e5e6e9f530f6f)},
    {S8_INITIALIZER("fcvtnu x0, s1\n"), {0x20, 0x00, 0x21, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTNU_64S_float2int"), UINT64_C(0xc2f38acbbc52f90d)},
    {S8_INITIALIZER("fcvtps w0, d1\n"), {0x20, 0x00, 0x68, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_32D_float2int"), UINT64_C(0xf6ba85f257737b99)},
    {S8_INITIALIZER("fcvtps w0, h1\n"), {0x20, 0x00, 0xe8, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_32H_float2int"), UINT64_C(0x181471a6b42d7cba)},
    {S8_INITIALIZER("fcvtps w0, s1\n"), {0x20, 0x00, 0x28, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_32S_float2int"), UINT64_C(0xa6082cd58100786d)},
    {S8_INITIALIZER("fcvtps x0, d1\n"), {0x20, 0x00, 0x68, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_64D_float2int"), UINT64_C(0x52c5892d0d1d2e46)},
    {S8_INITIALIZER("fcvtps x0, h1\n"), {0x20, 0x00, 0xe8, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_64H_float2int"), UINT64_C(0x3e34fdfe3f8990a8)},
    {S8_INITIALIZER("fcvtps x0, s1\n"), {0x20, 0x00, 0x28, 0x9e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPS_64S_float2int"), UINT64_C(0x626cef0816527810)},
    {S8_INITIALIZER("fcvtpu w0, d1\n"), {0x20, 0x00, 0x69, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPU_32D_float2int"), UINT64_C(0x4207dbdaa7a5dde5)},
    {S8_INITIALIZER("fcvtpu w0, h1\n"), {0x20, 0x00, 0xe9, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPU_32H_float2int"), UINT64_C(0xb3005f0eaa49f0ba)},
    {S8_INITIALIZER("fcvtpu w0, s1\n"), {0x20, 0x00, 0x29, 0x1e}, S8_INITIALIZER("arm-a64@2026-06:FCVTPU_32S_float2int"), UINT64_C(0x1321ee9f6aa27348)},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcvt_gpr_boundary_cases[] = {
    {S8_INITIALIZER("FCVTAS W30, D29\n"), {0xbe, 0x03, 0x64, 0x1e}},
    {S8_INITIALIZER("FCVTAS W30, H29\n"), {0xbe, 0x03, 0xe4, 0x1e}},
    {S8_INITIALIZER("FCVTAS W30, S29\n"), {0xbe, 0x03, 0x24, 0x1e}},
    {S8_INITIALIZER("FCVTAS X30, D29\n"), {0xbe, 0x03, 0x64, 0x9e}},
    {S8_INITIALIZER("FCVTAS X30, H29\n"), {0xbe, 0x03, 0xe4, 0x9e}},
    {S8_INITIALIZER("FCVTAS X30, S29\n"), {0xbe, 0x03, 0x24, 0x9e}},
    {S8_INITIALIZER("FCVTAU W30, D29\n"), {0xbe, 0x03, 0x65, 0x1e}},
    {S8_INITIALIZER("FCVTAU W30, H29\n"), {0xbe, 0x03, 0xe5, 0x1e}},
    {S8_INITIALIZER("FCVTAU W30, S29\n"), {0xbe, 0x03, 0x25, 0x1e}},
    {S8_INITIALIZER("FCVTAU X30, D29\n"), {0xbe, 0x03, 0x65, 0x9e}},
    {S8_INITIALIZER("FCVTAU X30, H29\n"), {0xbe, 0x03, 0xe5, 0x9e}},
    {S8_INITIALIZER("FCVTAU X30, S29\n"), {0xbe, 0x03, 0x25, 0x9e}},
    {S8_INITIALIZER("FCVTMS W30, D29\n"), {0xbe, 0x03, 0x70, 0x1e}},
    {S8_INITIALIZER("FCVTMS W30, H29\n"), {0xbe, 0x03, 0xf0, 0x1e}},
    {S8_INITIALIZER("FCVTMS W30, S29\n"), {0xbe, 0x03, 0x30, 0x1e}},
    {S8_INITIALIZER("FCVTMS X30, D29\n"), {0xbe, 0x03, 0x70, 0x9e}},
    {S8_INITIALIZER("FCVTMS X30, H29\n"), {0xbe, 0x03, 0xf0, 0x9e}},
    {S8_INITIALIZER("FCVTMS X30, S29\n"), {0xbe, 0x03, 0x30, 0x9e}},
    {S8_INITIALIZER("FCVTMU W30, D29\n"), {0xbe, 0x03, 0x71, 0x1e}},
    {S8_INITIALIZER("FCVTMU W30, H29\n"), {0xbe, 0x03, 0xf1, 0x1e}},
    {S8_INITIALIZER("FCVTMU W30, S29\n"), {0xbe, 0x03, 0x31, 0x1e}},
    {S8_INITIALIZER("FCVTMU X30, D29\n"), {0xbe, 0x03, 0x71, 0x9e}},
    {S8_INITIALIZER("FCVTMU X30, H29\n"), {0xbe, 0x03, 0xf1, 0x9e}},
    {S8_INITIALIZER("FCVTMU X30, S29\n"), {0xbe, 0x03, 0x31, 0x9e}},
    {S8_INITIALIZER("FCVTNS W30, D29\n"), {0xbe, 0x03, 0x60, 0x1e}},
    {S8_INITIALIZER("FCVTNS W30, H29\n"), {0xbe, 0x03, 0xe0, 0x1e}},
    {S8_INITIALIZER("FCVTNS W30, S29\n"), {0xbe, 0x03, 0x20, 0x1e}},
    {S8_INITIALIZER("FCVTNS X30, D29\n"), {0xbe, 0x03, 0x60, 0x9e}},
    {S8_INITIALIZER("FCVTNS X30, H29\n"), {0xbe, 0x03, 0xe0, 0x9e}},
    {S8_INITIALIZER("FCVTNS X30, S29\n"), {0xbe, 0x03, 0x20, 0x9e}},
    {S8_INITIALIZER("FCVTNU W30, D29\n"), {0xbe, 0x03, 0x61, 0x1e}},
    {S8_INITIALIZER("FCVTNU W30, H29\n"), {0xbe, 0x03, 0xe1, 0x1e}},
    {S8_INITIALIZER("FCVTNU W30, S29\n"), {0xbe, 0x03, 0x21, 0x1e}},
    {S8_INITIALIZER("FCVTNU X30, D29\n"), {0xbe, 0x03, 0x61, 0x9e}},
    {S8_INITIALIZER("FCVTNU X30, H29\n"), {0xbe, 0x03, 0xe1, 0x9e}},
    {S8_INITIALIZER("FCVTNU X30, S29\n"), {0xbe, 0x03, 0x21, 0x9e}},
    {S8_INITIALIZER("FCVTPS W30, D29\n"), {0xbe, 0x03, 0x68, 0x1e}},
    {S8_INITIALIZER("FCVTPS W30, H29\n"), {0xbe, 0x03, 0xe8, 0x1e}},
    {S8_INITIALIZER("FCVTPS W30, S29\n"), {0xbe, 0x03, 0x28, 0x1e}},
    {S8_INITIALIZER("FCVTPS X30, D29\n"), {0xbe, 0x03, 0x68, 0x9e}},
    {S8_INITIALIZER("FCVTPS X30, H29\n"), {0xbe, 0x03, 0xe8, 0x9e}},
    {S8_INITIALIZER("FCVTPS X30, S29\n"), {0xbe, 0x03, 0x28, 0x9e}},
    {S8_INITIALIZER("FCVTPU W30, D29\n"), {0xbe, 0x03, 0x69, 0x1e}},
    {S8_INITIALIZER("FCVTPU W30, H29\n"), {0xbe, 0x03, 0xe9, 0x1e}},
    {S8_INITIALIZER("FCVTPU W30, S29\n"), {0xbe, 0x03, 0x29, 0x1e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_fcvt_gpr_zr_cases[] = {
    {S8_INITIALIZER("fcvtas wzr, d0\n"), {0x1f, 0x00, 0x64, 0x1e}},
    {S8_INITIALIZER("fcvtau xzr, h0\n"), {0x1f, 0x00, 0xe5, 0x9e}},
    {S8_INITIALIZER("fcvtms wzr, s0\n"), {0x1f, 0x00, 0x30, 0x1e}},
    {S8_INITIALIZER("fcvtmu xzr, d0\n"), {0x1f, 0x00, 0x71, 0x9e}},
    {S8_INITIALIZER("fcvtns wzr, h0\n"), {0x1f, 0x00, 0xe0, 0x1e}},
    {S8_INITIALIZER("fcvtnu xzr, s0\n"), {0x1f, 0x00, 0x21, 0x9e}},
    {S8_INITIALIZER("fcvtps wzr, d0\n"), {0x1f, 0x00, 0x68, 0x1e}},
    {S8_INITIALIZER("fcvtpu wzr, h0\n"), {0x1f, 0x00, 0xe9, 0x1e}},
};

/* Exhaustive legal vector shift-by-register forms. Bytes are independent
 * llvm-mc 22.1.8 encodings. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_shift_cases[] = {
    {S8_INITIALIZER("srshl v0.8b, v1.8b, v31.8b\n"), {0x20, 0x54, 0x3f, 0x0e}},
    {S8_INITIALIZER("srshl v0.16b, v1.16b, v31.16b\n"), {0x20, 0x54, 0x3f, 0x4e}},
    {S8_INITIALIZER("srshl v0.4h, v1.4h, v31.4h\n"), {0x20, 0x54, 0x7f, 0x0e}},
    {S8_INITIALIZER("srshl v0.8h, v1.8h, v31.8h\n"), {0x20, 0x54, 0x7f, 0x4e}},
    {S8_INITIALIZER("srshl v0.2s, v1.2s, v31.2s\n"), {0x20, 0x54, 0xbf, 0x0e}},
    {S8_INITIALIZER("srshl v0.4s, v1.4s, v31.4s\n"), {0x20, 0x54, 0xbf, 0x4e}},
    {S8_INITIALIZER("srshl v0.2d, v1.2d, v31.2d\n"), {0x20, 0x54, 0xff, 0x4e}},
    {S8_INITIALIZER("sshl v0.8b, v1.8b, v31.8b\n"), {0x20, 0x44, 0x3f, 0x0e}},
    {S8_INITIALIZER("sshl v0.16b, v1.16b, v31.16b\n"), {0x20, 0x44, 0x3f, 0x4e}},
    {S8_INITIALIZER("sshl v0.4h, v1.4h, v31.4h\n"), {0x20, 0x44, 0x7f, 0x0e}},
    {S8_INITIALIZER("sshl v0.8h, v1.8h, v31.8h\n"), {0x20, 0x44, 0x7f, 0x4e}},
    {S8_INITIALIZER("sshl v0.2s, v1.2s, v31.2s\n"), {0x20, 0x44, 0xbf, 0x0e}},
    {S8_INITIALIZER("sshl v0.4s, v1.4s, v31.4s\n"), {0x20, 0x44, 0xbf, 0x4e}},
    {S8_INITIALIZER("sshl v0.2d, v1.2d, v31.2d\n"), {0x20, 0x44, 0xff, 0x4e}},
    {S8_INITIALIZER("urshl v0.8b, v1.8b, v31.8b\n"), {0x20, 0x54, 0x3f, 0x2e}},
    {S8_INITIALIZER("urshl v0.16b, v1.16b, v31.16b\n"), {0x20, 0x54, 0x3f, 0x6e}},
    {S8_INITIALIZER("urshl v0.4h, v1.4h, v31.4h\n"), {0x20, 0x54, 0x7f, 0x2e}},
    {S8_INITIALIZER("urshl v0.8h, v1.8h, v31.8h\n"), {0x20, 0x54, 0x7f, 0x6e}},
    {S8_INITIALIZER("urshl v0.2s, v1.2s, v31.2s\n"), {0x20, 0x54, 0xbf, 0x2e}},
    {S8_INITIALIZER("urshl v0.4s, v1.4s, v31.4s\n"), {0x20, 0x54, 0xbf, 0x6e}},
    {S8_INITIALIZER("urshl v0.2d, v1.2d, v31.2d\n"), {0x20, 0x54, 0xff, 0x6e}},
    {S8_INITIALIZER("ushl v0.8b, v1.8b, v31.8b\n"), {0x20, 0x44, 0x3f, 0x2e}},
    {S8_INITIALIZER("ushl v0.16b, v1.16b, v31.16b\n"), {0x20, 0x44, 0x3f, 0x6e}},
    {S8_INITIALIZER("ushl v0.4h, v1.4h, v31.4h\n"), {0x20, 0x44, 0x7f, 0x2e}},
    {S8_INITIALIZER("ushl v0.8h, v1.8h, v31.8h\n"), {0x20, 0x44, 0x7f, 0x6e}},
    {S8_INITIALIZER("ushl v0.2s, v1.2s, v31.2s\n"), {0x20, 0x44, 0xbf, 0x2e}},
    {S8_INITIALIZER("ushl v0.4s, v1.4s, v31.4s\n"), {0x20, 0x44, 0xbf, 0x6e}},
    {S8_INITIALIZER("ushl v0.2d, v1.2d, v31.2d\n"), {0x20, 0x44, 0xff, 0x6e}},
};

/* Exhaustive legal arrangements for the same-register multiply family.
 * Bytes are independent llvm-mc 22.1.8 literals. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_multiply_cases[] = {
    {S8_INITIALIZER("mla v0.8b, v1.8b, v31.8b\n"), {0x20, 0x94, 0x3f, 0x0e}},
    {S8_INITIALIZER("mla v0.16b, v1.16b, v31.16b\n"), {0x20, 0x94, 0x3f, 0x4e}},
    {S8_INITIALIZER("mla v0.4h, v1.4h, v31.4h\n"), {0x20, 0x94, 0x7f, 0x0e}},
    {S8_INITIALIZER("mla v0.8h, v1.8h, v31.8h\n"), {0x20, 0x94, 0x7f, 0x4e}},
    {S8_INITIALIZER("mla v0.2s, v1.2s, v31.2s\n"), {0x20, 0x94, 0xbf, 0x0e}},
    {S8_INITIALIZER("mla v0.4s, v1.4s, v31.4s\n"), {0x20, 0x94, 0xbf, 0x4e}},
    {S8_INITIALIZER("mls v0.8b, v1.8b, v31.8b\n"), {0x20, 0x94, 0x3f, 0x2e}},
    {S8_INITIALIZER("mls v0.16b, v1.16b, v31.16b\n"), {0x20, 0x94, 0x3f, 0x6e}},
    {S8_INITIALIZER("mls v0.4h, v1.4h, v31.4h\n"), {0x20, 0x94, 0x7f, 0x2e}},
    {S8_INITIALIZER("mls v0.8h, v1.8h, v31.8h\n"), {0x20, 0x94, 0x7f, 0x6e}},
    {S8_INITIALIZER("mls v0.2s, v1.2s, v31.2s\n"), {0x20, 0x94, 0xbf, 0x2e}},
    {S8_INITIALIZER("mls v0.4s, v1.4s, v31.4s\n"), {0x20, 0x94, 0xbf, 0x6e}},
    {S8_INITIALIZER("mul v0.8b, v1.8b, v31.8b\n"), {0x20, 0x9c, 0x3f, 0x0e}},
    {S8_INITIALIZER("mul v0.16b, v1.16b, v31.16b\n"), {0x20, 0x9c, 0x3f, 0x4e}},
    {S8_INITIALIZER("mul v0.4h, v1.4h, v31.4h\n"), {0x20, 0x9c, 0x7f, 0x0e}},
    {S8_INITIALIZER("mul v0.8h, v1.8h, v31.8h\n"), {0x20, 0x9c, 0x7f, 0x4e}},
    {S8_INITIALIZER("mul v0.2s, v1.2s, v31.2s\n"), {0x20, 0x9c, 0xbf, 0x0e}},
    {S8_INITIALIZER("mul v0.4s, v1.4s, v31.4s\n"), {0x20, 0x9c, 0xbf, 0x4e}},
    {S8_INITIALIZER("pmul v0.8b, v1.8b, v31.8b\n"), {0x20, 0x9c, 0x3f, 0x2e}},
    {S8_INITIALIZER("pmul v0.16b, v1.16b, v31.16b\n"), {0x20, 0x9c, 0x3f, 0x6e}},
    {S8_INITIALIZER("sqdmulh v0.4h, v1.4h, v31.4h\n"), {0x20, 0xb4, 0x7f, 0x0e}},
    {S8_INITIALIZER("sqdmulh v0.8h, v1.8h, v31.8h\n"), {0x20, 0xb4, 0x7f, 0x4e}},
    {S8_INITIALIZER("sqdmulh v0.2s, v1.2s, v31.2s\n"), {0x20, 0xb4, 0xbf, 0x0e}},
    {S8_INITIALIZER("sqdmulh v0.4s, v1.4s, v31.4s\n"), {0x20, 0xb4, 0xbf, 0x4e}},
    {S8_INITIALIZER("sqdmulh h0, h1, h31\n"), {0x20, 0xb4, 0x7f, 0x5e}},
    {S8_INITIALIZER("sqdmulh s0, s1, s31\n"), {0x20, 0xb4, 0xbf, 0x5e}},
    {S8_INITIALIZER("sqrdmulh v0.4h, v1.4h, v31.4h\n"), {0x20, 0xb4, 0x7f, 0x2e}},
    {S8_INITIALIZER("sqrdmulh v0.8h, v1.8h, v31.8h\n"), {0x20, 0xb4, 0x7f, 0x6e}},
    {S8_INITIALIZER("sqrdmulh v0.2s, v1.2s, v31.2s\n"), {0x20, 0xb4, 0xbf, 0x2e}},
    {S8_INITIALIZER("sqrdmulh v0.4s, v1.4s, v31.4s\n"), {0x20, 0xb4, 0xbf, 0x6e}},
    {S8_INITIALIZER("sqrdmulh h0, h1, h31\n"), {0x20, 0xb4, 0x7f, 0x7e}},
    {S8_INITIALIZER("sqrdmulh s0, s1, s31\n"), {0x20, 0xb4, 0xbf, 0x7e}},
};

/* The bit-select trio is deliberately separate from the legacy M1/GPR
 * mnemonic corpus: these are the only new rows in this cohort. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_bitselect_cases[] = {
    {S8_INITIALIZER("bif v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0xe2, 0x2e}},
    {S8_INITIALIZER("bif v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0xe2, 0x6e}},
    {S8_INITIALIZER("bit v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0xa2, 0x2e}},
    {S8_INITIALIZER("bit v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0xa2, 0x6e}},
    {S8_INITIALIZER("bsl v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0x62, 0x2e}},
    {S8_INITIALIZER("bsl v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0x62, 0x6e}},
};

/* Exhaustive legal arrangements for the integer comparison same-register
 * cohort. Bytes are independent llvm-mc 22.1.8 literals. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_compare_cases[] = {
    {S8_INITIALIZER("cmeq v0.8b, v1.8b, v2.8b\n"), {0x20, 0x8c, 0x22, 0x2e}},
    {S8_INITIALIZER("cmeq v0.16b, v1.16b, v2.16b\n"), {0x20, 0x8c, 0x22, 0x6e}},
    {S8_INITIALIZER("cmeq v0.4h, v1.4h, v2.4h\n"), {0x20, 0x8c, 0x62, 0x2e}},
    {S8_INITIALIZER("cmeq v0.8h, v1.8h, v2.8h\n"), {0x20, 0x8c, 0x62, 0x6e}},
    {S8_INITIALIZER("cmeq v0.2s, v1.2s, v2.2s\n"), {0x20, 0x8c, 0xa2, 0x2e}},
    {S8_INITIALIZER("cmeq v0.4s, v1.4s, v2.4s\n"), {0x20, 0x8c, 0xa2, 0x6e}},
    {S8_INITIALIZER("cmeq v0.2d, v1.2d, v2.2d\n"), {0x20, 0x8c, 0xe2, 0x6e}},
    {S8_INITIALIZER("cmge v0.8b, v1.8b, v2.8b\n"), {0x20, 0x3c, 0x22, 0x0e}},
    {S8_INITIALIZER("cmge v0.16b, v1.16b, v2.16b\n"), {0x20, 0x3c, 0x22, 0x4e}},
    {S8_INITIALIZER("cmge v0.4h, v1.4h, v2.4h\n"), {0x20, 0x3c, 0x62, 0x0e}},
    {S8_INITIALIZER("cmge v0.8h, v1.8h, v2.8h\n"), {0x20, 0x3c, 0x62, 0x4e}},
    {S8_INITIALIZER("cmge v0.2s, v1.2s, v2.2s\n"), {0x20, 0x3c, 0xa2, 0x0e}},
    {S8_INITIALIZER("cmge v0.4s, v1.4s, v2.4s\n"), {0x20, 0x3c, 0xa2, 0x4e}},
    {S8_INITIALIZER("cmge v0.2d, v1.2d, v2.2d\n"), {0x20, 0x3c, 0xe2, 0x4e}},
    {S8_INITIALIZER("cmgt v0.8b, v1.8b, v2.8b\n"), {0x20, 0x34, 0x22, 0x0e}},
    {S8_INITIALIZER("cmgt v0.16b, v1.16b, v2.16b\n"), {0x20, 0x34, 0x22, 0x4e}},
    {S8_INITIALIZER("cmgt v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0x62, 0x0e}},
    {S8_INITIALIZER("cmgt v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0x62, 0x4e}},
    {S8_INITIALIZER("cmgt v0.2s, v1.2s, v2.2s\n"), {0x20, 0x34, 0xa2, 0x0e}},
    {S8_INITIALIZER("cmgt v0.4s, v1.4s, v2.4s\n"), {0x20, 0x34, 0xa2, 0x4e}},
    {S8_INITIALIZER("cmgt v0.2d, v1.2d, v2.2d\n"), {0x20, 0x34, 0xe2, 0x4e}},
    {S8_INITIALIZER("cmhi v0.8b, v1.8b, v2.8b\n"), {0x20, 0x34, 0x22, 0x2e}},
    {S8_INITIALIZER("cmhi v0.16b, v1.16b, v2.16b\n"), {0x20, 0x34, 0x22, 0x6e}},
    {S8_INITIALIZER("cmhi v0.4h, v1.4h, v2.4h\n"), {0x20, 0x34, 0x62, 0x2e}},
    {S8_INITIALIZER("cmhi v0.8h, v1.8h, v2.8h\n"), {0x20, 0x34, 0x62, 0x6e}},
    {S8_INITIALIZER("cmhi v0.2s, v1.2s, v2.2s\n"), {0x20, 0x34, 0xa2, 0x2e}},
    {S8_INITIALIZER("cmhi v0.4s, v1.4s, v2.4s\n"), {0x20, 0x34, 0xa2, 0x6e}},
    {S8_INITIALIZER("cmhi v0.2d, v1.2d, v2.2d\n"), {0x20, 0x34, 0xe2, 0x6e}},
    {S8_INITIALIZER("cmhs v0.8b, v1.8b, v2.8b\n"), {0x20, 0x3c, 0x22, 0x2e}},
    {S8_INITIALIZER("cmhs v0.16b, v1.16b, v2.16b\n"), {0x20, 0x3c, 0x22, 0x6e}},
    {S8_INITIALIZER("cmhs v0.4h, v1.4h, v2.4h\n"), {0x20, 0x3c, 0x62, 0x2e}},
    {S8_INITIALIZER("cmhs v0.8h, v1.8h, v2.8h\n"), {0x20, 0x3c, 0x62, 0x6e}},
    {S8_INITIALIZER("cmhs v0.2s, v1.2s, v2.2s\n"), {0x20, 0x3c, 0xa2, 0x2e}},
    {S8_INITIALIZER("cmhs v0.4s, v1.4s, v2.4s\n"), {0x20, 0x3c, 0xa2, 0x6e}},
    {S8_INITIALIZER("cmhs v0.2d, v1.2d, v2.2d\n"), {0x20, 0x3c, 0xe2, 0x6e}},
    {S8_INITIALIZER("cmtst v0.8b, v1.8b, v2.8b\n"), {0x20, 0x8c, 0x22, 0x0e}},
    {S8_INITIALIZER("cmtst v0.16b, v1.16b, v2.16b\n"), {0x20, 0x8c, 0x22, 0x4e}},
    {S8_INITIALIZER("cmtst v0.4h, v1.4h, v2.4h\n"), {0x20, 0x8c, 0x62, 0x0e}},
    {S8_INITIALIZER("cmtst v0.8h, v1.8h, v2.8h\n"), {0x20, 0x8c, 0x62, 0x4e}},
    {S8_INITIALIZER("cmtst v0.2s, v1.2s, v2.2s\n"), {0x20, 0x8c, 0xa2, 0x0e}},
    {S8_INITIALIZER("cmtst v0.4s, v1.4s, v2.4s\n"), {0x20, 0x8c, 0xa2, 0x4e}},
    {S8_INITIALIZER("cmtst v0.2d, v1.2d, v2.2d\n"), {0x20, 0x8c, 0xe2, 0x4e}},
};

static AssemblyA64DirectSIMDNeonCase const assembly_a64_direct_simd_neon_cases[] = {
    {S8_INITIALIZER("addp d0, v1.2d\n"), {0x20, 0xb8, 0xf1, 0x5e}, S8_INITIALIZER("addp d31, v30.2d\n"), {0xdf, 0xbb, 0xf1, 0x5e}},
    {S8_INITIALIZER("cmeq d0, d1, d2\n"), {0x20, 0x8c, 0xe2, 0x7e}, S8_INITIALIZER("cmeq d31, d30, d29\n"), {0xdf, 0x8f, 0xfd, 0x7e}},
    {S8_INITIALIZER("cmge d0, d1, d2\n"), {0x20, 0x3c, 0xe2, 0x5e}, S8_INITIALIZER("cmge d31, d30, d29\n"), {0xdf, 0x3f, 0xfd, 0x5e}},
    {S8_INITIALIZER("cmgt d0, d1, d2\n"), {0x20, 0x34, 0xe2, 0x5e}, S8_INITIALIZER("cmgt d31, d30, d29\n"), {0xdf, 0x37, 0xfd, 0x5e}},
    {S8_INITIALIZER("cmhi d0, d1, d2\n"), {0x20, 0x34, 0xe2, 0x7e}, S8_INITIALIZER("cmhi d31, d30, d29\n"), {0xdf, 0x37, 0xfd, 0x7e}},
    {S8_INITIALIZER("cmhs d0, d1, d2\n"), {0x20, 0x3c, 0xe2, 0x7e}, S8_INITIALIZER("cmhs d31, d30, d29\n"), {0xdf, 0x3f, 0xfd, 0x7e}},
    {S8_INITIALIZER("cmtst d0, d1, d2\n"), {0x20, 0x8c, 0xe2, 0x5e}, S8_INITIALIZER("cmtst d31, d30, d29\n"), {0xdf, 0x8f, 0xfd, 0x5e}},
    {S8_INITIALIZER("fcvtxn s0, d1\n"), {0x20, 0x68, 0x61, 0x7e}, S8_INITIALIZER("fcvtxn s31, d30\n"), {0xdf, 0x6b, 0x61, 0x7e}},
    {S8_INITIALIZER("fmaxnmv s0, v1.4s\n"), {0x20, 0xc8, 0x30, 0x6e}, S8_INITIALIZER("fmaxnmv s31, v30.4s\n"), {0xdf, 0xcb, 0x30, 0x6e}},
    {S8_INITIALIZER("fmaxv s0, v1.4s\n"), {0x20, 0xf8, 0x30, 0x6e}, S8_INITIALIZER("fmaxv s31, v30.4s\n"), {0xdf, 0xfb, 0x30, 0x6e}},
    {S8_INITIALIZER("fminnmv s0, v1.4s\n"), {0x20, 0xc8, 0xb0, 0x6e}, S8_INITIALIZER("fminnmv s31, v30.4s\n"), {0xdf, 0xcb, 0xb0, 0x6e}},
    {S8_INITIALIZER("fminv s0, v1.4s\n"), {0x20, 0xf8, 0xb0, 0x6e}, S8_INITIALIZER("fminv s31, v30.4s\n"), {0xdf, 0xfb, 0xb0, 0x6e}},
    {S8_INITIALIZER("srshl d0, d1, d2\n"), {0x20, 0x54, 0xe2, 0x5e}, S8_INITIALIZER("srshl d31, d30, d29\n"), {0xdf, 0x57, 0xfd, 0x5e}},
    {S8_INITIALIZER("sshl d0, d1, d2\n"), {0x20, 0x44, 0xe2, 0x5e}, S8_INITIALIZER("sshl d31, d30, d29\n"), {0xdf, 0x47, 0xfd, 0x5e}},
    {S8_INITIALIZER("urshl d0, d1, d2\n"), {0x20, 0x54, 0xe2, 0x7e}, S8_INITIALIZER("urshl d31, d30, d29\n"), {0xdf, 0x57, 0xfd, 0x7e}},
    {S8_INITIALIZER("ushl d0, d1, d2\n"), {0x20, 0x44, 0xe2, 0x7e}, S8_INITIALIZER("ushl d31, d30, d29\n"), {0xdf, 0x47, 0xfd, 0x7e}},
};

typedef struct AssemblyA64DirectSIMDTransformCase AssemblyA64DirectSIMDTransformCase;
struct AssemblyA64DirectSIMDTransformCase
{
    String8 source;
    u8 bytes[4];
};

static AssemblyA64DirectSIMDTransformCase const assembly_a64_direct_simd_transform_cases[] = {
    {S8_INITIALIZER("abs v0.8b, v1.8b\n"), {0x20, 0xb8, 0x20, 0x0e}},
    {S8_INITIALIZER("abs v0.16b, v1.16b\n"), {0x20, 0xb8, 0x20, 0x4e}},
    {S8_INITIALIZER("abs v0.4h, v1.4h\n"), {0x20, 0xb8, 0x60, 0x0e}},
    {S8_INITIALIZER("abs v0.8h, v1.8h\n"), {0x20, 0xb8, 0x60, 0x4e}},
    {S8_INITIALIZER("abs v0.2s, v1.2s\n"), {0x20, 0xb8, 0xa0, 0x0e}},
    {S8_INITIALIZER("abs v0.4s, v1.4s\n"), {0x20, 0xb8, 0xa0, 0x4e}},
    {S8_INITIALIZER("abs v0.2d, v1.2d\n"), {0x20, 0xb8, 0xe0, 0x4e}},
    {S8_INITIALIZER("neg v0.8b, v1.8b\n"), {0x20, 0xb8, 0x20, 0x2e}},
    {S8_INITIALIZER("neg v0.16b, v1.16b\n"), {0x20, 0xb8, 0x20, 0x6e}},
    {S8_INITIALIZER("neg v0.4h, v1.4h\n"), {0x20, 0xb8, 0x60, 0x2e}},
    {S8_INITIALIZER("neg v0.8h, v1.8h\n"), {0x20, 0xb8, 0x60, 0x6e}},
    {S8_INITIALIZER("neg v0.2s, v1.2s\n"), {0x20, 0xb8, 0xa0, 0x2e}},
    {S8_INITIALIZER("neg v0.4s, v1.4s\n"), {0x20, 0xb8, 0xa0, 0x6e}},
    {S8_INITIALIZER("neg v0.2d, v1.2d\n"), {0x20, 0xb8, 0xe0, 0x6e}},
    {S8_INITIALIZER("shadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x04, 0x22, 0x0e}},
    {S8_INITIALIZER("shadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x04, 0x22, 0x4e}},
    {S8_INITIALIZER("shadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0x62, 0x0e}},
    {S8_INITIALIZER("shadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0x62, 0x4e}},
    {S8_INITIALIZER("shadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x04, 0xa2, 0x0e}},
    {S8_INITIALIZER("shadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x04, 0xa2, 0x4e}},
    {S8_INITIALIZER("shsub v0.8b, v1.8b, v2.8b\n"), {0x20, 0x24, 0x22, 0x0e}},
    {S8_INITIALIZER("shsub v0.16b, v1.16b, v2.16b\n"), {0x20, 0x24, 0x22, 0x4e}},
    {S8_INITIALIZER("shsub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x24, 0x62, 0x0e}},
    {S8_INITIALIZER("shsub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x24, 0x62, 0x4e}},
    {S8_INITIALIZER("shsub v0.2s, v1.2s, v2.2s\n"), {0x20, 0x24, 0xa2, 0x0e}},
    {S8_INITIALIZER("shsub v0.4s, v1.4s, v2.4s\n"), {0x20, 0x24, 0xa2, 0x4e}},
    {S8_INITIALIZER("cnt v0.8b, v1.8b\n"), {0x20, 0x58, 0x20, 0x0e}},
    {S8_INITIALIZER("cnt v0.16b, v1.16b\n"), {0x20, 0x58, 0x20, 0x4e}},
    {S8_INITIALIZER("trn1 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x28, 0x02, 0x0e}},
    {S8_INITIALIZER("trn1 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x28, 0x02, 0x4e}},
    {S8_INITIALIZER("trn1 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x28, 0x42, 0x0e}},
    {S8_INITIALIZER("trn1 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x28, 0x42, 0x4e}},
    {S8_INITIALIZER("trn1 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x28, 0x82, 0x0e}},
    {S8_INITIALIZER("trn1 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x28, 0x82, 0x4e}},
    {S8_INITIALIZER("trn1 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x28, 0xc2, 0x4e}},
    {S8_INITIALIZER("trn2 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x68, 0x02, 0x0e}},
    {S8_INITIALIZER("trn2 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x68, 0x02, 0x4e}},
    {S8_INITIALIZER("trn2 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x68, 0x42, 0x0e}},
    {S8_INITIALIZER("trn2 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x68, 0x42, 0x4e}},
    {S8_INITIALIZER("trn2 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x68, 0x82, 0x0e}},
    {S8_INITIALIZER("trn2 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x68, 0x82, 0x4e}},
    {S8_INITIALIZER("trn2 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x68, 0xc2, 0x4e}},
    {S8_INITIALIZER("uzp1 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x18, 0x02, 0x0e}},
    {S8_INITIALIZER("uzp1 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x18, 0x02, 0x4e}},
    {S8_INITIALIZER("uzp1 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x18, 0x42, 0x0e}},
    {S8_INITIALIZER("uzp1 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x18, 0x42, 0x4e}},
    {S8_INITIALIZER("uzp1 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x18, 0x82, 0x0e}},
    {S8_INITIALIZER("uzp1 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x18, 0x82, 0x4e}},
    {S8_INITIALIZER("uzp1 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x18, 0xc2, 0x4e}},
    {S8_INITIALIZER("uzp2 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x58, 0x02, 0x0e}},
    {S8_INITIALIZER("uzp2 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x58, 0x02, 0x4e}},
    {S8_INITIALIZER("uzp2 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x58, 0x42, 0x0e}},
    {S8_INITIALIZER("uzp2 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x58, 0x42, 0x4e}},
    {S8_INITIALIZER("uzp2 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x58, 0x82, 0x0e}},
    {S8_INITIALIZER("uzp2 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x58, 0x82, 0x4e}},
    {S8_INITIALIZER("uzp2 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x58, 0xc2, 0x4e}},
    {S8_INITIALIZER("zip1 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x38, 0x02, 0x0e}},
    {S8_INITIALIZER("zip1 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x38, 0x02, 0x4e}},
    {S8_INITIALIZER("zip1 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x38, 0x42, 0x0e}},
    {S8_INITIALIZER("zip1 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x38, 0x42, 0x4e}},
    {S8_INITIALIZER("zip1 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x38, 0x82, 0x0e}},
    {S8_INITIALIZER("zip1 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x38, 0x82, 0x4e}},
    {S8_INITIALIZER("zip1 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x38, 0xc2, 0x4e}},
    {S8_INITIALIZER("zip2 v0.8b, v1.8b, v2.8b\n"), {0x20, 0x78, 0x02, 0x0e}},
    {S8_INITIALIZER("zip2 v0.16b, v1.16b, v2.16b\n"), {0x20, 0x78, 0x02, 0x4e}},
    {S8_INITIALIZER("zip2 v0.4h, v1.4h, v2.4h\n"), {0x20, 0x78, 0x42, 0x0e}},
    {S8_INITIALIZER("zip2 v0.8h, v1.8h, v2.8h\n"), {0x20, 0x78, 0x42, 0x4e}},
    {S8_INITIALIZER("zip2 v0.2s, v1.2s, v2.2s\n"), {0x20, 0x78, 0x82, 0x0e}},
    {S8_INITIALIZER("zip2 v0.4s, v1.4s, v2.4s\n"), {0x20, 0x78, 0x82, 0x4e}},
    {S8_INITIALIZER("zip2 v0.2d, v1.2d, v2.2d\n"), {0x20, 0x78, 0xc2, 0x4e}},
    {S8_INITIALIZER("smaxp v0.8b, v1.8b, v2.8b\n"), {0x20, 0xa4, 0x22, 0x0e}},
    {S8_INITIALIZER("smaxp v0.16b, v1.16b, v2.16b\n"), {0x20, 0xa4, 0x22, 0x4e}},
    {S8_INITIALIZER("smaxp v0.4h, v1.4h, v2.4h\n"), {0x20, 0xa4, 0x62, 0x0e}},
    {S8_INITIALIZER("smaxp v0.8h, v1.8h, v2.8h\n"), {0x20, 0xa4, 0x62, 0x4e}},
    {S8_INITIALIZER("smaxp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xa4, 0xa2, 0x0e}},
    {S8_INITIALIZER("smaxp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xa4, 0xa2, 0x4e}},
    {S8_INITIALIZER("smax v0.8b, v1.8b, v2.8b\n"), {0x20, 0x64, 0x22, 0x0e}},
    {S8_INITIALIZER("smax v0.16b, v1.16b, v2.16b\n"), {0x20, 0x64, 0x22, 0x4e}},
    {S8_INITIALIZER("smax v0.4h, v1.4h, v2.4h\n"), {0x20, 0x64, 0x62, 0x0e}},
    {S8_INITIALIZER("smax v0.8h, v1.8h, v2.8h\n"), {0x20, 0x64, 0x62, 0x4e}},
    {S8_INITIALIZER("smax v0.2s, v1.2s, v2.2s\n"), {0x20, 0x64, 0xa2, 0x0e}},
    {S8_INITIALIZER("smax v0.4s, v1.4s, v2.4s\n"), {0x20, 0x64, 0xa2, 0x4e}},
    {S8_INITIALIZER("sminp v0.8b, v1.8b, v2.8b\n"), {0x20, 0xac, 0x22, 0x0e}},
    {S8_INITIALIZER("sminp v0.16b, v1.16b, v2.16b\n"), {0x20, 0xac, 0x22, 0x4e}},
    {S8_INITIALIZER("sminp v0.4h, v1.4h, v2.4h\n"), {0x20, 0xac, 0x62, 0x0e}},
    {S8_INITIALIZER("sminp v0.8h, v1.8h, v2.8h\n"), {0x20, 0xac, 0x62, 0x4e}},
    {S8_INITIALIZER("sminp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xac, 0xa2, 0x0e}},
    {S8_INITIALIZER("sminp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xac, 0xa2, 0x4e}},
    {S8_INITIALIZER("smin v0.8b, v1.8b, v2.8b\n"), {0x20, 0x6c, 0x22, 0x0e}},
    {S8_INITIALIZER("smin v0.16b, v1.16b, v2.16b\n"), {0x20, 0x6c, 0x22, 0x4e}},
    {S8_INITIALIZER("smin v0.4h, v1.4h, v2.4h\n"), {0x20, 0x6c, 0x62, 0x0e}},
    {S8_INITIALIZER("smin v0.8h, v1.8h, v2.8h\n"), {0x20, 0x6c, 0x62, 0x4e}},
    {S8_INITIALIZER("smin v0.2s, v1.2s, v2.2s\n"), {0x20, 0x6c, 0xa2, 0x0e}},
    {S8_INITIALIZER("smin v0.4s, v1.4s, v2.4s\n"), {0x20, 0x6c, 0xa2, 0x4e}},
    {S8_INITIALIZER("umaxp v0.8b, v1.8b, v2.8b\n"), {0x20, 0xa4, 0x22, 0x2e}},
    {S8_INITIALIZER("umaxp v0.16b, v1.16b, v2.16b\n"), {0x20, 0xa4, 0x22, 0x6e}},
    {S8_INITIALIZER("umaxp v0.4h, v1.4h, v2.4h\n"), {0x20, 0xa4, 0x62, 0x2e}},
    {S8_INITIALIZER("umaxp v0.8h, v1.8h, v2.8h\n"), {0x20, 0xa4, 0x62, 0x6e}},
    {S8_INITIALIZER("umaxp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xa4, 0xa2, 0x2e}},
    {S8_INITIALIZER("umaxp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xa4, 0xa2, 0x6e}},
    {S8_INITIALIZER("umax v0.8b, v1.8b, v2.8b\n"), {0x20, 0x64, 0x22, 0x2e}},
    {S8_INITIALIZER("umax v0.16b, v1.16b, v2.16b\n"), {0x20, 0x64, 0x22, 0x6e}},
    {S8_INITIALIZER("umax v0.4h, v1.4h, v2.4h\n"), {0x20, 0x64, 0x62, 0x2e}},
    {S8_INITIALIZER("umax v0.8h, v1.8h, v2.8h\n"), {0x20, 0x64, 0x62, 0x6e}},
    {S8_INITIALIZER("umax v0.2s, v1.2s, v2.2s\n"), {0x20, 0x64, 0xa2, 0x2e}},
    {S8_INITIALIZER("umax v0.4s, v1.4s, v2.4s\n"), {0x20, 0x64, 0xa2, 0x6e}},
    {S8_INITIALIZER("uminp v0.8b, v1.8b, v2.8b\n"), {0x20, 0xac, 0x22, 0x2e}},
    {S8_INITIALIZER("uminp v0.16b, v1.16b, v2.16b\n"), {0x20, 0xac, 0x22, 0x6e}},
    {S8_INITIALIZER("uminp v0.4h, v1.4h, v2.4h\n"), {0x20, 0xac, 0x62, 0x2e}},
    {S8_INITIALIZER("uminp v0.8h, v1.8h, v2.8h\n"), {0x20, 0xac, 0x62, 0x6e}},
    {S8_INITIALIZER("uminp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xac, 0xa2, 0x2e}},
    {S8_INITIALIZER("uminp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xac, 0xa2, 0x6e}},
    {S8_INITIALIZER("umin v0.8b, v1.8b, v2.8b\n"), {0x20, 0x6c, 0x22, 0x2e}},
    {S8_INITIALIZER("umin v0.16b, v1.16b, v2.16b\n"), {0x20, 0x6c, 0x22, 0x6e}},
    {S8_INITIALIZER("umin v0.4h, v1.4h, v2.4h\n"), {0x20, 0x6c, 0x62, 0x2e}},
    {S8_INITIALIZER("umin v0.8h, v1.8h, v2.8h\n"), {0x20, 0x6c, 0x62, 0x6e}},
    {S8_INITIALIZER("umin v0.2s, v1.2s, v2.2s\n"), {0x20, 0x6c, 0xa2, 0x2e}},
    {S8_INITIALIZER("umin v0.4s, v1.4s, v2.4s\n"), {0x20, 0x6c, 0xa2, 0x6e}},
    {S8_INITIALIZER("sqadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x0c, 0x22, 0x0e}},
    {S8_INITIALIZER("sqadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x0c, 0x22, 0x4e}},
    {S8_INITIALIZER("sqadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x0c, 0x62, 0x0e}},
    {S8_INITIALIZER("sqadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x0c, 0x62, 0x4e}},
    {S8_INITIALIZER("sqadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x0c, 0xa2, 0x0e}},
    {S8_INITIALIZER("sqadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x0c, 0xa2, 0x4e}},
    {S8_INITIALIZER("sqadd v0.2d, v1.2d, v2.2d\n"), {0x20, 0x0c, 0xe2, 0x4e}},
    {S8_INITIALIZER("sqsub v0.8b, v1.8b, v2.8b\n"), {0x20, 0x2c, 0x22, 0x0e}},
    {S8_INITIALIZER("sqsub v0.16b, v1.16b, v2.16b\n"), {0x20, 0x2c, 0x22, 0x4e}},
    {S8_INITIALIZER("sqsub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x2c, 0x62, 0x0e}},
    {S8_INITIALIZER("sqsub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x2c, 0x62, 0x4e}},
    {S8_INITIALIZER("sqsub v0.2s, v1.2s, v2.2s\n"), {0x20, 0x2c, 0xa2, 0x0e}},
    {S8_INITIALIZER("sqsub v0.4s, v1.4s, v2.4s\n"), {0x20, 0x2c, 0xa2, 0x4e}},
    {S8_INITIALIZER("sqsub v0.2d, v1.2d, v2.2d\n"), {0x20, 0x2c, 0xe2, 0x4e}},
    {S8_INITIALIZER("uqadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x0c, 0x22, 0x2e}},
    {S8_INITIALIZER("uqadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x0c, 0x22, 0x6e}},
    {S8_INITIALIZER("uqadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x0c, 0x62, 0x2e}},
    {S8_INITIALIZER("uqadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x0c, 0x62, 0x6e}},
    {S8_INITIALIZER("uqadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x0c, 0xa2, 0x2e}},
    {S8_INITIALIZER("uqadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x0c, 0xa2, 0x6e}},
    {S8_INITIALIZER("uqadd v0.2d, v1.2d, v2.2d\n"), {0x20, 0x0c, 0xe2, 0x6e}},
    {S8_INITIALIZER("uqsub v0.8b, v1.8b, v2.8b\n"), {0x20, 0x2c, 0x22, 0x2e}},
    {S8_INITIALIZER("uqsub v0.16b, v1.16b, v2.16b\n"), {0x20, 0x2c, 0x22, 0x6e}},
    {S8_INITIALIZER("uqsub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x2c, 0x62, 0x2e}},
    {S8_INITIALIZER("uqsub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x2c, 0x62, 0x6e}},
    {S8_INITIALIZER("uqsub v0.2s, v1.2s, v2.2s\n"), {0x20, 0x2c, 0xa2, 0x2e}},
    {S8_INITIALIZER("uqsub v0.4s, v1.4s, v2.4s\n"), {0x20, 0x2c, 0xa2, 0x6e}},
    {S8_INITIALIZER("uqsub v0.2d, v1.2d, v2.2d\n"), {0x20, 0x2c, 0xe2, 0x6e}},
    {S8_INITIALIZER("sqrshl v0.8b, v1.8b, v2.8b\n"), {0x20, 0x5c, 0x22, 0x0e}},
    {S8_INITIALIZER("sqrshl v0.16b, v1.16b, v2.16b\n"), {0x20, 0x5c, 0x22, 0x4e}},
    {S8_INITIALIZER("sqrshl v0.4h, v1.4h, v2.4h\n"), {0x20, 0x5c, 0x62, 0x0e}},
    {S8_INITIALIZER("sqrshl v0.8h, v1.8h, v2.8h\n"), {0x20, 0x5c, 0x62, 0x4e}},
    {S8_INITIALIZER("sqrshl v0.2s, v1.2s, v2.2s\n"), {0x20, 0x5c, 0xa2, 0x0e}},
    {S8_INITIALIZER("sqrshl v0.4s, v1.4s, v2.4s\n"), {0x20, 0x5c, 0xa2, 0x4e}},
    {S8_INITIALIZER("sqrshl v0.2d, v1.2d, v2.2d\n"), {0x20, 0x5c, 0xe2, 0x4e}},
    {S8_INITIALIZER("sqshl v0.8b, v1.8b, v2.8b\n"), {0x20, 0x4c, 0x22, 0x0e}},
    {S8_INITIALIZER("sqshl v0.16b, v1.16b, v2.16b\n"), {0x20, 0x4c, 0x22, 0x4e}},
    {S8_INITIALIZER("sqshl v0.4h, v1.4h, v2.4h\n"), {0x20, 0x4c, 0x62, 0x0e}},
    {S8_INITIALIZER("sqshl v0.8h, v1.8h, v2.8h\n"), {0x20, 0x4c, 0x62, 0x4e}},
    {S8_INITIALIZER("sqshl v0.2s, v1.2s, v2.2s\n"), {0x20, 0x4c, 0xa2, 0x0e}},
    {S8_INITIALIZER("sqshl v0.4s, v1.4s, v2.4s\n"), {0x20, 0x4c, 0xa2, 0x4e}},
    {S8_INITIALIZER("sqshl v0.2d, v1.2d, v2.2d\n"), {0x20, 0x4c, 0xe2, 0x4e}},
    {S8_INITIALIZER("uqrshl v0.8b, v1.8b, v2.8b\n"), {0x20, 0x5c, 0x22, 0x2e}},
    {S8_INITIALIZER("uqrshl v0.16b, v1.16b, v2.16b\n"), {0x20, 0x5c, 0x22, 0x6e}},
    {S8_INITIALIZER("uqrshl v0.4h, v1.4h, v2.4h\n"), {0x20, 0x5c, 0x62, 0x2e}},
    {S8_INITIALIZER("uqrshl v0.8h, v1.8h, v2.8h\n"), {0x20, 0x5c, 0x62, 0x6e}},
    {S8_INITIALIZER("uqrshl v0.2s, v1.2s, v2.2s\n"), {0x20, 0x5c, 0xa2, 0x2e}},
    {S8_INITIALIZER("uqrshl v0.4s, v1.4s, v2.4s\n"), {0x20, 0x5c, 0xa2, 0x6e}},
    {S8_INITIALIZER("uqrshl v0.2d, v1.2d, v2.2d\n"), {0x20, 0x5c, 0xe2, 0x6e}},
    {S8_INITIALIZER("uqshl v0.8b, v1.8b, v2.8b\n"), {0x20, 0x4c, 0x22, 0x2e}},
    {S8_INITIALIZER("uqshl v0.16b, v1.16b, v2.16b\n"), {0x20, 0x4c, 0x22, 0x6e}},
    {S8_INITIALIZER("uqshl v0.4h, v1.4h, v2.4h\n"), {0x20, 0x4c, 0x62, 0x2e}},
    {S8_INITIALIZER("uqshl v0.8h, v1.8h, v2.8h\n"), {0x20, 0x4c, 0x62, 0x6e}},
    {S8_INITIALIZER("uqshl v0.2s, v1.2s, v2.2s\n"), {0x20, 0x4c, 0xa2, 0x2e}},
    {S8_INITIALIZER("uqshl v0.4s, v1.4s, v2.4s\n"), {0x20, 0x4c, 0xa2, 0x6e}},
    {S8_INITIALIZER("uqshl v0.2d, v1.2d, v2.2d\n"), {0x20, 0x4c, 0xe2, 0x6e}},
    {S8_INITIALIZER("srhadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x14, 0x22, 0x0e}},
    {S8_INITIALIZER("srhadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x14, 0x22, 0x4e}},
    {S8_INITIALIZER("srhadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0x62, 0x0e}},
    {S8_INITIALIZER("srhadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0x62, 0x4e}},
    {S8_INITIALIZER("srhadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x14, 0xa2, 0x0e}},
    {S8_INITIALIZER("srhadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x14, 0xa2, 0x4e}},
    {S8_INITIALIZER("urhadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x14, 0x22, 0x2e}},
    {S8_INITIALIZER("urhadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x14, 0x22, 0x6e}},
    {S8_INITIALIZER("urhadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x14, 0x62, 0x2e}},
    {S8_INITIALIZER("urhadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x14, 0x62, 0x6e}},
    {S8_INITIALIZER("urhadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x14, 0xa2, 0x2e}},
    {S8_INITIALIZER("urhadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x14, 0xa2, 0x6e}},
    {S8_INITIALIZER("uhadd v0.8b, v1.8b, v2.8b\n"), {0x20, 0x04, 0x22, 0x2e}},
    {S8_INITIALIZER("uhadd v0.16b, v1.16b, v2.16b\n"), {0x20, 0x04, 0x22, 0x6e}},
    {S8_INITIALIZER("uhadd v0.4h, v1.4h, v2.4h\n"), {0x20, 0x04, 0x62, 0x2e}},
    {S8_INITIALIZER("uhadd v0.8h, v1.8h, v2.8h\n"), {0x20, 0x04, 0x62, 0x6e}},
    {S8_INITIALIZER("uhadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0x04, 0xa2, 0x2e}},
    {S8_INITIALIZER("uhadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0x04, 0xa2, 0x6e}},
    {S8_INITIALIZER("uhsub v0.8b, v1.8b, v2.8b\n"), {0x20, 0x24, 0x22, 0x2e}},
    {S8_INITIALIZER("uhsub v0.16b, v1.16b, v2.16b\n"), {0x20, 0x24, 0x22, 0x6e}},
    {S8_INITIALIZER("uhsub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x24, 0x62, 0x2e}},
    {S8_INITIALIZER("uhsub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x24, 0x62, 0x6e}},
    {S8_INITIALIZER("uhsub v0.2s, v1.2s, v2.2s\n"), {0x20, 0x24, 0xa2, 0x2e}},
    {S8_INITIALIZER("uhsub v0.4s, v1.4s, v2.4s\n"), {0x20, 0x24, 0xa2, 0x6e}},
    {S8_INITIALIZER("sqabs v0.8b, v1.8b\n"), {0x20, 0x78, 0x20, 0x0e}},
    {S8_INITIALIZER("sqabs v0.16b, v1.16b\n"), {0x20, 0x78, 0x20, 0x4e}},
    {S8_INITIALIZER("sqabs v0.4h, v1.4h\n"), {0x20, 0x78, 0x60, 0x0e}},
    {S8_INITIALIZER("sqabs v0.8h, v1.8h\n"), {0x20, 0x78, 0x60, 0x4e}},
    {S8_INITIALIZER("sqabs v0.2s, v1.2s\n"), {0x20, 0x78, 0xa0, 0x0e}},
    {S8_INITIALIZER("sqabs v0.4s, v1.4s\n"), {0x20, 0x78, 0xa0, 0x4e}},
    {S8_INITIALIZER("sqabs v0.2d, v1.2d\n"), {0x20, 0x78, 0xe0, 0x4e}},
    {S8_INITIALIZER("sqneg v0.8b, v1.8b\n"), {0x20, 0x78, 0x20, 0x2e}},
    {S8_INITIALIZER("sqneg v0.16b, v1.16b\n"), {0x20, 0x78, 0x20, 0x6e}},
    {S8_INITIALIZER("sqneg v0.4h, v1.4h\n"), {0x20, 0x78, 0x60, 0x2e}},
    {S8_INITIALIZER("sqneg v0.8h, v1.8h\n"), {0x20, 0x78, 0x60, 0x6e}},
    {S8_INITIALIZER("sqneg v0.2s, v1.2s\n"), {0x20, 0x78, 0xa0, 0x2e}},
    {S8_INITIALIZER("sqneg v0.4s, v1.4s\n"), {0x20, 0x78, 0xa0, 0x6e}},
    {S8_INITIALIZER("sqneg v0.2d, v1.2d\n"), {0x20, 0x78, 0xe0, 0x6e}},
    {S8_INITIALIZER("suqadd v0.8b, v1.8b\n"), {0x20, 0x38, 0x20, 0x0e}},
    {S8_INITIALIZER("suqadd v0.16b, v1.16b\n"), {0x20, 0x38, 0x20, 0x4e}},
    {S8_INITIALIZER("suqadd v0.4h, v1.4h\n"), {0x20, 0x38, 0x60, 0x0e}},
    {S8_INITIALIZER("suqadd v0.8h, v1.8h\n"), {0x20, 0x38, 0x60, 0x4e}},
    {S8_INITIALIZER("suqadd v0.2s, v1.2s\n"), {0x20, 0x38, 0xa0, 0x0e}},
    {S8_INITIALIZER("suqadd v0.4s, v1.4s\n"), {0x20, 0x38, 0xa0, 0x4e}},
    {S8_INITIALIZER("suqadd v0.2d, v1.2d\n"), {0x20, 0x38, 0xe0, 0x4e}},
    {S8_INITIALIZER("usqadd v0.8b, v1.8b\n"), {0x20, 0x38, 0x20, 0x2e}},
    {S8_INITIALIZER("usqadd v0.16b, v1.16b\n"), {0x20, 0x38, 0x20, 0x6e}},
    {S8_INITIALIZER("usqadd v0.4h, v1.4h\n"), {0x20, 0x38, 0x60, 0x2e}},
    {S8_INITIALIZER("usqadd v0.8h, v1.8h\n"), {0x20, 0x38, 0x60, 0x6e}},
    {S8_INITIALIZER("usqadd v0.2s, v1.2s\n"), {0x20, 0x38, 0xa0, 0x2e}},
    {S8_INITIALIZER("usqadd v0.4s, v1.4s\n"), {0x20, 0x38, 0xa0, 0x6e}},
    {S8_INITIALIZER("usqadd v0.2d, v1.2d\n"), {0x20, 0x38, 0xe0, 0x6e}},
    {S8_INITIALIZER("fabs v0.2s, v1.2s\n"), {0x20, 0xf8, 0xa0, 0x0e}},
    {S8_INITIALIZER("fabs v0.4s, v1.4s\n"), {0x20, 0xf8, 0xa0, 0x4e}},
    {S8_INITIALIZER("fabs v0.2d, v1.2d\n"), {0x20, 0xf8, 0xe0, 0x4e}},
    {S8_INITIALIZER("fcvtas v0.2s, v1.2s\n"), {0x20, 0xc8, 0x21, 0x0e}},
    {S8_INITIALIZER("fcvtas v0.4s, v1.4s\n"), {0x20, 0xc8, 0x21, 0x4e}},
    {S8_INITIALIZER("fcvtas v0.2d, v1.2d\n"), {0x20, 0xc8, 0x61, 0x4e}},
    {S8_INITIALIZER("fcvtau v0.2s, v1.2s\n"), {0x20, 0xc8, 0x21, 0x2e}},
    {S8_INITIALIZER("fcvtau v0.4s, v1.4s\n"), {0x20, 0xc8, 0x21, 0x6e}},
    {S8_INITIALIZER("fcvtau v0.2d, v1.2d\n"), {0x20, 0xc8, 0x61, 0x6e}},
    {S8_INITIALIZER("fcvtms v0.2s, v1.2s\n"), {0x20, 0xb8, 0x21, 0x0e}},
    {S8_INITIALIZER("fcvtms v0.4s, v1.4s\n"), {0x20, 0xb8, 0x21, 0x4e}},
    {S8_INITIALIZER("fcvtms v0.2d, v1.2d\n"), {0x20, 0xb8, 0x61, 0x4e}},
    {S8_INITIALIZER("fcvtmu v0.2s, v1.2s\n"), {0x20, 0xb8, 0x21, 0x2e}},
    {S8_INITIALIZER("fcvtmu v0.4s, v1.4s\n"), {0x20, 0xb8, 0x21, 0x6e}},
    {S8_INITIALIZER("fcvtmu v0.2d, v1.2d\n"), {0x20, 0xb8, 0x61, 0x6e}},
    {S8_INITIALIZER("fcvtns v0.2s, v1.2s\n"), {0x20, 0xa8, 0x21, 0x0e}},
    {S8_INITIALIZER("fcvtns v0.4s, v1.4s\n"), {0x20, 0xa8, 0x21, 0x4e}},
    {S8_INITIALIZER("fcvtns v0.2d, v1.2d\n"), {0x20, 0xa8, 0x61, 0x4e}},
    {S8_INITIALIZER("fcvtnu v0.2s, v1.2s\n"), {0x20, 0xa8, 0x21, 0x2e}},
    {S8_INITIALIZER("fcvtnu v0.4s, v1.4s\n"), {0x20, 0xa8, 0x21, 0x6e}},
    {S8_INITIALIZER("fcvtnu v0.2d, v1.2d\n"), {0x20, 0xa8, 0x61, 0x6e}},
    {S8_INITIALIZER("fcvtps v0.2s, v1.2s\n"), {0x20, 0xa8, 0xa1, 0x0e}},
    {S8_INITIALIZER("fcvtps v0.4s, v1.4s\n"), {0x20, 0xa8, 0xa1, 0x4e}},
    {S8_INITIALIZER("fcvtps v0.2d, v1.2d\n"), {0x20, 0xa8, 0xe1, 0x4e}},
    {S8_INITIALIZER("fcvtpu v0.2s, v1.2s\n"), {0x20, 0xa8, 0xa1, 0x2e}},
    {S8_INITIALIZER("fcvtpu v0.4s, v1.4s\n"), {0x20, 0xa8, 0xa1, 0x6e}},
    {S8_INITIALIZER("fcvtpu v0.2d, v1.2d\n"), {0x20, 0xa8, 0xe1, 0x6e}},
    {S8_INITIALIZER("fcvtzs v0.2s, v1.2s\n"), {0x20, 0xb8, 0xa1, 0x0e}},
    {S8_INITIALIZER("fcvtzs v0.4s, v1.4s\n"), {0x20, 0xb8, 0xa1, 0x4e}},
    {S8_INITIALIZER("fcvtzs v0.2d, v1.2d\n"), {0x20, 0xb8, 0xe1, 0x4e}},
    {S8_INITIALIZER("fcvtzu v0.2s, v1.2s\n"), {0x20, 0xb8, 0xa1, 0x2e}},
    {S8_INITIALIZER("fcvtzu v0.4s, v1.4s\n"), {0x20, 0xb8, 0xa1, 0x6e}},
    {S8_INITIALIZER("fcvtzu v0.2d, v1.2d\n"), {0x20, 0xb8, 0xe1, 0x6e}},
    {S8_INITIALIZER("fneg v0.2s, v1.2s\n"), {0x20, 0xf8, 0xa0, 0x2e}},
    {S8_INITIALIZER("fneg v0.4s, v1.4s\n"), {0x20, 0xf8, 0xa0, 0x6e}},
    {S8_INITIALIZER("fneg v0.2d, v1.2d\n"), {0x20, 0xf8, 0xe0, 0x6e}},
    {S8_INITIALIZER("frecpe v0.2s, v1.2s\n"), {0x20, 0xd8, 0xa1, 0x0e}},
    {S8_INITIALIZER("frecpe v0.4s, v1.4s\n"), {0x20, 0xd8, 0xa1, 0x4e}},
    {S8_INITIALIZER("frecpe v0.2d, v1.2d\n"), {0x20, 0xd8, 0xe1, 0x4e}},
    {S8_INITIALIZER("frinta v0.2s, v1.2s\n"), {0x20, 0x88, 0x21, 0x2e}},
    {S8_INITIALIZER("frinta v0.4s, v1.4s\n"), {0x20, 0x88, 0x21, 0x6e}},
    {S8_INITIALIZER("frinta v0.2d, v1.2d\n"), {0x20, 0x88, 0x61, 0x6e}},
    {S8_INITIALIZER("frinti v0.2s, v1.2s\n"), {0x20, 0x98, 0xa1, 0x2e}},
    {S8_INITIALIZER("frinti v0.4s, v1.4s\n"), {0x20, 0x98, 0xa1, 0x6e}},
    {S8_INITIALIZER("frinti v0.2d, v1.2d\n"), {0x20, 0x98, 0xe1, 0x6e}},
    {S8_INITIALIZER("frintm v0.2s, v1.2s\n"), {0x20, 0x98, 0x21, 0x0e}},
    {S8_INITIALIZER("frintm v0.4s, v1.4s\n"), {0x20, 0x98, 0x21, 0x4e}},
    {S8_INITIALIZER("frintm v0.2d, v1.2d\n"), {0x20, 0x98, 0x61, 0x4e}},
    {S8_INITIALIZER("frintn v0.2s, v1.2s\n"), {0x20, 0x88, 0x21, 0x0e}},
    {S8_INITIALIZER("frintn v0.4s, v1.4s\n"), {0x20, 0x88, 0x21, 0x4e}},
    {S8_INITIALIZER("frintn v0.2d, v1.2d\n"), {0x20, 0x88, 0x61, 0x4e}},
    {S8_INITIALIZER("frintp v0.2s, v1.2s\n"), {0x20, 0x88, 0xa1, 0x0e}},
    {S8_INITIALIZER("frintp v0.4s, v1.4s\n"), {0x20, 0x88, 0xa1, 0x4e}},
    {S8_INITIALIZER("frintp v0.2d, v1.2d\n"), {0x20, 0x88, 0xe1, 0x4e}},
    {S8_INITIALIZER("frintx v0.2s, v1.2s\n"), {0x20, 0x98, 0x21, 0x2e}},
    {S8_INITIALIZER("frintx v0.4s, v1.4s\n"), {0x20, 0x98, 0x21, 0x6e}},
    {S8_INITIALIZER("frintx v0.2d, v1.2d\n"), {0x20, 0x98, 0x61, 0x6e}},
    {S8_INITIALIZER("frintz v0.2s, v1.2s\n"), {0x20, 0x98, 0xa1, 0x0e}},
    {S8_INITIALIZER("frintz v0.4s, v1.4s\n"), {0x20, 0x98, 0xa1, 0x4e}},
    {S8_INITIALIZER("frintz v0.2d, v1.2d\n"), {0x20, 0x98, 0xe1, 0x4e}},
    {S8_INITIALIZER("frsqrte v0.2s, v1.2s\n"), {0x20, 0xd8, 0xa1, 0x2e}},
    {S8_INITIALIZER("frsqrte v0.4s, v1.4s\n"), {0x20, 0xd8, 0xa1, 0x6e}},
    {S8_INITIALIZER("frsqrte v0.2d, v1.2d\n"), {0x20, 0xd8, 0xe1, 0x6e}},
    {S8_INITIALIZER("fsqrt v0.2s, v1.2s\n"), {0x20, 0xf8, 0xa1, 0x2e}},
    {S8_INITIALIZER("fsqrt v0.4s, v1.4s\n"), {0x20, 0xf8, 0xa1, 0x6e}},
    {S8_INITIALIZER("fsqrt v0.2d, v1.2d\n"), {0x20, 0xf8, 0xe1, 0x6e}},
    {S8_INITIALIZER("scvtf v0.2s, v1.2s\n"), {0x20, 0xd8, 0x21, 0x0e}},
    {S8_INITIALIZER("scvtf v0.4s, v1.4s\n"), {0x20, 0xd8, 0x21, 0x4e}},
    {S8_INITIALIZER("scvtf v0.2d, v1.2d\n"), {0x20, 0xd8, 0x61, 0x4e}},
    {S8_INITIALIZER("ucvtf v0.2s, v1.2s\n"), {0x20, 0xd8, 0x21, 0x2e}},
    {S8_INITIALIZER("ucvtf v0.4s, v1.4s\n"), {0x20, 0xd8, 0x21, 0x6e}},
    {S8_INITIALIZER("ucvtf v0.2d, v1.2d\n"), {0x20, 0xd8, 0x61, 0x6e}},
    {S8_INITIALIZER("fabd v0.2s, v1.2s, v2.2s\n"), {0x20, 0xd4, 0xa2, 0x2e}},
    {S8_INITIALIZER("fabd v0.4s, v1.4s, v2.4s\n"), {0x20, 0xd4, 0xa2, 0x6e}},
    {S8_INITIALIZER("fabd v0.2d, v1.2d, v2.2d\n"), {0x20, 0xd4, 0xe2, 0x6e}},
    {S8_INITIALIZER("facge v0.2s, v1.2s, v2.2s\n"), {0x20, 0xec, 0x22, 0x2e}},
    {S8_INITIALIZER("facge v0.4s, v1.4s, v2.4s\n"), {0x20, 0xec, 0x22, 0x6e}},
    {S8_INITIALIZER("facge v0.2d, v1.2d, v2.2d\n"), {0x20, 0xec, 0x62, 0x6e}},
    {S8_INITIALIZER("facgt v0.2s, v1.2s, v2.2s\n"), {0x20, 0xec, 0xa2, 0x2e}},
    {S8_INITIALIZER("facgt v0.4s, v1.4s, v2.4s\n"), {0x20, 0xec, 0xa2, 0x6e}},
    {S8_INITIALIZER("facgt v0.2d, v1.2d, v2.2d\n"), {0x20, 0xec, 0xe2, 0x6e}},
    {S8_INITIALIZER("faddp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xd4, 0x22, 0x2e}},
    {S8_INITIALIZER("faddp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xd4, 0x22, 0x6e}},
    {S8_INITIALIZER("faddp v0.2d, v1.2d, v2.2d\n"), {0x20, 0xd4, 0x62, 0x6e}},
    {S8_INITIALIZER("fadd v0.2s, v1.2s, v2.2s\n"), {0x20, 0xd4, 0x22, 0x0e}},
    {S8_INITIALIZER("fadd v0.4s, v1.4s, v2.4s\n"), {0x20, 0xd4, 0x22, 0x4e}},
    {S8_INITIALIZER("fadd v0.2d, v1.2d, v2.2d\n"), {0x20, 0xd4, 0x62, 0x4e}},
    {S8_INITIALIZER("fcmeq v0.2s, v1.2s, v2.2s\n"), {0x20, 0xe4, 0x22, 0x0e}},
    {S8_INITIALIZER("fcmeq v0.4s, v1.4s, v2.4s\n"), {0x20, 0xe4, 0x22, 0x4e}},
    {S8_INITIALIZER("fcmeq v0.2d, v1.2d, v2.2d\n"), {0x20, 0xe4, 0x62, 0x4e}},
    {S8_INITIALIZER("fcmge v0.2s, v1.2s, v2.2s\n"), {0x20, 0xe4, 0x22, 0x2e}},
    {S8_INITIALIZER("fcmge v0.4s, v1.4s, v2.4s\n"), {0x20, 0xe4, 0x22, 0x6e}},
    {S8_INITIALIZER("fcmge v0.2d, v1.2d, v2.2d\n"), {0x20, 0xe4, 0x62, 0x6e}},
    {S8_INITIALIZER("fcmgt v0.2s, v1.2s, v2.2s\n"), {0x20, 0xe4, 0xa2, 0x2e}},
    {S8_INITIALIZER("fcmgt v0.4s, v1.4s, v2.4s\n"), {0x20, 0xe4, 0xa2, 0x6e}},
    {S8_INITIALIZER("fcmgt v0.2d, v1.2d, v2.2d\n"), {0x20, 0xe4, 0xe2, 0x6e}},
    {S8_INITIALIZER("fdiv v0.2s, v1.2s, v2.2s\n"), {0x20, 0xfc, 0x22, 0x2e}},
    {S8_INITIALIZER("fdiv v0.4s, v1.4s, v2.4s\n"), {0x20, 0xfc, 0x22, 0x6e}},
    {S8_INITIALIZER("fdiv v0.2d, v1.2d, v2.2d\n"), {0x20, 0xfc, 0x62, 0x6e}},
    {S8_INITIALIZER("fmaxnmp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xc4, 0x22, 0x2e}},
    {S8_INITIALIZER("fmaxnmp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xc4, 0x22, 0x6e}},
    {S8_INITIALIZER("fmaxnmp v0.2d, v1.2d, v2.2d\n"), {0x20, 0xc4, 0x62, 0x6e}},
    {S8_INITIALIZER("fmaxnm v0.2s, v1.2s, v2.2s\n"), {0x20, 0xc4, 0x22, 0x0e}},
    {S8_INITIALIZER("fmaxnm v0.4s, v1.4s, v2.4s\n"), {0x20, 0xc4, 0x22, 0x4e}},
    {S8_INITIALIZER("fmaxnm v0.2d, v1.2d, v2.2d\n"), {0x20, 0xc4, 0x62, 0x4e}},
    {S8_INITIALIZER("fmaxp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xf4, 0x22, 0x2e}},
    {S8_INITIALIZER("fmaxp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xf4, 0x22, 0x6e}},
    {S8_INITIALIZER("fmaxp v0.2d, v1.2d, v2.2d\n"), {0x20, 0xf4, 0x62, 0x6e}},
    {S8_INITIALIZER("fmax v0.2s, v1.2s, v2.2s\n"), {0x20, 0xf4, 0x22, 0x0e}},
    {S8_INITIALIZER("fmax v0.4s, v1.4s, v2.4s\n"), {0x20, 0xf4, 0x22, 0x4e}},
    {S8_INITIALIZER("fmax v0.2d, v1.2d, v2.2d\n"), {0x20, 0xf4, 0x62, 0x4e}},
    {S8_INITIALIZER("fminnmp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xc4, 0xa2, 0x2e}},
    {S8_INITIALIZER("fminnmp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xc4, 0xa2, 0x6e}},
    {S8_INITIALIZER("fminnmp v0.2d, v1.2d, v2.2d\n"), {0x20, 0xc4, 0xe2, 0x6e}},
    {S8_INITIALIZER("fminnm v0.2s, v1.2s, v2.2s\n"), {0x20, 0xc4, 0xa2, 0x0e}},
    {S8_INITIALIZER("fminnm v0.4s, v1.4s, v2.4s\n"), {0x20, 0xc4, 0xa2, 0x4e}},
    {S8_INITIALIZER("fminnm v0.2d, v1.2d, v2.2d\n"), {0x20, 0xc4, 0xe2, 0x4e}},
    {S8_INITIALIZER("fminp v0.2s, v1.2s, v2.2s\n"), {0x20, 0xf4, 0xa2, 0x2e}},
    {S8_INITIALIZER("fminp v0.4s, v1.4s, v2.4s\n"), {0x20, 0xf4, 0xa2, 0x6e}},
    {S8_INITIALIZER("fminp v0.2d, v1.2d, v2.2d\n"), {0x20, 0xf4, 0xe2, 0x6e}},
    {S8_INITIALIZER("fmin v0.2s, v1.2s, v2.2s\n"), {0x20, 0xf4, 0xa2, 0x0e}},
    {S8_INITIALIZER("fmin v0.4s, v1.4s, v2.4s\n"), {0x20, 0xf4, 0xa2, 0x4e}},
    {S8_INITIALIZER("fmin v0.2d, v1.2d, v2.2d\n"), {0x20, 0xf4, 0xe2, 0x4e}},
    {S8_INITIALIZER("fmla v0.2s, v1.2s, v2.2s\n"), {0x20, 0xcc, 0x22, 0x0e}},
    {S8_INITIALIZER("fmla v0.4s, v1.4s, v2.4s\n"), {0x20, 0xcc, 0x22, 0x4e}},
    {S8_INITIALIZER("fmla v0.2d, v1.2d, v2.2d\n"), {0x20, 0xcc, 0x62, 0x4e}},
    {S8_INITIALIZER("fmls v0.2s, v1.2s, v2.2s\n"), {0x20, 0xcc, 0xa2, 0x0e}},
    {S8_INITIALIZER("fmls v0.4s, v1.4s, v2.4s\n"), {0x20, 0xcc, 0xa2, 0x4e}},
    {S8_INITIALIZER("fmls v0.2d, v1.2d, v2.2d\n"), {0x20, 0xcc, 0xe2, 0x4e}},
    {S8_INITIALIZER("fmulx v0.2s, v1.2s, v2.2s\n"), {0x20, 0xdc, 0x22, 0x0e}},
    {S8_INITIALIZER("fmulx v0.4s, v1.4s, v2.4s\n"), {0x20, 0xdc, 0x22, 0x4e}},
    {S8_INITIALIZER("fmulx v0.2d, v1.2d, v2.2d\n"), {0x20, 0xdc, 0x62, 0x4e}},
    {S8_INITIALIZER("fmul v0.2s, v1.2s, v2.2s\n"), {0x20, 0xdc, 0x22, 0x2e}},
    {S8_INITIALIZER("fmul v0.4s, v1.4s, v2.4s\n"), {0x20, 0xdc, 0x22, 0x6e}},
    {S8_INITIALIZER("fmul v0.2d, v1.2d, v2.2d\n"), {0x20, 0xdc, 0x62, 0x6e}},
    {S8_INITIALIZER("frecps v0.2s, v1.2s, v2.2s\n"), {0x20, 0xfc, 0x22, 0x0e}},
    {S8_INITIALIZER("frecps v0.4s, v1.4s, v2.4s\n"), {0x20, 0xfc, 0x22, 0x4e}},
    {S8_INITIALIZER("frecps v0.2d, v1.2d, v2.2d\n"), {0x20, 0xfc, 0x62, 0x4e}},
    {S8_INITIALIZER("frsqrts v0.2s, v1.2s, v2.2s\n"), {0x20, 0xfc, 0xa2, 0x0e}},
    {S8_INITIALIZER("frsqrts v0.4s, v1.4s, v2.4s\n"), {0x20, 0xfc, 0xa2, 0x4e}},
    {S8_INITIALIZER("frsqrts v0.2d, v1.2d, v2.2d\n"), {0x20, 0xfc, 0xe2, 0x4e}},
    {S8_INITIALIZER("fsub v0.2s, v1.2s, v2.2s\n"), {0x20, 0xd4, 0xa2, 0x0e}},
    {S8_INITIALIZER("fsub v0.4s, v1.4s, v2.4s\n"), {0x20, 0xd4, 0xa2, 0x4e}},
    {S8_INITIALIZER("fsub v0.2d, v1.2d, v2.2d\n"), {0x20, 0xd4, 0xe2, 0x4e}},
};

/* M1 mnemonic-collision cohort: every legal direct row arrangement, with
 * bytes independently obtained from llvm-mc 22.1.8. */
static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_m1_collision_cases[] = {
    {S8_INITIALIZER("add v0.8b, v1.8b, v2.8b\n"), {0x20, 0x84, 0x22, 0x0e}},
    {S8_INITIALIZER("add v0.16b, v1.16b, v2.16b\n"), {0x20, 0x84, 0x22, 0x4e}},
    {S8_INITIALIZER("add v0.4h, v1.4h, v2.4h\n"), {0x20, 0x84, 0x62, 0x0e}},
    {S8_INITIALIZER("add v0.8h, v1.8h, v2.8h\n"), {0x20, 0x84, 0x62, 0x4e}},
    {S8_INITIALIZER("add v0.2s, v1.2s, v2.2s\n"), {0x20, 0x84, 0xa2, 0x0e}},
    {S8_INITIALIZER("add v0.4s, v1.4s, v2.4s\n"), {0x20, 0x84, 0xa2, 0x4e}},
    {S8_INITIALIZER("add v0.2d, v1.2d, v2.2d\n"), {0x20, 0x84, 0xe2, 0x4e}},
    {S8_INITIALIZER("add d0, d1, d2\n"), {0x20, 0x84, 0xe2, 0x5e}},
    {S8_INITIALIZER("and v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0x22, 0x0e}},
    {S8_INITIALIZER("and v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0x22, 0x4e}},
    {S8_INITIALIZER("bic v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0x62, 0x0e}},
    {S8_INITIALIZER("bic v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0x62, 0x4e}},
    {S8_INITIALIZER("cls v0.8b, v1.8b\n"), {0x20, 0x48, 0x20, 0x0e}},
    {S8_INITIALIZER("cls v0.16b, v1.16b\n"), {0x20, 0x48, 0x20, 0x4e}},
    {S8_INITIALIZER("cls v0.4h, v1.4h\n"), {0x20, 0x48, 0x60, 0x0e}},
    {S8_INITIALIZER("cls v0.8h, v1.8h\n"), {0x20, 0x48, 0x60, 0x4e}},
    {S8_INITIALIZER("cls v0.2s, v1.2s\n"), {0x20, 0x48, 0xa0, 0x0e}},
    {S8_INITIALIZER("cls v0.4s, v1.4s\n"), {0x20, 0x48, 0xa0, 0x4e}},
    {S8_INITIALIZER("clz v0.8b, v1.8b\n"), {0x20, 0x48, 0x20, 0x2e}},
    {S8_INITIALIZER("clz v0.16b, v1.16b\n"), {0x20, 0x48, 0x20, 0x6e}},
    {S8_INITIALIZER("clz v0.4h, v1.4h\n"), {0x20, 0x48, 0x60, 0x2e}},
    {S8_INITIALIZER("clz v0.8h, v1.8h\n"), {0x20, 0x48, 0x60, 0x6e}},
    {S8_INITIALIZER("clz v0.2s, v1.2s\n"), {0x20, 0x48, 0xa0, 0x2e}},
    {S8_INITIALIZER("clz v0.4s, v1.4s\n"), {0x20, 0x48, 0xa0, 0x6e}},
    {S8_INITIALIZER("eor v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0x22, 0x2e}},
    {S8_INITIALIZER("eor v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0x22, 0x6e}},
    {S8_INITIALIZER("orn v0.8b, v1.8b, v2.8b\n"), {0x20, 0x1c, 0xe2, 0x0e}},
    {S8_INITIALIZER("orn v0.16b, v1.16b, v2.16b\n"), {0x20, 0x1c, 0xe2, 0x4e}},
    {S8_INITIALIZER("rbit v0.8b, v1.8b\n"), {0x20, 0x58, 0x60, 0x2e}},
    {S8_INITIALIZER("rbit v0.16b, v1.16b\n"), {0x20, 0x58, 0x60, 0x6e}},
    {S8_INITIALIZER("rev16 v0.8b, v1.8b\n"), {0x20, 0x18, 0x20, 0x0e}},
    {S8_INITIALIZER("rev16 v0.16b, v1.16b\n"), {0x20, 0x18, 0x20, 0x4e}},
    {S8_INITIALIZER("rev32 v0.8b, v1.8b\n"), {0x20, 0x08, 0x20, 0x2e}},
    {S8_INITIALIZER("rev32 v0.16b, v1.16b\n"), {0x20, 0x08, 0x20, 0x6e}},
    {S8_INITIALIZER("rev32 v0.4h, v1.4h\n"), {0x20, 0x08, 0x60, 0x2e}},
    {S8_INITIALIZER("rev32 v0.8h, v1.8h\n"), {0x20, 0x08, 0x60, 0x6e}},
    {S8_INITIALIZER("sub v0.8b, v1.8b, v2.8b\n"), {0x20, 0x84, 0x22, 0x2e}},
    {S8_INITIALIZER("sub v0.16b, v1.16b, v2.16b\n"), {0x20, 0x84, 0x22, 0x6e}},
    {S8_INITIALIZER("sub v0.4h, v1.4h, v2.4h\n"), {0x20, 0x84, 0x62, 0x2e}},
    {S8_INITIALIZER("sub v0.8h, v1.8h, v2.8h\n"), {0x20, 0x84, 0x62, 0x6e}},
    {S8_INITIALIZER("sub v0.2s, v1.2s, v2.2s\n"), {0x20, 0x84, 0xa2, 0x2e}},
    {S8_INITIALIZER("sub v0.4s, v1.4s, v2.4s\n"), {0x20, 0x84, 0xa2, 0x6e}},
    {S8_INITIALIZER("sub v0.2d, v1.2d, v2.2d\n"), {0x20, 0x84, 0xe2, 0x6e}},
    {S8_INITIALIZER("sub d0, d1, d2\n"), {0x20, 0x84, 0xe2, 0x7e}},
};

static AssemblyA64DirectSIMDEncodingCase const assembly_a64_direct_simd_m1_collision_boundary_cases[] = {
    {S8_INITIALIZER("ADD V31.2D, V30.2D, V29.2D\n"), {0xdf, 0x87, 0xfd, 0x4e}},
    {S8_INITIALIZER("ADD D31, D30, D29\n"), {0xdf, 0x87, 0xfd, 0x5e}},
    {S8_INITIALIZER("AND V31.16B, V30.16B, V29.16B\n"), {0xdf, 0x1f, 0x3d, 0x4e}},
    {S8_INITIALIZER("BIC V31.16B, V30.16B, V29.16B\n"), {0xdf, 0x1f, 0x7d, 0x4e}},
    {S8_INITIALIZER("CLS V31.4S, V30.4S\n"), {0xdf, 0x4b, 0xa0, 0x4e}},
    {S8_INITIALIZER("CLZ V31.4S, V30.4S\n"), {0xdf, 0x4b, 0xa0, 0x6e}},
    {S8_INITIALIZER("EOR V31.16B, V30.16B, V29.16B\n"), {0xdf, 0x1f, 0x3d, 0x6e}},
    {S8_INITIALIZER("ORN V31.16B, V30.16B, V29.16B\n"), {0xdf, 0x1f, 0xfd, 0x4e}},
    {S8_INITIALIZER("RBIT V31.16B, V30.16B\n"), {0xdf, 0x5b, 0x60, 0x6e}},
    {S8_INITIALIZER("REV16 V31.16B, V30.16B\n"), {0xdf, 0x1b, 0x20, 0x4e}},
    {S8_INITIALIZER("REV32 V31.8H, V30.8H\n"), {0xdf, 0x0b, 0x60, 0x6e}},
    {S8_INITIALIZER("SUB V31.2D, V30.2D, V29.2D\n"), {0xdf, 0x87, 0xfd, 0x6e}},
    {S8_INITIALIZER("SUB D31, D30, D29\n"), {0xdf, 0x87, 0xfd, 0x7e}},
};

// End-to-end direct-GPR corpus.  Sources use ordinary W/X registers (the
// public encoder matrix below separately exercises register-31 roles), and
// every expected byte is an independent llvm-mc 22.1.8 literal.
typedef struct AssemblyA64M1GprCorpusCase AssemblyA64M1GprCorpusCase;
struct AssemblyA64M1GprCorpusCase
{
    String8 source;
    u8 bytes[4];
};

static AssemblyA64M1GprCorpusCase const assembly_a64_m1_gpr_corpus[] = {
    {S8_INITIALIZER("adcs w1, w2, w3\n"), {65, 0, 3, 58}},
    {S8_INITIALIZER("adcs x1, x2, x3\n"), {65, 0, 3, 186}},
    {S8_INITIALIZER("adc w1, w2, w3\n"), {65, 0, 3, 26}},
    {S8_INITIALIZER("adc x1, x2, x3\n"), {65, 0, 3, 154}},
    {S8_INITIALIZER("asrv w1, w2, w3\n"), {65, 40, 195, 26}},
    {S8_INITIALIZER("asrv x1, x2, x3\n"), {65, 40, 195, 154}},
    {S8_INITIALIZER("autda x1, x2\n"), {65, 24, 193, 218}},
    {S8_INITIALIZER("autdb x1, x2\n"), {65, 28, 193, 218}},
    {S8_INITIALIZER("autdza x1\n"), {225, 59, 193, 218}},
    {S8_INITIALIZER("autdzb x1\n"), {225, 63, 193, 218}},
    {S8_INITIALIZER("autia x1, x2\n"), {65, 16, 193, 218}},
    {S8_INITIALIZER("autib x1, x2\n"), {65, 20, 193, 218}},
    {S8_INITIALIZER("autiza x1\n"), {225, 51, 193, 218}},
    {S8_INITIALIZER("autizb x1\n"), {225, 55, 193, 218}},
    {S8_INITIALIZER("blraaz x1\n"), {63, 8, 63, 214}},
    {S8_INITIALIZER("blraa x1, x2\n"), {34, 8, 63, 215}},
    {S8_INITIALIZER("blrabz x1\n"), {63, 12, 63, 214}},
    {S8_INITIALIZER("blrab x1, x2\n"), {34, 12, 63, 215}},
    {S8_INITIALIZER("blr x1\n"), {32, 0, 63, 214}},
    {S8_INITIALIZER("braaz x1\n"), {63, 8, 31, 214}},
    {S8_INITIALIZER("braa x1, x2\n"), {34, 8, 31, 215}},
    {S8_INITIALIZER("brabz x1\n"), {63, 12, 31, 214}},
    {S8_INITIALIZER("brab x1, x2\n"), {34, 12, 31, 215}},
    {S8_INITIALIZER("br x1\n"), {32, 0, 31, 214}},
    {S8_INITIALIZER("cls w1, w2\n"), {65, 20, 192, 90}},
    {S8_INITIALIZER("cls x1, x2\n"), {65, 20, 192, 218}},
    {S8_INITIALIZER("clz w1, w2\n"), {65, 16, 192, 90}},
    {S8_INITIALIZER("clz x1, x2\n"), {65, 16, 192, 218}},
    {S8_INITIALIZER("crc32b w1, w2, w3\n"), {65, 64, 195, 26}},
    {S8_INITIALIZER("crc32cb w1, w2, w3\n"), {65, 80, 195, 26}},
    {S8_INITIALIZER("crc32ch w1, w2, w3\n"), {65, 84, 195, 26}},
    {S8_INITIALIZER("crc32cw w1, w2, w3\n"), {65, 88, 195, 26}},
    {S8_INITIALIZER("crc32cx w1, w2, x3\n"), {65, 92, 195, 154}},
    {S8_INITIALIZER("crc32h w1, w2, w3\n"), {65, 68, 195, 26}},
    {S8_INITIALIZER("crc32w w1, w2, w3\n"), {65, 72, 195, 26}},
    {S8_INITIALIZER("crc32x w1, w2, x3\n"), {65, 76, 195, 154}},
    {S8_INITIALIZER("lslv w1, w2, w3\n"), {65, 32, 195, 26}},
    {S8_INITIALIZER("lslv x1, x2, x3\n"), {65, 32, 195, 154}},
    {S8_INITIALIZER("lsrv w1, w2, w3\n"), {65, 36, 195, 26}},
    {S8_INITIALIZER("lsrv x1, x2, x3\n"), {65, 36, 195, 154}},
    {S8_INITIALIZER("madd w1, w2, w3, w4\n"), {65, 16, 3, 27}},
    {S8_INITIALIZER("madd x1, x2, x3, x4\n"), {65, 16, 3, 155}},
    {S8_INITIALIZER("msub w1, w2, w3, w4\n"), {65, 144, 3, 27}},
    {S8_INITIALIZER("msub x1, x2, x3, x4\n"), {65, 144, 3, 155}},
    {S8_INITIALIZER("pacda x1, x2\n"), {65, 8, 193, 218}},
    {S8_INITIALIZER("pacdb x1, x2\n"), {65, 12, 193, 218}},
    {S8_INITIALIZER("pacdza x1\n"), {225, 43, 193, 218}},
    {S8_INITIALIZER("pacdzb x1\n"), {225, 47, 193, 218}},
    {S8_INITIALIZER("pacga x1, x2, x3\n"), {65, 48, 195, 154}},
    {S8_INITIALIZER("pacia x1, x2\n"), {65, 0, 193, 218}},
    {S8_INITIALIZER("pacib x1, x2\n"), {65, 4, 193, 218}},
    {S8_INITIALIZER("paciza x1\n"), {225, 35, 193, 218}},
    {S8_INITIALIZER("pacizb x1\n"), {225, 39, 193, 218}},
    {S8_INITIALIZER("rbit w1, w2\n"), {65, 0, 192, 90}},
    {S8_INITIALIZER("rbit x1, x2\n"), {65, 0, 192, 218}},
    {S8_INITIALIZER("rev16 w1, w2\n"), {65, 4, 192, 90}},
    {S8_INITIALIZER("rev16 x1, x2\n"), {65, 4, 192, 218}},
    {S8_INITIALIZER("rev32 x1, x2\n"), {65, 8, 192, 218}},
    {S8_INITIALIZER("rev w1, w2\n"), {65, 8, 192, 90}},
    {S8_INITIALIZER("rev x1, x2\n"), {65, 12, 192, 218}},
    {S8_INITIALIZER("rorv w1, w2, w3\n"), {65, 44, 195, 26}},
    {S8_INITIALIZER("rorv x1, x2, x3\n"), {65, 44, 195, 154}},
    {S8_INITIALIZER("sbcs w1, w2, w3\n"), {65, 0, 3, 122}},
    {S8_INITIALIZER("sbcs x1, x2, x3\n"), {65, 0, 3, 250}},
    {S8_INITIALIZER("sbc w1, w2, w3\n"), {65, 0, 3, 90}},
    {S8_INITIALIZER("sbc x1, x2, x3\n"), {65, 0, 3, 218}},
    {S8_INITIALIZER("sdiv w1, w2, w3\n"), {65, 12, 195, 26}},
    {S8_INITIALIZER("sdiv x1, x2, x3\n"), {65, 12, 195, 154}},
    {S8_INITIALIZER("setf16 w1\n"), {45, 72, 0, 58}},
    {S8_INITIALIZER("setf8 w1\n"), {45, 8, 0, 58}},
    {S8_INITIALIZER("smaddl x1, w2, w3, x4\n"), {65, 16, 35, 155}},
    {S8_INITIALIZER("smsubl x1, w2, w3, x4\n"), {65, 144, 35, 155}},
    {S8_INITIALIZER("smulh x1, x2, x3\n"), {65, 124, 67, 155}},
    {S8_INITIALIZER("udiv w1, w2, w3\n"), {65, 8, 195, 26}},
    {S8_INITIALIZER("udiv x1, x2, x3\n"), {65, 8, 195, 154}},
    {S8_INITIALIZER("umaddl x1, w2, w3, x4\n"), {65, 16, 163, 155}},
    {S8_INITIALIZER("umsubl x1, w2, w3, x4\n"), {65, 144, 163, 155}},
    {S8_INITIALIZER("umulh x1, x2, x3\n"), {65, 124, 195, 155}},
    {S8_INITIALIZER("xpacd x1\n"), {225, 71, 193, 218}},
    {S8_INITIALIZER("xpaci x1\n"), {225, 67, 193, 218}},
};

UnitTestResult assembly_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    /* The generated direct-SIMD owner table is the bounded denominator for
     * the public spelling adapter.  Keep this census independent of the
     * handwritten instruction tests below: stale digests, IDs, spellings, or
     * generated-row metadata should fail before an individual form can mask
     * the gap. */
    u32 direct_simd_row_count = buster_a64_direct_simd_row_count();
    u32 direct_simd_executable_count = buster_a64_direct_simd_executable_row_count();
    u32 direct_simd_transform_count = buster_a64_direct_simd_transform_row_count();
    u32 direct_simd_binding_count = buster_a64_direct_simd_arrangement_binding_count();
    u32 direct_simd_spelling_count = assembly_test_aarch64_direct_simd_spelling_count();
    BUSTER_TEST(arguments, direct_simd_row_count == 390);
    BUSTER_TEST(arguments, direct_simd_executable_count == 390);
    BUSTER_TEST(arguments, direct_simd_transform_count == 263);
    BUSTER_TEST(arguments, direct_simd_binding_count == 658);
    /* The two FCVT{L,N} suffix spellings intentionally share canonical rows;
     * all other public entries are one-to-one with generated rows. */
    BUSTER_TEST(arguments, direct_simd_spelling_count > 0 && direct_simd_spelling_count <= direct_simd_row_count + 2);

    u8 direct_simd_covered_rows[390] = {0};
    u32 direct_simd_covered_count = 0;
    u32 direct_simd_covered_transform_count = 0;
    u32 direct_simd_covered_no_transform_count = 0;
    u32 direct_simd_invalid_spelling_count = 0;
    u32 direct_simd_duplicate_spelling_count = 0;
    u32 direct_simd_duplicate_row_count = 0;
    u32 direct_simd_compound_requirement_count = 0;
    u32 direct_simd_fhm_requirement_count = 0;
    u32 direct_simd_frintts_requirement_count = 0;
    u32 direct_simd_dotprod_requirement_count = 0;
    u32 direct_simd_rdm_requirement_count = 0;
    u32 direct_simd_fcsel_row_count = 0;
    bool direct_simd_compound_requirement_exact = true;
    bool direct_simd_fhm_requirement_exact = true;
    bool direct_simd_frintts_requirement_exact = true;
    bool direct_simd_dotprod_requirement_exact = true;
    bool direct_simd_rdm_requirement_exact = true;
    bool direct_simd_dotprod_fixed_field_exact = true;
    bool direct_simd_fcsel_rows_exact = true;
    bool direct_simd_first_fp16_row_exact = false;
    bool direct_simd_last_fp16_row_exact = false;
    bool direct_simd_rows_unique = true;
    bool direct_simd_spellings_unique = true;

    for (u32 row_index = 0; row_index < direct_simd_row_count; row_index += 1)
    {
        BusterA64DirectSIMDRowInfo row = {0};
        bool row_valid = buster_a64_direct_simd_row(row_index, &row) && row.row_index == row_index && row.executable &&
                         row.source_digest != 0 && row.id.length != 0 && row.operand_count > 0 &&
                         row.operand_count <= BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS;
        BUSTER_TEST(arguments, row_valid);
        for (u32 prior_index = 0; prior_index < row_index; prior_index += 1)
        {
            BusterA64DirectSIMDRowInfo prior = {0};
            bool prior_valid = buster_a64_direct_simd_row(prior_index, &prior);
            bool unique = !prior_valid || !row_valid ||
                          (prior.source_digest != row.source_digest && !string_equal(prior.id, row.id));
            direct_simd_rows_unique = direct_simd_rows_unique && unique;
            direct_simd_duplicate_row_count += (u32)!unique;
        }
    }

    for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
    {
        AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
        bool spelling_valid = assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                              spelling.mnemonic.length != 0 && spelling.source_digest != 0 && spelling.semantic_id.length != 0 &&
                              spelling.operand_count > 0 && spelling.operand_count <= 4 &&
                              spelling.requirement > BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE &&
                              spelling.requirement < BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT;
        TargetCpuFeatures spelling_required_features = {0};
        bool spelling_requirement_valid = spelling_valid &&
                                          buster_a64_direct_simd_requirement_features(spelling.requirement,
                                                                                       &spelling_required_features) &&
                                          target_cpu_features_any(spelling_required_features);
        BUSTER_TEST(arguments, spelling_requirement_valid);
        if (spelling_valid && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16)
        {
            direct_simd_compound_requirement_count += 1;
            direct_simd_compound_requirement_exact =
                direct_simd_compound_requirement_exact && spelling_required_features.words[0] == (UINT64_C(1) << 10) &&
                spelling_required_features.words[1] == (UINT64_C(1) << 57) && spelling_required_features.words[2] == 0 &&
                spelling_required_features.words[3] == 0;
        }
        if (spelling_valid && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML)
        {
            direct_simd_fhm_requirement_count += 1;
            direct_simd_fhm_requirement_exact =
                direct_simd_fhm_requirement_exact && spelling_required_features.words[0] == (UINT64_C(1) << 10) &&
                spelling_required_features.words[1] == (UINT64_C(1) << 55) && spelling_required_features.words[2] == 0 &&
                spelling_required_features.words[3] == 0;
        }
        if (spelling_valid && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT)
        {
            direct_simd_frintts_requirement_count += 1;
            direct_simd_frintts_requirement_exact =
                direct_simd_frintts_requirement_exact && spelling_required_features.words[0] == (UINT64_C(1) << 10) &&
                spelling_required_features.words[1] == (UINT64_C(1) << 56) && spelling_required_features.words[2] == 0 &&
                spelling_required_features.words[3] == 0;
        }
        if (spelling_valid && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD)
        {
            direct_simd_dotprod_requirement_count += 1;
            direct_simd_dotprod_requirement_exact =
                direct_simd_dotprod_requirement_exact && spelling_required_features.words[0] == (UINT64_C(1) << 10) &&
                spelling_required_features.words[1] == (UINT64_C(1) << 52) && spelling_required_features.words[2] == 0 &&
                spelling_required_features.words[3] == 0;
            direct_simd_dotprod_fixed_field_exact = direct_simd_dotprod_fixed_field_exact &&
                                                     spelling.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_SIZE &&
                                                     spelling.fixed_field_value == 2;
        }
        if (spelling_valid && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM)
        {
            direct_simd_rdm_requirement_count += 1;
            direct_simd_rdm_requirement_exact =
                direct_simd_rdm_requirement_exact && spelling_required_features.words[0] == (UINT64_C(1) << 10) &&
                spelling_required_features.words[1] == 0 && spelling_required_features.words[2] == (UINT64_C(1) << 3) &&
                spelling_required_features.words[3] == 0;
        }
        if (spelling_valid && string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FABD_asimdsamefp16_only")))
        {
            direct_simd_first_fp16_row_exact = spelling.source_digest == UINT64_C(0xeccb3d01478f107a) &&
                                               spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16;
        }
        if (spelling_valid && string_equal(spelling.semantic_id, S8("arm-a64@2026-06:UCVTF_asisdmiscfp16_R")))
        {
            direct_simd_last_fp16_row_exact = spelling.source_digest == UINT64_C(0xfa9f851b4423665f) &&
                                              spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16;
        }
        if (spelling_valid &&
            (string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FCSEL_D_floatsel")) ||
             string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FCSEL_H_floatsel")) ||
             string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FCSEL_S_floatsel"))))
        {
            direct_simd_fcsel_row_count += 1;
            bool is_d = string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FCSEL_D_floatsel"));
            bool is_h = string_equal(spelling.semantic_id, S8("arm-a64@2026-06:FCSEL_H_floatsel"));
            u64 expected_digest = is_d ? UINT64_C(0x334422a1dbe54a6a) :
                                 is_h ? UINT64_C(0x942ac11ca7582058) : UINT64_C(0x39955c862311f855);
            BusterA64DirectSIMDRequirement expected_requirement = is_h ? BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16
                                                                       : BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP;
            BusterA64DirectSIMDArrangement expected_arrangement = is_d ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D
                                                                        : is_h ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H
                                                                               : BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S;
            direct_simd_fcsel_rows_exact = direct_simd_fcsel_rows_exact && spelling.source_digest == expected_digest &&
                                           spelling.requirement == expected_requirement && spelling.operand_count == 4 &&
                                           spelling.arrangements[0] == expected_arrangement &&
                                           spelling.arrangements[1] == expected_arrangement &&
                                           spelling.arrangements[2] == expected_arrangement &&
                                           spelling.arrangements[3] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
        }
        for (u32 prior_index = 0; prior_index < spelling_index; prior_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest prior = {0};
            bool prior_valid = assembly_test_aarch64_direct_simd_spelling_at(prior_index, &prior);
            bool suffix_pair = prior_valid && spelling_valid && prior.source_digest == spelling.source_digest &&
                               string_equal(prior.semantic_id, spelling.semantic_id) &&
                               ((prior.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT &&
                                 prior.fixed_field_value == 0 && spelling.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT &&
                                 spelling.fixed_field_value == 1) ||
                                (spelling.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT && spelling.fixed_field_value == 0 &&
                                 prior.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT && prior.fixed_field_value == 1));
            bool unique = !prior_valid || !spelling_valid ||
                          suffix_pair || (prior.source_digest != spelling.source_digest && !string_equal(prior.semantic_id, spelling.semantic_id));
            direct_simd_spellings_unique = direct_simd_spellings_unique && unique;
            direct_simd_duplicate_spelling_count += (u32)!unique;
        }

        u32 row_index = UINT32_MAX;
        BusterA64DirectSIMDRowInfo row = {0};
        BusterA64SemanticForm row_form = {0};
        u32 public_operand_count = 0;
        bool has_condition_operand = false;
        bool arrangements_valid = true;
        bool list_public_seen = false;
        if (spelling_valid && buster_a64_direct_simd_find_source_digest(spelling.source_digest, &row_index) &&
            buster_a64_direct_simd_row(row_index, &row) && buster_a64_semantic_form(row.semantic_form_id, &row_form))
        {
            for (u32 operand_index = 0; operand_index < row_form.operand_count; operand_index += 1)
            {
                BusterA64SemanticOperand operand = {0};
                bool operand_valid = buster_a64_semantic_operand(row_form.operand_first + operand_index, &operand);
                bool list_member = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST ||
                                   (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0;
                bool index_vector = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE &&
                                    (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
                                    (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 &&
                                    (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0;
                if (operand_valid && list_member)
                {
                    public_operand_count += (u32)!list_public_seen;
                    list_public_seen = true;
                }
                else if (operand_valid &&
                         (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER ||
                          operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER || index_vector ||
                          (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_CONDITION &&
                           (operand.flags & BUSTER_A64_SEMANTIC_FLAG_CONDITION_FIELD) != 0)))
                {
                    public_operand_count += 1;
                }
                has_condition_operand = has_condition_operand ||
                                        (operand_valid && operand.kind == BUSTER_A64_SEMANTIC_OPERAND_CONDITION &&
                                         (operand.flags & BUSTER_A64_SEMANTIC_FLAG_CONDITION_FIELD) != 0);
            }
        }
        if (spelling_valid && row.executable)
        {
            if (row.transform_bearing && !has_condition_operand)
            {
                for (u32 operand_index = 0; operand_index < 4; operand_index += 1)
                {
                    arrangements_valid = arrangements_valid &&
                                          spelling.arrangements[operand_index] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
                }
            }
            else
            {
                u32 public_index = 0;
                for (u32 operand_index = 0; operand_index < row_form.operand_count; operand_index += 1)
                {
                    BusterA64SemanticOperand operand = {0};
                    if (!buster_a64_semantic_operand(row_form.operand_first + operand_index, &operand))
                    {
                        arrangements_valid = false;
                        continue;
                    }
                    if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER)
                    {
                        arrangements_valid = arrangements_valid && public_index < 4 &&
                                              spelling.arrangements[public_index] > BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID &&
                                              spelling.arrangements[public_index] < BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT;
                        public_index += 1;
                    }
                    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER)
                    {
                        arrangements_valid = arrangements_valid && public_index < 4 &&
                                              spelling.arrangements[public_index] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
                        public_index += 1;
                    }
                    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_CONDITION &&
                             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_CONDITION_FIELD) != 0)
                    {
                        arrangements_valid = arrangements_valid && public_index < 4 &&
                                              spelling.arrangements[public_index] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
                        public_index += 1;
                    }
                }
                for (u32 operand_index = public_index; operand_index < 4; operand_index += 1)
                {
                    arrangements_valid = arrangements_valid &&
                                          spelling.arrangements[operand_index] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
                }
            }
        }
        bool duplicate_row = row_index < direct_simd_row_count && direct_simd_covered_rows[row_index];
        bool suffix_spelling = spelling.fixed_field_kind == BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT && spelling.fixed_field_value == 1;
        bool resolved = spelling_valid && buster_a64_direct_simd_find_source_digest(spelling.source_digest, &row_index) &&
                        buster_a64_direct_simd_row(row_index, &row) && row.executable && public_operand_count == spelling.operand_count &&
                        string_equal(row.id, spelling.semantic_id) && arrangements_valid && row_index < direct_simd_row_count &&
                        (!direct_simd_covered_rows[row_index] || suffix_spelling);
        BUSTER_TEST(arguments, resolved);
        if (!resolved)
        {
            direct_simd_invalid_spelling_count += 1;
            continue;
        }
        if (!duplicate_row)
        {
            direct_simd_covered_rows[row_index] = 1;
            direct_simd_covered_count += 1;
            if (row.transform_bearing)
            {
                direct_simd_covered_transform_count += 1;
            }
            else
            {
                direct_simd_covered_no_transform_count += 1;
            }
        }
    }

    u32 direct_simd_uncovered_count = direct_simd_row_count >= direct_simd_covered_count
                                          ? direct_simd_row_count - direct_simd_covered_count
                                          : 0;
    u32 direct_simd_uncovered_transform_count = direct_simd_transform_count >= direct_simd_covered_transform_count
                                                    ? direct_simd_transform_count - direct_simd_covered_transform_count
                                                    : 0;
    u32 direct_simd_no_transform_count = direct_simd_row_count >= direct_simd_transform_count
                                             ? direct_simd_row_count - direct_simd_transform_count
                                             : 0;
    u32 direct_simd_uncovered_no_transform_count = direct_simd_no_transform_count >= direct_simd_covered_no_transform_count
                                                       ? direct_simd_no_transform_count - direct_simd_covered_no_transform_count
                                                       : 0;
    BUSTER_TEST(arguments, direct_simd_invalid_spelling_count == 0);
    BUSTER_TEST(arguments, direct_simd_spellings_unique && direct_simd_duplicate_spelling_count == 0);
    BUSTER_TEST(arguments, direct_simd_rows_unique && direct_simd_duplicate_row_count == 0);
    AssemblyAarch64DirectSIMDSpellingTest direct_simd_out_of_range_spelling = {0};
    BUSTER_TEST(arguments, !assembly_test_aarch64_direct_simd_spelling_at(direct_simd_spelling_count,
                                                                            &direct_simd_out_of_range_spelling));
    BUSTER_TEST(arguments, direct_simd_spelling_count == 392);
    BUSTER_TEST(arguments, direct_simd_covered_count == 390);
    BUSTER_TEST(arguments, direct_simd_uncovered_count == 0);
    BUSTER_TEST(arguments, direct_simd_covered_transform_count == 263);
    BUSTER_TEST(arguments, direct_simd_covered_no_transform_count == 127);
    BUSTER_TEST(arguments, direct_simd_uncovered_transform_count == 0);
    BUSTER_TEST(arguments, direct_simd_uncovered_no_transform_count == 0);
    BUSTER_TEST(arguments, direct_simd_covered_count + 2 == direct_simd_spelling_count);
    BUSTER_TEST(arguments, direct_simd_compound_requirement_count == 81 && direct_simd_compound_requirement_exact);
    BUSTER_TEST(arguments, direct_simd_fhm_requirement_count == 4 && direct_simd_fhm_requirement_exact);
    BUSTER_TEST(arguments, direct_simd_frintts_requirement_count == 4 && direct_simd_frintts_requirement_exact);
    BUSTER_TEST(arguments, direct_simd_dotprod_requirement_count == 2 && direct_simd_dotprod_requirement_exact);
    BUSTER_TEST(arguments, direct_simd_dotprod_fixed_field_exact);
    BUSTER_TEST(arguments, direct_simd_rdm_requirement_count == 4 && direct_simd_rdm_requirement_exact);
    BUSTER_TEST(arguments, direct_simd_fcsel_row_count == 3 && direct_simd_fcsel_rows_exact);
    BUSTER_TEST(arguments, direct_simd_first_fp16_row_exact && direct_simd_last_fp16_row_exact);
    static AssemblyA64DirectSIMDSpellingExpectation const m1_collision_spellings[] = {
        {S8_INITIALIZER("arm-a64@2026-06:ADD_asimdsame_only"), UINT64_C(0x979101fa1dd62d38), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:ADD_asisdsame_only"), UINT64_C(0x1f1182a2f55aaad9), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:AND_asimdsame_only"), UINT64_C(0x52df93db3d4db99d), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:BIC_asimdsame_only"), UINT64_C(0x70338c1f69cbc030), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:CLS_asimdmisc_R"), UINT64_C(0xc9168638e385e157), 2,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:CLZ_asimdmisc_R"), UINT64_C(0x0b1fe309be6bba71), 2,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:EOR_asimdsame_only"), UINT64_C(0xa6d57e7ad7709737), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:ORN_asimdsame_only"), UINT64_C(0xfb87dd553345428c), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:RBIT_asimdmisc_R"), UINT64_C(0x15375b291dd8409f), 2,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:REV16_asimdmisc_R"), UINT64_C(0x85e55f83cf9406e7), 2,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:REV32_asimdmisc_R"), UINT64_C(0x6be39e0e8b2edb2c), 2,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:SUB_asimdsame_only"), UINT64_C(0x9353fa8f698b9abb), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
        {S8_INITIALIZER("arm-a64@2026-06:SUB_asisdsame_only"), UINT64_C(0x5d1dde1d88b9da52), 3,
         {BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D,
          BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID}, 0, 0},
    };
    u32 m1_collision_first_spelling = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(m1_collision_spellings); expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = m1_collision_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        bool exact = true;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (!assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) ||
                spelling.source_digest != expected.source_digest)
            {
                continue;
            }
            found_count += 1;
            found_index = spelling_index;
            exact = exact && string_equal(spelling.semantic_id, expected.semantic_id) && spelling.operand_count == expected.operand_count &&
                    spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                    memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0;
        }
        BUSTER_TEST(arguments, found_count == 1 && exact);
        if (expected_index == 0)
        {
            m1_collision_first_spelling = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == m1_collision_first_spelling + expected_index);
        }
    }
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_suffix_spellings); expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_fcvt_suffix_spellings[expected_index];
        u32 found_count = 0;
        bool exact = true;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (!assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) ||
                !string_equal(spelling.semantic_id, expected.semantic_id) || spelling.source_digest != expected.source_digest ||
                spelling.fixed_field_kind != expected.fixed_field_kind || spelling.fixed_field_value != expected.fixed_field_value)
            {
                continue;
            }
            found_count += 1;
            exact = exact && spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                    spelling.operand_count == expected.operand_count &&
                    memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0;
        }
        BUSTER_TEST(arguments, found_count == 1 && exact);
    }
    arguments->show(arguments,
                    S8("A64_DIRECT_SIMD_COVERAGE rows={u32} executable={u32} transform={u32} bindings={u32} spellings={u32} covered={u32} remaining={u32} covered_transform={u32} covered_no_transform={u32} uncovered_transform={u32} uncovered_no_transform={u32}\n"),
                    direct_simd_row_count, direct_simd_executable_count, direct_simd_transform_count, direct_simd_binding_count,
                    direct_simd_spelling_count, direct_simd_covered_count, direct_simd_uncovered_count,
                    direct_simd_covered_transform_count, direct_simd_covered_no_transform_count,
                    direct_simd_uncovered_transform_count, direct_simd_uncovered_no_transform_count);

    /* Keep the six SHA-1 fixed rows tied to their canonical IDs and source
     * digests.  Their table entries are intentionally contiguous so a future
     * edit cannot silently drop or reorder one of this cohesive cohort. */
    u32 sha1_first_spelling_index = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_sha1_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_SHA2 &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            sha1_first_spelling_index = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == sha1_first_spelling_index + expected_index);
        }
    }

    /* Keep the four FHM transform rows tied to their canonical IDs/digests and
     * contiguous in the public spelling table. */
    u32 fhm_first_spelling_index = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fhm_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_fhm_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            fhm_first_spelling_index = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == fhm_first_spelling_index + expected_index);
        }
    }

    /* Keep the four FRINTTS transform rows tied to their canonical IDs/digests
     * and contiguous in the public spelling table. */
    u32 frintts_first_spelling_index = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_frintts_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            frintts_first_spelling_index = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == frintts_first_spelling_index + expected_index);
        }
    }

    /* Keep the two DotProd transform rows tied to their canonical IDs/digests
     * and contiguous in the public spelling table. */
    u32 dotprod_first_spelling_index = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_dotprod_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            dotprod_first_spelling_index = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == dotprod_first_spelling_index + expected_index);
        }
    }

    /* Keep the two vector RDM transform rows tied to their canonical
     * IDs/digests.  Each vector row is immediately followed by its scalar
     * same-mnemonic sibling in the public spelling table. */
    u32 rdm_first_spelling_index = UINT32_MAX;
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_rdm_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            rdm_first_spelling_index = found_index;
        }
        else
        {
            BUSTER_TEST(arguments, found_index == rdm_first_spelling_index + 2);
        }
    }

    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_rdm_scalar_spellings[expected_index];
        u32 found_count = 0;
        u32 found_index = UINT32_MAX;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                found_index = spelling_index;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
        if (expected_index == 0)
        {
            BUSTER_TEST(arguments, found_index == rdm_first_spelling_index + 1);
        }
        else
        {
            BUSTER_TEST(arguments, found_index == rdm_first_spelling_index + 3);
        }
    }

    /* Keep the five scalar S/D transform rows tied to their canonical IDs.
     * The lookup census below independently proves that each same-mnemonic
     * candidate group remains contiguous after insertion. */
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_selector_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_scalar_selector_spellings[expected_index];
        u32 found_count = 0;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
    }
    static String8 const scalar_selector_mnemonics[] = {
        S8_INITIALIZER("frecpe"), S8_INITIALIZER("frecpx"), S8_INITIALIZER("frsqrte"), S8_INITIALIZER("scvtf"),
        S8_INITIALIZER("ucvtf"),
    };
    static u32 const scalar_selector_group_counts[] = {4, 2, 4, 4, 4};
    for (u32 mnemonic_index = 0; mnemonic_index < BUSTER_ARRAY_LENGTH(scalar_selector_mnemonics); mnemonic_index += 1)
    {
        u32 group_count = 0;
        u32 last_index = UINT32_MAX;
        bool contiguous = true;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (!assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) ||
                !string_equal(spelling.mnemonic, scalar_selector_mnemonics[mnemonic_index]))
            {
                continue;
            }
            if (last_index != UINT32_MAX && spelling_index != last_index + 1)
            {
                contiguous = false;
            }
            last_index = spelling_index;
            group_count += 1;
        }
        BUSTER_TEST(arguments, contiguous && group_count == scalar_selector_group_counts[mnemonic_index]);
    }

    /* Keep the three scalar narrowing rows tied to their canonical IDs and
     * digests.  Their public arrangements are transform-derived, so all four
     * table slots remain INVALID in the spelling metadata. */
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_narrow_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_scalar_narrow_spellings[expected_index];
        u32 found_count = 0;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
    }

    /* Keep the three scalar widening multiply rows tied to their canonical
     * IDs and digests.  Their public arrangements are transform-derived, so
     * all four table slots remain INVALID in the spelling metadata. */
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_widen_spellings);
         expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_scalar_widen_spellings[expected_index];
        u32 found_count = 0;
        for (u32 spelling_index = 0; spelling_index < direct_simd_spelling_count; spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
    }

    Target x86_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    Target ace_target = x86_target;
    ace_target.cpu_model = CPU_MODEL_BASELINE;
    ace_target.cpu_features_explicit = true;
    ace_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                         TARGET_CPU_FEATURE_X86_ACE_1},
                                                              2);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(ace_target));
    AssemblyEncodeResult ace_intel = assembly_encode(arguments->arena, S8("bsrmovf zmm0, zmm0\n"),
                                                      (AssemblyEncodeOptions){.target = ace_target,
                                                                               .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_ace_bsr_movf[] = {0x62, 0xf6, 0xfc, 0x48, 0x95, 0xc0};
    BUSTER_TEST(arguments, ace_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_intel.bytes, expected_ace_bsr_movf,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_movf)));
    AssemblyEncodeResult ace_att = assembly_encode(arguments->arena, S8("bsrmovf %zmm0, %zmm0\n"),
                                                    (AssemblyEncodeOptions){.target = ace_target,
                                                                             .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, ace_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_att.bytes, expected_ace_bsr_movf,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_movf)));
    AssemblyEncodeResult ace_init_intel = assembly_encode(arguments->arena, S8("bsrinit\n"),
                                                           (AssemblyEncodeOptions){.target = ace_target,
                                                                                    .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_init_att = assembly_encode(arguments->arena, S8("bsrinit\n"),
                                                         (AssemblyEncodeOptions){.target = ace_target,
                                                                                  .syntax = ASSEMBLY_SYNTAX_ATT});
    AssemblyEncodeResult ace_init_explicit_bsr0 = assembly_encode(
        arguments->arena, S8("bsrinit bsr0\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_ace_bsr_init[] = {0xc4, 0xe2, 0xfb, 0x49, 0xc0};
    BUSTER_TEST(arguments, ace_init_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_init_intel.bytes, expected_ace_bsr_init,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_init)));
    BUSTER_TEST(arguments, ace_init_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_init_att.bytes, expected_ace_bsr_init,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_init)));
    BUSTER_TEST(arguments, ace_init_explicit_bsr0.diagnostic_count == 1 &&
                               ace_init_explicit_bsr0.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    // Every ACE-1 BSRMOV form is exposed through both source dialects.  The
    // explicit BSR0 operand carries the direction for the H/L rows; memory
    // rows use qword ptr in Intel and the fixed u64 schema normalization in
    // AT&T.  Distinct ZMM registers on BSRMOVF make operand topology visible
    // rather than allowing an accidental same-register encoding to pass.
    typedef struct AceAssemblyCase AceAssemblyCase;
    struct AceAssemblyCase
    {
        String8 source;
        AssemblySyntax syntax;
        u8 expected[6];
    };
    static AceAssemblyCase const ace_front_door_cases[] = {
        {S8_INITIALIZER("bsrmovf zmm1, zmm2\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0xc2}},
        {S8_INITIALIZER("bsrmovf zmm1, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh bsr0, zmm1\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xff, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh bsr0, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xff, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh zmm1, bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh qword ptr [rax], bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl bsr0, zmm1\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl bsr0, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl zmm1, bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl qword ptr [rax], bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovf %zmm2, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0xc2}},
        {S8_INITIALIZER("bsrmovf (%rax), %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh %zmm1, %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xff, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh (%rax), %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xff, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh %bsr0, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh %bsr0, (%rax)\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl %zmm1, %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl (%rax), %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl %bsr0, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl %bsr0, (%rax)\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0x00}},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(ace_front_door_cases); case_index += 1)
    {
        AceAssemblyCase const test_case = ace_front_door_cases[case_index];
        AssemblyEncodeResult encoded = assembly_encode(arguments->arena, test_case.source,
                                                        (AssemblyEncodeOptions){.target = ace_target, .syntax = test_case.syntax});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, test_case.expected,
                                                             BUSTER_ARRAY_LENGTH(test_case.expected)));
    }
    AssemblyEncodeResult ace_wrong_direction = assembly_encode(
        arguments->arena, S8("bsrmovh zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_wrong_class = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, ymm1\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_wrong_width = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, byte ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_wrong_direction.diagnostic_count == 1 &&
                               ace_wrong_direction.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    BUSTER_TEST(arguments, ace_wrong_class.diagnostic_count == 1 &&
                               ace_wrong_class.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    BUSTER_TEST(arguments, ace_wrong_width.diagnostic_count == 1 &&
                               ace_wrong_width.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    Target amx_tile_only_target = ace_target;
    amx_tile_only_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                                  TARGET_CPU_FEATURE_X86_AMX_TILE},
                                                                       2);
    AssemblyEncodeResult ace_without_feature = assembly_encode(
        arguments->arena, S8("bsrmovf zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_init_without_feature = assembly_encode(
        arguments->arena, S8("bsrinit\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_with_amx_tile = assembly_encode(
        arguments->arena, S8("bsrmovf zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = amx_tile_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_without_feature.diagnostic_count == 1 &&
                               ace_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, ace_init_without_feature.diagnostic_count == 1 &&
                               ace_init_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, ace_with_amx_tile.diagnostic_count == 1 &&
                               ace_with_amx_tile.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult ace_bsr0_without_feature = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, zmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_bsr0_without_feature.diagnostic_count == 1 &&
                               ace_bsr0_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target scalar_feature_target = x86_target;
    scalar_feature_target.cpu_model = CPU_MODEL_BASELINE;
    scalar_feature_target.cpu_features_explicit = true;
    scalar_feature_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_F16C,
        TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_SSE4_2, TARGET_CPU_FEATURE_X86_BMI2,
        TARGET_CPU_FEATURE_X86_RDRAND}, 7);
    AssemblyEncodeResult f16c_without_feature = assembly_encode(
        arguments->arena, S8("vcvtph2ps xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult f16c_with_feature = assembly_encode(
        arguments->arena, S8("vcvtph2ps xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult fma_without_feature = assembly_encode(
        arguments->arena, S8("vfmadd132ps xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult fma_with_feature = assembly_encode(
        arguments->arena, S8("vfmadd132ps xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult bmi2_without_feature = assembly_encode(
        arguments->arena, S8("bzhi rax, rbx, rcx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult bmi2_with_feature = assembly_encode(
        arguments->arena, S8("bzhi rax, rbx, rcx\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult sse42_without_feature = assembly_encode(
        arguments->arena, S8("crc32 eax, ecx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult sse42_with_feature = assembly_encode(
        arguments->arena, S8("crc32 eax, ecx\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, f16c_without_feature.diagnostic_count == 1 &&
                               f16c_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               f16c_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, fma_without_feature.diagnostic_count == 1 &&
                               fma_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               fma_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, bmi2_without_feature.diagnostic_count == 1 &&
                               bmi2_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               bmi2_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, sse42_without_feature.diagnostic_count == 1 &&
                               sse42_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               sse42_with_feature.diagnostic_count == 0);
    Target sse4a_target = x86_target;
    sse4a_target.cpu_model = CPU_MODEL_AMD_AMD_FAMILY_10;
    sse4a_target.cpu_features_explicit = true;
    sse4a_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                          TARGET_CPU_FEATURE_X86_SSE3,
                                                                                          TARGET_CPU_FEATURE_X86_SSE4A},
                                                               3);
    Target virtualization_target = x86_target;
    virtualization_target.cpu_model = CPU_MODEL_BASELINE;
    virtualization_target.cpu_features_explicit = true;
    virtualization_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_SVM, TARGET_CPU_FEATURE_X86_VMX}, 3);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("svm")) == TARGET_CPU_FEATURE_X86_SVM);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("vmx")) == TARGET_CPU_FEATURE_X86_VMX);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(virtualization_target));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, virtualization_target), S8("sse2,svm,vmx"));
    AssemblyEncodeResult x86 = assembly_encode(arguments->arena, S8("start:\n nop\n call external\n jmp start\n ret\n"),
                                                (AssemblyEncodeOptions){
                                                    .target = x86_target,
                                                    .syntax = ASSEMBLY_SYNTAX_INTEL,
                                                });
    u8 expected_x86[] = {
        0x90, 0xe8, 0x00, 0x00, 0x00, 0x00, 0xe9, 0xf5, 0xff, 0xff, 0xff, 0xc3,
    };
    BUSTER_TEST(arguments, x86.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86.bytes.length == sizeof(expected_x86) && memcmp(x86.bytes.pointer, expected_x86, sizeof(expected_x86)) == 0);
    BUSTER_TEST(arguments, x86.symbol_count == 2 && x86.symbols[0].defined && x86.symbols[0].offset == 0 &&
                               string_equal(x86.symbols[0].name, S8("start")) && !x86.symbols[1].defined &&
                               string_equal(x86.symbols[1].name, S8("external")));
    BUSTER_TEST(arguments, x86.relocation_count == 1 && x86.relocations[0].offset == 2 && x86.relocations[0].symbol == 1 &&
                               x86.relocations[0].addend == -4 && x86.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    // Front-door routing for the legacy prefix-control rows: RET/LEAVE use
    // the newly normalized DF64/IMMUNE66_LOOP64 forms, while LOOP-family
    // branches retain their 8-bit displacement and ignore redundant 66.
    AssemblyEncodeResult residual_controls = assembly_encode(
        arguments->arena, S8("ret\nleave\nloop loop_target\nloopne loop_target\nloope loop_target\nloop_target:\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_residual_controls[] = {0xc3, 0xc9, 0xe2, 0x04, 0xe0, 0x02, 0xe1, 0x00};
    BUSTER_TEST(arguments, residual_controls.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(residual_controls.bytes, expected_residual_controls,
                                                         sizeof(expected_residual_controls)));

    AssemblyEncodeResult x86_syntax_switches = assembly_encode(
        arguments->arena,
        S8("mov rax, rbx ; Intel comment\n"
           ".att_syntax prefix\n"
           "movq %rcx, %rdx # AT&T comment\n"
           ".intel_syntax noprefix\n"
           "add r8, r9 // common comment\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_syntax_switches[] = {
        0x48, 0x89, 0xd8,
        0x48, 0x89, 0xca,
        0x4d, 0x01, 0xc8,
    };
    BUSTER_TEST(arguments, x86_syntax_switches.diagnostic_count == 0 &&
                               x86_syntax_switches.bytes.length == sizeof(expected_x86_syntax_switches) &&
                               memcmp(x86_syntax_switches.bytes.pointer, expected_x86_syntax_switches,
                                      sizeof(expected_x86_syntax_switches)) == 0);
    AssemblyEncodeResult invalid_x86_syntax_switches = assembly_encode(
        arguments->arena, S8(".intel_syntax prefix\n.att_syntax noprefix\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_syntax_switches.diagnostic_count == 2 &&
                               invalid_x86_syntax_switches.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX &&
                               invalid_x86_syntax_switches.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);
    Target aarch64_syntax_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    AssemblyEncodeResult invalid_aarch64_syntax_switch = assembly_encode(
        arguments->arena, S8(".intel_syntax noprefix\n"),
        (AssemblyEncodeOptions){.target = aarch64_syntax_target, .syntax = ASSEMBLY_SYNTAX_DEFAULT});
    BUSTER_TEST(arguments, invalid_aarch64_syntax_switch.diagnostic_count == 1 &&
                               invalid_aarch64_syntax_switch.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);

    u8 expected_x86_register_forms[] = {
        0x48, 0x89, 0xd8,
        0x49, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x48, 0x83, 0xc0, 0x07,
        0x45, 0x29, 0xd1,
        0x66, 0x21, 0xc8,
        0x45, 0x30, 0xc8,
        0x4d, 0x0f, 0xaf, 0xdc,
        0x41, 0x57,
        0x41, 0x5f,
        0x48, 0xff, 0xc0,
        0x41, 0xf7, 0xd8,
        0x48, 0xc1, 0xe0, 0x03,
        0x66, 0x98,
        0x98,
        0x48, 0x98,
        0x41, 0xff, 0xd3,
        0x4d, 0x01, 0xec,
        0x09, 0xc8,
        0x49, 0x81, 0xff, 0x7f, 0xff, 0xff, 0xff,
        0x41, 0xf7, 0xc0, 0x78, 0x56, 0x34, 0x12,
        0x48, 0xff, 0xcb,
        0x66, 0x41, 0xf7, 0xd1,
        0x41, 0xc1, 0xea, 0x04,
        0x49, 0xd1, 0xfb,
        0x41, 0xff, 0xe6,
    };
    String8 x86_intel_source =
        S8("mov rax, rbx\n"
           "mov r8, 0x1122334455667788\n"
           "add rax, 7\n"
           "sub r9d, r10d\n"
           "and ax, cx\n"
           "xor r8b, r9b\n"
           "imul r11, r12\n"
           "push r15\n"
           "pop r15\n"
           "inc rax\n"
           "neg r8d\n"
           "shl rax, 3\n"
           "cbw\n"
           "cwde\n"
           "cdqe\n"
           "call r11\n"
           "add r12, r13\n"
           "or eax, ecx\n"
           "cmp r15, -129\n"
           "test r8d, 0x12345678\n"
           "dec rbx\n"
           "not r9w\n"
           "shr r10d, 4\n"
           "sar r11, 1\n"
           "jmp r14\n");
    AssemblyEncodeResult x86_intel = assembly_encode(arguments->arena, x86_intel_source,
                                                      (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_intel.bytes.length == sizeof(expected_x86_register_forms) &&
                               memcmp(x86_intel.bytes.pointer, expected_x86_register_forms, sizeof(expected_x86_register_forms)) == 0);
    String8 x86_att_source =
        S8("movq %rbx, %rax\n"
           "movq $0x1122334455667788, %r8\n"
           "addq $7, %rax\n"
           "subl %r10d, %r9d\n"
           "andw %cx, %ax\n"
           "xorb %r9b, %r8b\n"
           "imulq %r12, %r11\n"
           "pushq %r15\n"
           "popq %r15\n"
           "incq %rax\n"
           "negl %r8d\n"
           "shlq $3, %rax\n"
           "cbtw\n"
           "cwtl\n"
           "cltq\n"
           "callq *%r11\n"
           "addq %r13, %r12\n"
           "orl %ecx, %eax\n"
           "cmpq $-129, %r15\n"
           "testl $0x12345678, %r8d\n"
           "decq %rbx\n"
           "notw %r9w\n"
           "shrl $4, %r10d\n"
           "sarq $1, %r11\n"
           "jmpq *%r14\n");
    AssemblyEncodeResult x86_att = assembly_encode(arguments->arena, x86_att_source,
                                                    (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_att.bytes.length == sizeof(expected_x86_register_forms) &&
                               memcmp(x86_att.bytes.pointer, expected_x86_register_forms, sizeof(expected_x86_register_forms)) == 0);
    u8 expected_x86_memory_forms[] = {
        0x48, 0x8b, 0x44, 0x8b, 0x10,
        0x47, 0x89, 0x54, 0xcc, 0xe0,
        0x66, 0x03, 0x45, 0x00,
        0x49, 0x83, 0x6d, 0x7f, 0x05,
        0x80, 0x36, 0x7f,
        0x4c, 0x0f, 0xaf, 0x1d, 0x00, 0x00, 0x00, 0x00,
        0x41, 0xff, 0x00,
        0x48, 0xd1, 0x64, 0x24, 0x08,
        0xff, 0x50, 0x18,
        0xc7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
        0xc3,
    };
    String8 x86_intel_memory_source =
        S8("mov rax, [rbx + rcx*4 + 16]\n"
           "mov [r12 + r9*8 - 32], r10d\n"
           "add ax, word ptr [rbp]\n"
           "sub qword ptr [r13 + 127], 5\n"
           "xor byte ptr [rsi], 0x7f\n"
           "imul r11, [rip + external]\n"
           "inc dword ptr [r8]\n"
           "shl qword ptr [rsp + 8], 1\n"
           "call qword ptr [rax + 24]\n"
           "mov dword ptr [rip + local], 0x12345678\n"
           "local:\n"
           "ret\n");
    AssemblyEncodeResult x86_intel_memory = assembly_encode(
        arguments->arena, x86_intel_memory_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_memory.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_intel_memory.bytes.length == sizeof(expected_x86_memory_forms) &&
                               memcmp(x86_intel_memory.bytes.pointer, expected_x86_memory_forms, sizeof(expected_x86_memory_forms)) == 0);
    BUSTER_TEST(arguments, x86_intel_memory.relocation_count == 1 && x86_intel_memory.relocations[0].offset == 26 &&
                               x86_intel_memory.relocations[0].addend == -4 &&
                               x86_intel_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_memory.symbols[x86_intel_memory.relocations[0].symbol].name, S8("external")));
    String8 x86_att_memory_source =
        S8("movq 16(%rbx,%rcx,4), %rax\n"
           "movl %r10d, -32(%r12,%r9,8)\n"
           "addw (%rbp), %ax\n"
           "subq $5, 127(%r13)\n"
           "xorb $0x7f, (%rsi)\n"
           "imulq external(%rip), %r11\n"
           "incl (%r8)\n"
           "shlq $1, 8(%rsp)\n"
           "callq *24(%rax)\n"
           "movl $0x12345678, local(%rip)\n"
           "local:\n"
           "ret\n");
    AssemblyEncodeResult x86_att_memory = assembly_encode(
        arguments->arena, x86_att_memory_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_memory.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_att_memory.bytes.length == sizeof(expected_x86_memory_forms) &&
                               memcmp(x86_att_memory.bytes.pointer, expected_x86_memory_forms, sizeof(expected_x86_memory_forms)) == 0);
    BUSTER_TEST(arguments, x86_att_memory.relocation_count == 1 && x86_att_memory.relocations[0].offset == 26 &&
                               x86_att_memory.relocations[0].addend == -4 &&
                               x86_att_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    // GNU/AT&T treats a bare non-immediate expression as an absolute memory
    // operand.  Keep the explicit '$' spelling as an immediate and preserve
    // direct versus indirect branch targets despite their shared bare form.
    AssemblyEncodeResult x86_att_absolute = assembly_encode(
        arguments->arena,
        S8("movq 0x12345678, %rax\n"
           "movq %rax, 0x12345678\n"
           "movb external, %al\n"
           "movb %al, external\n"
           "movq 0x123456789, %rax\n"
           "call target\n"
           "call *target\n"
           "target:\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_att_absolute[] = {
        0x48, 0x8b, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x48, 0x89, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x8a, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x88, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x48, 0xa1, 0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
        0xe8, 0x07, 0x00, 0x00, 0x00,
        0xff, 0x14, 0x25, 0x34, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_att_absolute.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_absolute.bytes, expected_x86_att_absolute,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_att_absolute)));
    BUSTER_TEST(arguments, x86_att_absolute.relocation_count == 2 &&
                               x86_att_absolute.relocations[0].offset == 19 &&
                               x86_att_absolute.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               x86_att_absolute.relocations[1].offset == 26 &&
                               x86_att_absolute.relocations[1].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               string_equal(x86_att_absolute.symbols[x86_att_absolute.relocations[0].symbol].name, S8("external")) &&
                               string_equal(x86_att_absolute.symbols[x86_att_absolute.relocations[1].symbol].name, S8("external")));
    AssemblyEncodeResult x86_att_immediate = assembly_encode(
        arguments->arena, S8("movq $0x10, %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_att_immediate[] = {0x48, 0xb8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_att_immediate.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_immediate.bytes, expected_x86_att_immediate,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_att_immediate)));
    AssemblyEncodeResult push_immediate_probe = assembly_encode(
        arguments->arena, S8("push 0x7f\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, push_immediate_probe.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(push_immediate_probe.bytes, (u8 const[]){0x6a, 0x7f}, 2));
    typedef struct PushImmediateAssemblyCase PushImmediateAssemblyCase;
    struct PushImmediateAssemblyCase
    {
        String8 intel;
        String8 att;
        u8 bytes[5];
        u8 byte_count;
        bool valid;
    };
    static PushImmediateAssemblyCase const push_immediate_cases[] = {
        {S8_INITIALIZER("push -128\n"), S8_INITIALIZER("pushq $-128\n"), {0x6a, 0x80}, 2, true},
        {S8_INITIALIZER("push 127\n"), S8_INITIALIZER("pushq $127\n"), {0x6a, 0x7f}, 2, true},
        {S8_INITIALIZER("push -129\n"), S8_INITIALIZER("pushq $-129\n"), {0x68, 0x7f, 0xff, 0xff, 0xff}, 5, true},
        {S8_INITIALIZER("push 128\n"), S8_INITIALIZER("pushq $128\n"), {0x68, 0x80, 0x00, 0x00, 0x00}, 5, true},
        {S8_INITIALIZER("push -2147483648\n"), S8_INITIALIZER("pushq $-2147483648\n"), {0x68, 0x00, 0x00, 0x00, 0x80}, 5, true},
        {S8_INITIALIZER("push 2147483647\n"), S8_INITIALIZER("pushq $2147483647\n"), {0x68, 0xff, 0xff, 0xff, 0x7f}, 5, true},
        {S8_INITIALIZER("push 2147483648\n"), S8_INITIALIZER("pushq $2147483648\n"), {0}, 0, false},
        {S8_INITIALIZER("push 0x80000000\n"), S8_INITIALIZER("pushq $0x80000000\n"), {0}, 0, false},
    };
    for (u32 push_index = 0; push_index < BUSTER_ARRAY_LENGTH(push_immediate_cases); push_index += 1)
    {
        PushImmediateAssemblyCase push_case = push_immediate_cases[push_index];
        AssemblyEncodeResult push_intel = assembly_encode(
            arguments->arena, push_case.intel,
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult push_att = assembly_encode(
            arguments->arena, push_case.att,
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        if (push_case.valid)
        {
            BUSTER_TEST(arguments, push_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(push_intel.bytes, push_case.bytes, push_case.byte_count) &&
                                       push_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(push_att.bytes, push_case.bytes, push_case.byte_count));
        }
        else
        {
            BUSTER_TEST(arguments, push_intel.diagnostic_count == 1 && push_intel.bytes.length == 0 &&
                                       push_att.diagnostic_count == 1 && push_att.bytes.length == 0);
        }
    }
    AssemblyEncodeResult push_symbol = assembly_encode(
        arguments->arena, S8("push external\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult push_symbol_att = assembly_encode(
        arguments->arena, S8("pushq $external\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    AssemblyEncodeResult push_att_wrong_suffix = assembly_encode(
        arguments->arena, S8("pushl $1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, push_symbol.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(push_symbol.bytes, (u8 const[]){0x68, 0, 0, 0, 0}, 5) &&
                               push_symbol.relocation_count == 1 && push_symbol.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32 &&
                               push_symbol.relocations[0].offset == 1 && push_symbol_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(push_symbol_att.bytes, (u8 const[]){0x68, 0, 0, 0, 0}, 5) &&
                               push_symbol_att.relocation_count == 1 && push_symbol_att.relocations[0].offset == 1 &&
                               push_symbol_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32);
    BUSTER_TEST(arguments, push_att_wrong_suffix.diagnostic_count == 1 && push_att_wrong_suffix.bytes.length == 0);
    // CET indirect-branch tracking has a typed `notrack` source prefix.  It
    // is accepted in both dialects for register and memory CALL/JMP forms,
    // while ordinary handwritten call/jmp syntax remains unprefixed.
    String8 x86_notrack_intel_source =
        S8("notrack call rax\n"
           "notrack jmp rax\n"
           "notrack call qword ptr [rax]\n"
           "notrack jmp qword ptr [rax]\n");
    u8 expected_x86_notrack[] = {
        0x3e, 0xff, 0xd0,
        0x3e, 0xff, 0xe0,
        0x3e, 0xff, 0x10,
        0x3e, 0xff, 0x20,
    };
    AssemblyEncodeResult x86_notrack_intel = assembly_encode(
        arguments->arena, x86_notrack_intel_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_notrack_intel.diagnostic_count == 0 &&
                               x86_notrack_intel.bytes.length == sizeof(expected_x86_notrack) &&
                               memcmp(x86_notrack_intel.bytes.pointer, expected_x86_notrack, sizeof(expected_x86_notrack)) == 0);
    String8 x86_notrack_att_source =
        S8("notrack callq *%rax\n"
           "notrack jmpq *%rax\n"
           "notrack callq *(%rax)\n"
           "notrack jmpq *(%rax)\n");
    AssemblyEncodeResult x86_notrack_att = assembly_encode(
        arguments->arena, x86_notrack_att_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_notrack_att.diagnostic_count == 0 &&
                               x86_notrack_att.bytes.length == sizeof(expected_x86_notrack) &&
                               memcmp(x86_notrack_att.bytes.pointer, expected_x86_notrack, sizeof(expected_x86_notrack)) == 0);
    AssemblyEncodeResult x86_notrack_invalid = assembly_encode(
        arguments->arena, S8("notrack call external\nnotrack jmp external\nnotrack notrack call rax\nrep notrack call rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_notrack_invalid.diagnostic_count == 4 && x86_notrack_invalid.bytes.length == 0);
    AssemblyEncodeResult x86_absolute_memory = assembly_encode(
        arguments->arena, S8("mov rax, [rbx + external + 8]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_absolute_memory[] = {0x48, 0x8b, 0x83, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_absolute_memory.diagnostic_count == 0 &&
                               x86_absolute_memory.bytes.length == sizeof(expected_x86_absolute_memory) &&
                               memcmp(x86_absolute_memory.bytes.pointer, expected_x86_absolute_memory, sizeof(expected_x86_absolute_memory)) == 0);
    BUSTER_TEST(arguments, x86_absolute_memory.relocation_count == 1 && x86_absolute_memory.relocations[0].offset == 3 &&
                               x86_absolute_memory.relocations[0].addend == 8 &&
                               x86_absolute_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32);

    // MOV moffs is the one legacy absolute-memory encoding whose accumulator
    // is implicit in the opcode.  Keep the accumulator written in source so
    // the assembly front door binds AL/RAX and direction to A0/A2 exactly;
    // the 64-bit address is deliberately outside ModRM's signed-32 range.
    String8 x86_moffs_source =
        S8("mov al, byte ptr es:[0x155667788]\n"
           "mov byte ptr es:[0x155667788], al\n");
    AssemblyEncodeResult x86_moffs = assembly_encode(
        arguments->arena, x86_moffs_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_moffs[] = {
        0x26, 0xa0, 0x88, 0x77, 0x66, 0x55, 0x01, 0x00, 0x00, 0x00,
        0x26, 0xa2, 0x88, 0x77, 0x66, 0x55, 0x01, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_moffs.diagnostic_count == 0 &&
                               x86_moffs.bytes.length == sizeof(expected_x86_moffs) &&
                               memcmp(x86_moffs.bytes.pointer, expected_x86_moffs, sizeof(expected_x86_moffs)) == 0);
    AssemblyEncodeResult x86_moffs_wrong_accumulator = assembly_encode(
        arguments->arena, S8("mov bl, byte ptr es:[0x155667788]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_moffs_wrong_accumulator.diagnostic_count != 0 && x86_moffs_wrong_accumulator.bytes.length == 0);

    // MASKMOV's architectural destination is the implicit [DI] location;
    // only the two visible vector registers appear in source.  Intel and
    // AT&T spellings select the same REG/RM bytes after AT&T's operand
    // reversal, while address-size 32 carries the ordinary 67 override.
    AssemblyEncodeResult x86_maskmov_intel = assembly_encode(
        arguments->arena,
        S8("maskmovq mm0, mm1\nmaskmovdqu xmm0, xmm1\naddr32 maskmovq mm0, mm1\n"
           "fs maskmovq mm0, mm1\ngs addr32 maskmovdqu xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_maskmov_intel[] = {
        0x0f, 0xf7, 0xc1,
        0x66, 0x0f, 0xf7, 0xc1,
        0x67, 0x0f, 0xf7, 0xc1,
        0x64, 0x0f, 0xf7, 0xc1,
        0x65, 0x67, 0x66, 0x0f, 0xf7, 0xc1,
    };
    BUSTER_TEST(arguments, x86_maskmov_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_maskmov_intel.bytes, expected_x86_maskmov_intel,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_maskmov_intel)));
    AssemblyEncodeResult x86_maskmov_att = assembly_encode(
        arguments->arena, S8("maskmovq %mm1, %mm0\nmaskmovdqu %xmm1, %xmm0\nfs maskmovq %mm1, %mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_maskmov_att[] = {0x0f, 0xf7, 0xc1, 0x66, 0x0f, 0xf7, 0xc1, 0x64, 0x0f, 0xf7, 0xc1};
    BUSTER_TEST(arguments, x86_maskmov_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_maskmov_att.bytes, expected_x86_maskmov_att,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_maskmov_att)));
    AssemblyEncodeResult x86_maskmov_wrong_class = assembly_encode(
        arguments->arena, S8("maskmovq xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_wrong_count = assembly_encode(
        arguments->arena, S8("maskmovq mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_wrong_direction = assembly_encode(
        // The implicit [DI] store cannot be written as an explicit memory
        // destination in either operand direction.
        arguments->arena, S8("maskmovq [rdi], mm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_maskmov_wrong_class.diagnostic_count != 0 && x86_maskmov_wrong_class.bytes.length == 0 &&
                               x86_maskmov_wrong_count.diagnostic_count != 0 && x86_maskmov_wrong_count.bytes.length == 0 &&
                               x86_maskmov_wrong_direction.diagnostic_count != 0 && x86_maskmov_wrong_direction.bytes.length == 0);
    AssemblyEncodeResult x86_maskmov_fs = assembly_encode(
        arguments->arena, S8("fs:maskmovq mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_gs_att = assembly_encode(
        arguments->arena, S8("%gs:maskmovq %mm1, %mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_maskmov_fs.diagnostic_count != 0 && x86_maskmov_fs.bytes.length == 0);
    BUSTER_TEST(arguments, x86_maskmov_gs_att.diagnostic_count != 0 && x86_maskmov_gs_att.bytes.length == 0);

    u8 expected_x86_adc_sbb[] = {
        0x10, 0xd8,
        0x66, 0x11, 0xc8,
        0x11, 0xc8,
        0x4d, 0x11, 0xc8,
        0x45, 0x10, 0x4c, 0x24, 0x08,
        0x66, 0x45, 0x13, 0x55, 0x7f,
        0x48, 0x83, 0xd0, 0x7f,
        0x48, 0x81, 0xd3, 0x80, 0x00, 0x00, 0x00,
        0x18, 0xd8,
        0x66, 0x19, 0xc8,
        0x19, 0xc8,
        0x4d, 0x19, 0xc8,
        0x45, 0x18, 0x4c, 0x24, 0x08,
        0x66, 0x45, 0x1b, 0x55, 0x7f,
        0x48, 0x83, 0xd8, 0x7f,
        0x48, 0x81, 0xdb, 0x80, 0x00, 0x00, 0x00,
    };
    String8 x86_intel_adc_sbb_source =
        S8("adc al, bl\n"
           "adc ax, cx\n"
           "adc eax, ecx\n"
           "adc r8, r9\n"
           "adc byte ptr [r12 + 8], r9b\n"
           "adc r10w, word ptr [r13 + 127]\n"
           "adc rax, 127\n"
           "adc rbx, 128\n"
           "sbb al, bl\n"
           "sbb ax, cx\n"
           "sbb eax, ecx\n"
           "sbb r8, r9\n"
           "sbb byte ptr [r12 + 8], r9b\n"
           "sbb r10w, word ptr [r13 + 127]\n"
           "sbb rax, 127\n"
           "sbb rbx, 128\n");
    AssemblyEncodeResult x86_intel_adc_sbb = assembly_encode(
        arguments->arena, x86_intel_adc_sbb_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_adc_sbb.diagnostic_count == 0 &&
                               x86_intel_adc_sbb.bytes.length == sizeof(expected_x86_adc_sbb) &&
                               memcmp(x86_intel_adc_sbb.bytes.pointer, expected_x86_adc_sbb, sizeof(expected_x86_adc_sbb)) == 0);
    String8 x86_att_adc_sbb_source =
        S8("adcb %bl, %al\n"
           "adcw %cx, %ax\n"
           "adcl %ecx, %eax\n"
           "adcq %r9, %r8\n"
           "adcb %r9b, 8(%r12)\n"
           "adcw 127(%r13), %r10w\n"
           "adcq $127, %rax\n"
           "adcq $128, %rbx\n"
           "sbbb %bl, %al\n"
           "sbbw %cx, %ax\n"
           "sbbl %ecx, %eax\n"
           "sbbq %r9, %r8\n"
           "sbbb %r9b, 8(%r12)\n"
           "sbbw 127(%r13), %r10w\n"
           "sbbq $127, %rax\n"
           "sbbq $128, %rbx\n");
    AssemblyEncodeResult x86_att_adc_sbb = assembly_encode(
        arguments->arena, x86_att_adc_sbb_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_adc_sbb.diagnostic_count == 0 &&
                               x86_att_adc_sbb.bytes.length == sizeof(expected_x86_adc_sbb) &&
                               memcmp(x86_att_adc_sbb.bytes.pointer, expected_x86_adc_sbb, sizeof(expected_x86_adc_sbb)) == 0);

    u8 expected_x86_unary_integer[] = {
        0xf6, 0xe0,
        0x66, 0xf7, 0xe1,
        0xf7, 0xe2,
        0x49, 0xf7, 0xe0,
        0xf6, 0xeb,
        0x66, 0xf7, 0xe9,
        0xf7, 0xea,
        0x49, 0xf7, 0xe9,
        0x41, 0xf6, 0x64, 0x24, 0x08,
        0x66, 0x41, 0xf7, 0x6d, 0x10,
        0x41, 0xf7, 0x30,
        0x49, 0xf7, 0x79, 0x7f,
        0xf6, 0xf1,
        0x66, 0xf7, 0xf6,
        0xf7, 0xf6,
        0x49, 0xf7, 0xf2,
        0xf6, 0xf9,
        0x66, 0xf7, 0xff,
        0xf7, 0xfe,
        0x49, 0xf7, 0xfb,
    };
    String8 x86_intel_unary_integer_source =
        S8("mul al\n"
           "mul cx\n"
           "mul edx\n"
           "mul r8\n"
           "imul bl\n"
           "imul cx\n"
           "imul edx\n"
           "imul r9\n"
           "mul byte ptr [r12 + 8]\n"
           "imul word ptr [r13 + 16]\n"
           "div dword ptr [r8]\n"
           "idiv qword ptr [r9 + 127]\n"
           "div cl\n"
           "div si\n"
           "div esi\n"
           "div r10\n"
           "idiv cl\n"
           "idiv di\n"
           "idiv esi\n"
           "idiv r11\n");
    AssemblyEncodeResult x86_intel_unary_integer = assembly_encode(
        arguments->arena, x86_intel_unary_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_unary_integer.diagnostic_count == 0 &&
                               x86_intel_unary_integer.bytes.length == sizeof(expected_x86_unary_integer) &&
                               memcmp(x86_intel_unary_integer.bytes.pointer, expected_x86_unary_integer,
                                      sizeof(expected_x86_unary_integer)) == 0);
    String8 x86_att_unary_integer_source =
        S8("mulb %al\n"
           "mulw %cx\n"
           "mull %edx\n"
           "mulq %r8\n"
           "imulb %bl\n"
           "imulw %cx\n"
           "imull %edx\n"
           "imulq %r9\n"
           "mulb 8(%r12)\n"
           "imulw 16(%r13)\n"
           "divl (%r8)\n"
           "idivq 127(%r9)\n"
           "divb %cl\n"
           "divw %si\n"
           "divl %esi\n"
           "divq %r10\n"
           "idivb %cl\n"
           "idivw %di\n"
           "idivl %esi\n"
           "idivq %r11\n");
    AssemblyEncodeResult x86_att_unary_integer = assembly_encode(
        arguments->arena, x86_att_unary_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_unary_integer.diagnostic_count == 0 &&
                               x86_att_unary_integer.bytes.length == sizeof(expected_x86_unary_integer) &&
                               memcmp(x86_att_unary_integer.bytes.pointer, expected_x86_unary_integer,
                                      sizeof(expected_x86_unary_integer)) == 0);

    u8 expected_x86_imul_integer[] = {
        0x66, 0x0f, 0xaf, 0xc1,
        0x41, 0x0f, 0xaf, 0xc1,
        0x4d, 0x0f, 0xaf, 0xd3,
        0x66, 0x45, 0x0f, 0xaf, 0x65, 0x20,
        0x66, 0x6b, 0xc0, 0x80,
        0x6b, 0xc0, 0x7f,
        0x4d, 0x69, 0xc0, 0x80, 0x00, 0x00, 0x00,
        0x4d, 0x69, 0xc9, 0x7f, 0xff, 0xff, 0xff,
        0x66, 0x6b, 0xc1, 0x80,
        0x41, 0x6b, 0x44, 0x24, 0x08, 0x7f,
        0x4d, 0x69, 0xc1, 0x80, 0x00, 0x00, 0x00,
        0x4d, 0x69, 0x54, 0x24, 0x08, 0x7f, 0xff, 0xff, 0xff,
    };
    String8 x86_intel_imul_integer_source =
        S8("imul ax, cx\n"
           "imul eax, r9d\n"
           "imul r10, r11\n"
           "imul r12w, word ptr [r13 + 32]\n"
           "imul ax, -128\n"
           "imul eax, 127\n"
           "imul r8, 128\n"
           "imul r9, -129\n"
           "imul ax, cx, -128\n"
           "imul eax, dword ptr [r12 + 8], 127\n"
           "imul r8, r9, 128\n"
           "imul r10, qword ptr [r12 + 8], -129\n");
    AssemblyEncodeResult x86_intel_imul_integer = assembly_encode(
        arguments->arena, x86_intel_imul_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_imul_integer.diagnostic_count == 0 &&
                               x86_intel_imul_integer.bytes.length == sizeof(expected_x86_imul_integer) &&
                               memcmp(x86_intel_imul_integer.bytes.pointer, expected_x86_imul_integer,
                                      sizeof(expected_x86_imul_integer)) == 0);
    String8 x86_att_imul_integer_source =
        S8("imulw %cx, %ax\n"
           "imull %r9d, %eax\n"
           "imulq %r11, %r10\n"
           "imulw 32(%r13), %r12w\n"
           "imulw $-128, %ax\n"
           "imull $127, %eax\n"
           "imulq $128, %r8\n"
           "imulq $-129, %r9\n"
           "imulw $-128, %cx, %ax\n"
           "imull $127, 8(%r12), %eax\n"
           "imulq $128, %r9, %r8\n"
           "imulq $-129, 8(%r12), %r10\n");
    AssemblyEncodeResult x86_att_imul_integer = assembly_encode(
        arguments->arena, x86_att_imul_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_imul_integer.diagnostic_count == 0 &&
                               x86_att_imul_integer.bytes.length == sizeof(expected_x86_imul_integer) &&
                               memcmp(x86_att_imul_integer.bytes.pointer, expected_x86_imul_integer,
                                      sizeof(expected_x86_imul_integer)) == 0);

    u8 expected_x86_cwd_cdq_cqo[] = {0x66, 0x99, 0x99, 0x48, 0x99};
    AssemblyEncodeResult x86_intel_cwd_cdq_cqo = assembly_encode(
        arguments->arena, S8("cwd\ncdq\ncqo\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_cwd_cdq_cqo.diagnostic_count == 0 &&
                               x86_intel_cwd_cdq_cqo.bytes.length == sizeof(expected_x86_cwd_cdq_cqo) &&
                               memcmp(x86_intel_cwd_cdq_cqo.bytes.pointer, expected_x86_cwd_cdq_cqo,
                                      sizeof(expected_x86_cwd_cdq_cqo)) == 0);
    AssemblyEncodeResult x86_att_cwd_cdq_cqo = assembly_encode(
        arguments->arena, S8("cwtd\ncltd\ncqto\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_cwd_cdq_cqo.diagnostic_count == 0 &&
                               x86_att_cwd_cdq_cqo.bytes.length == sizeof(expected_x86_cwd_cdq_cqo) &&
                               memcmp(x86_att_cwd_cdq_cqo.bytes.pointer, expected_x86_cwd_cdq_cqo,
                                      sizeof(expected_x86_cwd_cdq_cqo)) == 0);

    u8 expected_x86_imul_rip_relative[] = {0x4c, 0x0f, 0xaf, 0x15, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_intel_imul_rip_relative = assembly_encode(
        arguments->arena, S8("imul r10, qword ptr [rip + external]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_imul_rip_relative.diagnostic_count == 0 &&
                               x86_intel_imul_rip_relative.bytes.length == sizeof(expected_x86_imul_rip_relative) &&
                               memcmp(x86_intel_imul_rip_relative.bytes.pointer, expected_x86_imul_rip_relative,
                                      sizeof(expected_x86_imul_rip_relative)) == 0);
    BUSTER_TEST(arguments, x86_intel_imul_rip_relative.relocation_count == 1 &&
                               x86_intel_imul_rip_relative.relocations[0].offset == 4 &&
                               x86_intel_imul_rip_relative.relocations[0].addend == -4 &&
                               x86_intel_imul_rip_relative.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_imul_rip_relative.symbols[x86_intel_imul_rip_relative.relocations[0].symbol].name,
                                            S8("external")));
    AssemblyEncodeResult x86_att_imul_rip_relative = assembly_encode(
        arguments->arena, S8("imulq external(%rip), %r10\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_imul_rip_relative.diagnostic_count == 0 &&
                               x86_att_imul_rip_relative.bytes.length == sizeof(expected_x86_imul_rip_relative) &&
                               memcmp(x86_att_imul_rip_relative.bytes.pointer, expected_x86_imul_rip_relative,
                                      sizeof(expected_x86_imul_rip_relative)) == 0 &&
                               x86_att_imul_rip_relative.relocation_count == 1 &&
                               x86_att_imul_rip_relative.relocations[0].offset == 4 &&
                               x86_att_imul_rip_relative.relocations[0].addend == -4 &&
                               x86_att_imul_rip_relative.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_x86_integer_increment = assembly_encode(
        arguments->arena,
        S8("adc eax, rbx\n"
           "sbb rax, external\n"
           "mul eax, ecx\n"
           "imul al, bl\n"
           "imul al, bl, 1\n"
           "imul eax, ebx, external\n"
           "imul eax, ebx, 0x80000000\n"
           "cwd eax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_integer_increment.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_integer_increment.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_integer_increment.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_att_integer_increment = assembly_encode(
        arguments->arena,
        S8("adcq %rax, %eax\n"
           "sbbq %external, %rax\n"
           "mulq $1, %rax\n"
           "imulb %bl, %al\n"
           "imulb $1, %bl, %al\n"
           "imulq $external, %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_integer_increment.diagnostic_count == 6);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_att_integer_increment.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_att_integer_increment.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    u8 expected_x86_sse2[] = {
        0x0f, 0x28, 0xc1,
        0x47, 0x0f, 0x10, 0x44, 0x4c, 0x20,
        0x66, 0x45, 0x0f, 0x29, 0x7d, 0x00,
        0x66, 0x0f, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00,
        0x66, 0x0f, 0x6f, 0xdc,
        0xf3, 0x44, 0x0f, 0x7f, 0x48, 0x01,
        0x45, 0x0f, 0x57, 0xd3,
        0x66, 0x44, 0x0f, 0x57, 0x65, 0x00,
        0x66, 0x0f, 0xef, 0xca,
        0x0f, 0x58, 0xdc,
        0x66, 0x0f, 0x58, 0xee,
        0xf3, 0x41, 0x0f, 0x58, 0xf8,
        0xf2, 0x45, 0x0f, 0x58, 0xca,
        0x45, 0x0f, 0x5c, 0xdc,
        0x66, 0x45, 0x0f, 0x5c, 0xee,
        0x44, 0x0f, 0x59, 0xf8,
        0x66, 0x0f, 0x59, 0xca,
        0x0f, 0x5e, 0xdc,
        0x66, 0x0f, 0x5e, 0xee,
    };
    String8 x86_intel_sse2_source =
        S8("movaps xmm0, xmm1\n"
           "movups xmm8, [r12 + r9*2 + 32]\n"
           "movapd [r13], xmm15\n"
           "movupd xmm2, [rip + external]\n"
           "movdqa xmm3, xmm4\n"
           "movdqu [rax + 1], xmm9\n"
           "xorps xmm10, xmm11\n"
           "xorpd xmm12, [rbp]\n"
           "pxor xmm1, xmm2\n"
           "addps xmm3, xmm4\n"
           "addpd xmm5, xmm6\n"
           "addss xmm7, xmm8\n"
           "addsd xmm9, xmm10\n"
           "subps xmm11, xmm12\n"
           "subpd xmm13, xmm14\n"
           "mulps xmm15, xmm0\n"
           "mulpd xmm1, xmm2\n"
           "divps xmm3, xmm4\n"
           "divpd xmm5, xmm6\n");
    AssemblyEncodeResult x86_intel_sse2 = assembly_encode(
        arguments->arena, x86_intel_sse2_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_sse2.diagnostic_count == 0 && x86_intel_sse2.bytes.length == sizeof(expected_x86_sse2) &&
                               memcmp(x86_intel_sse2.bytes.pointer, expected_x86_sse2, sizeof(expected_x86_sse2)) == 0);
    BUSTER_TEST(arguments, x86_intel_sse2.relocation_count == 1 && x86_intel_sse2.relocations[0].offset == 19 &&
                               x86_intel_sse2.relocations[0].addend == -4 &&
                               x86_intel_sse2.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_sse2_source =
        S8("movaps %xmm1, %xmm0\n"
           "movups 32(%r12,%r9,2), %xmm8\n"
           "movapd %xmm15, (%r13)\n"
           "movupd external(%rip), %xmm2\n"
           "movdqa %xmm4, %xmm3\n"
           "movdqu %xmm9, 1(%rax)\n"
           "xorps %xmm11, %xmm10\n"
           "xorpd (%rbp), %xmm12\n"
           "pxor %xmm2, %xmm1\n"
           "addps %xmm4, %xmm3\n"
           "addpd %xmm6, %xmm5\n"
           "addss %xmm8, %xmm7\n"
           "addsd %xmm10, %xmm9\n"
           "subps %xmm12, %xmm11\n"
           "subpd %xmm14, %xmm13\n"
           "mulps %xmm0, %xmm15\n"
           "mulpd %xmm2, %xmm1\n"
           "divps %xmm4, %xmm3\n"
           "divpd %xmm6, %xmm5\n");
    AssemblyEncodeResult x86_att_sse2 = assembly_encode(
        arguments->arena, x86_att_sse2_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_sse2.diagnostic_count == 0 && x86_att_sse2.bytes.length == sizeof(expected_x86_sse2) &&
                               memcmp(x86_att_sse2.bytes.pointer, expected_x86_sse2, sizeof(expected_x86_sse2)) == 0);
    Target x86_without_sse2 = x86_target;
    x86_without_sse2.cpu_features_explicit = true;
    x86_without_sse2.cpu_features = target_cpu_features_empty();
    AssemblyEncodeResult unsupported_sse2 = assembly_encode(
        arguments->arena, S8("pxor xmm0, xmm0\n"),
        (AssemblyEncodeOptions){.target = x86_without_sse2, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_sse2.diagnostic_count == 1 &&
                               unsupported_sse2.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    u8 expected_x86_mmx[] = {
        0x0f, 0x6f, 0xc1,
        0x41, 0x0f, 0x7f, 0x7c, 0x24, 0x08,
        0x0f, 0xfc, 0xd3,
        0x0f, 0xfd, 0xe5,
        0x0f, 0xfe, 0xf7,
        0x0f, 0xd4, 0x45, 0x00,
        0x0f, 0xf8, 0xca,
        0x0f, 0xf9, 0xdc,
        0x0f, 0xfa, 0xee,
        0x0f, 0xfb, 0xf8,
        0x0f, 0xdb, 0xca,
        0x0f, 0xeb, 0xdc,
        0x0f, 0xef, 0xee,
        0x0f, 0x74, 0xf8,
        0x0f, 0x75, 0xca,
        0x0f, 0x76, 0xdc,
        0x0f, 0x64, 0xee,
        0x0f, 0x65, 0xf8,
        0x0f, 0x66, 0xca,
        0x0f, 0xd5, 0xdc,
        0x0f, 0x77,
    };
    String8 x86_intel_mmx_source =
        S8("movq mm0, mm1\n"
           "movq [r12 + 8], mm7\n"
           "paddb mm2, mm3\n"
           "paddw mm4, mm5\n"
           "paddd mm6, mm7\n"
           "paddq mm0, [rbp]\n"
           "psubb mm1, mm2\n"
           "psubw mm3, mm4\n"
           "psubd mm5, mm6\n"
           "psubq mm7, mm0\n"
           "pand mm1, mm2\n"
           "por mm3, mm4\n"
           "pxor mm5, mm6\n"
           "pcmpeqb mm7, mm0\n"
           "pcmpeqw mm1, mm2\n"
           "pcmpeqd mm3, mm4\n"
           "pcmpgtb mm5, mm6\n"
           "pcmpgtw mm7, mm0\n"
           "pcmpgtd mm1, mm2\n"
           "pmullw mm3, mm4\n"
           "emms\n");
    AssemblyEncodeResult x86_intel_mmx = assembly_encode(
        arguments->arena, x86_intel_mmx_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_mmx.diagnostic_count == 0 && x86_intel_mmx.bytes.length == sizeof(expected_x86_mmx) &&
                               memcmp(x86_intel_mmx.bytes.pointer, expected_x86_mmx, sizeof(expected_x86_mmx)) == 0);
    u8 expected_x86_address32_mmx[] = {0x67, 0x0f, 0x6f, 0x00};
    AssemblyEncodeResult x86_address32_mmx = assembly_encode(
        arguments->arena, S8("movq mm0, qword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_mmx.diagnostic_count == 0 && x86_address32_mmx.bytes.length == sizeof(expected_x86_address32_mmx) &&
                               memcmp(x86_address32_mmx.bytes.pointer, expected_x86_address32_mmx, sizeof(expected_x86_address32_mmx)) == 0);
    String8 x86_att_mmx_source =
        S8("movq %mm1, %mm0\n"
           "movq %mm7, 8(%r12)\n"
           "paddb %mm3, %mm2\n"
           "paddw %mm5, %mm4\n"
           "paddd %mm7, %mm6\n"
           "paddq (%rbp), %mm0\n"
           "psubb %mm2, %mm1\n"
           "psubw %mm4, %mm3\n"
           "psubd %mm6, %mm5\n"
           "psubq %mm0, %mm7\n"
           "pand %mm2, %mm1\n"
           "por %mm4, %mm3\n"
           "pxor %mm6, %mm5\n"
           "pcmpeqb %mm0, %mm7\n"
           "pcmpeqw %mm2, %mm1\n"
           "pcmpeqd %mm4, %mm3\n"
           "pcmpgtb %mm6, %mm5\n"
           "pcmpgtw %mm0, %mm7\n"
           "pcmpgtd %mm2, %mm1\n"
           "pmullw %mm4, %mm3\n"
           "emms\n");
    AssemblyEncodeResult x86_att_mmx = assembly_encode(
        arguments->arena, x86_att_mmx_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_mmx.diagnostic_count == 0 && x86_att_mmx.bytes.length == sizeof(expected_x86_mmx) &&
                               memcmp(x86_att_mmx.bytes.pointer, expected_x86_mmx, sizeof(expected_x86_mmx)) == 0);
    u8 expected_x86_x87[] = {
        0xd9, 0xc3, 0xdd, 0xd4, 0xdd, 0xdd, 0xd9, 0xce,
        0xd8, 0xc2, 0xdc, 0xcb, 0xd8, 0xe4, 0xdc, 0xe5,
        0xd8, 0xf6, 0xdc, 0xf7, 0xde, 0xc1, 0xde, 0xca,
        0xde, 0xeb, 0xde, 0xe4, 0xde, 0xfd, 0xde, 0xf6,
        0xd9, 0x40, 0x08, 0x41, 0xdd, 0x01, 0x41, 0xdb, 0x2c, 0x24,
        0x41, 0xd9, 0x55, 0x10, 0x41, 0xdd, 0x16, 0x41, 0xd9, 0x1f,
        0xdd, 0x1b, 0xdb, 0x39, 0xdf, 0x02, 0xdb, 0x06, 0xdf, 0x2f,
        0x41, 0xdf, 0x10, 0xdb, 0x55, 0x00, 0xdf, 0x1c, 0x24,
        0x41, 0xdb, 0x1a, 0x41, 0xdf, 0x3b,
        0xd8, 0x00, 0xdc, 0x09, 0xd8, 0x22, 0xdc, 0x2b, 0xd8, 0x36, 0xdc, 0x3f,
        0xd9, 0xf0, 0xd9, 0xe1, 0xd9, 0xe0, 0xd9, 0xe8, 0xd9, 0xee,
        0xd9, 0xeb, 0xd9, 0xea, 0xd9, 0xe9, 0xd9, 0xec, 0xd9, 0xed,
        0xd9, 0xfa, 0xd9, 0xfe, 0xd9, 0xff, 0xd9, 0xfb, 0xd9, 0xf2,
        0xd9, 0xf3, 0xd9, 0xf1, 0xd9, 0xf9, 0xd9, 0xfc, 0xd9, 0xfd,
        0xd9, 0xf8, 0xd9, 0xf5, 0xd9, 0xf4, 0xd9, 0xe4, 0xd9, 0xe5,
        0xd9, 0xd0, 0x9b, 0xdb, 0xe3, 0xdb, 0xe3, 0x9b, 0xdb, 0xe2, 0xdb, 0xe2, 0x9b,
    };
    String8 x86_intel_x87_source =
        S8("fld st(3)\n"
           "fst st(4)\n"
           "fstp st(5)\n"
           "fxch st(6)\n"
           "fadd st(0), st(2)\n"
           "fmul st(3), st(0)\n"
           "fsub st(0), st(4)\n"
           "fsubr st(5), st(0)\n"
           "fdiv st(0), st(6)\n"
           "fdivr st(7), st(0)\n"
           "faddp st(1), st(0)\n"
           "fmulp st(2), st(0)\n"
           "fsubp st(3), st(0)\n"
           "fsubrp st(4), st(0)\n"
           "fdivp st(5), st(0)\n"
           "fdivrp st(6), st(0)\n"
           "fld dword ptr [rax + 8]\n"
           "fld qword ptr [r9]\n"
           "fld tbyte ptr [r12]\n"
           "fst dword ptr [r13 + 16]\n"
           "fst qword ptr [r14]\n"
           "fstp dword ptr [r15]\n"
           "fstp qword ptr [rbx]\n"
           "fstp tbyte ptr [rcx]\n"
           "fild word ptr [rdx]\n"
           "fild dword ptr [rsi]\n"
           "fild qword ptr [rdi]\n"
           "fist word ptr [r8]\n"
           "fist dword ptr [rbp]\n"
           "fistp word ptr [rsp]\n"
           "fistp dword ptr [r10]\n"
           "fistp qword ptr [r11]\n"
           "fadd dword ptr [rax]\n"
           "fmul qword ptr [rcx]\n"
           "fsub dword ptr [rdx]\n"
           "fsubr qword ptr [rbx]\n"
           "fdiv dword ptr [rsi]\n"
           "fdivr qword ptr [rdi]\n"
           "f2xm1\n"
           "fabs\n"
           "fchs\n"
           "fld1\n"
           "fldz\n"
           "fldpi\n"
           "fldl2e\n"
           "fldl2t\n"
           "fldlg2\n"
           "fldln2\n"
           "fsqrt\n"
           "fsin\n"
           "fcos\n"
           "fsincos\n"
           "fptan\n"
           "fpatan\n"
           "fyl2x\n"
           "fyl2xp1\n"
           "frndint\n"
           "fscale\n"
           "fprem\n"
           "fprem1\n"
           "fxtract\n"
           "ftst\n"
           "fxam\n"
           "fnop\n"
           "finit\n"
           "fninit\n"
           "fclex\n"
           "fnclex\n"
           "fwait\n");
    AssemblyEncodeResult x86_intel_x87 = assembly_encode(
        arguments->arena, x86_intel_x87_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87.diagnostic_count == 0 && x86_intel_x87.bytes.length == sizeof(expected_x86_x87) &&
                               memcmp(x86_intel_x87.bytes.pointer, expected_x86_x87, sizeof(expected_x86_x87)) == 0);
    String8 x86_att_x87_source =
        S8("fld %st(3)\n"
           "fst %st(4)\n"
           "fstp %st(5)\n"
           "fxch %st(6)\n"
           "fadd %st(2), %st\n"
           "fmul %st, %st(3)\n"
           "fsub %st(4), %st\n"
           "fsub %st, %st(5)\n"
           "fdiv %st(6), %st\n"
           "fdiv %st, %st(7)\n"
           "faddp %st, %st(1)\n"
           "fmulp %st, %st(2)\n"
           "fsubrp %st, %st(3)\n"
           "fsubp %st, %st(4)\n"
           "fdivrp %st, %st(5)\n"
           "fdivp %st, %st(6)\n"
           "flds 8(%rax)\n"
           "fldl (%r9)\n"
           "fldt (%r12)\n"
           "fsts 16(%r13)\n"
           "fstl (%r14)\n"
           "fstps (%r15)\n"
           "fstpl (%rbx)\n"
           "fstpt (%rcx)\n"
           "filds (%rdx)\n"
           "fildl (%rsi)\n"
           "fildq (%rdi)\n"
           "fists (%r8)\n"
           "fistl (%rbp)\n"
           "fistps (%rsp)\n"
           "fistpl (%r10)\n"
           "fistpq (%r11)\n"
           "fadds (%rax)\n"
           "fmull (%rcx)\n"
           "fsubs (%rdx)\n"
           "fsubrl (%rbx)\n"
           "fdivs (%rsi)\n"
           "fdivrl (%rdi)\n"
           "f2xm1\n"
           "fabs\n"
           "fchs\n"
           "fld1\n"
           "fldz\n"
           "fldpi\n"
           "fldl2e\n"
           "fldl2t\n"
           "fldlg2\n"
           "fldln2\n"
           "fsqrt\n"
           "fsin\n"
           "fcos\n"
           "fsincos\n"
           "fptan\n"
           "fpatan\n"
           "fyl2x\n"
           "fyl2xp1\n"
           "frndint\n"
           "fscale\n"
           "fprem\n"
           "fprem1\n"
           "fxtract\n"
           "ftst\n"
           "fxam\n"
           "fnop\n"
           "finit\n"
           "fninit\n"
           "fclex\n"
           "fnclex\n"
           "fwait\n");
    AssemblyEncodeResult x86_att_x87 = assembly_encode(
        arguments->arena, x86_att_x87_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87.diagnostic_count == 0 && x86_att_x87.bytes.length == sizeof(expected_x86_x87) &&
                               memcmp(x86_att_x87.bytes.pointer, expected_x86_x87, sizeof(expected_x86_x87)) == 0);
    AssemblyEncodeResult x86_x87_relocation = assembly_encode(
        arguments->arena, S8("fld qword ptr [rip + external_x87]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_x87_relocation[] = {0xdd, 0x05, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_x87_relocation.diagnostic_count == 0 &&
                               x86_x87_relocation.bytes.length == sizeof(expected_x86_x87_relocation) &&
                               memcmp(x86_x87_relocation.bytes.pointer, expected_x86_x87_relocation,
                                      sizeof(expected_x86_x87_relocation)) == 0);
    BUSTER_TEST(arguments, x86_x87_relocation.relocation_count == 1 && x86_x87_relocation.relocations[0].offset == 2 &&
                               x86_x87_relocation.relocations[0].addend == -4 &&
                               x86_x87_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_x87_relocation.symbols[x86_x87_relocation.relocations[0].symbol].name,
                                            S8("external_x87")));
    u8 expected_x86_x87_state[] = {
        0xd9, 0x28, 0xd9, 0x39, 0x9b, 0xd9, 0x3a, 0xd9, 0x23, 0xd9, 0x34, 0x24, 0x9b, 0xd9, 0x75, 0x00,
        0xdd, 0x26, 0xdd, 0x37, 0x9b, 0x41, 0xdd, 0x30, 0xdf, 0xe0, 0x9b, 0xdf, 0xe0, 0x41, 0xdd, 0x39,
        0x9b, 0x41, 0xdd, 0x3a, 0x41, 0xdf, 0x23, 0x41, 0xdf, 0x34, 0x24, 0xdd, 0xc3, 0xdf, 0xc4,
        0xd9, 0xf7, 0xd9, 0xf6,
    };
    String8 x86_intel_x87_state_source =
        S8("fldcw word ptr [rax]\n"
           "fnstcw word ptr [rcx]\n"
           "fstcw word ptr [rdx]\n"
           "fldenv [rbx]\n"
           "fnstenv [rsp]\n"
           "fstenv [rbp]\n"
           "frstor [rsi]\n"
           "fnsave [rdi]\n"
           "fsave [r8]\n"
           "fnstsw ax\n"
           "fstsw ax\n"
           "fnstsw word ptr [r9]\n"
           "fstsw word ptr [r10]\n"
           "fbld tbyte ptr [r11]\n"
           "fbstp tbyte ptr [r12]\n"
           "ffree st(3)\n"
           "ffreep st(4)\n"
           "fincstp\n"
           "fdecstp\n");
    AssemblyEncodeResult x86_intel_x87_state = assembly_encode(
        arguments->arena, x86_intel_x87_state_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_state.diagnostic_count == 0 &&
                               x86_intel_x87_state.bytes.length == sizeof(expected_x86_x87_state) &&
                               memcmp(x86_intel_x87_state.bytes.pointer, expected_x86_x87_state, sizeof(expected_x86_x87_state)) == 0);
    String8 x86_att_x87_state_source =
        S8("fldcw (%rax)\n"
           "fnstcw (%rcx)\n"
           "fstcw (%rdx)\n"
           "fldenv (%rbx)\n"
           "fnstenv (%rsp)\n"
           "fstenv (%rbp)\n"
           "frstor (%rsi)\n"
           "fnsave (%rdi)\n"
           "fsave (%r8)\n"
           "fnstsw %ax\n"
           "fstsw %ax\n"
           "fnstsw (%r9)\n"
           "fstsw (%r10)\n"
           "fbld (%r11)\n"
           "fbstp (%r12)\n"
           "ffree %st(3)\n"
           "ffreep %st(4)\n"
           "fincstp\n"
           "fdecstp\n");
    AssemblyEncodeResult x86_att_x87_state = assembly_encode(
        arguments->arena, x86_att_x87_state_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_state.diagnostic_count == 0 &&
                               x86_att_x87_state.bytes.length == sizeof(expected_x86_x87_state) &&
                               memcmp(x86_att_x87_state.bytes.pointer, expected_x86_x87_state, sizeof(expected_x86_x87_state)) == 0);
    AssemblyEncodeResult x86_x87_state_relocation = assembly_encode(
        arguments->arena, S8("fldcw word ptr [rip + external_state]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_x87_state_relocation[] = {0xd9, 0x2d, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_x87_state_relocation.diagnostic_count == 0 &&
                               x86_x87_state_relocation.bytes.length == sizeof(expected_x86_x87_state_relocation) &&
                               memcmp(x86_x87_state_relocation.bytes.pointer, expected_x86_x87_state_relocation,
                                      sizeof(expected_x86_x87_state_relocation)) == 0);
    BUSTER_TEST(arguments, x86_x87_state_relocation.relocation_count == 1 &&
                               x86_x87_state_relocation.relocations[0].offset == 2 &&
                               x86_x87_state_relocation.relocations[0].addend == -4 &&
                               x86_x87_state_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_x87_state_relocation.symbols[x86_x87_state_relocation.relocations[0].symbol].name,
                                            S8("external_state")));
    String8 x86_x87_alias_source_prefix =
        S8("fxch\n"
           "fcom\n"
           "fcomp\n"
           "fucom\n"
           "fucomp\n"
           "fadd\n"
           "fmul\n"
           "fsub\n"
           "fsubr\n"
           "fdiv\n"
           "fdivr\n"
           "faddp\n"
           "fmulp\n"
           "fsubp\n"
           "fsubrp\n"
           "fdivp\n"
           "fdivrp\n");
    String8 x86_intel_x87_alias_source = string_format(
        arguments->arena,
        S8("{S8}"
           "fadd st(2)\n"
           "fmul st(3)\n"
           "fsub st(4)\n"
           "fsubr st(5)\n"
           "fdiv st(6)\n"
           "fdivr st(7)\n"
           "faddp st(2)\n"
           "fmulp st(3)\n"
           "fsubp st(4)\n"
           "fsubrp st(5)\n"
           "fdivp st(6)\n"
           "fdivrp st(7)\n"),
        x86_x87_alias_source_prefix);
    u8 expected_x86_intel_x87_alias[] = {
        0xd9, 0xc9, 0xd8, 0xd1, 0xd8, 0xd9, 0xdd, 0xe1, 0xdd, 0xe9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe9, 0xde, 0xe1, 0xde, 0xf9, 0xde, 0xf1,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe9, 0xde, 0xe1, 0xde, 0xf9, 0xde, 0xf1,
        0xd8, 0xc2, 0xd8, 0xcb, 0xd8, 0xe4, 0xd8, 0xed, 0xd8, 0xf6, 0xd8, 0xff,
        0xde, 0xc2, 0xde, 0xcb, 0xde, 0xec, 0xde, 0xe5, 0xde, 0xfe, 0xde, 0xf7,
    };
    AssemblyEncodeResult x86_intel_x87_alias = assembly_encode(
        arguments->arena, x86_intel_x87_alias_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_alias.diagnostic_count == 0 &&
                               x86_intel_x87_alias.bytes.length == sizeof(expected_x86_intel_x87_alias) &&
                               memcmp(x86_intel_x87_alias.bytes.pointer, expected_x86_intel_x87_alias,
                                      sizeof(expected_x86_intel_x87_alias)) == 0);
    String8 x86_att_x87_alias_source = string_format(
        arguments->arena,
        S8("{S8}"
           "fadd %st(2)\n"
           "fmul %st(3)\n"
           "fsub %st(4)\n"
           "fsubr %st(5)\n"
           "fdiv %st(6)\n"
           "fdivr %st(7)\n"
           "faddp %st(2)\n"
           "fmulp %st(3)\n"
           "fsubp %st(4)\n"
           "fsubrp %st(5)\n"
           "fdivp %st(6)\n"
           "fdivrp %st(7)\n"),
        x86_x87_alias_source_prefix);
    u8 expected_x86_att_x87_alias[] = {
        0xd9, 0xc9, 0xd8, 0xd1, 0xd8, 0xd9, 0xdd, 0xe1, 0xdd, 0xe9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe1, 0xde, 0xe9, 0xde, 0xf1, 0xde, 0xf9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe1, 0xde, 0xe9, 0xde, 0xf1, 0xde, 0xf9,
        0xd8, 0xc2, 0xd8, 0xcb, 0xd8, 0xe4, 0xd8, 0xed, 0xd8, 0xf6, 0xd8, 0xff,
        0xde, 0xc2, 0xde, 0xcb, 0xde, 0xe4, 0xde, 0xed, 0xde, 0xf6, 0xde, 0xff,
    };
    AssemblyEncodeResult x86_att_x87_alias = assembly_encode(
        arguments->arena, x86_att_x87_alias_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_alias.diagnostic_count == 0 &&
                               x86_att_x87_alias.bytes.length == sizeof(expected_x86_att_x87_alias) &&
                               memcmp(x86_att_x87_alias.bytes.pointer, expected_x86_att_x87_alias,
                                      sizeof(expected_x86_att_x87_alias)) == 0);
    Target x86_sse3_target = x86_target;
    x86_sse3_target.cpu_features_explicit = true;
    x86_sse3_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_SSE3}, 2);
    u8 expected_x86_x87_compare_integer[] = {
        0xd8, 0xd3, 0xd8, 0xdc, 0xd8, 0x10, 0x41, 0xdc, 0x19,
        0xde, 0xd9, 0xdd, 0xe5, 0xdd, 0xee, 0xda, 0xe9,
        0xdb, 0xf1, 0xdf, 0xf2, 0xdb, 0xeb, 0xdf, 0xec,
        0xda, 0xc1, 0xda, 0xca, 0xda, 0xd3, 0xda, 0xdc,
        0xdb, 0xc5, 0xdb, 0xce, 0xdb, 0xd7, 0xdb, 0xd9,
        0xde, 0x00, 0xda, 0x01, 0xde, 0x0a, 0xda, 0x0b,
        0xde, 0x24, 0x24, 0xda, 0x65, 0x00, 0xde, 0x2e, 0xda, 0x2f,
        0x41, 0xde, 0x30, 0x41, 0xda, 0x31, 0x41, 0xde, 0x3a, 0x41, 0xda, 0x3b,
        0x41, 0xdf, 0x0c, 0x24, 0x41, 0xdb, 0x4d, 0x00, 0x41, 0xdd, 0x0e,
    };
    String8 x86_intel_x87_compare_integer_source =
        S8("fcom st(3)\n"
           "fcomp st(4)\n"
           "fcom dword ptr [rax]\n"
           "fcomp qword ptr [r9]\n"
           "fcompp\n"
           "fucom st(5)\n"
           "fucomp st(6)\n"
           "fucompp\n"
           "fcomi st(0), st(1)\n"
           "fcomip st(0), st(2)\n"
           "fucomi st(0), st(3)\n"
           "fucomip st(0), st(4)\n"
           "fcmovb st(0), st(1)\n"
           "fcmove st(0), st(2)\n"
           "fcmovbe st(0), st(3)\n"
           "fcmovu st(0), st(4)\n"
           "fcmovnb st(0), st(5)\n"
           "fcmovne st(0), st(6)\n"
           "fcmovnbe st(0), st(7)\n"
           "fcmovnu st(0), st(1)\n"
           "fiadd word ptr [rax]\n"
           "fiadd dword ptr [rcx]\n"
           "fimul word ptr [rdx]\n"
           "fimul dword ptr [rbx]\n"
           "fisub word ptr [rsp]\n"
           "fisub dword ptr [rbp]\n"
           "fisubr word ptr [rsi]\n"
           "fisubr dword ptr [rdi]\n"
           "fidiv word ptr [r8]\n"
           "fidiv dword ptr [r9]\n"
           "fidivr word ptr [r10]\n"
           "fidivr dword ptr [r11]\n"
           "fisttp word ptr [r12]\n"
           "fisttp dword ptr [r13]\n"
           "fisttp qword ptr [r14]\n");
    AssemblyEncodeResult x86_intel_x87_compare_integer = assembly_encode(
        arguments->arena, x86_intel_x87_compare_integer_source,
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_compare_integer.diagnostic_count == 0 &&
                               x86_intel_x87_compare_integer.bytes.length == sizeof(expected_x86_x87_compare_integer) &&
                               memcmp(x86_intel_x87_compare_integer.bytes.pointer, expected_x86_x87_compare_integer,
                                      sizeof(expected_x86_x87_compare_integer)) == 0);
    u8 expected_x86_address32_fisttp[] = {0x67, 0xdb, 0x08};
    AssemblyEncodeResult x86_address32_fisttp = assembly_encode(
        arguments->arena, S8("fisttp dword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_fisttp.diagnostic_count == 0 &&
                               x86_address32_fisttp.bytes.length == sizeof(expected_x86_address32_fisttp) &&
                               memcmp(x86_address32_fisttp.bytes.pointer, expected_x86_address32_fisttp,
                                      sizeof(expected_x86_address32_fisttp)) == 0);
    String8 x86_att_x87_compare_integer_source =
        S8("fcom %st(3)\n"
           "fcomp %st(4)\n"
           "fcoms (%rax)\n"
           "fcompl (%r9)\n"
           "fcompp\n"
           "fucom %st(5)\n"
           "fucomp %st(6)\n"
           "fucompp\n"
           "fcomi %st(1), %st\n"
           "fcomip %st(2), %st\n"
           "fucomi %st(3), %st\n"
           "fucomip %st(4), %st\n"
           "fcmovb %st(1), %st\n"
           "fcmove %st(2), %st\n"
           "fcmovbe %st(3), %st\n"
           "fcmovu %st(4), %st\n"
           "fcmovnb %st(5), %st\n"
           "fcmovne %st(6), %st\n"
           "fcmovnbe %st(7), %st\n"
           "fcmovnu %st(1), %st\n"
           "fiadds (%rax)\n"
           "fiaddl (%rcx)\n"
           "fimuls (%rdx)\n"
           "fimull (%rbx)\n"
           "fisubs (%rsp)\n"
           "fisubl (%rbp)\n"
           "fisubrs (%rsi)\n"
           "fisubrl (%rdi)\n"
           "fidivs (%r8)\n"
           "fidivl (%r9)\n"
           "fidivrs (%r10)\n"
           "fidivrl (%r11)\n"
           "fisttps (%r12)\n"
           "fisttpl (%r13)\n"
           "fisttpq (%r14)\n");
    AssemblyEncodeResult x86_att_x87_compare_integer = assembly_encode(
        arguments->arena, x86_att_x87_compare_integer_source,
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_compare_integer.diagnostic_count == 0 &&
                               x86_att_x87_compare_integer.bytes.length == sizeof(expected_x86_x87_compare_integer) &&
                               memcmp(x86_att_x87_compare_integer.bytes.pointer, expected_x86_x87_compare_integer,
                                      sizeof(expected_x86_x87_compare_integer)) == 0);
    AssemblyEncodeResult unsupported_x86_fisttp = assembly_encode(
        arguments->arena, S8("fisttp word ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_x86_fisttp.diagnostic_count == 1 &&
                               unsupported_x86_fisttp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult invalid_x86_x87_compare_integer = assembly_encode(
        arguments->arena,
        S8("fcom [rax]\n"
           "fcomp tbyte ptr [rax]\n"
           "fucom qword ptr [rax]\n"
           "fcomi st(1), st(0)\n"
           "fcomi st(0), rax\n"
           "fcmovb st(1), st(0)\n"
           "fiadd qword ptr [rax]\n"
           "fiadd st(0)\n"
           "fisttp tbyte ptr [rax]\n"
           "fisttp st(0)\n"),
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87_compare_integer.diagnostic_count == 10);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87_compare_integer.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_x86_x87_compare_integer.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_x86_x87 = assembly_encode(
        arguments->arena,
        S8("fld [rax]\n"
           "fstp word ptr [rax]\n"
           "fild tbyte ptr [rax]\n"
           "fistp tbyte ptr [rax]\n"
           "fadd st(2), st(3)\n"
           "fadd qword ptr [rax], st(0)\n"
           "faddp st(2), st(1)\n"
           "fxch rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_x87.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_x86_x87_state = assembly_encode(
        arguments->arena,
        S8("fldcw dword ptr [rax]\n"
           "fldenv qword ptr [rax]\n"
           "fnstsw rax\n"
           "fstsw bx\n"
           "fstcw ax\n"
           "fbld qword ptr [rax]\n"
           "fbstp st(0)\n"
           "ffree rax\n"
           "fincstp st(0)\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87_state.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87_state.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_x87_state.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult x86_xmm_packed = assembly_encode(
        arguments->arena, S8("paddd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_xmm_packed[] = {0x66, 0x0f, 0xfe, 0xc1};
    BUSTER_TEST(arguments, x86_xmm_packed.diagnostic_count == 0 && x86_xmm_packed.bytes.length == sizeof(expected_x86_xmm_packed) &&
                               memcmp(x86_xmm_packed.bytes.pointer, expected_x86_xmm_packed, sizeof(expected_x86_xmm_packed)) == 0);
    AssemblyEncodeResult unsupported_xmm_packed = assembly_encode(
        arguments->arena, S8("paddd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_without_sse2, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_xmm_packed.diagnostic_count == 1 &&
                               unsupported_xmm_packed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    u8 expected_x86_conditions[] = {
        0x0f, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x85, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x95, 0xc0,
        0x41, 0x0f, 0x92, 0xc1,
        0x41, 0x0f, 0x9f, 0x45, 0x08,
        0x48, 0x0f, 0x44, 0xc3,
        0x47, 0x0f, 0x42, 0x14, 0x8c,
        0x66, 0x45, 0x0f, 0x49, 0xdc,
    };
    String8 x86_intel_condition_source =
        S8("je external\n"
           "jnz external2\n"
           "setne al\n"
           "setb r9b\n"
           "setg byte ptr [r13 + 8]\n"
           "cmove rax, rbx\n"
           "cmovb r10d, [r12 + r9*4]\n"
           "cmovns r11w, r12w\n");
    AssemblyEncodeResult x86_intel_conditions = assembly_encode(
        arguments->arena, x86_intel_condition_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_conditions.diagnostic_count == 0 &&
                               x86_intel_conditions.bytes.length == sizeof(expected_x86_conditions) &&
                               memcmp(x86_intel_conditions.bytes.pointer, expected_x86_conditions, sizeof(expected_x86_conditions)) == 0);
    BUSTER_TEST(arguments, x86_intel_conditions.relocation_count == 2 && x86_intel_conditions.relocations[0].offset == 2 &&
                               x86_intel_conditions.relocations[1].offset == 8 && x86_intel_conditions.relocations[0].addend == -4 &&
                               x86_intel_conditions.relocations[1].addend == -4 &&
                               x86_intel_conditions.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               x86_intel_conditions.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_condition_source =
        S8("je external\n"
           "jnz external2\n"
           "setne %al\n"
           "setb %r9b\n"
           "setg 8(%r13)\n"
           "cmoveq %rbx, %rax\n"
           "cmovbl (%r12,%r9,4), %r10d\n"
           "cmovnsw %r12w, %r11w\n");
    AssemblyEncodeResult x86_att_conditions = assembly_encode(
        arguments->arena, x86_att_condition_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_conditions.diagnostic_count == 0 && x86_att_conditions.bytes.length == sizeof(expected_x86_conditions) &&
                               memcmp(x86_att_conditions.bytes.pointer, expected_x86_conditions, sizeof(expected_x86_conditions)) == 0);

    // CS/DS are the architectural not-taken/taken branch-hint prefixes.  A
    // literal targets exercise short-displacement sizing (including the
    // prefix byte) and force a 32-bit displacement for a distant target in
    // both source syntaxes.
    AssemblyEncodeResult x86_intel_branch_hints = assembly_encode(
        arguments->arena, S8("cs jz 0\n"
                             "ds jnz 0\n"
                             "cs jz 1000\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_intel_branch_hints[] = {
        0x2e, 0x74, 0xfd,
        0x3e, 0x75, 0xfa,
        0x2e, 0x0f, 0x84, 0xdb, 0x03, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_intel_branch_hints.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_intel_branch_hints.bytes, expected_x86_intel_branch_hints,
                                                         sizeof(expected_x86_intel_branch_hints)));
    AssemblyEncodeResult x86_att_branch_hints = assembly_encode(
        arguments->arena, S8("cs jz $0\n"
                             "ds jnz $0\n"
                             "cs jz $1000\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_branch_hints.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_branch_hints.bytes, expected_x86_intel_branch_hints,
                                                         sizeof(expected_x86_intel_branch_hints)));
    AssemblyEncodeResult x86_invalid_branch_hints = assembly_encode(
        arguments->arena, S8("cs mov rax, rbx\n"
                             "cs ds jz 0\n"
                             "cs cs jz 0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_invalid_branch_hints.diagnostic_count == 3);
    Target x86_avx_target = x86_target;
    x86_avx_target.cpu_features_explicit = true;
    x86_avx_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX}, 2);
    u8 expected_x86_avx[] = {
        0xc5, 0xfc, 0x28, 0xc1,
        0xc4, 0x01, 0x7c, 0x10, 0x44, 0x4c, 0x20,
        0xc4, 0x41, 0x7d, 0x29, 0x7d, 0x00,
        0xc5, 0xf9, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00,
        0xc4, 0x41, 0x24, 0x57, 0xd4,
        0xc5, 0xe9, 0x57, 0xcb,
        0xc5, 0xdc, 0x58, 0xdd,
        0xc5, 0xc5, 0x58, 0x75, 0x00,
        0xc4, 0xc1, 0x3a, 0x58, 0xf9,
        0xc4, 0x41, 0x2b, 0x58, 0xcb,
        0xc4, 0x41, 0x1c, 0x5c, 0xdd,
        0xc4, 0x41, 0x09, 0x5c, 0xef,
        0xc5, 0x7c, 0x59, 0xf9,
        0xc5, 0xe9, 0x59, 0xcb,
        0xc5, 0xdc, 0x5e, 0xdd,
        0xc5, 0xc9, 0x5e, 0xef,
    };
    String8 x86_intel_avx_source =
        S8("vmovaps ymm0, ymm1\n"
           "vmovups ymm8, [r12 + r9*2 + 32]\n"
           "vmovapd [r13], ymm15\n"
           "vmovupd xmm2, [rip + external]\n"
           "vxorps ymm10, ymm11, ymm12\n"
           "vxorpd xmm1, xmm2, xmm3\n"
           "vaddps ymm3, ymm4, ymm5\n"
           "vaddpd ymm6, ymm7, [rbp]\n"
           "vaddss xmm7, xmm8, xmm9\n"
           "vaddsd xmm9, xmm10, xmm11\n"
           "vsubps ymm11, ymm12, ymm13\n"
           "vsubpd xmm13, xmm14, xmm15\n"
           "vmulps ymm15, ymm0, ymm1\n"
           "vmulpd xmm1, xmm2, xmm3\n"
           "vdivps ymm3, ymm4, ymm5\n"
           "vdivpd xmm5, xmm6, xmm7\n");
    AssemblyEncodeResult x86_intel_avx = assembly_encode(
        arguments->arena, x86_intel_avx_source, (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_avx.diagnostic_count == 0 && x86_intel_avx.bytes.length == sizeof(expected_x86_avx) &&
                               memcmp(x86_intel_avx.bytes.pointer, expected_x86_avx, sizeof(expected_x86_avx)) == 0);
    BUSTER_TEST(arguments, x86_intel_avx.relocation_count == 1 && x86_intel_avx.relocations[0].offset == 21 &&
                               x86_intel_avx.relocations[0].addend == -4 &&
                               x86_intel_avx.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_avx_source =
        S8("vmovaps %ymm1, %ymm0\n"
           "vmovups 32(%r12,%r9,2), %ymm8\n"
           "vmovapd %ymm15, (%r13)\n"
           "vmovupd external(%rip), %xmm2\n"
           "vxorps %ymm12, %ymm11, %ymm10\n"
           "vxorpd %xmm3, %xmm2, %xmm1\n"
           "vaddps %ymm5, %ymm4, %ymm3\n"
           "vaddpd (%rbp), %ymm7, %ymm6\n"
           "vaddss %xmm9, %xmm8, %xmm7\n"
           "vaddsd %xmm11, %xmm10, %xmm9\n"
           "vsubps %ymm13, %ymm12, %ymm11\n"
           "vsubpd %xmm15, %xmm14, %xmm13\n"
           "vmulps %ymm1, %ymm0, %ymm15\n"
           "vmulpd %xmm3, %xmm2, %xmm1\n"
           "vdivps %ymm5, %ymm4, %ymm3\n"
           "vdivpd %xmm7, %xmm6, %xmm5\n");
    AssemblyEncodeResult x86_att_avx = assembly_encode(
        arguments->arena, x86_att_avx_source, (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_avx.diagnostic_count == 0 && x86_att_avx.bytes.length == sizeof(expected_x86_avx) &&
                               memcmp(x86_att_avx.bytes.pointer, expected_x86_avx, sizeof(expected_x86_avx)) == 0);
    AssemblyEncodeResult unsupported_avx = assembly_encode(
        arguments->arena, S8("vaddps ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_avx.diagnostic_count == 1 &&
                               unsupported_avx.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target x86_avx2_target = x86_avx_target;
    x86_avx2_target.cpu_features = target_cpu_features_add(x86_avx2_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
    u8 expected_x86_avx2[] = {
        0xc5, 0xfd, 0x6f, 0xc1,
        0xc4, 0x01, 0x7e, 0x7f, 0x44, 0x4c, 0x20,
        0xc5, 0xe5, 0xfc, 0xd4,
        0xc5, 0xc9, 0xfd, 0xef,
        0xc4, 0x41, 0x35, 0xfe, 0x45, 0x00,
        0xc4, 0x41, 0x25, 0xd4, 0xd4,
        0xc4, 0x41, 0x0d, 0xf8, 0xef,
        0xc5, 0xf1, 0xf9, 0xc2,
        0xc5, 0xdd, 0xfa, 0xdd,
        0xc4, 0xc1, 0x41, 0xfb, 0xf0,
        0xc4, 0x41, 0x2d, 0xdb, 0xcb,
        0xc4, 0x41, 0x11, 0xeb, 0xe6,
        0xc5, 0x7d, 0xef, 0xf9,
        0xc5, 0xe1, 0x74, 0xd4,
        0xc5, 0xcd, 0x75, 0xef,
        0xc4, 0x41, 0x31, 0x76, 0xc2,
        0xc4, 0x42, 0x1d, 0x29, 0xdd,
        0xc5, 0x01, 0x64, 0xf0,
        0xc5, 0xed, 0x65, 0xcb,
        0xc5, 0xd1, 0x66, 0xe6,
        0xc4, 0xc2, 0x3d, 0x37, 0xf9,
        0xc4, 0x41, 0x21, 0xd5, 0xd4,
        0xc4, 0x42, 0x0d, 0x40, 0xef,
    };
    String8 x86_intel_avx2_source =
        S8("vmovdqa ymm0, ymm1\n"
           "vmovdqu [r12 + r9*2 + 32], ymm8\n"
           "vpaddb ymm2, ymm3, ymm4\n"
           "vpaddw xmm5, xmm6, xmm7\n"
           "vpaddd ymm8, ymm9, [r13]\n"
           "vpaddq ymm10, ymm11, ymm12\n"
           "vpsubb ymm13, ymm14, ymm15\n"
           "vpsubw xmm0, xmm1, xmm2\n"
           "vpsubd ymm3, ymm4, ymm5\n"
           "vpsubq xmm6, xmm7, xmm8\n"
           "vpand ymm9, ymm10, ymm11\n"
           "vpor xmm12, xmm13, xmm14\n"
           "vpxor ymm15, ymm0, ymm1\n"
           "vpcmpeqb xmm2, xmm3, xmm4\n"
           "vpcmpeqw ymm5, ymm6, ymm7\n"
           "vpcmpeqd xmm8, xmm9, xmm10\n"
           "vpcmpeqq ymm11, ymm12, ymm13\n"
           "vpcmpgtb xmm14, xmm15, xmm0\n"
           "vpcmpgtw ymm1, ymm2, ymm3\n"
           "vpcmpgtd xmm4, xmm5, xmm6\n"
           "vpcmpgtq ymm7, ymm8, ymm9\n"
           "vpmullw xmm10, xmm11, xmm12\n"
           "vpmulld ymm13, ymm14, ymm15\n");
    AssemblyEncodeResult x86_intel_avx2 = assembly_encode(
        arguments->arena, x86_intel_avx2_source, (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_avx2.diagnostic_count == 0 && x86_intel_avx2.bytes.length == sizeof(expected_x86_avx2) &&
                               memcmp(x86_intel_avx2.bytes.pointer, expected_x86_avx2, sizeof(expected_x86_avx2)) == 0);
    String8 x86_att_avx2_source =
        S8("vmovdqa %ymm1, %ymm0\n"
           "vmovdqu %ymm8, 32(%r12,%r9,2)\n"
           "vpaddb %ymm4, %ymm3, %ymm2\n"
           "vpaddw %xmm7, %xmm6, %xmm5\n"
           "vpaddd (%r13), %ymm9, %ymm8\n"
           "vpaddq %ymm12, %ymm11, %ymm10\n"
           "vpsubb %ymm15, %ymm14, %ymm13\n"
           "vpsubw %xmm2, %xmm1, %xmm0\n"
           "vpsubd %ymm5, %ymm4, %ymm3\n"
           "vpsubq %xmm8, %xmm7, %xmm6\n"
           "vpand %ymm11, %ymm10, %ymm9\n"
           "vpor %xmm14, %xmm13, %xmm12\n"
           "vpxor %ymm1, %ymm0, %ymm15\n"
           "vpcmpeqb %xmm4, %xmm3, %xmm2\n"
           "vpcmpeqw %ymm7, %ymm6, %ymm5\n"
           "vpcmpeqd %xmm10, %xmm9, %xmm8\n"
           "vpcmpeqq %ymm13, %ymm12, %ymm11\n"
           "vpcmpgtb %xmm0, %xmm15, %xmm14\n"
           "vpcmpgtw %ymm3, %ymm2, %ymm1\n"
           "vpcmpgtd %xmm6, %xmm5, %xmm4\n"
           "vpcmpgtq %ymm9, %ymm8, %ymm7\n"
           "vpmullw %xmm12, %xmm11, %xmm10\n"
           "vpmulld %ymm15, %ymm14, %ymm13\n");
    AssemblyEncodeResult x86_att_avx2 = assembly_encode(
        arguments->arena, x86_att_avx2_source, (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_avx2.diagnostic_count == 0 && x86_att_avx2.bytes.length == sizeof(expected_x86_avx2) &&
                               memcmp(x86_att_avx2.bytes.pointer, expected_x86_avx2, sizeof(expected_x86_avx2)) == 0);
    AssemblyEncodeResult x86_avx_integer_128 = assembly_encode(
        arguments->arena, S8("vpaddd xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_avx_integer_128[] = {0xc5, 0xf1, 0xfe, 0xc2};
    BUSTER_TEST(arguments, x86_avx_integer_128.diagnostic_count == 0 &&
                               x86_avx_integer_128.bytes.length == sizeof(expected_x86_avx_integer_128) &&
                               memcmp(x86_avx_integer_128.bytes.pointer, expected_x86_avx_integer_128,
                                      sizeof(expected_x86_avx_integer_128)) == 0);
    AssemblyEncodeResult unsupported_avx2 = assembly_encode(
        arguments->arena, S8("vpaddd ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_avx2.diagnostic_count == 1 &&
                               unsupported_avx2.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

    // LLVM byte oracles for the VV1 width-control rows.  The 32/64-bit
    // conversion pairs exercise NOREXW/REXW, while the extract pairs cover
    // the same metadata rule in a different opcode family.
    String8 x86_vv1_width_intel_source =
        S8("vcvtsd2si rax, qword ptr [rbx]\n"
           "vcvtsd2si eax, qword ptr [rbx]\n"
           "vcvtsi2sd xmm0, xmm1, rax\n"
           "vcvtsi2sd xmm0, xmm1, eax\n"
           "vpextrq qword ptr [rbx], xmm0, 1\n"
           "vpextrd dword ptr [rbx], xmm0, 1\n");
    u8 expected_x86_vv1_width[] = {
        0xc4, 0xe1, 0xfb, 0x2d, 0x03,
        0xc5, 0xfb, 0x2d, 0x03,
        0xc4, 0xe1, 0xf3, 0x2a, 0xc0,
        0xc5, 0xf3, 0x2a, 0xc0,
        0xc4, 0xe3, 0xf9, 0x16, 0x03, 0x01,
        0xc4, 0xe3, 0x79, 0x16, 0x03, 0x01,
    };
    AssemblyEncodeResult x86_vv1_width_intel = assembly_encode(
        arguments->arena, x86_vv1_width_intel_source,
        (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_vv1_width_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_vv1_width_intel.bytes, expected_x86_vv1_width,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_vv1_width)));
    Target x86_bit_atomic_target = x86_target;
    x86_bit_atomic_target.cpu_model = CPU_MODEL_BASELINE;
    x86_bit_atomic_target.cpu_features_explicit = true;
    x86_bit_atomic_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT,
        TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16}, 5);
    u8 expected_x86_bit_atomic[] = {
        0x66, 0x0f, 0xbc, 0xc1,
        0x0f, 0xbd, 0xc2,
        0x4f, 0x0f, 0xbc, 0x44, 0x8c, 0x10,
        0x0f, 0xc8,
        0x49, 0x0f, 0xc8,
        0x66, 0x0f, 0xa3, 0xc8,
        0x41, 0x0f, 0xbb, 0x54, 0x24, 0x08,
        0x4d, 0x0f, 0xb3, 0xc8,
        0x48, 0x0f, 0xba, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x3f,
        0x86, 0xc3,
        0x66, 0x41, 0x90,
        0x93,
        0x4d, 0x87, 0xc1,
        0x4f, 0x87, 0x44, 0x4c, 0x20,
        0x45, 0x0f, 0xc0, 0x4d, 0x00,
        0x66, 0x45, 0x0f, 0xc1, 0xda,
        0x0f, 0xb1, 0xc8,
        0x4c, 0x0f, 0xb1, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x0f, 0xc7, 0x4d, 0x00,
        0x49, 0x0f, 0xc7, 0x0e,
        0xf3, 0x4d, 0x0f, 0xb8, 0x04, 0x24,
        0xf3, 0x0f, 0xbd, 0xca,
        0xf3, 0x48, 0x0f, 0xbc, 0xc3,
    };
    String8 x86_intel_bit_atomic_source =
        S8("bsf ax, cx\n"
           "bsr eax, edx\n"
           "bsf r8, qword ptr [r12 + r9*4 + 16]\n"
           "bswap eax\n"
           "bswap r8\n"
           "bt ax, cx\n"
           "btc dword ptr [r12 + 8], edx\n"
           "btr r8, r9\n"
           "bts qword ptr [rip + external], 63\n"
           "xchg al, bl\n"
           "xchg ax, r8w\n"
           "xchg eax, ebx\n"
           "xchg r8, r9\n"
           "xchg qword ptr [r12 + r9*2 + 32], r8\n"
           "xadd byte ptr [r13], r9b\n"
           "xadd r10w, r11w\n"
           "cmpxchg eax, ecx\n"
           "cmpxchg qword ptr [rip + external2], r8\n"
           "cmpxchg8b qword ptr [r13]\n"
           "cmpxchg16b xmmword ptr [r14]\n"
           "popcnt r8, qword ptr [r12]\n"
           "lzcnt ecx, edx\n"
           "tzcnt rax, rbx\n");
    AssemblyEncodeResult x86_intel_bit_atomic = assembly_encode(
        arguments->arena, x86_intel_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_bit_atomic.diagnostic_count == 0 &&
                               x86_intel_bit_atomic.bytes.length == sizeof(expected_x86_bit_atomic) &&
                               memcmp(x86_intel_bit_atomic.bytes.pointer, expected_x86_bit_atomic,
                                      sizeof(expected_x86_bit_atomic)) == 0);
    u8 expected_x86_address32_cmpxchg16b[] = {0x67, 0x48, 0x0f, 0xc7, 0x08};
    AssemblyEncodeResult x86_address32_cmpxchg16b = assembly_encode(
        arguments->arena, S8("cmpxchg16b xmmword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_cmpxchg16b.diagnostic_count == 0 &&
                               x86_address32_cmpxchg16b.bytes.length == sizeof(expected_x86_address32_cmpxchg16b) &&
                               memcmp(x86_address32_cmpxchg16b.bytes.pointer, expected_x86_address32_cmpxchg16b,
                                      sizeof(expected_x86_address32_cmpxchg16b)) == 0);
    BUSTER_TEST(arguments, x86_intel_bit_atomic.relocation_count == 2 && x86_intel_bit_atomic.relocations[0].offset == 36 &&
                               x86_intel_bit_atomic.relocations[1].offset == 72 && x86_intel_bit_atomic.relocations[0].addend == -4 &&
                               x86_intel_bit_atomic.relocations[1].addend == -4 &&
                               x86_intel_bit_atomic.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               x86_intel_bit_atomic.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_bit_atomic.symbols[x86_intel_bit_atomic.relocations[0].symbol].name,
                                            S8("external")) &&
                               string_equal(x86_intel_bit_atomic.symbols[x86_intel_bit_atomic.relocations[1].symbol].name,
                                            S8("external2")));
    String8 x86_att_bit_atomic_source =
        S8("bsfw %cx, %ax\n"
           "bsrl %edx, %eax\n"
           "bsfq 16(%r12,%r9,4), %r8\n"
           "bswapl %eax\n"
           "bswapq %r8\n"
           "btw %cx, %ax\n"
           "btcl %edx, 8(%r12)\n"
           "btrq %r9, %r8\n"
           "btsq $63, external(%rip)\n"
           "xchgb %bl, %al\n"
           "xchgw %r8w, %ax\n"
           "xchgl %ebx, %eax\n"
           "xchgq %r9, %r8\n"
           "xchgq %r8, 32(%r12,%r9,2)\n"
           "xaddb %r9b, (%r13)\n"
           "xaddw %r11w, %r10w\n"
           "cmpxchgl %ecx, %eax\n"
           "cmpxchgq %r8, external2(%rip)\n"
           "cmpxchg8b (%r13)\n"
           "cmpxchg16b (%r14)\n"
           "popcntq (%r12), %r8\n"
           "lzcntl %edx, %ecx\n"
           "tzcntq %rbx, %rax\n");
    AssemblyEncodeResult x86_att_bit_atomic = assembly_encode(
        arguments->arena, x86_att_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_bit_atomic.diagnostic_count == 0 && x86_att_bit_atomic.bytes.length == sizeof(expected_x86_bit_atomic) &&
                               memcmp(x86_att_bit_atomic.bytes.pointer, expected_x86_bit_atomic,
                                      sizeof(expected_x86_bit_atomic)) == 0);
    BUSTER_TEST(arguments, x86_att_bit_atomic.relocation_count == 2 && x86_att_bit_atomic.relocations[0].offset == 36 &&
                               x86_att_bit_atomic.relocations[1].offset == 72);

    u8 expected_x86_locked_bit_atomic[] = {
        0xf0, 0x01, 0x08,
        0xf0, 0x0f, 0xbb, 0x08,
        0xf0, 0x0f, 0xba, 0x30, 0x03,
        0xf0, 0x48, 0x0f, 0xba, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x07,
        0xf0, 0x41, 0x87, 0x08,
        0xf0, 0x41, 0x0f, 0xc1, 0x08,
        0xf0, 0x41, 0x0f, 0xb1, 0x08,
        0xf0, 0x41, 0x0f, 0xc7, 0x08,
    };
    String8 x86_intel_locked_bit_atomic_source =
        S8("lock add dword ptr [rax], ecx\n"
           "lock btc dword ptr [rax], ecx\n"
           "lock btr dword ptr [rax], 3\n"
           "lock bts qword ptr [rip + lock_external], 7\n"
           "lock xchg dword ptr [r8], ecx\n"
           "lock xadd dword ptr [r8], ecx\n"
           "lock cmpxchg dword ptr [r8], ecx\n"
           "lock cmpxchg8b qword ptr [r8]\n");
    AssemblyEncodeResult x86_intel_locked_bit_atomic = assembly_encode(
        arguments->arena, x86_intel_locked_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_locked_bit_atomic.diagnostic_count == 0 &&
                               x86_intel_locked_bit_atomic.bytes.length == sizeof(expected_x86_locked_bit_atomic) &&
                               memcmp(x86_intel_locked_bit_atomic.bytes.pointer, expected_x86_locked_bit_atomic,
                                      sizeof(expected_x86_locked_bit_atomic)) == 0);
    BUSTER_TEST(arguments, x86_intel_locked_bit_atomic.relocation_count == 1 &&
                               x86_intel_locked_bit_atomic.relocations[0].offset == 17 &&
                               x86_intel_locked_bit_atomic.relocations[0].addend == -4);
    AssemblyEncodeResult x86_att_locked_bit_atomic = assembly_encode(
        arguments->arena,
        S8("lock addl %ecx, (%rax)\n"
           "lock btcl %ecx, (%rax)\n"
           "lock btrl $3, (%rax)\n"
           "lock btsq $7, lock_external(%rip)\n"
           "lock xchgl %ecx, (%r8)\n"
           "lock xaddl %ecx, (%r8)\n"
           "lock cmpxchgl %ecx, (%r8)\n"
           "lock cmpxchg8b (%r8)\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_locked_bit_atomic.diagnostic_count == 0 &&
                               x86_att_locked_bit_atomic.bytes.length == sizeof(expected_x86_locked_bit_atomic) &&
                               memcmp(x86_att_locked_bit_atomic.bytes.pointer, expected_x86_locked_bit_atomic,
                                      sizeof(expected_x86_locked_bit_atomic)) == 0);

    u8 expected_x86_high_byte_atomic[] = {
        0x86, 0xe0,
        0x0f, 0xc0, 0xc4,
        0x0f, 0xb0, 0xc4,
    };
    AssemblyEncodeResult x86_high_byte_atomic = assembly_encode(
        arguments->arena, S8("xchg ah, al\nxadd ah, al\ncmpxchg ah, al\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_high_byte_atomic.diagnostic_count == 0 &&
                               x86_high_byte_atomic.bytes.length == sizeof(expected_x86_high_byte_atomic) &&
                               memcmp(x86_high_byte_atomic.bytes.pointer, expected_x86_high_byte_atomic,
                                      sizeof(expected_x86_high_byte_atomic)) == 0);
    AssemblyEncodeResult invalid_x86_high_byte_atomic = assembly_encode(
        arguments->arena,
        S8("xchg ah, r8b\n"
           "xadd ah, r8b\n"
           "cmpxchg ah, r8b\n"
           "mov byte ptr [r8], ah\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_high_byte_atomic.diagnostic_count == 4);

    AssemblyEncodeResult unsupported_x86_bit_atomic = assembly_encode(
        arguments->arena,
        S8("popcnt eax, ebx\n"
           "lzcnt eax, ebx\n"
           "tzcnt eax, ebx\n"
           "cmpxchg16b xmmword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_x86_bit_atomic.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < unsupported_x86_bit_atomic.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, unsupported_x86_bit_atomic.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult invalid_x86_bit_atomic = assembly_encode(
        arguments->arena,
        S8("bsf al, bl\n"
           "bswap ax\n"
           "bt qword ptr [rax], 256\n"
           "xadd dword ptr [rax], dword ptr [rbx]\n"
           "cmpxchg8b rax\n"
           "lock bt dword ptr [rax], ecx\n"
           "lock xadd eax, ecx\n"
           "lock mov dword ptr [rax], ecx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_bit_atomic.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_bit_atomic.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_bit_atomic.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_x86_forms =
        assembly_encode(arguments->arena, S8("mov rax, eax\nadd rax, 0x80000000\nnopq\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_forms.diagnostic_count == 3);
    AssemblyEncodeResult invalid_att_forms =
        assembly_encode(arguments->arena, S8("movq %rbx, 3(,%rax,2,4)\naddq 3(,%rax,2,4), %rax\ncallq %r11\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_forms.diagnostic_count == 3);
    AssemblyEncodeResult invalid_att_absolute = assembly_encode(
        arguments->arena,
        S8("movq , %rax\n"
           "movq broken(,%rax\n"
           "movq broken(,%rax,2,4), %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_absolute.diagnostic_count == 3 && invalid_att_absolute.bytes.length == 0);
    AssemblyEncodeResult invalid_x86_memory =
        assembly_encode(arguments->arena, S8("mov rax, [rsp*2]\nmov rax, [rip + rbx]\ninc [rax]\nmov rax, [rbx + 0x80000000]\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_memory.diagnostic_count == 4);
    AssemblyEncodeResult invalid_x86_sse2 =
        assembly_encode(arguments->arena, S8("mov rax, xmm0\naddps xmm0, rax\nmovaps [rax], [rbx]\naddps [rax], xmm0\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_sse2.diagnostic_count == 4);
    AssemblyEncodeResult invalid_x86_conditions =
        assembly_encode(arguments->arena, S8("seteb %al\nsete %rax\ncmove %al, %bl\ncmoveq (%rax), (%rbx)\nje %rax\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_x86_conditions.diagnostic_count == 5);
    AssemblyEncodeResult invalid_x86_avx =
        assembly_encode(arguments->arena,
                        S8("vaddps ymm0, xmm1, ymm2\nvaddss ymm0, ymm1, ymm2\nvmovaps [rax], [rbx]\nvaddps rax, ymm1, ymm2\n"),
                        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_avx.diagnostic_count == 4);

    u8 expected_x86_lea[] = {
        0x66, 0x8d, 0x44, 0x8b, 0x10,
        0x43, 0x8d, 0x44, 0xc8, 0xe0,
        0x4e, 0x8d, 0x7c, 0x64, 0x7f,
        0x44, 0x8d, 0x45, 0x00,
        0x4d, 0x8d, 0x8c, 0x24, 0x78, 0x56, 0x34, 0x12,
    };
    String8 x86_intel_lea_source =
        S8("lea ax, [rbx + rcx*4 + 16]\n"
           "lea eax, [r8 + r9*8 - 32]\n"
           "lea r15, [rsp + r12*2 + 127]\n"
           "lea r8d, [rbp]\n"
           "lea r9, [r12 + 0x12345678]\n");
    AssemblyEncodeResult x86_intel_lea = assembly_encode(
        arguments->arena, x86_intel_lea_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_lea.diagnostic_count == 0 && x86_intel_lea.bytes.length == sizeof(expected_x86_lea) &&
                               memcmp(x86_intel_lea.bytes.pointer, expected_x86_lea, sizeof(expected_x86_lea)) == 0);
    String8 x86_att_lea_source =
        S8("leaw 16(%rbx,%rcx,4), %ax\n"
           "leal -32(%r8,%r9,8), %eax\n"
           "leaq 127(%rsp,%r12,2), %r15\n"
           "leal (%rbp), %r8d\n"
           "leaq 0x12345678(%r12), %r9\n");
    AssemblyEncodeResult x86_att_lea =
        assembly_encode(arguments->arena, x86_att_lea_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_lea.diagnostic_count == 0 && x86_att_lea.bytes.length == sizeof(expected_x86_lea) &&
                               memcmp(x86_att_lea.bytes.pointer, expected_x86_lea, sizeof(expected_x86_lea)) == 0);
    u8 expected_x86_lea_rip[] = {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_lea_rip = assembly_encode(
        arguments->arena, S8("lea rax, [rip + external]\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_lea_rip.diagnostic_count == 0 && x86_lea_rip.bytes.length == sizeof(expected_x86_lea_rip) &&
                               memcmp(x86_lea_rip.bytes.pointer, expected_x86_lea_rip, sizeof(expected_x86_lea_rip)) == 0 &&
                               x86_lea_rip.relocation_count == 1 && x86_lea_rip.relocations[0].offset == 3 &&
                               x86_lea_rip.relocations[0].addend == -4 && x86_lea_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_lea_absolute[] = {0x4d, 0x8d, 0x8c, 0x24, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_lea_absolute = assembly_encode(
        arguments->arena, S8("lea r9, [r12 + external + 8]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_lea_absolute.diagnostic_count == 0 && x86_lea_absolute.bytes.length == sizeof(expected_x86_lea_absolute) &&
                               memcmp(x86_lea_absolute.bytes.pointer, expected_x86_lea_absolute, sizeof(expected_x86_lea_absolute)) == 0 &&
                               x86_lea_absolute.relocation_count == 1 && x86_lea_absolute.relocations[0].offset == 4 &&
                               x86_lea_absolute.relocations[0].addend == 8 && x86_lea_absolute.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32);

    u8 expected_x86_scalar_extend[] = {
        0x66, 0x0f, 0xb6, 0xc0,
        0x0f, 0xb6, 0xc4,
        0x44, 0x0f, 0xb6, 0xc4,
        0x66, 0x40, 0x0f, 0xb6, 0xc4,
        0x4f, 0x0f, 0xb7, 0x4c, 0x94, 0x08,
        0x66, 0x0f, 0xbe, 0xc7,
        0x48, 0x0f, 0xbe, 0x06,
        0x4d, 0x63, 0x41, 0x10,
        0x48, 0x63, 0xc0,
    };
    String8 x86_intel_scalar_extend_source =
        S8("movzx ax, al\n"
           "movzx eax, ah\n"
           "movzx r8d, spl\n"
           "movzx ax, spl\n"
           "movzx r9, word ptr [r12 + r10*4 + 8]\n"
           "movsx ax, bh\n"
           "movsx rax, byte ptr [rsi]\n"
           "movsxd r8, dword ptr [r9 + 16]\n"
           "movsxd rax, eax\n");
    AssemblyEncodeResult x86_intel_scalar_extend = assembly_encode(
        arguments->arena, x86_intel_scalar_extend_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_scalar_extend.diagnostic_count == 0 &&
                               x86_intel_scalar_extend.bytes.length == sizeof(expected_x86_scalar_extend) &&
                               memcmp(x86_intel_scalar_extend.bytes.pointer, expected_x86_scalar_extend,
                                      sizeof(expected_x86_scalar_extend)) == 0);
    u8 expected_x86_att_scalar_extend[] = {
        0x66, 0x0f, 0xb6, 0xc0,
        0x0f, 0xb6, 0xc4,
        0x48, 0x0f, 0xb6, 0xc4,
        0x66, 0x40, 0x0f, 0xb6, 0xc4,
        0x47, 0x0f, 0xb7, 0x4c, 0x94, 0x08,
        0x66, 0x0f, 0xbe, 0xc7,
        0x44, 0x0f, 0xbe, 0x06,
        0x4c, 0x0f, 0xbe, 0x0e,
        0x45, 0x0f, 0xbf, 0xda,
        0x4d, 0x0f, 0xbf, 0xec,
        0x4d, 0x63, 0xfe,
    };
    String8 x86_att_scalar_extend_source =
        S8("movzbw %al, %ax\n"
           "movzbl %ah, %eax\n"
           "movzbq %spl, %rax\n"
           "movzbw %spl, %ax\n"
           "movzwl 8(%r12,%r10,4), %r9d\n"
           "movsbw %bh, %ax\n"
           "movsbl (%rsi), %r8d\n"
           "movsbq (%rsi), %r9\n"
           "movswl %r10w, %r11d\n"
           "movswq %r12w, %r13\n"
           "movslq %r14d, %r15\n");
    AssemblyEncodeResult x86_att_scalar_extend = assembly_encode(
        arguments->arena, x86_att_scalar_extend_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_scalar_extend.diagnostic_count == 0 &&
                               x86_att_scalar_extend.bytes.length == sizeof(expected_x86_att_scalar_extend) &&
                               memcmp(x86_att_scalar_extend.bytes.pointer, expected_x86_att_scalar_extend,
                                      sizeof(expected_x86_att_scalar_extend)) == 0);
    u8 expected_x86_scalar_extend_rip[] = {0x48, 0x0f, 0xbe, 0x05, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_scalar_extend_rip = assembly_encode(
        arguments->arena, S8("movsx rax, byte ptr [rip + external]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_scalar_extend_rip.diagnostic_count == 0 &&
                               x86_scalar_extend_rip.bytes.length == sizeof(expected_x86_scalar_extend_rip) &&
                               memcmp(x86_scalar_extend_rip.bytes.pointer, expected_x86_scalar_extend_rip,
                                      sizeof(expected_x86_scalar_extend_rip)) == 0 &&
                               x86_scalar_extend_rip.relocation_count == 1 && x86_scalar_extend_rip.relocations[0].offset == 4 &&
                               x86_scalar_extend_rip.relocations[0].addend == -4 &&
                               x86_scalar_extend_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_scalar_extend_memory_widths[] = {
        0x0f, 0xb6, 0x06,
        0x4c, 0x0f, 0xb6, 0x06,
        0x0f, 0xbf, 0x06,
        0x4c, 0x0f, 0xbf, 0x0e,
        0x48, 0x63, 0x06,
    };
    AssemblyEncodeResult x86_intel_scalar_extend_memory_widths = assembly_encode(
        arguments->arena,
        S8("movzx eax, byte ptr [rsi]\n"
           "movzx r8, byte ptr [rsi]\n"
           "movsx eax, word ptr [rsi]\n"
           "movsx r9, word ptr [rsi]\n"
           "movsxd rax, dword ptr [rsi]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_scalar_extend_memory_widths.diagnostic_count == 0 &&
                               x86_intel_scalar_extend_memory_widths.bytes.length == sizeof(expected_x86_scalar_extend_memory_widths) &&
                               memcmp(x86_intel_scalar_extend_memory_widths.bytes.pointer, expected_x86_scalar_extend_memory_widths,
                                      sizeof(expected_x86_scalar_extend_memory_widths)) == 0);
    AssemblyEncodeResult x86_att_scalar_extend_memory_widths = assembly_encode(
        arguments->arena,
        S8("movzbl (%rsi), %eax\n"
           "movzbq (%rsi), %r8\n"
           "movswl (%rsi), %eax\n"
           "movswq (%rsi), %r9\n"
           "movslq (%rsi), %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_scalar_extend_memory_widths.diagnostic_count == 0 &&
                               x86_att_scalar_extend_memory_widths.bytes.length == sizeof(expected_x86_scalar_extend_memory_widths) &&
                               memcmp(x86_att_scalar_extend_memory_widths.bytes.pointer, expected_x86_scalar_extend_memory_widths,
                                      sizeof(expected_x86_scalar_extend_memory_widths)) == 0);
    u8 expected_x86_high_byte_extend[] = {
        0x0f, 0xb6, 0xc4,
        0x0f, 0xb6, 0xcd,
        0x0f, 0xbe, 0xd6,
        0x0f, 0xb6, 0xdf,
    };
    AssemblyEncodeResult x86_high_byte_extend = assembly_encode(
        arguments->arena,
        S8("movzx eax, ah\nmovzx ecx, ch\nmovsx edx, dh\nmovzx ebx, bh\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_high_byte_extend.diagnostic_count == 0 &&
                               x86_high_byte_extend.bytes.length == sizeof(expected_x86_high_byte_extend) &&
                               memcmp(x86_high_byte_extend.bytes.pointer, expected_x86_high_byte_extend,
                                      sizeof(expected_x86_high_byte_extend)) == 0);

    u8 expected_x86_rotate[] = {
        0xd0, 0xc0,
        0x66, 0xd3, 0xc8,
        0xc1, 0xd0, 0x7f,
        0x49, 0xc1, 0xd8, 0xff,
        0x43, 0xd0, 0x44, 0x51, 0x08,
        0x66, 0xd3, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0xc1, 0x54, 0x24, 0x10, 0x07,
        0x49, 0xd3, 0x1c, 0x24,
        0xd0, 0xc4,
        0x41, 0xc0, 0xcf, 0xff,
    };
    String8 x86_intel_rotate_source =
        S8("rol al, 1\n"
           "ror ax, cl\n"
           "rcl eax, 0x7f\n"
           "rcr r8, 0xff\n"
           "rol byte ptr [r9 + r10*2 + 8], 1\n"
           "ror word ptr [rip + rotate_external], cl\n"
           "rcl dword ptr [rsp + 16], 7\n"
           "rcr qword ptr [r12], cl\n"
           "rol ah, 1\n"
           "ror r15b, -1\n");
    AssemblyEncodeResult x86_intel_rotate = assembly_encode(
        arguments->arena, x86_intel_rotate_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_rotate.diagnostic_count == 0 && x86_intel_rotate.bytes.length == sizeof(expected_x86_rotate) &&
                               memcmp(x86_intel_rotate.bytes.pointer, expected_x86_rotate, sizeof(expected_x86_rotate)) == 0 &&
                               x86_intel_rotate.relocation_count == 1 && x86_intel_rotate.relocations[0].offset == 20 &&
                               x86_intel_rotate.relocations[0].addend == -4 &&
                               x86_intel_rotate.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_rotate_source =
        S8("rolb $1, %al\n"
           "rorw %cl, %ax\n"
           "rcll $0x7f, %eax\n"
           "rcrq $-1, %r8\n"
           "rolb $1, 8(%r9,%r10,2)\n"
           "rorw %cl, rotate_external(%rip)\n"
           "rcll $7, 16(%rsp)\n"
           "rcrq %cl, (%r12)\n"
           "rolb $1, %ah\n"
           "rorb $-1, %r15b\n");
    AssemblyEncodeResult x86_att_rotate = assembly_encode(
        arguments->arena, x86_att_rotate_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_rotate.diagnostic_count == 0 && x86_att_rotate.bytes.length == sizeof(expected_x86_rotate) &&
                               memcmp(x86_att_rotate.bytes.pointer, expected_x86_rotate, sizeof(expected_x86_rotate)) == 0 &&
                               x86_att_rotate.relocation_count == 1 && x86_att_rotate.relocations[0].offset == 20 &&
                               x86_att_rotate.relocations[0].addend == -4 &&
                               x86_att_rotate.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    u8 expected_x86_double_shift[] = {
        0x66, 0x0f, 0xa5, 0xd8,
        0x44, 0x0f, 0xa4, 0xc0, 0x07,
        0x4d, 0x0f, 0xa4, 0xd1, 0xff,
        0x66, 0x47, 0x0f, 0xad, 0x74, 0xac, 0x08,
        0x0f, 0xac, 0x6c, 0x24, 0x10, 0x01,
        0x45, 0x0f, 0xad, 0xda,
    };
    String8 x86_intel_double_shift_source =
        S8("shld ax, bx, cl\n"
           "shld eax, r8d, 7\n"
           "shld r9, r10, 255\n"
           "shrd word ptr [r12 + r13*4 + 8], r14w, cl\n"
           "shrd dword ptr [rsp + 16], ebp, 1\n"
           "shrd r10d, r11d, cl\n");
    AssemblyEncodeResult x86_intel_double_shift = assembly_encode(
        arguments->arena, x86_intel_double_shift_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_double_shift.diagnostic_count == 0 &&
                               x86_intel_double_shift.bytes.length == sizeof(expected_x86_double_shift) &&
                               memcmp(x86_intel_double_shift.bytes.pointer, expected_x86_double_shift,
                                      sizeof(expected_x86_double_shift)) == 0);
    String8 x86_att_double_shift_source =
        S8("shldw %cl, %bx, %ax\n"
           "shldl $7, %r8d, %eax\n"
           "shldq $255, %r10, %r9\n"
           "shrdw %cl, %r14w, 8(%r12,%r13,4)\n"
           "shrdl $1, %ebp, 16(%rsp)\n"
           "shrdl %cl, %r11d, %r10d\n");
    AssemblyEncodeResult x86_att_double_shift = assembly_encode(
        arguments->arena, x86_att_double_shift_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_double_shift.diagnostic_count == 0 &&
                               x86_att_double_shift.bytes.length == sizeof(expected_x86_double_shift) &&
                               memcmp(x86_att_double_shift.bytes.pointer, expected_x86_double_shift,
                                      sizeof(expected_x86_double_shift)) == 0);
    u8 expected_x86_double_shift_rip[] = {0x4c, 0x0f, 0xa4, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80};
    AssemblyEncodeResult x86_double_shift_rip = assembly_encode(
        arguments->arena, S8("shld qword ptr [rip + shift_external], r8, 0x80\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_double_shift_rip.diagnostic_count == 0 &&
                               x86_double_shift_rip.bytes.length == sizeof(expected_x86_double_shift_rip) &&
                               memcmp(x86_double_shift_rip.bytes.pointer, expected_x86_double_shift_rip,
                                      sizeof(expected_x86_double_shift_rip)) == 0 &&
                               x86_double_shift_rip.relocation_count == 1 && x86_double_shift_rip.relocations[0].offset == 4 &&
                               x86_double_shift_rip.relocations[0].addend == -5 &&
                               x86_double_shift_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_double_shift_rip_shrd[] = {0x4c, 0x0f, 0xac, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80};
    AssemblyEncodeResult x86_double_shift_rip_shrd = assembly_encode(
        arguments->arena, S8("shrd qword ptr [rip + shift_external], r8, 0x80\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_double_shift_rip_shrd.diagnostic_count == 0 &&
                               x86_double_shift_rip_shrd.bytes.length == sizeof(expected_x86_double_shift_rip_shrd) &&
                               memcmp(x86_double_shift_rip_shrd.bytes.pointer, expected_x86_double_shift_rip_shrd,
                                      sizeof(expected_x86_double_shift_rip_shrd)) == 0 &&
                               x86_double_shift_rip_shrd.relocation_count == 1 &&
                               x86_double_shift_rip_shrd.relocations[0].offset == 4 &&
                               x86_double_shift_rip_shrd.relocations[0].addend == -5 &&
                               x86_double_shift_rip_shrd.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_x86_scalar_integer_family = assembly_encode(
        arguments->arena,
        S8("lea r8b, [rax]\n"
           "lea eax, rbx\n"
           "movzx al, byte ptr [rax]\n"
           "movzx eax, [rax]\n"
           "movsx eax, dword ptr [rax]\n"
           "movsxd eax, dword ptr [rax]\n"
           "movzx r8d, ah\n"
           "movzx r9d, ch\n"
           "movsx r10d, dh\n"
           "movsx rax, bh\n"
           "movzx ax, ax\n"
           "movsx ax, word ptr [rax]\n"
           "movsx al, byte ptr [rax]\n"
           "movzx eax, dword ptr [rax]\n"
           "movsx eax, ebx\n"
           "movsxd rax, ax\n"
           "movsxd r8d, eax\n"
           "rol eax, edx\n"
           "ror [rax], 1\n"
           "rcl eax, external\n"
           "shld eax, ebx, dl\n"
           "shrd eax, [rbx], cl\n"
           "shld eax, ebx, 256\n"
           "shrd eax, ebx, -129\n"
           "lock lea rax, [rbx]\n"
           "lock movzx eax, byte ptr [rbx]\n"
           "lock rol eax, 1\n"
           "lock shld eax, ebx, cl\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_scalar_integer_family.diagnostic_count == 28);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_scalar_integer_family.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_scalar_integer_family.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target aarch64_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    AssemblyEncodeResult aarch64 = assembly_encode(arguments->arena, S8("entry:\n nop\n bl external\n b entry\n ret\n"),
                                                    (AssemblyEncodeOptions){
                                                        .target = aarch64_target,
                                                    });
    u8 expected_aarch64[] = {
        0x1f, 0x20, 0x03, 0xd5, 0x00, 0x00, 0x00, 0x94,
        0xfe, 0xff, 0xff, 0x17, 0xc0, 0x03, 0x5f, 0xd6,
    };
    BUSTER_TEST(arguments, aarch64.diagnostic_count == 0);
    BUSTER_TEST(arguments, aarch64.bytes.length == sizeof(expected_aarch64) &&
                               memcmp(aarch64.bytes.pointer, expected_aarch64, sizeof(expected_aarch64)) == 0);
    BUSTER_TEST(arguments, aarch64.relocation_count == 1 && aarch64.relocations[0].offset == 4 &&
                               aarch64.relocations[0].kind == ASSEMBLY_RELOCATION_AARCH64_CALL26);
    AssemblyEncodeResult aarch64_jump = assembly_encode(arguments->arena, S8("b external\n"),
                                                         (AssemblyEncodeOptions){
                                                             .target = aarch64_target,
                                                         });
    u8 expected_aarch64_jump[] = {0, 0, 0, 0x14};
    BUSTER_TEST(arguments, aarch64_jump.diagnostic_count == 0 &&
                               aarch64_jump.bytes.length == sizeof(expected_aarch64_jump) &&
                               memcmp(aarch64_jump.bytes.pointer, expected_aarch64_jump, sizeof(expected_aarch64_jump)) == 0);
    BUSTER_TEST(arguments, aarch64_jump.relocation_count == 1 && aarch64_jump.relocations[0].offset == 0 &&
                               aarch64_jump.relocations[0].kind == ASSEMBLY_RELOCATION_AARCH64_JUMP26);

    AssemblyEncodeResult invalid = assembly_encode(arguments->arena, S8("same:\n same: nop\n ret x0\n unknown\n"),
                                                    (AssemblyEncodeOptions){
                                                        .target = aarch64_target,
                                                    });
    BUSTER_TEST(arguments, invalid.diagnostic_count == 3);
    if (invalid.diagnostic_count == 3)
    {
        BUSTER_TEST(arguments, invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL);
        BUSTER_TEST(arguments, invalid.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
        BUSTER_TEST(arguments, invalid.diagnostics[2].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }

    Target aarch64_m1_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
    };
    AssemblyEncodeResult aarch64_fixed = assembly_encode(
        arguments->arena,
        S8("NOP\n"
           "aUtIaSp\n"
           "RETAA\n"
           "AXFLAG\n"
           "PSSBB\n"
           "TSB    CSYNC\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_fixed[] = {
        0x1f, 0x20, 0x03, 0xd5,
        0xbf, 0x23, 0x03, 0xd5,
        0xff, 0x0b, 0x5f, 0xd6,
        0x5f, 0x40, 0x00, 0xd5,
        0x9f, 0x34, 0x03, 0xd5,
        0x5f, 0x22, 0x03, 0xd5,
    };
    BUSTER_TEST(arguments, aarch64_fixed.diagnostic_count == 0 &&
                               aarch64_fixed.bytes.length == sizeof(expected_aarch64_fixed) &&
                               memcmp(aarch64_fixed.bytes.pointer, expected_aarch64_fixed, sizeof(expected_aarch64_fixed)) == 0);
    AssemblyEncodeResult aarch64_fixed_bad_operand = assembly_encode(
        arguments->arena, S8("NOP x0\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_fixed_bad_operand.diagnostic_count == 1 &&
                               aarch64_fixed_bad_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    AssemblyEncodeResult aarch64_fixed_bad_token = assembly_encode(
        arguments->arena, S8("AUTIASP x0\nTSB CSYNC extra\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_fixed_bad_token.diagnostic_count == 2 &&
                               aarch64_fixed_bad_token.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               aarch64_fixed_bad_token.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    AssemblyEncodeResult aarch64_fixed_bad_prefix = assembly_encode(
        arguments->arena, S8("lock AUTIASP\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_fixed_bad_prefix.diagnostic_count == 1 && aarch64_fixed_bad_prefix.bytes.length == 0 &&
                               aarch64_fixed_bad_prefix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    Target aarch64_m1_explicit_target = aarch64_m1_target;
    aarch64_m1_explicit_target.cpu_features_explicit = true;
    aarch64_m1_explicit_target.cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1);
    Target aarch64_m1_no_pauth = aarch64_m1_explicit_target;
    aarch64_m1_no_pauth.cpu_features = target_cpu_features_remove(aarch64_m1_no_pauth.cpu_features, TARGET_CPU_FEATURE_AARCH64_PAUTH);
    AssemblyEncodeResult aarch64_fixed_no_pauth = assembly_encode(
        arguments->arena, S8("RETAA\nNOP\n"), (AssemblyEncodeOptions){.target = aarch64_m1_no_pauth});
    BUSTER_TEST(arguments, aarch64_fixed_no_pauth.diagnostic_count == 1 && aarch64_fixed_no_pauth.bytes.length == 4 &&
                               aarch64_fixed_no_pauth.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               memcmp(aarch64_fixed_no_pauth.bytes.pointer, expected_aarch64_fixed, 4) == 0);
    Target aarch64_m1_no_trace = aarch64_m1_explicit_target;
    aarch64_m1_no_trace.cpu_features =
        target_cpu_features_remove(aarch64_m1_no_trace.cpu_features, TARGET_CPU_FEATURE_AARCH64_TRACEV8_4);
    AssemblyEncodeResult aarch64_fixed_no_trace = assembly_encode(
        arguments->arena, S8("TSB CSYNC\nNOP\n"), (AssemblyEncodeOptions){.target = aarch64_m1_no_trace});
    BUSTER_TEST(arguments, aarch64_fixed_no_trace.diagnostic_count == 1 && aarch64_fixed_no_trace.bytes.length == 4 &&
                               aarch64_fixed_no_trace.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               memcmp(aarch64_fixed_no_trace.bytes.pointer, expected_aarch64_fixed, 4) == 0);
    AssemblyEncodeResult aarch64_fixed_generic = assembly_encode(
        arguments->arena, S8("AUTIASP\nRETAA\n"), (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_fixed_generic.diagnostic_count == 2 &&
                               aarch64_fixed_generic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               aarch64_fixed_generic.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);

    AssemblyEncodeResult aarch64_direct_gpr = assembly_encode(
        arguments->arena,
        S8("adcs w1, w2, w3\n"
           "madd x1, x2, x3, x4\n"
           "smaddl x1, w2, w3, x4\n"
           "blraa x1, sp\n"
           "pacia x1, sp\n"
           "autiza xzr\n"
           "setf8 w1\n"
           "crc32w w1, w2, w3\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_direct_gpr[] = {
        0x41, 0x00, 0x03, 0x3a,
        0x41, 0x10, 0x03, 0x9b,
        0x41, 0x10, 0x23, 0x9b,
        0x3f, 0x08, 0x3f, 0xd7,
        0xe1, 0x03, 0xc1, 0xda,
        0xff, 0x33, 0xc1, 0xda,
        0x2d, 0x08, 0x00, 0x3a,
        0x41, 0x48, 0xc3, 0x1a,
    };
    BUSTER_TEST(arguments, aarch64_direct_gpr.diagnostic_count == 0 &&
                               aarch64_direct_gpr.bytes.length == sizeof(expected_aarch64_direct_gpr) &&
                               memcmp(aarch64_direct_gpr.bytes.pointer, expected_aarch64_direct_gpr,
                                      sizeof(expected_aarch64_direct_gpr)) == 0);
    AssemblyEncodeResult aarch64_direct_gpr_bad = assembly_encode(
        arguments->arena,
        S8("madd x1, w2, x3, x4\n"
           "adcs w31, w2, w3\n"
           "pacia x1, xzr\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_direct_gpr_bad.diagnostic_count == 3 && aarch64_direct_gpr_bad.bytes.length == 0);

    AssemblyEncodeResult aarch64_aes = assembly_encode(
        arguments->arena,
        S8("aesd v0.16b, v1.16b\n"
           "aese v0.16b, v1.16b\n"
           "aesimc v0.16b, v1.16b\n"
           "aesmc v0.16b, v1.16b\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_explicit_target});
    static u8 const expected_aarch64_aes[] = {
        0x20, 0x58, 0x28, 0x4e,
        0x20, 0x48, 0x28, 0x4e,
        0x20, 0x78, 0x28, 0x4e,
        0x20, 0x68, 0x28, 0x4e,
    };
    BUSTER_TEST(arguments, aarch64_aes.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_aes.bytes, expected_aarch64_aes,
                                                         sizeof(expected_aarch64_aes)));
    AssemblyEncodeResult aarch64_aes_case_insensitive = assembly_encode(
        arguments->arena, S8("AESD V0.16B, V1.16B\n"), (AssemblyEncodeOptions){.target = aarch64_m1_explicit_target});
    BUSTER_TEST(arguments, aarch64_aes_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_aes_case_insensitive.bytes, expected_aarch64_aes,
                                                         4));
    AssemblyEncodeResult aarch64_aes_boundary = assembly_encode(
        arguments->arena,
        S8("aesd v31.16b, v31.16b\n"
           "aese v31.16b, v31.16b\n"
           "aesimc v31.16b, v31.16b\n"
           "aesmc v31.16b, v31.16b\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_explicit_target});
    static u8 const expected_aarch64_aes_boundary[] = {
        0xff, 0x5b, 0x28, 0x4e,
        0xff, 0x4b, 0x28, 0x4e,
        0xff, 0x7b, 0x28, 0x4e,
        0xff, 0x6b, 0x28, 0x4e,
    };
    BUSTER_TEST(arguments, aarch64_aes_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_aes_boundary.bytes, expected_aarch64_aes_boundary,
                                                         sizeof(expected_aarch64_aes_boundary)));
    AssemblyEncodeResult aarch64_aes_without_feature = assembly_encode(
        arguments->arena, S8("aesd v0.16b, v1.16b\n"), (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_aes_without_feature.diagnostic_count == 1 &&
                               aarch64_aes_without_feature.bytes.length == 0 &&
                               aarch64_aes_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_aes[] = {
        S8_INITIALIZER("aesd v0.8b, v1.8b\n"),
        S8_INITIALIZER("aese v0.16b\n"),
        S8_INITIALIZER("aesimc x0.16b, v1.16b\n"),
        S8_INITIALIZER("aesmc v0.16b, v32.16b\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_aes); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_aes_case = assembly_encode(
            arguments->arena, invalid_aarch64_aes[invalid_index], (AssemblyEncodeOptions){.target = aarch64_m1_explicit_target});
        BUSTER_TEST(arguments, invalid_aes_case.diagnostic_count == 1 && invalid_aes_case.bytes.length == 0 &&
                                   invalid_aes_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target aarch64_sha3_target = aarch64_m1_explicit_target;
    BUSTER_TEST(arguments, target_cpu_feature_has(aarch64_sha3_target, TARGET_CPU_FEATURE_AARCH64_SHA3));
    AssemblyEncodeResult aarch64_sha3 = assembly_encode(
        arguments->arena,
        S8("bcax v0.16b, v1.16b, v2.16b, v3.16b\n"
           "eor3 v0.16b, v1.16b, v2.16b, v3.16b\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha3[] = {
        0x20, 0x0c, 0x22, 0xce,
        0x20, 0x0c, 0x02, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha3.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha3.bytes, expected_aarch64_sha3,
                                                         sizeof(expected_aarch64_sha3)));
    AssemblyEncodeResult aarch64_sha3_case_insensitive = assembly_encode(
        arguments->arena, S8("BCAX V0.16B, V1.16B, V2.16B, V3.16B\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    BUSTER_TEST(arguments, aarch64_sha3_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha3_case_insensitive.bytes, expected_aarch64_sha3, 4));
    AssemblyEncodeResult aarch64_sha3_boundary = assembly_encode(
        arguments->arena,
        S8("bcax v31.16b, v31.16b, v31.16b, v31.16b\n"
           "eor3 v31.16b, v31.16b, v31.16b, v31.16b\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha3_boundary[] = {
        0xff, 0x7f, 0x3f, 0xce,
        0xff, 0x7f, 0x1f, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha3_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha3_boundary.bytes, expected_aarch64_sha3_boundary,
                                                         sizeof(expected_aarch64_sha3_boundary)));
    Target aarch64_no_sha3 = aarch64_sha3_target;
    aarch64_no_sha3.cpu_features = target_cpu_features_remove(aarch64_no_sha3.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA3);
    AssemblyEncodeResult aarch64_sha3_without_feature = assembly_encode(
        arguments->arena, S8("bcax v0.16b, v1.16b, v2.16b, v3.16b\n"),
        (AssemblyEncodeOptions){.target = aarch64_no_sha3});
    BUSTER_TEST(arguments, aarch64_sha3_without_feature.diagnostic_count == 1 && aarch64_sha3_without_feature.bytes.length == 0 &&
                               aarch64_sha3_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_sha3[] = {
        S8_INITIALIZER("bcax v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("eor3 v0.16b, v1.16b, v2.16b\n"),
        S8_INITIALIZER("bcax x0.16b, v1.16b, v2.16b, v3.16b\n"),
        S8_INITIALIZER("eor3 v0.16b, v1.16b, v2.16b, v32.16b\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha3); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha3_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha3[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha3_target});
        BUSTER_TEST(arguments, invalid_sha3_case.diagnostic_count == 1 && invalid_sha3_case.bytes.length == 0 &&
                                   invalid_sha3_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult aarch64_rax1 = assembly_encode(
        arguments->arena, S8("rax1 v0.2d, v1.2d, v2.2d\n"), (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_rax1[] = {0x20, 0x8c, 0x62, 0xce};
    BUSTER_TEST(arguments, aarch64_rax1.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_rax1.bytes, expected_aarch64_rax1,
                                                         sizeof(expected_aarch64_rax1)));
    AssemblyEncodeResult aarch64_rax1_case_insensitive = assembly_encode(
        arguments->arena, S8("RAX1 V0.2D, V1.2D, V2.2D\n"), (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    BUSTER_TEST(arguments, aarch64_rax1_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_rax1_case_insensitive.bytes, expected_aarch64_rax1,
                                                         sizeof(expected_aarch64_rax1)));
    AssemblyEncodeResult aarch64_rax1_boundary = assembly_encode(
        arguments->arena, S8("rax1 v31.2d, v30.2d, v29.2d\n"), (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_rax1_boundary[] = {0xdf, 0x8f, 0x7d, 0xce};
    BUSTER_TEST(arguments, aarch64_rax1_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_rax1_boundary.bytes, expected_aarch64_rax1_boundary,
                                                         sizeof(expected_aarch64_rax1_boundary)));
    Target aarch64_no_rax1_sha3 = aarch64_sha3_target;
    aarch64_no_rax1_sha3.cpu_features =
        target_cpu_features_remove(aarch64_no_rax1_sha3.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA3);
    AssemblyEncodeResult aarch64_rax1_without_feature = assembly_encode(
        arguments->arena, S8("rax1 v0.2d, v1.2d, v2.2d\n"), (AssemblyEncodeOptions){.target = aarch64_no_rax1_sha3});
    BUSTER_TEST(arguments, aarch64_rax1_without_feature.diagnostic_count == 1 &&
                               aarch64_rax1_without_feature.bytes.length == 0 &&
                               aarch64_rax1_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_rax1[] = {
        S8_INITIALIZER("rax1 v0.16b, v1.16b, v2.16b\n"),
        S8_INITIALIZER("rax1 v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("rax1 v0.2d, v1.2d\n"),
        S8_INITIALIZER("rax1 x0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("rax1 v0.2d, v1.2d, v32.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_rax1); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_rax1_case = assembly_encode(
            arguments->arena, invalid_aarch64_rax1[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha3_target});
        BUSTER_TEST(arguments, invalid_rax1_case.diagnostic_count == 1 && invalid_rax1_case.bytes.length == 0 &&
                                   invalid_rax1_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult aarch64_sha512su = assembly_encode(
        arguments->arena,
        S8("sha512su0 v0.2d, v1.2d\n"
           "sha512su1 v0.2d, v1.2d, v2.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha512su[] = {
        0x20, 0x80, 0xc0, 0xce,
        0x20, 0x88, 0x62, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha512su.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512su.bytes, expected_aarch64_sha512su,
                                                         sizeof(expected_aarch64_sha512su)));
    AssemblyEncodeResult aarch64_sha512su_case_insensitive = assembly_encode(
        arguments->arena, S8("SHA512SU0 V0.2D, V1.2D\n"), (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    BUSTER_TEST(arguments, aarch64_sha512su_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512su_case_insensitive.bytes,
                                                         expected_aarch64_sha512su, 4));
    AssemblyEncodeResult aarch64_sha512su_boundary = assembly_encode(
        arguments->arena,
        S8("sha512su0 v31.2d, v30.2d\n"
           "sha512su1 v31.2d, v30.2d, v29.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha512su_boundary[] = {
        0xdf, 0x83, 0xc0, 0xce,
        0xdf, 0x8b, 0x7d, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha512su_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512su_boundary.bytes,
                                                         expected_aarch64_sha512su_boundary,
                                                         sizeof(expected_aarch64_sha512su_boundary)));
    Target aarch64_no_sha512_sha3 = aarch64_sha3_target;
    aarch64_no_sha512_sha3.cpu_features =
        target_cpu_features_remove(aarch64_no_sha512_sha3.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA3);
    AssemblyEncodeResult aarch64_sha512su_without_feature = assembly_encode(
        arguments->arena, S8("sha512su0 v0.2d, v1.2d\n"), (AssemblyEncodeOptions){.target = aarch64_no_sha512_sha3});
    BUSTER_TEST(arguments, aarch64_sha512su_without_feature.diagnostic_count == 1 &&
                               aarch64_sha512su_without_feature.bytes.length == 0 &&
                               aarch64_sha512su_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_sha512su[] = {
        S8_INITIALIZER("sha512su0 v0.16b, v1.16b\n"),
        S8_INITIALIZER("sha512su1 v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("sha512su0 v0.2d\n"),
        S8_INITIALIZER("sha512su1 v0.2d, v1.2d\n"),
        S8_INITIALIZER("sha512su0 x0.2d, v1.2d\n"),
        S8_INITIALIZER("sha512su1 v0.2d, v1.2d, x2.2d\n"),
        S8_INITIALIZER("sha512su0 v32.2d, v1.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha512su); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha512su_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha512su[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha3_target});
        BUSTER_TEST(arguments, invalid_sha512su_case.diagnostic_count == 1 && invalid_sha512su_case.bytes.length == 0 &&
                                   invalid_sha512su_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult aarch64_sha512h = assembly_encode(
        arguments->arena,
        S8("sha512h q0, q1, v2.2d\n"
           "sha512h2 q0, q1, v2.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha512h[] = {
        0x20, 0x80, 0x62, 0xce,
        0x20, 0x84, 0x62, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha512h.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512h.bytes, expected_aarch64_sha512h,
                                                         sizeof(expected_aarch64_sha512h)));
    AssemblyEncodeResult aarch64_sha512h_case_insensitive = assembly_encode(
        arguments->arena, S8("SHA512H Q0, Q1, V2.2D\n"), (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    BUSTER_TEST(arguments, aarch64_sha512h_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512h_case_insensitive.bytes,
                                                         expected_aarch64_sha512h, 4));
    AssemblyEncodeResult aarch64_sha512h_boundary = assembly_encode(
        arguments->arena,
        S8("sha512h q31, q30, v29.2d\n"
           "sha512h2 q31, q30, v29.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha3_target});
    static u8 const expected_aarch64_sha512h_boundary[] = {
        0xdf, 0x83, 0x7d, 0xce,
        0xdf, 0x87, 0x7d, 0xce,
    };
    BUSTER_TEST(arguments, aarch64_sha512h_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha512h_boundary.bytes,
                                                         expected_aarch64_sha512h_boundary,
                                                         sizeof(expected_aarch64_sha512h_boundary)));
    Target aarch64_no_sha512h_sha3 = aarch64_sha3_target;
    aarch64_no_sha512h_sha3.cpu_features =
        target_cpu_features_remove(aarch64_no_sha512h_sha3.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA3);
    AssemblyEncodeResult aarch64_sha512h_without_feature = assembly_encode(
        arguments->arena, S8("sha512h q0, q1, v2.2d\n"), (AssemblyEncodeOptions){.target = aarch64_no_sha512h_sha3});
    BUSTER_TEST(arguments, aarch64_sha512h_without_feature.diagnostic_count == 1 &&
                               aarch64_sha512h_without_feature.bytes.length == 0 &&
                               aarch64_sha512h_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_sha512h[] = {
        S8_INITIALIZER("sha512h q0.2d, q1, v2.2d\n"),
        S8_INITIALIZER("sha512h v0, q1, v2.2d\n"),
        S8_INITIALIZER("sha512h q0, v1, v2.2d\n"),
        S8_INITIALIZER("sha512h q0, q1, v2\n"),
        S8_INITIALIZER("sha512h q0, q1, v2.16b\n"),
        S8_INITIALIZER("sha512h q0, q1\n"),
        S8_INITIALIZER("sha512h q0, q1, v2.2d, v3.2d\n"),
        S8_INITIALIZER("sha512h q32, q1, v2.2d\n"),
        S8_INITIALIZER("sha512h q0, q32, v2.2d\n"),
        S8_INITIALIZER("sha512h q0, q1, v32.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha512h); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha512h_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha512h[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha3_target});
        BUSTER_TEST(arguments, invalid_sha512h_case.diagnostic_count == 1 && invalid_sha512h_case.bytes.length == 0 &&
                                   invalid_sha512h_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target aarch64_sha2_target = aarch64_m1_explicit_target;
    BUSTER_TEST(arguments, target_cpu_feature_has(aarch64_sha2_target, TARGET_CPU_FEATURE_AARCH64_SHA2));
    AssemblyEncodeResult aarch64_sha256su = assembly_encode(
        arguments->arena,
        S8("sha256su0 v0.4s, v1.4s\n"
           "sha256su1 v0.4s, v1.4s, v2.4s\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    static u8 const expected_aarch64_sha256su[] = {
        0x20, 0x28, 0x28, 0x5e,
        0x20, 0x60, 0x02, 0x5e,
    };
    BUSTER_TEST(arguments, aarch64_sha256su.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256su.bytes, expected_aarch64_sha256su,
                                                         sizeof(expected_aarch64_sha256su)));
    AssemblyEncodeResult aarch64_sha256su_case_insensitive = assembly_encode(
        arguments->arena, S8("SHA256SU0 V0.4S, V1.4S\n"), (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    BUSTER_TEST(arguments, aarch64_sha256su_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256su_case_insensitive.bytes,
                                                         expected_aarch64_sha256su, 4));
    AssemblyEncodeResult aarch64_sha256su_boundary = assembly_encode(
        arguments->arena,
        S8("sha256su0 v31.4s, v30.4s\n"
           "sha256su1 v31.4s, v30.4s, v29.4s\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    static u8 const expected_aarch64_sha256su_boundary[] = {
        0xdf, 0x2b, 0x28, 0x5e,
        0xdf, 0x63, 0x1d, 0x5e,
    };
    BUSTER_TEST(arguments, aarch64_sha256su_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256su_boundary.bytes,
                                                         expected_aarch64_sha256su_boundary,
                                                         sizeof(expected_aarch64_sha256su_boundary)));
    Target aarch64_no_sha256_sha2 = aarch64_sha2_target;
    aarch64_no_sha256_sha2.cpu_features =
        target_cpu_features_remove(aarch64_no_sha256_sha2.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA2);
    AssemblyEncodeResult aarch64_sha256su_without_feature = assembly_encode(
        arguments->arena, S8("sha256su0 v0.4s, v1.4s\n"), (AssemblyEncodeOptions){.target = aarch64_no_sha256_sha2});
    BUSTER_TEST(arguments, aarch64_sha256su_without_feature.diagnostic_count == 1 &&
                               aarch64_sha256su_without_feature.bytes.length == 0 &&
                               aarch64_sha256su_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_sha256su[] = {
        S8_INITIALIZER("sha256su0 v0.2d, v1.2d\n"),
        S8_INITIALIZER("sha256su1 v0.16b, v1.16b, v2.16b\n"),
        S8_INITIALIZER("sha256su0 v0.4s\n"),
        S8_INITIALIZER("sha256su1 v0.4s, v1.4s\n"),
        S8_INITIALIZER("sha256su0 x0.4s, v1.4s\n"),
        S8_INITIALIZER("sha256su1 v0.4s, v1.4s, x2.4s\n"),
        S8_INITIALIZER("sha256su0 v32.4s, v1.4s\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha256su); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha256su_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha256su[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha2_target});
        BUSTER_TEST(arguments, invalid_sha256su_case.diagnostic_count == 1 && invalid_sha256su_case.bytes.length == 0 &&
                                   invalid_sha256su_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult aarch64_sha256h = assembly_encode(
        arguments->arena,
        S8("sha256h q0, q1, v2.4s\n"
           "sha256h2 q0, q1, v2.4s\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    static u8 const expected_aarch64_sha256h[] = {
        0x20, 0x40, 0x02, 0x5e,
        0x20, 0x50, 0x02, 0x5e,
    };
    BUSTER_TEST(arguments, aarch64_sha256h.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256h.bytes, expected_aarch64_sha256h,
                                                         sizeof(expected_aarch64_sha256h)));
    AssemblyEncodeResult aarch64_sha256h_case_insensitive = assembly_encode(
        arguments->arena, S8("SHA256H Q0, Q1, V2.4S\n"), (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    BUSTER_TEST(arguments, aarch64_sha256h_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256h_case_insensitive.bytes,
                                                         expected_aarch64_sha256h, 4));
    AssemblyEncodeResult aarch64_sha256h_boundary = assembly_encode(
        arguments->arena,
        S8("sha256h q31, q30, v29.4s\n"
           "sha256h2 q31, q30, v29.4s\n"),
        (AssemblyEncodeOptions){.target = aarch64_sha2_target});
    static u8 const expected_aarch64_sha256h_boundary[] = {
        0xdf, 0x43, 0x1d, 0x5e,
        0xdf, 0x53, 0x1d, 0x5e,
    };
    BUSTER_TEST(arguments, aarch64_sha256h_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_sha256h_boundary.bytes,
                                                         expected_aarch64_sha256h_boundary,
                                                         sizeof(expected_aarch64_sha256h_boundary)));
    Target aarch64_no_sha256h_sha2 = aarch64_sha2_target;
    aarch64_no_sha256h_sha2.cpu_features =
        target_cpu_features_remove(aarch64_no_sha256h_sha2.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA2);
    AssemblyEncodeResult aarch64_sha256h_without_feature = assembly_encode(
        arguments->arena, S8("sha256h q0, q1, v2.4s\n"), (AssemblyEncodeOptions){.target = aarch64_no_sha256h_sha2});
    BUSTER_TEST(arguments, aarch64_sha256h_without_feature.diagnostic_count == 1 &&
                               aarch64_sha256h_without_feature.bytes.length == 0 &&
                               aarch64_sha256h_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_sha256h[] = {
        S8_INITIALIZER("sha256h q0.4s, q1, v2.4s\n"),
        S8_INITIALIZER("sha256h v0, q1, v2.4s\n"),
        S8_INITIALIZER("sha256h q0, v1, v2.4s\n"),
        S8_INITIALIZER("sha256h q0, q1, v2\n"),
        S8_INITIALIZER("sha256h q0, q1, v2.16b\n"),
        S8_INITIALIZER("sha256h q0, q1\n"),
        S8_INITIALIZER("sha256h q0, q1, v2.4s, v3.4s\n"),
        S8_INITIALIZER("sha256h q32, q1, v2.4s\n"),
        S8_INITIALIZER("sha256h q0, q32, v2.4s\n"),
        S8_INITIALIZER("sha256h q0, q1, v32.4s\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha256h); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha256h_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha256h[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_sha2_target});
        BUSTER_TEST(arguments, invalid_sha256h_case.diagnostic_count == 1 && invalid_sha256h_case.bytes.length == 0 &&
                                   invalid_sha256h_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    /* SHA-1 AdvSIMD rows use the same HasSHA2 requirement as LLVM's canonical
     * decoder.  Exercise the exact minimal target independently of the M1
     * aggregate target used by the neighboring SHA2/SHA3 regressions. */
    Target aarch64_sha1_target = aarch64_sha2_target;
    aarch64_sha1_target.cpu_model = CPU_MODEL_BASELINE;
    aarch64_sha1_target.cpu_features_explicit = true;
    aarch64_sha1_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_SHA2},
        3);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_sha1_target) &&
                               target_cpu_feature_has(aarch64_sha1_target, TARGET_CPU_FEATURE_AARCH64_SHA2));
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_cases) == 6 &&
                               BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_boundary_cases) == 6);
    for (u32 sha1_index = 0; sha1_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_cases); sha1_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase sha1_case = assembly_a64_direct_simd_sha1_cases[sha1_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, sha1_case.source, (AssemblyEncodeOptions){.target = aarch64_sha1_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, sha1_case.bytes, 4));
    }
    for (u32 sha1_index = 0; sha1_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_boundary_cases); sha1_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase sha1_case = assembly_a64_direct_simd_sha1_boundary_cases[sha1_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, sha1_case.source, (AssemblyEncodeOptions){.target = aarch64_sha1_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, sha1_case.bytes, 4));
    }
    Target aarch64_sha1_without_sha2 = aarch64_sha1_target;
    aarch64_sha1_without_sha2.cpu_features =
        target_cpu_features_remove(aarch64_sha1_without_sha2.cpu_features, TARGET_CPU_FEATURE_AARCH64_SHA2);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_sha1_without_sha2));
    for (u32 sha1_index = 0; sha1_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_sha1_cases); sha1_index += 1)
    {
        AssemblyEncodeResult without_feature = assembly_encode(
            arguments->arena, assembly_a64_direct_simd_sha1_cases[sha1_index].source,
            (AssemblyEncodeOptions){.target = aarch64_sha1_without_sha2});
        BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                   without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    static String8 const invalid_aarch64_sha1[] = {
        S8_INITIALIZER("sha1c v0.4s, s1, v2.4s\n"),
        S8_INITIALIZER("sha1h d0, s1\n"),
        S8_INITIALIZER("sha1su0 q0, q1, q2\n"),
        S8_INITIALIZER("sha1c q0, d1, v2.4s\n"),
        S8_INITIALIZER("sha1c q0, s1, v2.2d\n"),
        S8_INITIALIZER("sha1su1 v0.2d, v1.2d\n"),
        S8_INITIALIZER("sha1c q0, s1\n"),
        S8_INITIALIZER("sha1h s0\n"),
        S8_INITIALIZER("sha1su0 v0.4s, v1.4s\n"),
        S8_INITIALIZER("sha1su1 v0.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("sha1c q32, s1, v2.4s\n"),
        S8_INITIALIZER("sha1h s0, s32\n"),
        S8_INITIALIZER("sha1su0 v0.4s, v1.4s, v32.4s\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_sha1); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_sha1_case = assembly_encode(
            arguments->arena, invalid_aarch64_sha1[invalid_index], (AssemblyEncodeOptions){.target = aarch64_sha1_target});
        BUSTER_TEST(arguments, invalid_sha1_case.diagnostic_count == 1 && invalid_sha1_case.bytes.length == 0 &&
                                   invalid_sha1_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target aarch64_advsimd_target = aarch64_m1_explicit_target;
    BUSTER_TEST(arguments, target_cpu_feature_has(aarch64_advsimd_target, TARGET_CPU_FEATURE_AARCH64_NEON));
    AssemblyEncodeResult aarch64_advsimd_absneg = assembly_encode(
        arguments->arena, S8("abs d0, d1\nneg d0, d1\n"), (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_absneg[] = {
        0x20, 0xb8, 0xe0, 0x5e,
        0x20, 0xb8, 0xe0, 0x7e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_absneg.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_absneg.bytes, expected_aarch64_advsimd_absneg,
                                                         sizeof(expected_aarch64_advsimd_absneg)));
    AssemblyEncodeResult aarch64_advsimd_absneg_case_insensitive = assembly_encode(
        arguments->arena, S8("ABS D0, D1\nNEG D0, D1\n"), (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, aarch64_advsimd_absneg_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_absneg_case_insensitive.bytes,
                                                         expected_aarch64_advsimd_absneg,
                                                         sizeof(expected_aarch64_advsimd_absneg)));
    AssemblyEncodeResult aarch64_advsimd_absneg_boundary = assembly_encode(
        arguments->arena, S8("abs d31, d30\nneg d31, d30\n"), (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_absneg_boundary[] = {
        0xdf, 0xbb, 0xe0, 0x5e,
        0xdf, 0xbb, 0xe0, 0x7e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_absneg_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_absneg_boundary.bytes,
                                                         expected_aarch64_advsimd_absneg_boundary,
                                                         sizeof(expected_aarch64_advsimd_absneg_boundary)));
    Target aarch64_no_advsimd_neon = aarch64_advsimd_target;
    aarch64_no_advsimd_neon.cpu_features =
        target_cpu_features_remove(aarch64_no_advsimd_neon.cpu_features, TARGET_CPU_FEATURE_AARCH64_NEON);
    AssemblyEncodeResult aarch64_advsimd_without_feature = assembly_encode(
        arguments->arena, S8("abs d0, d1\n"), (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
    BUSTER_TEST(arguments, aarch64_advsimd_without_feature.diagnostic_count == 1 &&
                               aarch64_advsimd_without_feature.bytes.length == 0 &&
                               aarch64_advsimd_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    static String8 const invalid_aarch64_advsimd_absneg[] = {
        S8_INITIALIZER("abs q0, q1\n"),
        S8_INITIALIZER("abs d0.2d, d1.2d\n"),
        S8_INITIALIZER("neg d0\n"),
        S8_INITIALIZER("abs d32, d1\n"),
        S8_INITIALIZER("neg d0, d32\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_advsimd_absneg); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_advsimd_case = assembly_encode(
            arguments->arena, invalid_aarch64_advsimd_absneg[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_advsimd_case.diagnostic_count == 1 && invalid_advsimd_case.bytes.length == 0 &&
                                   invalid_advsimd_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    for (u32 transform_index = 0; transform_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_transform_cases); transform_index += 1)
    {
        AssemblyA64DirectSIMDTransformCase transform_case = assembly_a64_direct_simd_transform_cases[transform_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, transform_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, transform_case.bytes, 4));
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_m1_collision_cases) == 44);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_m1_collision_boundary_cases) == 13);
    for (u32 collision_index = 0; collision_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_m1_collision_cases);
         collision_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase collision_case = assembly_a64_direct_simd_m1_collision_cases[collision_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, collision_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, collision_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, collision_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 collision_index = 0; collision_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_m1_collision_boundary_cases);
         collision_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase collision_case = assembly_a64_direct_simd_m1_collision_boundary_cases[collision_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, collision_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, collision_case.bytes, 4));
    }
    static String8 const malformed_aarch64_m1_collision[] = {
        S8_INITIALIZER("add v0.8b, v1.8b\n"),
        S8_INITIALIZER("add v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("add q0, q1, q2\n"),
        S8_INITIALIZER("add v32.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("and v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("and d0, d1, d2\n"),
        S8_INITIALIZER("bic v0.8b, v1.8b\n"),
        S8_INITIALIZER("cls v0.2d, v1.2d\n"),
        S8_INITIALIZER("cls v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("cls q0, q1\n"),
        S8_INITIALIZER("clz v0.8b, v1.8b[0]\n"),
        S8_INITIALIZER("eor v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("orn v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("rbit v0.4h, v1.4h\n"),
        S8_INITIALIZER("rev16 v0.4h, v1.4h\n"),
        S8_INITIALIZER("rev32 v0.4s, v1.4s\n"),
        S8_INITIALIZER("rev32 v0.8h, v1.8h, v2.8h\n"),
        S8_INITIALIZER("sub s0, s1, s2\n"),
        S8_INITIALIZER("sub d0.2d, d1.2d, d2.2d\n"),
        S8_INITIALIZER("sub v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("sub v0.2d, v1.2d[0], v2.2d\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_m1_collision);
         malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_m1_collision[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    /* FCVTL/FCVTN expose the generated fixed literal `2` as an optional
     * spelling suffix.  Keep the parser grammar at two public vector
     * operands while the generic semantic builder supplies the hidden
     * fixed-constant operand and the spelling-owned Q override. */
    Target aarch64_fcvt_suffix_target = aarch64_advsimd_target;
    aarch64_fcvt_suffix_target.cpu_model = CPU_MODEL_BASELINE;
    aarch64_fcvt_suffix_target.cpu_features_explicit = true;
    aarch64_fcvt_suffix_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON}, 2);
    Target aarch64_fcvt_suffix_no_neon = aarch64_fcvt_suffix_target;
    aarch64_fcvt_suffix_no_neon.cpu_features = target_cpu_features_remove(aarch64_fcvt_suffix_no_neon.cpu_features,
                                                                            TARGET_CPU_FEATURE_AARCH64_NEON);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fcvt_suffix_target) &&
                               target_cpu_features_are_valid(aarch64_fcvt_suffix_no_neon));
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_suffix_cases) == 8 &&
                               BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_suffix_boundary_cases) == 8);
    for (u32 fcvt_index = 0; fcvt_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_suffix_cases); fcvt_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fcvt_case = assembly_a64_direct_simd_fcvt_suffix_cases[fcvt_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcvt_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_no_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 fcvt_index = 0; fcvt_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_suffix_boundary_cases); fcvt_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fcvt_case = assembly_a64_direct_simd_fcvt_suffix_boundary_cases[fcvt_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcvt_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_no_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    static String8 const invalid_aarch64_fcvt_suffix[] = {
        S8_INITIALIZER("fcvtl3 v0.4s, v1.4h\n"),
        S8_INITIALIZER("fcvtn3 v0.4h, v1.4s\n"),
        S8_INITIALIZER("fcvtl.2 v0.4s, v1.8h\n"),
        S8_INITIALIZER("fcvtn.2 v0.8h, v1.4s\n"),
        S8_INITIALIZER("fcvtl v0.8s, v1.4h\n"),
        S8_INITIALIZER("fcvtl v0.4s, v1.4s\n"),
        S8_INITIALIZER("fcvtl2 v0.2d, v1.2d\n"),
        S8_INITIALIZER("fcvtn v0.8h, v1.4s\n"),
        S8_INITIALIZER("fcvtn2 v0.4h, v1.4s\n"),
        S8_INITIALIZER("fcvtl v32.4s, v1.4h\n"),
        S8_INITIALIZER("fcvtl v0.4s, v32.4h\n"),
        S8_INITIALIZER("fcvtl s0, s1\n"),
        S8_INITIALIZER("fcvtl v0.4s, v1.4h[0]\n"),
        S8_INITIALIZER("fcvtl v0.4s, {v1.4h}\n"),
        S8_INITIALIZER("fcvtl v0.4s\n"),
        S8_INITIALIZER("fcvtn2 v0.4s, v1.2d, v2.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_fcvt_suffix); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_fcvt = assembly_encode(
            arguments->arena, invalid_aarch64_fcvt_suffix[invalid_index], (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, invalid_fcvt.diagnostic_count == 1 && invalid_fcvt.bytes.length == 0 &&
                                   (invalid_fcvt.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS ||
                                    invalid_fcvt.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION));
    }
    Target aarch64_fp16_both = aarch64_advsimd_target;
    aarch64_fp16_both.cpu_model = CPU_MODEL_BASELINE;
    aarch64_fp16_both.cpu_features_explicit = true;
    aarch64_fp16_both.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_FULLFP16},
        3);
    Target aarch64_fp16_no_full = aarch64_fp16_both;
    aarch64_fp16_no_full.cpu_features = target_cpu_features_remove(aarch64_fp16_no_full.cpu_features,
                                                                    TARGET_CPU_FEATURE_AARCH64_FULLFP16);
    Target aarch64_fp16_no_neon = aarch64_fp16_both;
    aarch64_fp16_no_neon.cpu_features = target_cpu_features_remove(aarch64_fp16_no_neon.cpu_features,
                                                                   TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_fp16_no_both = aarch64_fp16_no_full;
    aarch64_fp16_no_both.cpu_features = target_cpu_features_remove(aarch64_fp16_no_both.cpu_features,
                                                                    TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_fp_only = aarch64_fp16_both;
    aarch64_fp_only.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8}, 1);
    Target aarch64_fp16_scalar_only = aarch64_fp16_both;
    aarch64_fp16_scalar_only.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_FULLFP16}, 2);
    Target aarch64_fhm_both = aarch64_fp16_both;
    aarch64_fhm_both.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_FULLFP16, TARGET_CPU_FEATURE_AARCH64_FP16FML},
        4);
    Target aarch64_fhm_no_fp16fml = aarch64_fhm_both;
    aarch64_fhm_no_fp16fml.cpu_features = target_cpu_features_remove(aarch64_fhm_no_fp16fml.cpu_features,
                                                                       TARGET_CPU_FEATURE_AARCH64_FP16FML);
    Target aarch64_fhm_invalid_no_fullfp16 = aarch64_fhm_both;
    aarch64_fhm_invalid_no_fullfp16.cpu_features = target_cpu_features_remove(
        aarch64_fhm_invalid_no_fullfp16.cpu_features, TARGET_CPU_FEATURE_AARCH64_FULLFP16);
    Target aarch64_fhm_invalid_no_neon = aarch64_fhm_both;
    aarch64_fhm_invalid_no_neon.cpu_features = target_cpu_features_remove(aarch64_fhm_invalid_no_neon.cpu_features,
                                                                            TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_no_fp = aarch64_fp_only;
    aarch64_no_fp.cpu_features = (TargetCpuFeatures){0};
    Target aarch64_invalid_fullfp16 = aarch64_fp_only;
    aarch64_invalid_fullfp16.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FULLFP16}, 1);
    Target aarch64_frintts_target = aarch64_fp16_both;
    aarch64_frintts_target.cpu_model = CPU_MODEL_BASELINE;
    aarch64_frintts_target.cpu_features_explicit = true;
    aarch64_frintts_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_FPTOINT},
        3);
    Target aarch64_frintts_no_fptoint = aarch64_frintts_target;
    aarch64_frintts_no_fptoint.cpu_features = target_cpu_features_remove(aarch64_frintts_no_fptoint.cpu_features,
                                                                          TARGET_CPU_FEATURE_AARCH64_FPTOINT);
    Target aarch64_frintts_no_neon = aarch64_frintts_target;
    aarch64_frintts_no_neon.cpu_features = target_cpu_features_remove(aarch64_frintts_no_neon.cpu_features,
                                                                       TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_frintts_invalid_no_fp = aarch64_frintts_target;
    aarch64_frintts_invalid_no_fp.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_NEON, TARGET_CPU_FEATURE_AARCH64_FPTOINT}, 2);
    Target aarch64_dotprod_target = aarch64_fp16_both;
    aarch64_dotprod_target.cpu_model = CPU_MODEL_BASELINE;
    aarch64_dotprod_target.cpu_features_explicit = true;
    aarch64_dotprod_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_DOTPROD},
        3);
    Target aarch64_dotprod_no_dotprod = aarch64_dotprod_target;
    aarch64_dotprod_no_dotprod.cpu_features = target_cpu_features_remove(aarch64_dotprod_no_dotprod.cpu_features,
                                                                          TARGET_CPU_FEATURE_AARCH64_DOTPROD);
    Target aarch64_dotprod_no_neon = aarch64_dotprod_target;
    aarch64_dotprod_no_neon.cpu_features = target_cpu_features_remove(aarch64_dotprod_no_neon.cpu_features,
                                                                       TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_dotprod_no_both = aarch64_dotprod_no_dotprod;
    aarch64_dotprod_no_both.cpu_features = target_cpu_features_remove(aarch64_dotprod_no_both.cpu_features,
                                                                       TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_dotprod_invalid_no_fp = aarch64_dotprod_target;
    aarch64_dotprod_invalid_no_fp.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_NEON, TARGET_CPU_FEATURE_AARCH64_DOTPROD}, 2);
    Target aarch64_rdm_target = aarch64_fp16_both;
    aarch64_rdm_target.cpu_model = CPU_MODEL_BASELINE;
    aarch64_rdm_target.cpu_features_explicit = true;
    aarch64_rdm_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, TARGET_CPU_FEATURE_AARCH64_NEON,
                                   TARGET_CPU_FEATURE_AARCH64_RDM},
        3);
    Target aarch64_rdm_no_rdm = aarch64_rdm_target;
    aarch64_rdm_no_rdm.cpu_features = target_cpu_features_remove(aarch64_rdm_no_rdm.cpu_features,
                                                                  TARGET_CPU_FEATURE_AARCH64_RDM);
    Target aarch64_rdm_no_neon = aarch64_rdm_target;
    aarch64_rdm_no_neon.cpu_features = target_cpu_features_remove(aarch64_rdm_no_neon.cpu_features,
                                                                   TARGET_CPU_FEATURE_AARCH64_NEON);
    Target aarch64_rdm_no_both = aarch64_rdm_target;
    aarch64_rdm_no_both.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_FP_ARMV8}, 1);
    Target aarch64_rdm_invalid_no_fp = aarch64_rdm_target;
    aarch64_rdm_invalid_no_fp.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){TARGET_CPU_FEATURE_AARCH64_NEON, TARGET_CPU_FEATURE_AARCH64_RDM}, 2);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp16_both));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp16_no_full));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp16_no_neon));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp16_no_both));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp_only));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fp16_scalar_only));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_no_fp));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_invalid_fullfp16));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fhm_both));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_fhm_no_fp16fml));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_fhm_invalid_no_fullfp16));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_fhm_invalid_no_neon));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_frintts_target));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_frintts_no_fptoint));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_frintts_no_neon));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_frintts_invalid_no_fp));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_dotprod_target));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_dotprod_no_dotprod));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_dotprod_no_neon));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_dotprod_no_both));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_dotprod_invalid_no_fp));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_rdm_target));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_rdm_no_rdm));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_rdm_no_neon));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(aarch64_rdm_no_both));
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(aarch64_rdm_invalid_no_fp));
    TargetCpuFeatures invalid_requirement_features = {0};
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON, 0));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE,
                                                                         &invalid_requirement_features));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT,
                                                                         &invalid_requirement_features));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT, 0));
    Target invalid_aarch64_requirement_target = aarch64_fp16_both;
    invalid_aarch64_requirement_target.cpu_arch = CPU_ARCH_X86_64;
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(
                               invalid_aarch64_requirement_target, BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fp16_both,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fp16_both,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT));
    TargetCpuFeatures fp_requirement_features = {0};
    TargetCpuFeatures fullfp16_requirement_features = {0};
    TargetCpuFeatures fhm_requirement_features = {0};
    TargetCpuFeatures frintts_requirement_features = {0};
    TargetCpuFeatures dotprod_requirement_features = {0};
    TargetCpuFeatures rdm_requirement_features = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP,
                                                                        &fp_requirement_features) &&
                               fp_requirement_features.words[0] == 0 &&
                               fp_requirement_features.words[1] == (UINT64_C(1) << 54) &&
                               fp_requirement_features.words[2] == 0 && fp_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16,
                                                                        &fullfp16_requirement_features) &&
                               fullfp16_requirement_features.words[0] == 0 &&
                               fullfp16_requirement_features.words[1] == (UINT64_C(1) << 57) &&
                               fullfp16_requirement_features.words[2] == 0 && fullfp16_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML,
                                                                        &fhm_requirement_features) &&
                               fhm_requirement_features.words[0] == (UINT64_C(1) << 10) &&
                               fhm_requirement_features.words[1] == (UINT64_C(1) << 55) &&
                               fhm_requirement_features.words[2] == 0 && fhm_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT,
                                                                        &frintts_requirement_features) &&
                               frintts_requirement_features.words[0] == (UINT64_C(1) << 10) &&
                               frintts_requirement_features.words[1] == (UINT64_C(1) << 56) &&
                               frintts_requirement_features.words[2] == 0 && frintts_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD,
                                                                        &dotprod_requirement_features) &&
                               dotprod_requirement_features.words[0] == (UINT64_C(1) << 10) &&
                               dotprod_requirement_features.words[1] == (UINT64_C(1) << 52) &&
                               dotprod_requirement_features.words[2] == 0 && dotprod_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_features(BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM,
                                                                        &rdm_requirement_features) &&
                               rdm_requirement_features.words[0] == (UINT64_C(1) << 10) &&
                               rdm_requirement_features.words[1] == 0 &&
                               rdm_requirement_features.words[2] == (UINT64_C(1) << 3) &&
                               rdm_requirement_features.words[3] == 0);
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_fp_only,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_no_fp,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP));
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_fp16_both,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fp16_no_full,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_invalid_fullfp16,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16));
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_fhm_both,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fhm_no_fp16fml,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fhm_invalid_no_fullfp16,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_fhm_invalid_no_neon,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML));
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_frintts_target,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_frintts_no_fptoint,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_frintts_no_neon,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_frintts_invalid_no_fp,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT));
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_dotprod_target,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_dotprod_no_dotprod,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_dotprod_no_neon,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_dotprod_no_both,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_dotprod_invalid_no_fp,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD));
    BUSTER_TEST(arguments, buster_a64_direct_simd_requirement_supported(aarch64_rdm_target,
                                                                          BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_rdm_no_rdm,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_rdm_no_neon,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_rdm_no_both,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM));
    BUSTER_TEST(arguments, !buster_a64_direct_simd_requirement_supported(aarch64_rdm_invalid_no_fp,
                                                                           BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM));

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_cases) == 12);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_boundary_cases) == 4);
    for (u32 frintts_index = 0; frintts_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_cases); frintts_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase frintts_case = assembly_a64_direct_simd_frintts_cases[frintts_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, frintts_case.source, (AssemblyEncodeOptions){.target = aarch64_frintts_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, frintts_case.bytes, 4));
    }
    for (u32 frintts_index = 0; frintts_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_boundary_cases); frintts_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase frintts_case = assembly_a64_direct_simd_frintts_boundary_cases[frintts_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, frintts_case.source, (AssemblyEncodeOptions){.target = aarch64_frintts_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, frintts_case.bytes, 4));
    }
    Target const frintts_negative_targets[] = {aarch64_frintts_no_fptoint, aarch64_frintts_no_neon, aarch64_frintts_invalid_no_fp};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(frintts_negative_targets); target_index += 1)
    {
        for (u32 frintts_index = 0; frintts_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_frintts_cases); frintts_index += 1)
        {
            AssemblyEncodeResult without_feature = assembly_encode(
                arguments->arena, assembly_a64_direct_simd_frintts_cases[frintts_index].source,
                (AssemblyEncodeOptions){.target = frintts_negative_targets[target_index]});
            BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                       without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
    }
    static String8 const invalid_aarch64_frintts[] = {
        S8_INITIALIZER("frint32x v0.1d, v1.1d\n"),
        S8_INITIALIZER("frint32x v0.2s, v1.4s\n"),
        S8_INITIALIZER("frint32x v0.4s, v1.2s\n"),
        S8_INITIALIZER("frint32x d0, d1\n"),
        S8_INITIALIZER("frint32x q0, q1\n"),
        S8_INITIALIZER("frint32x v0.2s, v1.2s, v2.2s\n"),
        S8_INITIALIZER("frint32x v32.2s, v1.2s\n"),
        S8_INITIALIZER("frint32x v0.2s, v32.2s\n"),
        S8_INITIALIZER("frint32x v0.2s, v1.s\n"),
        S8_INITIALIZER("frint32x v0.2s, v1.2s[0]\n"),
        S8_INITIALIZER("frint32z v0.2s\n"),
        S8_INITIALIZER("frint64x v0.2s, v1.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_frintts); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_case = assembly_encode(
            arguments->arena, invalid_aarch64_frintts[invalid_index], (AssemblyEncodeOptions){.target = aarch64_frintts_target});
        BUSTER_TEST(arguments, invalid_case.diagnostic_count == 1 && invalid_case.bytes.length == 0 &&
                                   invalid_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_cases) == 4);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_boundary_cases) == 2);
    for (u32 dotprod_index = 0; dotprod_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_cases); dotprod_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase dotprod_case = assembly_a64_direct_simd_dotprod_cases[dotprod_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, dotprod_case.source, (AssemblyEncodeOptions){.target = aarch64_dotprod_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, dotprod_case.bytes, 4));
    }
    for (u32 dotprod_index = 0; dotprod_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_boundary_cases); dotprod_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase dotprod_case = assembly_a64_direct_simd_dotprod_boundary_cases[dotprod_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, dotprod_case.source, (AssemblyEncodeOptions){.target = aarch64_dotprod_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, dotprod_case.bytes, 4));
    }
    Target const dotprod_negative_targets[] = {
        aarch64_dotprod_no_dotprod,
        aarch64_dotprod_no_neon,
        aarch64_dotprod_no_both,
        aarch64_dotprod_invalid_no_fp,
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(dotprod_negative_targets); target_index += 1)
    {
        for (u32 dotprod_index = 0; dotprod_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_dotprod_cases); dotprod_index += 1)
        {
            AssemblyEncodeResult without_feature = assembly_encode(
                arguments->arena, assembly_a64_direct_simd_dotprod_cases[dotprod_index].source,
                (AssemblyEncodeOptions){.target = dotprod_negative_targets[target_index]});
            BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                       without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
    }
    static String8 const malformed_aarch64_dotprod[] = {
        S8_INITIALIZER("sdot v0.2s, v1.4b, v2.8b\n"),
        S8_INITIALIZER("sdot v0.2s, v1.8b, v2.16b\n"),
        S8_INITIALIZER("sdot v0.4s, v1.8b, v2.8b\n"),
        S8_INITIALIZER("sdot v0.2d, v1.8b, v2.8b\n"),
        S8_INITIALIZER("sdot q0, q1, q2\n"),
        S8_INITIALIZER("sdot s0, s1, s2\n"),
        S8_INITIALIZER("sdot v0.2s, v1.8b, v2.8b[0]\n"),
        S8_INITIALIZER("sdot {v0.2s, v1.2s}, v2.8b, v3.8b\n"),
        S8_INITIALIZER("sdot v0.2s, v1.8b\n"),
        S8_INITIALIZER("sdot v0.2s, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("sdot v32.2s, v1.8b, v2.8b\n"),
        S8_INITIALIZER("sdot v0.2s, v32.8b, v2.8b\n"),
        S8_INITIALIZER("sdot v0.2s, v1.8b, v32.8b\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_dotprod); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_dotprod[malformed_index], (AssemblyEncodeOptions){.target = aarch64_dotprod_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_cases) == 8);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_boundary_cases) == 8);
    for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_cases); rdm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase rdm_case = assembly_a64_direct_simd_rdm_cases[rdm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, rdm_case.source, (AssemblyEncodeOptions){.target = aarch64_rdm_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, rdm_case.bytes, 4));
    }
    for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_boundary_cases); rdm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase rdm_case = assembly_a64_direct_simd_rdm_boundary_cases[rdm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, rdm_case.source, (AssemblyEncodeOptions){.target = aarch64_rdm_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, rdm_case.bytes, 4));
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_cases) == 4);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_boundary_cases) == 4);
    for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_cases); rdm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase rdm_case = assembly_a64_direct_simd_rdm_scalar_cases[rdm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, rdm_case.source, (AssemblyEncodeOptions){.target = aarch64_rdm_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, rdm_case.bytes, 4));
    }
    for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_boundary_cases); rdm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase rdm_case = assembly_a64_direct_simd_rdm_scalar_boundary_cases[rdm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, rdm_case.source, (AssemblyEncodeOptions){.target = aarch64_rdm_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, rdm_case.bytes, 4));
    }
    Target const rdm_negative_targets[] = {
        aarch64_rdm_no_rdm,
        aarch64_rdm_no_neon,
        aarch64_rdm_no_both,
        aarch64_rdm_invalid_no_fp,
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(rdm_negative_targets); target_index += 1)
    {
        for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_cases); rdm_index += 1)
        {
            AssemblyEncodeResult without_feature = assembly_encode(
                arguments->arena, assembly_a64_direct_simd_rdm_cases[rdm_index].source,
                (AssemblyEncodeOptions){.target = rdm_negative_targets[target_index]});
            BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                       without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
        for (u32 rdm_index = 0; rdm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_rdm_scalar_cases); rdm_index += 1)
        {
            AssemblyEncodeResult without_feature = assembly_encode(
                arguments->arena, assembly_a64_direct_simd_rdm_scalar_cases[rdm_index].source,
                (AssemblyEncodeOptions){.target = rdm_negative_targets[target_index]});
            BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                       without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
    }
    static String8 const malformed_aarch64_rdm[] = {
        S8_INITIALIZER("sqrdmlah v0.2h, v1.2h, v2.2h\n"),
        S8_INITIALIZER("sqrdmlah v0.8s, v1.8s, v2.8s\n"),
        S8_INITIALIZER("sqrdmlah v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("sqrdmlsh v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sqrdmlah v0.4h, v1.8h, v2.8h\n"),
        S8_INITIALIZER("sqrdmlsh v0.4s, v1.2s, v2.2s\n"),
        S8_INITIALIZER("sqrdmlah h0, s1, s2\n"),
        S8_INITIALIZER("sqrdmlsh s0, h1, h2\n"),
        S8_INITIALIZER("sqrdmlah b0, b1, b2\n"),
        S8_INITIALIZER("sqrdmlsh d0, d1, d2\n"),
        S8_INITIALIZER("sqrdmlah {h0, h1}, h2, h3\n"),
        S8_INITIALIZER("sqrdmlsh h0, h1\n"),
        S8_INITIALIZER("sqrdmlah s0, s1, s2, s3\n"),
        S8_INITIALIZER("sqrdmlsh h32, h1, h2\n"),
        S8_INITIALIZER("sqrdmlah q0, q1, q2\n"),
        S8_INITIALIZER("sqrdmlsh v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("sqrdmlah {v0.4s, v1.4s}, v2.4s, v3.4s\n"),
        S8_INITIALIZER("sqrdmlsh v0.4s, v1.4s\n"),
        S8_INITIALIZER("sqrdmlah v0.4s, v1.4s, v2.4s, v3.4s\n"),
        S8_INITIALIZER("sqrdmlsh v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("sqrdmlah v0.4s, v32.4s, v2.4s\n"),
        S8_INITIALIZER("sqrdmlsh v0.4s, v1.4s, v32.4s\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_rdm); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_rdm[malformed_index], (AssemblyEncodeOptions){.target = aarch64_rdm_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_selector_cases) == 10);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_selector_boundary_cases) == 10);
    for (u32 scalar_selector_index = 0;
         scalar_selector_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_selector_cases);
         scalar_selector_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_selector_case =
            assembly_a64_direct_simd_scalar_selector_cases[scalar_selector_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_selector_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_selector_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, scalar_selector_case.source, (AssemblyEncodeOptions){.target = aarch64_fp_only});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 scalar_selector_index = 0;
         scalar_selector_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_selector_boundary_cases);
         scalar_selector_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_selector_case =
            assembly_a64_direct_simd_scalar_selector_boundary_cases[scalar_selector_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_selector_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_selector_case.bytes, 4));
    }
    static String8 const malformed_aarch64_scalar_selector[] = {
        S8_INITIALIZER("frecpe s0, d1\n"),
        S8_INITIALIZER("frecpx d0, s1\n"),
        S8_INITIALIZER("frsqrte b0, b1\n"),
        S8_INITIALIZER("scvtf q0, q1\n"),
        S8_INITIALIZER("ucvtf s0, s1, s2\n"),
        S8_INITIALIZER("frecpe s32, s1\n"),
        S8_INITIALIZER("frecpx s0, s32\n"),
        S8_INITIALIZER("frsqrte s0\n"),
        S8_INITIALIZER("scvtf s0, s1[0]\n"),
        S8_INITIALIZER("ucvtf v0.2s, v1.2d\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_scalar_selector);
         malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_scalar_selector[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_narrow_cases) == 9);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_narrow_boundary_cases) == 9);
    for (u32 scalar_narrow_index = 0;
         scalar_narrow_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_narrow_cases);
         scalar_narrow_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_narrow_case =
            assembly_a64_direct_simd_scalar_narrow_cases[scalar_narrow_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_narrow_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_narrow_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, scalar_narrow_case.source, (AssemblyEncodeOptions){.target = aarch64_fp_only});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 scalar_narrow_index = 0;
         scalar_narrow_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_narrow_boundary_cases);
         scalar_narrow_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_narrow_case =
            assembly_a64_direct_simd_scalar_narrow_boundary_cases[scalar_narrow_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_narrow_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_narrow_case.bytes, 4));
    }
    static String8 const malformed_aarch64_scalar_narrow[] = {
        /* Width-direction and source/destination mismatches. */
        S8_INITIALIZER("sqxtn h0, h1\n"),
        S8_INITIALIZER("sqxtn s0, s1\n"),
        S8_INITIALIZER("sqxtun b0, s1\n"),
        S8_INITIALIZER("uqxtn h0, h1\n"),
        S8_INITIALIZER("sqxtn d0, s1\n"),
        S8_INITIALIZER("sqxtun s0, s1\n"),
        S8_INITIALIZER("uqxtn b0, s1\n"),
        /* Vector, lane, list, arity, and register-range forms. */
        S8_INITIALIZER("sqxtn v0.4h, v1.4s\n"),
        S8_INITIALIZER("sqxtun v0.4h, v1.4s\n"),
        S8_INITIALIZER("uqxtn v0.4h, v1.4s\n"),
        S8_INITIALIZER("sqxtn b0\n"),
        S8_INITIALIZER("sqxtun b0, h1, h2\n"),
        S8_INITIALIZER("uqxtn b32, h1\n"),
        S8_INITIALIZER("sqxtn b0, h32\n"),
        S8_INITIALIZER("sqxtun b0, h1[0]\n"),
        S8_INITIALIZER("uqxtn {b0, b1}, h2\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_scalar_narrow);
         malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_scalar_narrow[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_widen_cases) == 6);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_widen_boundary_cases) == 6);
    for (u32 scalar_widen_index = 0;
         scalar_widen_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_widen_cases);
         scalar_widen_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_widen_case =
            assembly_a64_direct_simd_scalar_widen_cases[scalar_widen_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_widen_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_widen_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, scalar_widen_case.source, (AssemblyEncodeOptions){.target = aarch64_fp_only});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 scalar_widen_index = 0;
         scalar_widen_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_widen_boundary_cases);
         scalar_widen_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_widen_case =
            assembly_a64_direct_simd_scalar_widen_boundary_cases[scalar_widen_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_widen_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, scalar_widen_case.bytes, 4));
    }

    static String8 const malformed_aarch64_scalar_widen[] = {
        /* Reserved destination/source widths and mismatched widening pairs. */
        S8_INITIALIZER("sqdmlal b0, h1, h2\n"),
        S8_INITIALIZER("sqdmlsl h0, s1, s2\n"),
        S8_INITIALIZER("sqdmull d0, h1, h2\n"),
        S8_INITIALIZER("sqdmlal s0, s1, s2\n"),
        S8_INITIALIZER("sqdmlsl d0, h1, h2\n"),
        S8_INITIALIZER("sqdmull s0, s1, s2\n"),
        /* Arity, register range, lane, list, and vector forms. */
        S8_INITIALIZER("sqdmlal s0, h1\n"),
        S8_INITIALIZER("sqdmlsl s0, h1, h2, h3\n"),
        S8_INITIALIZER("sqdmull s32, h1, h2\n"),
        S8_INITIALIZER("sqdmlal s0, h32, h2\n"),
        S8_INITIALIZER("sqdmlsl s0, h1, h32\n"),
        S8_INITIALIZER("sqdmlal s0, h1, h2[0]\n"),
        S8_INITIALIZER("sqdmlsl s0, {h1, h2}, h3\n"),
        S8_INITIALIZER("sqdmull v0.2s, v1.4h, v2.4h\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_scalar_widen);
         malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_scalar_widen[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    /* The same canonical vector spellings remain owned by the complex SIMD
     * syntax table; adding scalar direct spellings must not erase them. */
    static String8 const scalar_widen_complex_rows[] = {
        S8_INITIALIZER("SQDMLAL <Va><d>, <Vb><n>, <Vb><m>"),
        S8_INITIALIZER("SQDMLSL <Va><d>, <Vb><n>, <Vb><m>"),
        S8_INITIALIZER("SQDMULL <Va><d>, <Vb><n>, <Vb><m>"),
    };
    BusterAarch64SyntaxCounts scalar_widen_syntax_counts = buster_aarch64_syntax_counts();
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(scalar_widen_complex_rows); expected_index += 1)
    {
        bool found = false;
        for (u32 syntax_index = 0; syntax_index < scalar_widen_syntax_counts.row_count; syntax_index += 1)
        {
            BusterAarch64SyntaxRow syntax_row = {0};
            if (buster_aarch64_syntax_row(syntax_index, &syntax_row) &&
                string_equal(syntax_row.assembly, scalar_widen_complex_rows[expected_index]))
            {
                found = true;
                break;
            }
        }
        BUSTER_TEST(arguments, found);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_gpr_cases) == 45);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_gpr_boundary_cases) == 45);
    for (u32 fcvt_gpr_index = 0; fcvt_gpr_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_gpr_cases);
         fcvt_gpr_index += 1)
    {
        AssemblyA64DirectSIMDFcvtGprCase fcvt_gpr_case = assembly_a64_direct_simd_fcvt_gpr_cases[fcvt_gpr_index];
        bool half_precision = assembly_test_source_has_half_precision(fcvt_gpr_case.source);
        Target positive_target = half_precision ? aarch64_fp16_scalar_only : aarch64_fp_only;
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcvt_gpr_case.source, (AssemblyEncodeOptions){.target = positive_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcvt_gpr_case.bytes, 4));

        u32 semantic_row_index = UINT32_MAX;
        BusterA64DirectSIMDRowInfo semantic_row = {0};
        BusterA64SemanticForm semantic_form = {0};
        BusterA64SemanticOperand gpr_operand = {0};
        BusterA64SemanticOperand simd_operand = {0};
        bool semantic_shape = buster_a64_direct_simd_find_source_digest(fcvt_gpr_case.source_digest, &semantic_row_index) &&
                              buster_a64_direct_simd_row(semantic_row_index, &semantic_row) &&
                              buster_a64_semantic_form(semantic_row.semantic_form_id, &semantic_form) &&
                              semantic_form.operand_count == 2 &&
                              buster_a64_semantic_operand(semantic_form.operand_first, &gpr_operand) &&
                              buster_a64_semantic_operand(semantic_form.operand_first + 1, &simd_operand);
        bool x_register = assembly_test_source_has_x_register(fcvt_gpr_case.source);
        char8 source_width = fcvt_gpr_case.source.pointer[fcvt_gpr_case.source.length - 3];
        BUSTER_TEST(arguments, semantic_shape && gpr_operand.kind == BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER &&
                                   (gpr_operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) == (x_register ? 0 : BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) &&
                                   (gpr_operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64) == (x_register ? BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64 : 0) &&
                                   (gpr_operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0 &&
                                   simd_operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER &&
                                   (simd_operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0 &&
                                   (simd_operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) == 0 &&
                                   (simd_operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_H16) ==
                                       (source_width == 'h' ? BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_H16 : 0) &&
                                   (simd_operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_S32) ==
                                       (source_width == 's' ? BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_S32 : 0) &&
                                   (simd_operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_D64) ==
                                       (source_width == 'd' ? BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_D64 : 0));

        bool found_spelling = false;
        for (u32 spelling_index = 0; spelling_index < assembly_test_aarch64_direct_simd_spelling_count();
             spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) &&
                spelling.source_digest == fcvt_gpr_case.source_digest)
            {
                bool requirement_ok = spelling.requirement ==
                                      (half_precision ? BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16
                                                       : BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP);
                found_spelling = string_equal(spelling.semantic_id, fcvt_gpr_case.semantic_id) &&
                                 spelling.operand_count == 2 &&
                                 spelling.arrangements[0] == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID &&
                                 spelling.arrangements[1] ==
                                     (half_precision ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H
                                                      : fcvt_gpr_case.source.pointer[fcvt_gpr_case.source.length - 3] == 'd'
                                                            ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D
                                                            : BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S) &&
                                 requirement_ok;
                break;
            }
        }
        BUSTER_TEST(arguments, found_spelling);

        AssemblyEncodeResult no_fp = assembly_encode(
            arguments->arena, fcvt_gpr_case.source, (AssemblyEncodeOptions){.target = aarch64_no_fp});
        BUSTER_TEST(arguments, no_fp.diagnostic_count == 1 && no_fp.bytes.length == 0 &&
                                   no_fp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        if (half_precision)
        {
            AssemblyEncodeResult no_full = assembly_encode(
                arguments->arena, fcvt_gpr_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_no_full});
            BUSTER_TEST(arguments, no_full.diagnostic_count == 1 && no_full.bytes.length == 0 &&
                                       no_full.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
            AssemblyEncodeResult invalid_features = assembly_encode(
                arguments->arena, fcvt_gpr_case.source, (AssemblyEncodeOptions){.target = aarch64_invalid_fullfp16});
            BUSTER_TEST(arguments, invalid_features.bytes.length == 0 && invalid_features.diagnostic_count != 0);
        }
        else
        {
            AssemblyEncodeResult no_full = assembly_encode(
                arguments->arena, fcvt_gpr_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_no_full});
            BUSTER_TEST(arguments, no_full.diagnostic_count == 0 && assembly_test_bytes_equal(no_full.bytes, fcvt_gpr_case.bytes, 4));
        }
    }
    for (u32 boundary_index = 0; boundary_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_gpr_boundary_cases);
         boundary_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase boundary_case = assembly_a64_direct_simd_fcvt_gpr_boundary_cases[boundary_index];
        Target positive_target = assembly_test_source_has_half_precision(boundary_case.source) ? aarch64_fp16_scalar_only
                                                                                               : aarch64_fp_only;
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, boundary_case.source, (AssemblyEncodeOptions){.target = positive_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, boundary_case.bytes, 4));
    }
    for (u32 zr_index = 0; zr_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_gpr_zr_cases); zr_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase zr_case = assembly_a64_direct_simd_fcvt_gpr_zr_cases[zr_index];
        Target positive_target = assembly_test_source_has_half_precision(zr_case.source) ? aarch64_fp16_scalar_only
                                                                                         : aarch64_fp_only;
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, zr_case.source, (AssemblyEncodeOptions){.target = positive_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, zr_case.bytes, 4));
    }
    static String8 const malformed_fcvt_gpr_cases[] = {
        S8_INITIALIZER("fcvtas wsp, d0\n"), S8_INITIALIZER("fcvtas sp, d0\n"), S8_INITIALIZER("fcvtas w32, d0\n"),
        S8_INITIALIZER("fcvtas w0, d32\n"), S8_INITIALIZER("fcvtas w0, v1.2s\n"), S8_INITIALIZER("fcvtas w0, s1[0]\n"),
        S8_INITIALIZER("fcvtas w0, w1\n"), S8_INITIALIZER("fcvtas w0\n"), S8_INITIALIZER("fcvtas w0, d0, d1\n"),
        S8_INITIALIZER("fcvtas q0, q1\n"), S8_INITIALIZER("fcvtas w0, d0.1d\n"), S8_INITIALIZER("fcvtas w0, d0, #0\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_fcvt_gpr_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_fcvt_gpr_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_fp16_both});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp16_cases) == 133);
    for (u32 fp16_index = 0; fp16_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp16_cases); fp16_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fp16_case = assembly_a64_direct_simd_fp16_cases[fp16_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fp16_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_both});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fp16_case.bytes, 4));
    }
    AssemblyEncodeResult fp16_upper_boundary = assembly_encode(
        arguments->arena, S8("FABS V31.8H, V30.8H\n"), (AssemblyEncodeOptions){.target = aarch64_fp16_both});
    BUSTER_TEST(arguments, fp16_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(fp16_upper_boundary.bytes, (u8 const[]){0xdf, 0xfb, 0xf8, 0x4e}, 4));
    AssemblyEncodeResult fp16_scalar_boundary = assembly_encode(
        arguments->arena, S8("FADDP H31, V30.2H\n"), (AssemblyEncodeOptions){.target = aarch64_fp16_both});
    BUSTER_TEST(arguments, fp16_scalar_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(fp16_scalar_boundary.bytes, (u8 const[]){0xdf, 0xdb, 0x30, 0x5e}, 4));
    static String8 const malformed_fp16_cases[] = {
        S8_INITIALIZER("fabs v0.8h, v1.8s\n"),
        S8_INITIALIZER("fabs q0, q1\n"),
        S8_INITIALIZER("fabs v0.8h\n"),
        S8_INITIALIZER("fabs v0.8h, v1.8h, v2.8h\n"),
        S8_INITIALIZER("fabs v32.8h, v1.8h\n"),
        S8_INITIALIZER("fabs v0.8h, h1\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_fp16_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_fp16_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_fp16_both});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    for (u32 fp16_index = 0; fp16_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp16_cases); fp16_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fp16_case = assembly_a64_direct_simd_fp16_cases[fp16_index];
        AssemblyEncodeResult no_full = assembly_encode(
            arguments->arena, fp16_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_no_full});
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, fp16_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_no_neon});
        AssemblyEncodeResult no_both = assembly_encode(
            arguments->arena, fp16_case.source, (AssemblyEncodeOptions){.target = aarch64_fp16_no_both});
        BUSTER_TEST(arguments, no_full.diagnostic_count == 1 && no_full.bytes.length == 0 &&
                                   no_full.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, no_both.diagnostic_count == 1 && no_both.bytes.length == 0 &&
                                   no_both.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fhm_cases) == 8 &&
                               BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fhm_boundary_cases) == 4);
    for (u32 fhm_index = 0; fhm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fhm_cases); fhm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fhm_case = assembly_a64_direct_simd_fhm_cases[fhm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fhm_case.source, (AssemblyEncodeOptions){.target = aarch64_fhm_both});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fhm_case.bytes, 4));
        AssemblyEncodeResult without_feature = assembly_encode(
            arguments->arena, fhm_case.source, (AssemblyEncodeOptions){.target = aarch64_fhm_no_fp16fml});
        BUSTER_TEST(arguments, without_feature.diagnostic_count == 1 && without_feature.bytes.length == 0 &&
                                   without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 fhm_index = 0; fhm_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fhm_boundary_cases); fhm_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fhm_case = assembly_a64_direct_simd_fhm_boundary_cases[fhm_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fhm_case.source, (AssemblyEncodeOptions){.target = aarch64_fhm_both});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fhm_case.bytes, 4));
    }
    static String8 const malformed_aarch64_fhm[] = {
        S8_INITIALIZER("fmlal v0.4s, v1.2h, v2.2h\n"),
        S8_INITIALIZER("fmlal v0.2s, v1.4h, v2.2h\n"),
        S8_INITIALIZER("fmlal v0.2s, v1.2h, v2.4h\n"),
        S8_INITIALIZER("fmlal v0.8s, v1.8h, v2.8h\n"),
        S8_INITIALIZER("fmlal v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fmlal v0.2s, v1.2h\n"),
        S8_INITIALIZER("fmlal v0.2s, v1.2h, v2.2h, v3.2h\n"),
        S8_INITIALIZER("fmlal v32.2s, v1.2h, v2.2h\n"),
        S8_INITIALIZER("fmlal s0, s1, s2\n"),
        S8_INITIALIZER("fmlal q0, q1, q2\n"),
        S8_INITIALIZER("fmlal v0.2s, v1.2h, v2.2h[0]\n"),
        S8_INITIALIZER("fmlal {v0.2s, v1.2s}, v2.2h, v3.2h\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_fhm); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_fhm[malformed_index], (AssemblyEncodeOptions){.target = aarch64_fhm_both});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp_scalar_cases) == 12);
    for (u32 fp_scalar_index = 0; fp_scalar_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp_scalar_cases);
         fp_scalar_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fp_scalar_case = assembly_a64_direct_simd_fp_scalar_cases[fp_scalar_index];
        bool half_precision = fp_scalar_case.source.pointer[5] == 'h' || fp_scalar_case.source.pointer[5] == 'H' ||
                              (fp_scalar_case.source.length > 6 &&
                               (fp_scalar_case.source.pointer[6] == 'h' || fp_scalar_case.source.pointer[6] == 'H'));
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fp_scalar_case.source,
            (AssemblyEncodeOptions){.target = half_precision ? aarch64_fp16_scalar_only : aarch64_fp_only});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fp_scalar_case.bytes, 4));
    }
    for (u32 fp_scalar_index = 0; fp_scalar_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp_scalar_boundary_cases);
         fp_scalar_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fp_scalar_case = assembly_a64_direct_simd_fp_scalar_boundary_cases[fp_scalar_index];
        bool half_precision = fp_scalar_case.source.pointer[5] == 'h' || fp_scalar_case.source.pointer[5] == 'H' ||
                              (fp_scalar_case.source.length > 6 &&
                               (fp_scalar_case.source.pointer[6] == 'h' || fp_scalar_case.source.pointer[6] == 'H'));
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fp_scalar_case.source,
            (AssemblyEncodeOptions){.target = half_precision ? aarch64_fp16_scalar_only : aarch64_fp_only});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fp_scalar_case.bytes, 4));
    }
    for (u32 fp_scalar_index = 0; fp_scalar_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fp_scalar_cases);
         fp_scalar_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fp_scalar_case = assembly_a64_direct_simd_fp_scalar_cases[fp_scalar_index];
        bool half_precision = fp_scalar_case.source.pointer[5] == 'h' ||
                              (fp_scalar_case.source.length > 6 && fp_scalar_case.source.pointer[6] == 'h');
        AssemblyEncodeResult no_feature = assembly_encode(
            arguments->arena, fp_scalar_case.source,
            (AssemblyEncodeOptions){.target = half_precision ? aarch64_fp16_no_full : aarch64_no_fp});
        BUSTER_TEST(arguments, no_feature.diagnostic_count == 1 && no_feature.bytes.length == 0 &&
                                   no_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    static String8 const malformed_fp_scalar_cases[] = {
        S8_INITIALIZER("fabs v0.2s, d1\n"), S8_INITIALIZER("fabs d0, s1\n"),
        S8_INITIALIZER("fabs d0\n"), S8_INITIALIZER("fabs d0, d1, d2\n"), S8_INITIALIZER("fabs d32, d1\n"),
        S8_INITIALIZER("fadd s0, s1, d2\n"), S8_INITIALIZER("fadd s0, s1\n"),
        S8_INITIALIZER("fadd s0, s1, s2, s3\n"), S8_INITIALIZER("fadd s32, s1, s2\n"),
        S8_INITIALIZER("fcmp q0, q1\n"), S8_INITIALIZER("fcmp d0\n"), S8_INITIALIZER("fcmp d0, d1, d2\n"),
        S8_INITIALIZER("fcmp d0, x1\n"), S8_INITIALIZER("fcmpe h0, s1\n"), S8_INITIALIZER("fcmpe h0, h1[0]\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_fp_scalar_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_fp_scalar_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_fp16_both});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcsel_cases) == 54);
    for (u32 fcsel_index = 0; fcsel_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcsel_cases); fcsel_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fcsel_case = assembly_a64_direct_simd_fcsel_cases[fcsel_index];
        bool half_precision = fcsel_case.source.pointer[6] == 'h';
        Target scalar_target = half_precision ? aarch64_fp16_scalar_only : aarch64_fp_only;
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcsel_case.source, (AssemblyEncodeOptions){.target = scalar_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcsel_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, fcsel_case.source,
            (AssemblyEncodeOptions){.target = half_precision ? aarch64_fp16_no_neon : aarch64_fp_only});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 0 && assembly_test_bytes_equal(no_neon.bytes, fcsel_case.bytes, 4));
        AssemblyEncodeResult no_fp = assembly_encode(
            arguments->arena, fcsel_case.source, (AssemblyEncodeOptions){.target = aarch64_no_fp});
        BUSTER_TEST(arguments, no_fp.diagnostic_count == 1 && no_fp.bytes.length == 0 &&
                                   no_fp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        if (half_precision)
        {
            AssemblyEncodeResult no_full = assembly_encode(
                arguments->arena, fcsel_case.source, (AssemblyEncodeOptions){.target = aarch64_fp_only});
            BUSTER_TEST(arguments, no_full.diagnostic_count == 1 && no_full.bytes.length == 0 &&
                                       no_full.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
        AssemblyEncodeResult invalid_target = assembly_encode(
            arguments->arena, fcsel_case.source, (AssemblyEncodeOptions){.target = aarch64_invalid_fullfp16});
        BUSTER_TEST(arguments, invalid_target.diagnostic_count == 1 && invalid_target.bytes.length == 0 &&
                                   invalid_target.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    static AssemblyA64DirectSIMDEncodingCase const fcsel_boundary_cases[] = {
        {S8_INITIALIZER("FCSEL D31, D30, D29, NV\n"), {0xdf, 0xff, 0x7d, 0x1e}},
        {S8_INITIALIZER("FCSEL H31, H30, H29, AL\n"), {0xdf, 0xef, 0xfd, 0x1e}},
        {S8_INITIALIZER("FCSEL S31, S30, S29, GE\n"), {0xdf, 0xaf, 0x3d, 0x1e}},
    };
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(fcsel_boundary_cases) == 3);
    for (u32 fcsel_index = 0; fcsel_index < BUSTER_ARRAY_LENGTH(fcsel_boundary_cases); fcsel_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fcsel_case = fcsel_boundary_cases[fcsel_index];
        bool half_precision = fcsel_case.source.pointer[6] == 'H';
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcsel_case.source,
            (AssemblyEncodeOptions){.target = half_precision ? aarch64_fp16_scalar_only : aarch64_fp_only});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcsel_case.bytes, 4));
    }
    static String8 const malformed_fcsel_cases[] = {
        S8_INITIALIZER("fcsel d0, d1, d2\n"),
        S8_INITIALIZER("fcsel d0, d1, d2, eq, d3\n"),
        S8_INITIALIZER("fcsel s0, d1, s2, eq\n"),
        S8_INITIALIZER("fcsel v0.2s, v1.2s, v2.2s, eq\n"),
        S8_INITIALIZER("fcsel d0, d1, d2[0], eq\n"),
        S8_INITIALIZER("fcsel d0, x1, d2, eq\n"),
        S8_INITIALIZER("fcsel d32, d1, d2, eq\n"),
        S8_INITIALIZER("fcsel d0, d1, d2, foo\n"),
        S8_INITIALIZER("fcsel d0, d1, d2, #0\n"),
        S8_INITIALIZER("fcsel q0, q1, q2, eq\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_fcsel_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_fcsel_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_fp16_both});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_same_cases) == 50);
    for (u32 scalar_same_index = 0; scalar_same_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_scalar_same_cases); scalar_same_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase scalar_same_case = assembly_a64_direct_simd_scalar_same_cases[scalar_same_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, scalar_same_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, scalar_same_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, scalar_same_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult scalar_same_upper_boundary = assembly_encode(
        arguments->arena,
        S8("FABD S31, S30, S29\nFABD D31, D30, D29\nFACGE S31, S30, S29\nFACGE D31, D30, D29\n"
           "FACGT S31, S30, S29\nFACGT D31, D30, D29\nFCMEQ S31, S30, S29\nFCMEQ D31, D30, D29\n"
           "FCMGE S31, S30, S29\nFCMGE D31, D30, D29\nFCMGT S31, S30, S29\nFCMGT D31, D30, D29\n"
           "FMULX S31, S30, S29\nFMULX D31, D30, D29\nFRECPS S31, S30, S29\nFRECPS D31, D30, D29\n"
           "FRSQRTS S31, S30, S29\nFRSQRTS D31, D30, D29\nSQADD B31, B30, B29\nSQADD H31, H30, H29\n"
           "SQADD S31, S30, S29\nSQADD D31, D30, D29\nSQRSHL B31, B30, B29\nSQRSHL H31, H30, H29\n"
           "SQRSHL S31, S30, S29\nSQRSHL D31, D30, D29\nSQSHL B31, B30, B29\nSQSHL H31, H30, H29\n"
           "SQSHL S31, S30, S29\nSQSHL D31, D30, D29\nSQSUB B31, B30, B29\nSQSUB H31, H30, H29\n"
           "SQSUB S31, S30, S29\nSQSUB D31, D30, D29\nUQADD B31, B30, B29\nUQADD H31, H30, H29\n"
           "UQADD S31, S30, S29\nUQADD D31, D30, D29\nUQRSHL B31, B30, B29\nUQRSHL H31, H30, H29\n"
           "UQRSHL S31, S30, S29\nUQRSHL D31, D30, D29\nUQSHL B31, B30, B29\nUQSHL H31, H30, H29\n"
           "UQSHL S31, S30, S29\nUQSHL D31, D30, D29\nUQSUB B31, B30, B29\nUQSUB H31, H30, H29\n"
           "UQSUB S31, S30, S29\nUQSUB D31, D30, D29\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, scalar_same_upper_boundary.diagnostic_count == 0 && scalar_same_upper_boundary.bytes.length == 200);
    static String8 const malformed_scalar_same_cases[] = {
        S8_INITIALIZER("fabd b0, b1, b2\n"), S8_INITIALIZER("fabd s0, d1, s2\n"),
        S8_INITIALIZER("fabd s0, s1\n"), S8_INITIALIZER("fabd s0, s1, s2, s3\n"),
        S8_INITIALIZER("sqadd v0.1d, v1.1d, v2.1d\n"), S8_INITIALIZER("sqadd b0, h1, b2\n"),
        S8_INITIALIZER("sqadd b0, b1\n"), S8_INITIALIZER("sqadd b32, b1, b2\n"),
        S8_INITIALIZER("uqsub q0, q1, q2\n"), S8_INITIALIZER("frecps s0, s1, v2.s[0]\n"),
        S8_INITIALIZER("frsqrts d0, d1, d2, d3\n"), S8_INITIALIZER("fcmgt s0, w1, s2\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_scalar_same_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_scalar_same_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_scalar_cases) == 20);
    for (u32 fcvt_index = 0; fcvt_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_fcvt_scalar_cases); fcvt_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase fcvt_case = assembly_a64_direct_simd_fcvt_scalar_cases[fcvt_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, fcvt_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, fcvt_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult fcvt_upper_boundary = assembly_encode(
        arguments->arena,
        S8("FCVTAS S31, S30\nFCVTAS D31, D30\nFCVTAU S31, S30\nFCVTAU D31, D30\n"
           "FCVTMS S31, S30\nFCVTMS D31, D30\nFCVTMU S31, S30\nFCVTMU D31, D30\n"
           "FCVTNS S31, S30\nFCVTNS D31, D30\nFCVTNU S31, S30\nFCVTNU D31, D30\n"
           "FCVTPS S31, S30\nFCVTPS D31, D30\nFCVTPU S31, S30\nFCVTPU D31, D30\n"
           "FCVTZS S31, S30\nFCVTZS D31, D30\nFCVTZU S31, S30\nFCVTZU D31, D30\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, fcvt_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(fcvt_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0xcb, 0x21, 0x5e,
                                                                      0xdf, 0xcb, 0x61, 0x5e,
                                                                      0xdf, 0xcb, 0x21, 0x7e,
                                                                      0xdf, 0xcb, 0x61, 0x7e,
                                                                      0xdf, 0xbb, 0x21, 0x5e,
                                                                      0xdf, 0xbb, 0x61, 0x5e,
                                                                      0xdf, 0xbb, 0x21, 0x7e,
                                                                      0xdf, 0xbb, 0x61, 0x7e,
                                                                      0xdf, 0xab, 0x21, 0x5e,
                                                                      0xdf, 0xab, 0x61, 0x5e,
                                                                      0xdf, 0xab, 0x21, 0x7e,
                                                                      0xdf, 0xab, 0x61, 0x7e,
                                                                      0xdf, 0xab, 0xa1, 0x5e,
                                                                      0xdf, 0xab, 0xe1, 0x5e,
                                                                      0xdf, 0xab, 0xa1, 0x7e,
                                                                      0xdf, 0xab, 0xe1, 0x7e,
                                                                      0xdf, 0xbb, 0xa1, 0x5e,
                                                                      0xdf, 0xbb, 0xe1, 0x5e,
                                                                      0xdf, 0xbb, 0xa1, 0x7e,
                                                                      0xdf, 0xbb, 0xe1, 0x7e},
                                                         80));
    static String8 const invalid_fcvt_scalar_cases[] = {
        S8_INITIALIZER("fcvtas h0, s1\n"),
        S8_INITIALIZER("fcvtas b0, b1\n"),
        S8_INITIALIZER("fcvtas q0, q1\n"),
        S8_INITIALIZER("fcvtas s0, d1\n"),
        S8_INITIALIZER("fcvtas s0\n"),
        S8_INITIALIZER("fcvtas s0, s1, s2\n"),
        S8_INITIALIZER("fcvtas s32, s1\n"),
        S8_INITIALIZER("fcvtas s0, v1.s[0]\n"),
        S8_INITIALIZER("fcvtps v0.2s, v1.4s\n"),
        S8_INITIALIZER("fcvtps w0, w1\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_fcvt_scalar_cases); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_fcvt = assembly_encode(
            arguments->arena, invalid_fcvt_scalar_cases[invalid_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_fcvt.diagnostic_count == 1 && invalid_fcvt.bytes.length == 0 &&
                                   invalid_fcvt.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_widening_cases) == 48);
    for (u32 widening_index = 0; widening_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_widening_cases); widening_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase widening_case = assembly_a64_direct_simd_widening_cases[widening_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, widening_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, widening_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, widening_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_reduction_cases) == 35);
    for (u32 reduction_index = 0; reduction_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_reduction_cases); reduction_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase reduction_case = assembly_a64_direct_simd_reduction_cases[reduction_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, reduction_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, reduction_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, reduction_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_addp_cases) == 7);
    for (u32 addp_index = 0; addp_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_addp_cases); addp_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase addp_case = assembly_a64_direct_simd_addp_cases[addp_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, addp_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, addp_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, addp_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult addp_upper_boundary = assembly_encode(
        arguments->arena,
        S8("ADDP V31.8B, V30.8B, V29.8B\n"
           "ADDP V31.16B, V30.16B, V29.16B\n"
           "ADDP V31.4H, V30.4H, V29.4H\n"
           "ADDP V31.8H, V30.8H, V29.8H\n"
           "ADDP V31.2S, V30.2S, V29.2S\n"
           "ADDP V31.4S, V30.4S, V29.4S\n"
           "ADDP V31.2D, V30.2D, V29.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, addp_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(addp_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0xbf, 0x3d, 0x0e,
                                                                      0xdf, 0xbf, 0x3d, 0x4e,
                                                                      0xdf, 0xbf, 0x7d, 0x0e,
                                                                      0xdf, 0xbf, 0x7d, 0x4e,
                                                                      0xdf, 0xbf, 0xbd, 0x0e,
                                                                      0xdf, 0xbf, 0xbd, 0x4e,
                                                                      0xdf, 0xbf, 0xfd, 0x4e},
                                                         28));
    AssemblyEncodeResult addp_pair_regression = assembly_encode(
        arguments->arena, S8("addp d0, v1.2d\nADDP D31, V30.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, addp_pair_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(addp_pair_regression.bytes,
                                                         (u8 const[]){0x20, 0xb8, 0xf1, 0x5e,
                                                                      0xdf, 0xbb, 0xf1, 0x5e},
                                                         8));
    static String8 const invalid_addp_cases[] = {
        S8_INITIALIZER("addp v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("addp v0.8b, v1.16b, v2.8b\n"),
        S8_INITIALIZER("addp v0.8b, v1.8b\n"),
        S8_INITIALIZER("addp v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("addp q0, q1, q2\n"),
        S8_INITIALIZER("addp v32.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("addp v0.2d, v1.2d, d2\n"),
        S8_INITIALIZER("addp d0, d1, d2\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_addp_cases); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_addp = assembly_encode(
            arguments->arena, invalid_addp_cases[invalid_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_addp.diagnostic_count == 1 && invalid_addp.bytes.length == 0 &&
                                   invalid_addp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_pair_fp_cases) == 10);
    for (u32 pair_fp_index = 0; pair_fp_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_pair_fp_cases); pair_fp_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase pair_fp_case = assembly_a64_direct_simd_pair_fp_cases[pair_fp_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, pair_fp_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, pair_fp_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, pair_fp_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult pair_fp_upper_boundary = assembly_encode(
        arguments->arena,
        S8("FADDP S31, V30.2S\nFADDP D31, V30.2D\n"
           "FMAXNMP S31, V30.2S\nFMAXNMP D31, V30.2D\n"
           "FMAXP S31, V30.2S\nFMAXP D31, V30.2D\n"
           "FMINNMP S31, V30.2S\nFMINNMP D31, V30.2D\n"
           "FMINP S31, V30.2S\nFMINP D31, V30.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, pair_fp_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(pair_fp_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0xdb, 0x30, 0x7e,
                                                                      0xdf, 0xdb, 0x70, 0x7e,
                                                                      0xdf, 0xcb, 0x30, 0x7e,
                                                                      0xdf, 0xcb, 0x70, 0x7e,
                                                                      0xdf, 0xfb, 0x30, 0x7e,
                                                                      0xdf, 0xfb, 0x70, 0x7e,
                                                                      0xdf, 0xcb, 0xb0, 0x7e,
                                                                      0xdf, 0xcb, 0xf0, 0x7e,
                                                                      0xdf, 0xfb, 0xb0, 0x7e,
                                                                      0xdf, 0xfb, 0xf0, 0x7e},
                                                         40));
    AssemblyEncodeResult pair_fp_h_regression = assembly_encode(
        arguments->arena,
        S8("FADDP H31, V30.2H\nFMAXNMP H31, V30.2H\nFMAXP H31, V30.2H\n"
           "FMINNMP H31, V30.2H\nFMINP H31, V30.2H\n"),
        (AssemblyEncodeOptions){.target = aarch64_fp16_both});
    BUSTER_TEST(arguments, pair_fp_h_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(pair_fp_h_regression.bytes,
                                                         (u8 const[]){0xdf, 0xdb, 0x30, 0x5e,
                                                                      0xdf, 0xcb, 0x30, 0x5e,
                                                                      0xdf, 0xfb, 0x30, 0x5e,
                                                                      0xdf, 0xcb, 0xb0, 0x5e,
                                                                      0xdf, 0xfb, 0xb0, 0x5e},
                                                         20));
    static String8 const malformed_pair_fp_cases[] = {
        S8_INITIALIZER("faddp s0, v1.2h\n"),
        S8_INITIALIZER("faddp d0, v1.2s\n"),
        S8_INITIALIZER("faddp s0, v1.4s\n"),
        S8_INITIALIZER("faddp s0\n"),
        S8_INITIALIZER("faddp s0, v1.2s, v2.2s\n"),
        S8_INITIALIZER("faddp s32, v1.2s\n"),
        S8_INITIALIZER("faddp s0, v1.s[0]\n"),
        S8_INITIALIZER("faddp q0, q1\n"),
        S8_INITIALIZER("faddp s0, x1\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_pair_fp_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_pair_fp_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult pair_fp_h_without_full = assembly_encode(
        arguments->arena, S8("faddp h0, v1.2h\n"), (AssemblyEncodeOptions){.target = aarch64_fp16_no_full});
    BUSTER_TEST(arguments, pair_fp_h_without_full.diagnostic_count == 1 && pair_fp_h_without_full.bytes.length == 0 &&
                               pair_fp_h_without_full.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_unary_cases) == 10);
    for (u32 unary_index = 0; unary_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_unary_cases); unary_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase unary_case = assembly_a64_direct_simd_unary_cases[unary_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, unary_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, unary_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, unary_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult unary_upper_boundary = assembly_encode(
        arguments->arena,
        S8("REV64 V31.4S, V30.4S\nURECPE V31.4S, V30.4S\nURSQRTE V31.4S, V30.4S\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, unary_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(unary_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0x0b, 0xa0, 0x4e,
                                                                      0xdf, 0xcb, 0xa1, 0x4e,
                                                                      0xdf, 0xcb, 0xa1, 0x6e},
                                                         12));
    static String8 const invalid_unary_cases[] = {
        S8_INITIALIZER("rev64 v0.2d, v1.2d\n"),
        S8_INITIALIZER("rev64 v0.8b, v1.16b\n"),
        S8_INITIALIZER("rev64 v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("urecpe v0.2d, v1.2d\n"),
        S8_INITIALIZER("urecpe v0.8b, v1.8b\n"),
        S8_INITIALIZER("urecpe q0, q1\n"),
        S8_INITIALIZER("ursqrte v0.2d, v1.2d\n"),
        S8_INITIALIZER("ursqrte v32.4s, v1.4s\n"),
        S8_INITIALIZER("ursqrte v0.4s, v1.s[0]\n"),
        S8_INITIALIZER("ursqrte v0.4s\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_unary_cases); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_unary = assembly_encode(
            arguments->arena, invalid_unary_cases[invalid_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_unary.diagnostic_count == 1 && invalid_unary.bytes.length == 0 &&
                                   invalid_unary.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult reduction_upper = assembly_encode(
        arguments->arena,
        S8("ADDV B31, V30.16B\nSADDLV D31, V30.4S\nUADDLV D31, V30.4S\n"
           "SMAXV S31, V30.4S\nSMINV S31, V30.4S\nUMAXV S31, V30.4S\nUMINV S31, V30.4S\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, reduction_upper.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(reduction_upper.bytes,
                                                         (u8 const[]){0xdf, 0xbb, 0x31, 0x4e,
                                                                      0xdf, 0x3b, 0xb0, 0x4e,
                                                                      0xdf, 0x3b, 0xb0, 0x6e,
                                                                      0xdf, 0xab, 0xb0, 0x4e,
                                                                      0xdf, 0xab, 0xb1, 0x4e,
                                                                      0xdf, 0xab, 0xb0, 0x6e,
                                                                      0xdf, 0xab, 0xb1, 0x6e},
                                                         28));
    static String8 const invalid_reduction_cases[] = {
        S8_INITIALIZER("addv s0, v1.2s\n"), S8_INITIALIZER("addv d0, v1.2d\n"),
        S8_INITIALIZER("saddlv d0, v1.2s\n"), S8_INITIALIZER("saddlv q0, v1.8b\n"),
        S8_INITIALIZER("uaddlv d0, v1.2s\n"), S8_INITIALIZER("smaxv s0, v1.2s\n"),
        S8_INITIALIZER("sminv s0, v1.2s\n"), S8_INITIALIZER("umaxv s0, v1.2s\n"),
        S8_INITIALIZER("uminv s0, v1.2s\n"), S8_INITIALIZER("addv b0, v1.8h\n"),
        S8_INITIALIZER("addv b0\n"), S8_INITIALIZER("addv b32, v1.8b\n"),
        S8_INITIALIZER("addv b0, x1.8b\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_reduction_cases); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_reduction = assembly_encode(
            arguments->arena, invalid_reduction_cases[invalid_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_reduction.diagnostic_count == 1 && invalid_reduction.bytes.length == 0 &&
                                   invalid_reduction.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_shift_cases) == 28);
    for (u32 shift_index = 0; shift_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_shift_cases); shift_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase shift_case = assembly_a64_direct_simd_shift_cases[shift_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, shift_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, shift_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, shift_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult shift_upper = assembly_encode(
        arguments->arena,
        S8("SRSHL V31.2D, V30.2D, V29.2D\nSSHL V31.2D, V30.2D, V29.2D\n"
           "URSHL V31.2D, V30.2D, V29.2D\nUSHL V31.2D, V30.2D, V29.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, shift_upper.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(shift_upper.bytes,
                                                         (u8 const[]){0xdf, 0x57, 0xfd, 0x4e,
                                                                      0xdf, 0x47, 0xfd, 0x4e,
                                                                      0xdf, 0x57, 0xfd, 0x6e,
                                                                      0xdf, 0x47, 0xfd, 0x6e},
                                                         16));
    AssemblyEncodeResult shift_scalar_regression = assembly_encode(
        arguments->arena,
        S8("SRSHL D31, D30, D29\nSSHL D31, D30, D29\nURSHL D31, D30, D29\nUSHL D31, D30, D29\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, shift_scalar_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(shift_scalar_regression.bytes,
                                                         (u8 const[]){0xdf, 0x57, 0xfd, 0x5e,
                                                                      0xdf, 0x47, 0xfd, 0x5e,
                                                                      0xdf, 0x57, 0xfd, 0x7e,
                                                                      0xdf, 0x47, 0xfd, 0x7e},
                                                         16));
    static String8 const invalid_shift_cases[] = {
        S8_INITIALIZER("srshl v0.8b, v1.16b, v2.8b\n"), S8_INITIALIZER("sshl v0.4h, v1.4h\n"),
        S8_INITIALIZER("urshl v0.2d, v1.2d, v2.2d, v3.2d\n"), S8_INITIALIZER("ushl v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("srshl d0, d1, v2.2d\n"), S8_INITIALIZER("sshl v0.2s, v1.2s, x2\n"),
        S8_INITIALIZER("urshl v0.4s, v1.4s, v2.s[0]\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_shift_cases); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_shift = assembly_encode(
            arguments->arena, invalid_shift_cases[invalid_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_shift.diagnostic_count == 1 && invalid_shift.bytes.length == 0 &&
                                   invalid_shift.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_multiply_cases) == 32);
    for (u32 multiply_index = 0; multiply_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_multiply_cases); multiply_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase multiply_case = assembly_a64_direct_simd_multiply_cases[multiply_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, multiply_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, multiply_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, multiply_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_bitselect_cases) == 6);
    for (u32 bitselect_index = 0; bitselect_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_bitselect_cases); bitselect_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase bitselect_case = assembly_a64_direct_simd_bitselect_cases[bitselect_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, bitselect_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, bitselect_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, bitselect_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult bitselect_upper_boundary = assembly_encode(
        arguments->arena,
        S8("BIF V31.16B, V30.16B, V29.16B\n"
           "BIT V31.16B, V30.16B, V29.16B\n"
           "BSL V31.16B, V30.16B, V29.16B\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, bitselect_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(bitselect_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0x1f, 0xfd, 0x6e,
                                                                      0xdf, 0x1f, 0xbd, 0x6e,
                                                                      0xdf, 0x1f, 0x7d, 0x6e},
                                                         12));
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_compare_cases) == 42);
    for (u32 compare_index = 0; compare_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_compare_cases); compare_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase compare_case = assembly_a64_direct_simd_compare_cases[compare_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, compare_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, compare_case.bytes, 4));
        AssemblyEncodeResult no_neon = assembly_encode(
            arguments->arena, compare_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon});
        BUSTER_TEST(arguments, no_neon.diagnostic_count == 1 && no_neon.bytes.length == 0 &&
                                   no_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult compare_upper_boundary = assembly_encode(
        arguments->arena,
        S8("CMEQ V31.2D, V30.2D, V29.2D\n"
           "CMGE V31.2D, V30.2D, V29.2D\n"
           "CMGT V31.2D, V30.2D, V29.2D\n"
           "CMHI V31.2D, V30.2D, V29.2D\n"
           "CMHS V31.2D, V30.2D, V29.2D\n"
           "CMTST V31.2D, V30.2D, V29.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, compare_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(compare_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0x8f, 0xfd, 0x6e,
                                                                      0xdf, 0x3f, 0xfd, 0x4e,
                                                                      0xdf, 0x37, 0xfd, 0x4e,
                                                                      0xdf, 0x37, 0xfd, 0x6e,
                                                                      0xdf, 0x3f, 0xfd, 0x6e,
                                                                      0xdf, 0x8f, 0xfd, 0x4e},
                                                         24));
    AssemblyEncodeResult compare_fixed_d_regression = assembly_encode(
        arguments->arena,
        S8("cmeq d0, d1, d2\n"
           "cmge d0, d1, d2\n"
           "cmgt d0, d1, d2\n"
           "cmhi d0, d1, d2\n"
           "cmhs d0, d1, d2\n"
           "cmtst d0, d1, d2\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, compare_fixed_d_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(compare_fixed_d_regression.bytes,
                                                         (u8 const[]){0x20, 0x8c, 0xe2, 0x7e,
                                                                      0x20, 0x3c, 0xe2, 0x5e,
                                                                      0x20, 0x34, 0xe2, 0x5e,
                                                                      0x20, 0x34, 0xe2, 0x7e,
                                                                      0x20, 0x3c, 0xe2, 0x7e,
                                                                      0x20, 0x8c, 0xe2, 0x5e},
                                                         24));
    static String8 const malformed_aarch64_advsimd_compare[] = {
        S8_INITIALIZER("cmeq v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("cmge v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("cmgt v0.4s, v1.2s, v2.4s\n"),
        S8_INITIALIZER("cmhi v0.2d, v1.2d\n"),
        S8_INITIALIZER("cmhs v0.8h, v1.8h, v2.4h\n"),
        S8_INITIALIZER("cmtst v32.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("cmeq q0, q1, q2\n"),
        S8_INITIALIZER("cmge s0, s1, s2\n"),
        S8_INITIALIZER("cmgt v0.8b, v1.b[0], v2.8b\n"),
        S8_INITIALIZER("cmhi v0.8b, v1.8b, v2.b[0]\n"),
        S8_INITIALIZER("cmhs v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("cmtst v0.8b, v1.8b\n"),
        S8_INITIALIZER("cmeq v0.8b, v1.8b, #0\n"),
        S8_INITIALIZER("cmge v0.4s, v1.4s, #0\n"),
        S8_INITIALIZER("cmgt d0, d1, #0\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_advsimd_compare); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_advsimd_compare[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult bitselect_m1_regression = assembly_encode(
        arguments->arena,
        S8("and w0, w1, w2\n"
           "bic w3, w4, w5\n"
           "eor x6, x7, x8\n"
           "orn x9, x10, x11\n"
           "orr w12, w13, w14\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, bitselect_m1_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(bitselect_m1_regression.bytes,
                                                         (u8 const[]){0x20, 0x00, 0x02, 0x0a,
                                                                      0x83, 0x00, 0x25, 0x0a,
                                                                      0xe6, 0x00, 0x08, 0xca,
                                                                      0x49, 0x01, 0x2b, 0xaa,
                                                                      0xac, 0x01, 0x0e, 0x2a},
                                                         20));
    static String8 const malformed_aarch64_advsimd_bitselect[] = {
        S8_INITIALIZER("bif v0.8b, v1.8b, v2.b[0]\n"),
        S8_INITIALIZER("bit v0.8b, v1.8b, v2.b[0]\n"),
        S8_INITIALIZER("bsl v0.8b, v1.8b, v2.b[0]\n"),
        S8_INITIALIZER("bif v0.8h, v1.8h, v2.8h\n"),
        S8_INITIALIZER("bit v0.8b, v1.16b, v2.8b\n"),
        S8_INITIALIZER("bsl v0.8b, v1.8b, v2.4h\n"),
        S8_INITIALIZER("bif v0.8b, v1.8b\n"),
        S8_INITIALIZER("bit v0.8b, v1.8b, v2.8b, v3.8b\n"),
        S8_INITIALIZER("bsl s0, s1, s2\n"),
        S8_INITIALIZER("bif w0, w1, w2\n"),
        S8_INITIALIZER("bit x0, x1, x2\n"),
        S8_INITIALIZER("bsl v32.8b, v1.8b, v2.8b\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_advsimd_bitselect); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_advsimd_bitselect[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult multiply_upper_boundary = assembly_encode(
        arguments->arena,
        S8("MLA V31.4S, V30.4S, V29.4S\n"
           "MLS V31.4S, V30.4S, V29.4S\n"
           "MUL V31.4S, V30.4S, V29.4S\n"
           "PMUL V31.16B, V30.16B, V29.16B\n"
           "SQDMULH V31.4S, V30.4S, V29.4S\n"
           "SQDMULH S31, S30, S29\n"
           "SQRDMULH V31.4S, V30.4S, V29.4S\n"
           "SQRDMULH S31, S30, S29\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, multiply_upper_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(multiply_upper_boundary.bytes,
                                                         (u8 const[]){0xdf, 0x97, 0xbd, 0x4e,
                                                                      0xdf, 0x97, 0xbd, 0x6e,
                                                                      0xdf, 0x9f, 0xbd, 0x4e,
                                                                      0xdf, 0x9f, 0x3d, 0x6e,
                                                                      0xdf, 0xb7, 0xbd, 0x4e,
                                                                      0xdf, 0xb7, 0xbd, 0x5e,
                                                                      0xdf, 0xb7, 0xbd, 0x6e,
                                                                      0xdf, 0xb7, 0xbd, 0x7e},
                                                         32));
    static String8 const malformed_aarch64_advsimd_multiply[] = {
        S8_INITIALIZER("mla v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("mla v0.4s, v1.2s, v2.4s\n"),
        S8_INITIALIZER("mla v0.4s, v1.4s\n"),
        S8_INITIALIZER("mla v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("mls v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("mul v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("mul v0.4s, v1.4s\n"),
        S8_INITIALIZER("pmul v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("sqdmulh v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("sqdmulh v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sqdmulh v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("sqrdmulh v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("sqrdmulh v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sqrdmulh v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("sqdmulh h0, s1, h2\n"),
        S8_INITIALIZER("sqdmulh h0, h1, s2\n"),
        S8_INITIALIZER("sqrdmulh h0, h1\n"),
        S8_INITIALIZER("mla v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("sqrdmulh s32, s1, s2\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_advsimd_multiply); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_advsimd_multiply[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult widening_upper = assembly_encode(
        arguments->arena,
        S8("SABA V31.4S, V30.4S, V29.4S\n"
           "SABD V31.4S, V30.4S, V29.4S\n"
           "SADALP V31.2D, V30.4S\n"
           "SADDLP V31.2D, V30.4S\n"
           "UABA V31.4S, V30.4S, V29.4S\n"
           "UABD V31.4S, V30.4S, V29.4S\n"
           "UADALP V31.2D, V30.4S\n"
           "UADDLP V31.2D, V30.4S\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, widening_upper.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(widening_upper.bytes,
                                                         (u8 const[]){0xdf, 0x7f, 0xbd, 0x4e,
                                                                      0xdf, 0x77, 0xbd, 0x4e,
                                                                      0xdf, 0x6b, 0xa0, 0x4e,
                                                                      0xdf, 0x2b, 0xa0, 0x4e,
                                                                      0xdf, 0x7f, 0xbd, 0x6e,
                                                                      0xdf, 0x77, 0xbd, 0x6e,
                                                                      0xdf, 0x6b, 0xa0, 0x6e,
                                                                      0xdf, 0x2b, 0xa0, 0x6e},
                                                         32));
    static String8 const malformed_widening_cases[] = {
        S8_INITIALIZER("saba v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("saba v0.4s, v1.2s, v2.4s\n"),
        S8_INITIALIZER("sadalp v0.2d, v1.2s\n"),
        S8_INITIALIZER("saddlp v0.2d, v1.4s, v2.4s\n"),
        S8_INITIALIZER("uaba v0.4s, v1.4s\n"),
        S8_INITIALIZER("uaddlp v0.3d, v1.4s\n"),
        S8_INITIALIZER("uabd v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("saba d0, d1, d2\n"),
        S8_INITIALIZER("sadalp d0, d1\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_widening_cases); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_widening_cases[malformed_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult aarch64_advsimd_transform_boundary = assembly_encode(
        arguments->arena,
        S8("abs v31.2d, v30.2d\n"
           "neg v31.2d, v30.2d\n"
           "shadd v31.4s, v30.4s, v29.4s\n"
           "shsub v31.4s, v30.4s, v29.4s\n"
           "cnt v31.16b, v30.16b\n"
           "trn1 v31.2d, v30.2d, v29.2d\n"
           "trn2 v31.2d, v30.2d, v29.2d\n"
           "uzp1 v31.2d, v30.2d, v29.2d\n"
           "uzp2 v31.2d, v30.2d, v29.2d\n"
           "zip1 v31.2d, v30.2d, v29.2d\n"
           "zip2 v31.2d, v30.2d, v29.2d\n"
           "smaxp v31.4s, v30.4s, v29.4s\n"
           "smax v31.4s, v30.4s, v29.4s\n"
           "sminp v31.4s, v30.4s, v29.4s\n"
           "smin v31.4s, v30.4s, v29.4s\n"
           "umaxp v31.4s, v30.4s, v29.4s\n"
           "umax v31.4s, v30.4s, v29.4s\n"
           "uminp v31.4s, v30.4s, v29.4s\n"
           "umin v31.4s, v30.4s, v29.4s\n"
           "sqadd v31.2d, v30.2d, v29.2d\n"
           "sqsub v31.2d, v30.2d, v29.2d\n"
           "uqadd v31.2d, v30.2d, v29.2d\n"
           "uqsub v31.2d, v30.2d, v29.2d\n"
           "sqrshl v31.2d, v30.2d, v29.2d\n"
           "sqshl v31.2d, v30.2d, v29.2d\n"
           "uqrshl v31.2d, v30.2d, v29.2d\n"
           "uqshl v31.2d, v30.2d, v29.2d\n"
           "srhadd v31.4s, v30.4s, v29.4s\n"
           "urhadd v31.4s, v30.4s, v29.4s\n"
           "uhadd v31.4s, v30.4s, v29.4s\n"
           "uhsub v31.4s, v30.4s, v29.4s\n"
           "sqabs v31.2d, v30.2d\n"
           "sqneg v31.2d, v30.2d\n"
           "suqadd v31.2d, v30.2d\n"
           "usqadd v31.2d, v30.2d\n"
           "fabs v31.2d, v30.2d\n"
           "fcvtas v31.2d, v30.2d\n"
           "fcvtau v31.2d, v30.2d\n"
           "fcvtms v31.2d, v30.2d\n"
           "fcvtmu v31.2d, v30.2d\n"
           "fcvtns v31.2d, v30.2d\n"
           "fcvtnu v31.2d, v30.2d\n"
           "fcvtps v31.2d, v30.2d\n"
           "fcvtpu v31.2d, v30.2d\n"
           "fcvtzs v31.2d, v30.2d\n"
           "fcvtzu v31.2d, v30.2d\n"
           "fneg v31.2d, v30.2d\n"
           "frecpe v31.2d, v30.2d\n"
           "frinta v31.2d, v30.2d\n"
           "frinti v31.2d, v30.2d\n"
           "frintm v31.2d, v30.2d\n"
           "frintn v31.2d, v30.2d\n"
           "frintp v31.2d, v30.2d\n"
           "frintx v31.2d, v30.2d\n"
           "frintz v31.2d, v30.2d\n"
           "frsqrte v31.2d, v30.2d\n"
           "fsqrt v31.2d, v30.2d\n"
           "scvtf v31.2d, v30.2d\n"
           "ucvtf v31.2d, v30.2d\n"
           "fabd v31.2d, v30.2d, v29.2d\n"
           "facge v31.2d, v30.2d, v29.2d\n"
           "facgt v31.2d, v30.2d, v29.2d\n"
           "faddp v31.2d, v30.2d, v29.2d\n"
           "fadd v31.2d, v30.2d, v29.2d\n"
           "fcmeq v31.2d, v30.2d, v29.2d\n"
           "fcmge v31.2d, v30.2d, v29.2d\n"
           "fcmgt v31.2d, v30.2d, v29.2d\n"
           "fdiv v31.2d, v30.2d, v29.2d\n"
           "fmaxnmp v31.2d, v30.2d, v29.2d\n"
           "fmaxnm v31.2d, v30.2d, v29.2d\n"
           "fmaxp v31.2d, v30.2d, v29.2d\n"
           "fmax v31.2d, v30.2d, v29.2d\n"
           "fminnmp v31.2d, v30.2d, v29.2d\n"
           "fminnm v31.2d, v30.2d, v29.2d\n"
           "fminp v31.2d, v30.2d, v29.2d\n"
           "fmin v31.2d, v30.2d, v29.2d\n"
           "fmla v31.2d, v30.2d, v29.2d\n"
           "fmls v31.2d, v30.2d, v29.2d\n"
           "fmulx v31.2d, v30.2d, v29.2d\n"
           "fmul v31.2d, v30.2d, v29.2d\n"
           "frecps v31.2d, v30.2d, v29.2d\n"
           "frsqrts v31.2d, v30.2d, v29.2d\n"
           "fsub v31.2d, v30.2d, v29.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_transform_boundary[] = {
        0xdf, 0xbb, 0xe0, 0x4e,
        0xdf, 0xbb, 0xe0, 0x6e,
        0xdf, 0x07, 0xbd, 0x4e,
        0xdf, 0x27, 0xbd, 0x4e,
        0xdf, 0x5b, 0x20, 0x4e,
        0xdf, 0x2b, 0xdd, 0x4e,
        0xdf, 0x6b, 0xdd, 0x4e,
        0xdf, 0x1b, 0xdd, 0x4e,
        0xdf, 0x5b, 0xdd, 0x4e,
        0xdf, 0x3b, 0xdd, 0x4e,
        0xdf, 0x7b, 0xdd, 0x4e,
        0xdf, 0xa7, 0xbd, 0x4e,
        0xdf, 0x67, 0xbd, 0x4e,
        0xdf, 0xaf, 0xbd, 0x4e,
        0xdf, 0x6f, 0xbd, 0x4e,
        0xdf, 0xa7, 0xbd, 0x6e,
        0xdf, 0x67, 0xbd, 0x6e,
        0xdf, 0xaf, 0xbd, 0x6e,
        0xdf, 0x6f, 0xbd, 0x6e,
        0xdf, 0x0f, 0xfd, 0x4e,
        0xdf, 0x2f, 0xfd, 0x4e,
        0xdf, 0x0f, 0xfd, 0x6e,
        0xdf, 0x2f, 0xfd, 0x6e,
        0xdf, 0x5f, 0xfd, 0x4e,
        0xdf, 0x4f, 0xfd, 0x4e,
        0xdf, 0x5f, 0xfd, 0x6e,
        0xdf, 0x4f, 0xfd, 0x6e,
        0xdf, 0x17, 0xbd, 0x4e,
        0xdf, 0x17, 0xbd, 0x6e,
        0xdf, 0x07, 0xbd, 0x6e,
        0xdf, 0x27, 0xbd, 0x6e,
        0xdf, 0x7b, 0xe0, 0x4e,
        0xdf, 0x7b, 0xe0, 0x6e,
        0xdf, 0x3b, 0xe0, 0x4e,
        0xdf, 0x3b, 0xe0, 0x6e,
        0xdf, 0xfb, 0xe0, 0x4e,
        0xdf, 0xcb, 0x61, 0x4e,
        0xdf, 0xcb, 0x61, 0x6e,
        0xdf, 0xbb, 0x61, 0x4e,
        0xdf, 0xbb, 0x61, 0x6e,
        0xdf, 0xab, 0x61, 0x4e,
        0xdf, 0xab, 0x61, 0x6e,
        0xdf, 0xab, 0xe1, 0x4e,
        0xdf, 0xab, 0xe1, 0x6e,
        0xdf, 0xbb, 0xe1, 0x4e,
        0xdf, 0xbb, 0xe1, 0x6e,
        0xdf, 0xfb, 0xe0, 0x6e,
        0xdf, 0xdb, 0xe1, 0x4e,
        0xdf, 0x8b, 0x61, 0x6e,
        0xdf, 0x9b, 0xe1, 0x6e,
        0xdf, 0x9b, 0x61, 0x4e,
        0xdf, 0x8b, 0x61, 0x4e,
        0xdf, 0x8b, 0xe1, 0x4e,
        0xdf, 0x9b, 0x61, 0x6e,
        0xdf, 0x9b, 0xe1, 0x4e,
        0xdf, 0xdb, 0xe1, 0x6e,
        0xdf, 0xfb, 0xe1, 0x6e,
        0xdf, 0xdb, 0x61, 0x4e,
        0xdf, 0xdb, 0x61, 0x6e,
        0xdf, 0xd7, 0xfd, 0x6e,
        0xdf, 0xef, 0x7d, 0x6e,
        0xdf, 0xef, 0xfd, 0x6e,
        0xdf, 0xd7, 0x7d, 0x6e,
        0xdf, 0xd7, 0x7d, 0x4e,
        0xdf, 0xe7, 0x7d, 0x4e,
        0xdf, 0xe7, 0x7d, 0x6e,
        0xdf, 0xe7, 0xfd, 0x6e,
        0xdf, 0xff, 0x7d, 0x6e,
        0xdf, 0xc7, 0x7d, 0x6e,
        0xdf, 0xc7, 0x7d, 0x4e,
        0xdf, 0xf7, 0x7d, 0x6e,
        0xdf, 0xf7, 0x7d, 0x4e,
        0xdf, 0xc7, 0xfd, 0x6e,
        0xdf, 0xc7, 0xfd, 0x4e,
        0xdf, 0xf7, 0xfd, 0x6e,
        0xdf, 0xf7, 0xfd, 0x4e,
        0xdf, 0xcf, 0x7d, 0x4e,
        0xdf, 0xcf, 0xfd, 0x4e,
        0xdf, 0xdf, 0x7d, 0x4e,
        0xdf, 0xdf, 0x7d, 0x6e,
        0xdf, 0xff, 0x7d, 0x4e,
        0xdf, 0xff, 0xfd, 0x4e,
        0xdf, 0xd7, 0xfd, 0x4e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_transform_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_transform_boundary.bytes,
                                                         expected_aarch64_advsimd_transform_boundary,
                                                         sizeof(expected_aarch64_advsimd_transform_boundary)));
    AssemblyEncodeResult aarch64_advsimd_scalar_boundary = assembly_encode(
        arguments->arena,
        S8("sqabs b31, b30\n"
           "sqabs h31, h30\n"
           "sqabs s31, s30\n"
           "sqabs d31, d30\n"
           "sqneg b31, b30\n"
           "sqneg h31, h30\n"
           "sqneg s31, s30\n"
           "sqneg d31, d30\n"
           "suqadd b31, b30\n"
           "suqadd h31, h30\n"
           "suqadd s31, s30\n"
           "suqadd d31, d30\n"
           "usqadd b31, b30\n"
           "usqadd h31, h30\n"
           "usqadd s31, s30\n"
           "usqadd d31, d30\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_scalar_boundary[] = {
        0xdf, 0x7b, 0x20, 0x5e, 0xdf, 0x7b, 0x60, 0x5e, 0xdf, 0x7b, 0xa0, 0x5e, 0xdf, 0x7b, 0xe0, 0x5e,
        0xdf, 0x7b, 0x20, 0x7e, 0xdf, 0x7b, 0x60, 0x7e, 0xdf, 0x7b, 0xa0, 0x7e, 0xdf, 0x7b, 0xe0, 0x7e,
        0xdf, 0x3b, 0x20, 0x5e, 0xdf, 0x3b, 0x60, 0x5e, 0xdf, 0x3b, 0xa0, 0x5e, 0xdf, 0x3b, 0xe0, 0x5e,
        0xdf, 0x3b, 0x20, 0x7e, 0xdf, 0x3b, 0x60, 0x7e, 0xdf, 0x3b, 0xa0, 0x7e, 0xdf, 0x3b, 0xe0, 0x7e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_scalar_boundary.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_scalar_boundary.bytes,
                                                         expected_aarch64_advsimd_scalar_boundary,
                                                         sizeof(expected_aarch64_advsimd_scalar_boundary)));
    AssemblyEncodeResult aarch64_advsimd_transform_case_insensitive = assembly_encode(
        arguments->arena, S8("SHADD V0.4S, V1.4S, V2.4S\nTRN1 V0.4S, V1.4S, V2.4S\n"
                             "SMAXP V0.4S, V1.4S, V2.4S\nSMAX V0.4S, V1.4S, V2.4S\n"
                             "SMINP V0.4S, V1.4S, V2.4S\nSMIN V0.4S, V1.4S, V2.4S\n"
                             "UMAXP V0.4S, V1.4S, V2.4S\nUMAX V0.4S, V1.4S, V2.4S\n"
                             "UMINP V0.4S, V1.4S, V2.4S\nUMIN V0.4S, V1.4S, V2.4S\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_transform_case_insensitive[] = {
        0x20, 0x04, 0xa2, 0x4e,
        0x20, 0x28, 0x82, 0x4e,
        0x20, 0xa4, 0xa2, 0x4e,
        0x20, 0x64, 0xa2, 0x4e,
        0x20, 0xac, 0xa2, 0x4e,
        0x20, 0x6c, 0xa2, 0x4e,
        0x20, 0xa4, 0xa2, 0x6e,
        0x20, 0x64, 0xa2, 0x6e,
        0x20, 0xac, 0xa2, 0x6e,
        0x20, 0x6c, 0xa2, 0x6e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_transform_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_transform_case_insensitive.bytes,
                                                         expected_aarch64_advsimd_transform_case_insensitive,
                               sizeof(expected_aarch64_advsimd_transform_case_insensitive)));
    AssemblyEncodeResult aarch64_advsimd_new_case_insensitive = assembly_encode(
        arguments->arena, S8("SQADD V0.2D, V1.2D, V2.2D\nSQSUB V0.2D, V1.2D, V2.2D\n"
                             "UQADD V0.2D, V1.2D, V2.2D\nUQSUB V0.2D, V1.2D, V2.2D\n"
                             "SQRSHL V0.2D, V1.2D, V2.2D\nSQSHL V0.2D, V1.2D, V2.2D\n"
                             "UQRSHL V0.2D, V1.2D, V2.2D\nUQSHL V0.2D, V1.2D, V2.2D\n"
                             "SRHADD V0.4S, V1.4S, V2.4S\nURHADD V0.4S, V1.4S, V2.4S\n"
                             "UHADD V0.4S, V1.4S, V2.4S\nUHSUB V0.4S, V1.4S, V2.4S\n"
                             "SQABS D0, D1\nSQNEG D0, D1\nSUQADD D0, D1\nUSQADD D0, D1\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_new_case_insensitive[] = {
        0x20, 0x0c, 0xe2, 0x4e, 0x20, 0x2c, 0xe2, 0x4e,
        0x20, 0x0c, 0xe2, 0x6e, 0x20, 0x2c, 0xe2, 0x6e,
        0x20, 0x5c, 0xe2, 0x4e, 0x20, 0x4c, 0xe2, 0x4e,
        0x20, 0x5c, 0xe2, 0x6e, 0x20, 0x4c, 0xe2, 0x6e,
        0x20, 0x14, 0xa2, 0x4e, 0x20, 0x14, 0xa2, 0x6e,
        0x20, 0x04, 0xa2, 0x6e, 0x20, 0x24, 0xa2, 0x6e,
        0x20, 0x78, 0xe0, 0x5e, 0x20, 0x78, 0xe0, 0x7e,
        0x20, 0x38, 0xe0, 0x5e, 0x20, 0x38, 0xe0, 0x7e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_new_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_new_case_insensitive.bytes,
                                                         expected_aarch64_advsimd_new_case_insensitive,
                                                         sizeof(expected_aarch64_advsimd_new_case_insensitive)));
    AssemblyEncodeResult aarch64_advsimd_fp_ternary_case_insensitive = assembly_encode(
        arguments->arena,
        S8("FABD V0.2D, V1.2D, V2.2D\nFACGE V0.2D, V1.2D, V2.2D\nFACGT V0.2D, V1.2D, V2.2D\n"
           "FADDP V0.2D, V1.2D, V2.2D\nFADD V0.2D, V1.2D, V2.2D\nFCMEQ V0.2D, V1.2D, V2.2D\n"
           "FCMGE V0.2D, V1.2D, V2.2D\nFCMGT V0.2D, V1.2D, V2.2D\nFDIV V0.2D, V1.2D, V2.2D\n"
           "FMAXNMP V0.2D, V1.2D, V2.2D\nFMAXNM V0.2D, V1.2D, V2.2D\nFMAXP V0.2D, V1.2D, V2.2D\n"
           "FMAX V0.2D, V1.2D, V2.2D\nFMINNMP V0.2D, V1.2D, V2.2D\nFMINNM V0.2D, V1.2D, V2.2D\n"
           "FMINP V0.2D, V1.2D, V2.2D\nFMIN V0.2D, V1.2D, V2.2D\nFMLA V0.2D, V1.2D, V2.2D\n"
           "FMLS V0.2D, V1.2D, V2.2D\nFMULX V0.2D, V1.2D, V2.2D\nFMUL V0.2D, V1.2D, V2.2D\n"
           "FRECPS V0.2D, V1.2D, V2.2D\nFRSQRTS V0.2D, V1.2D, V2.2D\nFSUB V0.2D, V1.2D, V2.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_fp_ternary_case_insensitive[] = {
        0x20, 0xd4, 0xe2, 0x6e, 0x20, 0xec, 0x62, 0x6e, 0x20, 0xec, 0xe2, 0x6e,
        0x20, 0xd4, 0x62, 0x6e, 0x20, 0xd4, 0x62, 0x4e, 0x20, 0xe4, 0x62, 0x4e,
        0x20, 0xe4, 0x62, 0x6e, 0x20, 0xe4, 0xe2, 0x6e, 0x20, 0xfc, 0x62, 0x6e,
        0x20, 0xc4, 0x62, 0x6e, 0x20, 0xc4, 0x62, 0x4e, 0x20, 0xf4, 0x62, 0x6e,
        0x20, 0xf4, 0x62, 0x4e, 0x20, 0xc4, 0xe2, 0x6e, 0x20, 0xc4, 0xe2, 0x4e,
        0x20, 0xf4, 0xe2, 0x6e, 0x20, 0xf4, 0xe2, 0x4e, 0x20, 0xcc, 0x62, 0x4e,
        0x20, 0xcc, 0xe2, 0x4e, 0x20, 0xdc, 0x62, 0x4e, 0x20, 0xdc, 0x62, 0x6e,
        0x20, 0xfc, 0x62, 0x4e, 0x20, 0xfc, 0xe2, 0x4e, 0x20, 0xd4, 0xe2, 0x4e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_fp_ternary_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_fp_ternary_case_insensitive.bytes,
                                                         expected_aarch64_advsimd_fp_ternary_case_insensitive,
                                                         sizeof(expected_aarch64_advsimd_fp_ternary_case_insensitive)));
    AssemblyEncodeResult aarch64_advsimd_fp_unary_case_insensitive = assembly_encode(
        arguments->arena,
        S8("FABS V0.2D, V1.2D\nFCVTAS V0.2D, V1.2D\nFCVTAU V0.2D, V1.2D\n"
           "FCVTMS V0.2D, V1.2D\nFCVTMU V0.2D, V1.2D\nFCVTNS V0.2D, V1.2D\n"
           "FCVTNU V0.2D, V1.2D\nFCVTPS V0.2D, V1.2D\nFCVTPU V0.2D, V1.2D\n"
           "FCVTZS V0.2D, V1.2D\nFCVTZU V0.2D, V1.2D\nFNEG V0.2D, V1.2D\n"
           "FRECPE V0.2D, V1.2D\nFRINTA V0.2D, V1.2D\nFRINTI V0.2D, V1.2D\n"
           "FRINTM V0.2D, V1.2D\nFRINTN V0.2D, V1.2D\nFRINTP V0.2D, V1.2D\n"
           "FRINTX V0.2D, V1.2D\nFRINTZ V0.2D, V1.2D\nFRSQRTE V0.2D, V1.2D\n"
           "FSQRT V0.2D, V1.2D\nSCVTF V0.2D, V1.2D\nUCVTF V0.2D, V1.2D\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    static u8 const expected_aarch64_advsimd_fp_unary_case_insensitive[] = {
        0x20, 0xf8, 0xe0, 0x4e, 0x20, 0xc8, 0x61, 0x4e, 0x20, 0xc8, 0x61, 0x6e,
        0x20, 0xb8, 0x61, 0x4e, 0x20, 0xb8, 0x61, 0x6e, 0x20, 0xa8, 0x61, 0x4e,
        0x20, 0xa8, 0x61, 0x6e, 0x20, 0xa8, 0xe1, 0x4e, 0x20, 0xa8, 0xe1, 0x6e,
        0x20, 0xb8, 0xe1, 0x4e, 0x20, 0xb8, 0xe1, 0x6e, 0x20, 0xf8, 0xe0, 0x6e,
        0x20, 0xd8, 0xe1, 0x4e, 0x20, 0x88, 0x61, 0x6e, 0x20, 0x98, 0xe1, 0x6e,
        0x20, 0x98, 0x61, 0x4e, 0x20, 0x88, 0x61, 0x4e, 0x20, 0x88, 0xe1, 0x4e,
        0x20, 0x98, 0x61, 0x6e, 0x20, 0x98, 0xe1, 0x4e, 0x20, 0xd8, 0xe1, 0x6e,
        0x20, 0xf8, 0xe1, 0x6e, 0x20, 0xd8, 0x61, 0x4e, 0x20, 0xd8, 0x61, 0x6e,
    };
    BUSTER_TEST(arguments, aarch64_advsimd_fp_unary_case_insensitive.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_advsimd_fp_unary_case_insensitive.bytes,
                                                         expected_aarch64_advsimd_fp_unary_case_insensitive,
                                                         sizeof(expected_aarch64_advsimd_fp_unary_case_insensitive)));
    Target aarch64_no_transform_neon = aarch64_advsimd_target;
    aarch64_no_transform_neon.cpu_features =
        target_cpu_features_remove(aarch64_no_transform_neon.cpu_features, TARGET_CPU_FEATURE_AARCH64_NEON);
    AssemblyEncodeResult aarch64_advsimd_transform_without_feature = assembly_encode(
        arguments->arena, S8("shadd v0.4s, v1.4s, v2.4s\n"
                             "cnt v0.16b, v1.16b\n"
                             "trn1 v0.4s, v1.4s, v2.4s\n"
                             "trn2 v0.4s, v1.4s, v2.4s\n"
                             "uzp1 v0.4s, v1.4s, v2.4s\n"
                             "uzp2 v0.4s, v1.4s, v2.4s\n"
                             "zip1 v0.4s, v1.4s, v2.4s\n"
                             "zip2 v0.4s, v1.4s, v2.4s\n"
                             "smaxp v0.4s, v1.4s, v2.4s\n"
                             "smax v0.4s, v1.4s, v2.4s\n"
                             "sminp v0.4s, v1.4s, v2.4s\n"
                             "smin v0.4s, v1.4s, v2.4s\n"
                             "umaxp v0.4s, v1.4s, v2.4s\n"
                             "umax v0.4s, v1.4s, v2.4s\n"
                             "uminp v0.4s, v1.4s, v2.4s\n"
                             "umin v0.4s, v1.4s, v2.4s\n"
                             "sqadd v0.2d, v1.2d, v2.2d\n"
                             "sqsub v0.2d, v1.2d, v2.2d\n"
                             "uqadd v0.2d, v1.2d, v2.2d\n"
                             "uqsub v0.2d, v1.2d, v2.2d\n"
                             "sqrshl v0.2d, v1.2d, v2.2d\n"
                             "sqshl v0.2d, v1.2d, v2.2d\n"
                             "uqrshl v0.2d, v1.2d, v2.2d\n"
                             "uqshl v0.2d, v1.2d, v2.2d\n"
                             "srhadd v0.4s, v1.4s, v2.4s\n"
                             "urhadd v0.4s, v1.4s, v2.4s\n"
                             "uhadd v0.4s, v1.4s, v2.4s\n"
                             "uhsub v0.4s, v1.4s, v2.4s\n"
                             "sqabs v0.2d, v1.2d\n"
                             "sqneg v0.2d, v1.2d\n"
                             "suqadd v0.2d, v1.2d\n"
                             "usqadd v0.2d, v1.2d\n"
                             "sqabs d0, d1\n"
                             "sqneg d0, d1\n"
                             "suqadd d0, d1\n"
                             "usqadd d0, d1\n"
                             "fabs v0.2d, v1.2d\n"
                             "fcvtas v0.2d, v1.2d\n"
                             "fcvtau v0.2d, v1.2d\n"
                             "fcvtms v0.2d, v1.2d\n"
                             "fcvtmu v0.2d, v1.2d\n"
                             "fcvtns v0.2d, v1.2d\n"
                             "fcvtnu v0.2d, v1.2d\n"
                             "fcvtps v0.2d, v1.2d\n"
                             "fcvtpu v0.2d, v1.2d\n"
                             "fcvtzs v0.2d, v1.2d\n"
                             "fcvtzu v0.2d, v1.2d\n"
                             "fneg v0.2d, v1.2d\n"
                             "frecpe v0.2d, v1.2d\n"
                             "frinta v0.2d, v1.2d\n"
                             "frinti v0.2d, v1.2d\n"
                             "frintm v0.2d, v1.2d\n"
                             "frintn v0.2d, v1.2d\n"
                             "frintp v0.2d, v1.2d\n"
                             "frintx v0.2d, v1.2d\n"
                             "frintz v0.2d, v1.2d\n"
                             "frsqrte v0.2d, v1.2d\n"
                             "fsqrt v0.2d, v1.2d\n"
                             "scvtf v0.2d, v1.2d\n"
                             "ucvtf v0.2d, v1.2d\n"
                             "fabd v0.2d, v1.2d, v2.2d\n"
                             "facge v0.2d, v1.2d, v2.2d\n"
                             "facgt v0.2d, v1.2d, v2.2d\n"
                             "faddp v0.2d, v1.2d, v2.2d\n"
                             "fadd v0.2d, v1.2d, v2.2d\n"
                             "fcmeq v0.2d, v1.2d, v2.2d\n"
                             "fcmge v0.2d, v1.2d, v2.2d\n"
                             "fcmgt v0.2d, v1.2d, v2.2d\n"
                             "fdiv v0.2d, v1.2d, v2.2d\n"
                             "fmaxnmp v0.2d, v1.2d, v2.2d\n"
                             "fmaxnm v0.2d, v1.2d, v2.2d\n"
                             "fmaxp v0.2d, v1.2d, v2.2d\n"
                             "fmax v0.2d, v1.2d, v2.2d\n"
                             "fminnmp v0.2d, v1.2d, v2.2d\n"
                             "fminnm v0.2d, v1.2d, v2.2d\n"
                             "fminp v0.2d, v1.2d, v2.2d\n"
                             "fmin v0.2d, v1.2d, v2.2d\n"
                             "fmla v0.2d, v1.2d, v2.2d\n"
                             "fmls v0.2d, v1.2d, v2.2d\n"
                             "fmulx v0.2d, v1.2d, v2.2d\n"
                             "fmul v0.2d, v1.2d, v2.2d\n"
                             "frecps v0.2d, v1.2d, v2.2d\n"
                             "frsqrts v0.2d, v1.2d, v2.2d\n"
                             "fsub v0.2d, v1.2d, v2.2d\n"),
        (AssemblyEncodeOptions){.target = aarch64_no_transform_neon});
    BUSTER_TEST(arguments, aarch64_advsimd_transform_without_feature.diagnostic_count == 84 &&
                               aarch64_advsimd_transform_without_feature.bytes.length == 0 &&
                               aarch64_advsimd_transform_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    for (u32 diagnostic_index = 0;
         diagnostic_index < aarch64_advsimd_transform_without_feature.diagnostic_count;
         diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, aarch64_advsimd_transform_without_feature.diagnostics[diagnostic_index].kind ==
                                   ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    static String8 const invalid_aarch64_advsimd_transform[] = {
        S8_INITIALIZER("abs v0.2d, v1.2q\n"),
        S8_INITIALIZER("abs v0.1d, v1.1d\n"),
        S8_INITIALIZER("abs v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("abs q0, q1\n"),
        S8_INITIALIZER("abs v32.8b, v1.8b\n"),
        S8_INITIALIZER("shadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("shadd v0.4s, v1.4s\n"),
        S8_INITIALIZER("shadd v0.4s, v1.4s, v2.4s, v3.4s\n"),
        S8_INITIALIZER("cnt v0.4h, v1.4h\n"),
        S8_INITIALIZER("cnt d0, d1\n"),
        S8_INITIALIZER("trn1 v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("trn1 v0.4s, v1.4s\n"),
        S8_INITIALIZER("trn2 v0.4s, v1.4s, v2.4s, v3.4s\n"),
        S8_INITIALIZER("uzp1 d0, d1, d2\n"),
        S8_INITIALIZER("uzp2 v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("zip1 v0.4s, v1.4s, v2.4s, v3.4s\n"),
        S8_INITIALIZER("zip2 v0.4s, v1.4s\n"),
        S8_INITIALIZER("smaxp v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("smax v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sminp v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("smin v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("umaxp v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("umax v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("uminp v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("umin v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sqadd v0.2d, v1.2d\n"),
        S8_INITIALIZER("sqsub v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("uqadd v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("uqsub v0.4s, v1.4s, v32.4s\n"),
        S8_INITIALIZER("sqrshl v0.2d, v1.4s, v2.2d\n"),
        S8_INITIALIZER("sqshl v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("uqrshl q0, q1, q2\n"),
        S8_INITIALIZER("uqshl v0.2q, v1.2q, v2.2q\n"),
        S8_INITIALIZER("srhadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("urhadd v0.4s, v1.4s\n"),
        S8_INITIALIZER("uhadd v0.4s, v1.4s, v2.4s, v3.4s\n"),
        S8_INITIALIZER("uhsub v32.4s, v1.4s, v2.4s\n"),
        S8_INITIALIZER("sqabs v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("sqabs q0, q1\n"),
        S8_INITIALIZER("sqabs b0, h1\n"),
        S8_INITIALIZER("sqabs h0, b1\n"),
        S8_INITIALIZER("sqneg v0.1d, v1.1d\n"),
        S8_INITIALIZER("sqneg v0, v1\n"),
        S8_INITIALIZER("suqadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("suqadd q0, q1\n"),
        S8_INITIALIZER("usqadd d32, d1\n"),
        S8_INITIALIZER("usqadd v0, v1\n"),
        S8_INITIALIZER("fabs v0.1d, v1.1d\n"),
        S8_INITIALIZER("fabs v0.8b, v1.8b\n"),
        S8_INITIALIZER("fabs v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("fabs q0, q1\n"),
        S8_INITIALIZER("fabs v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fabs v32.2d, v1.2d\n"),
        S8_INITIALIZER("fcvtas v0.2s, v1.2d\n"),
        S8_INITIALIZER("fcvtau v0.2d, v1.2s\n"),
        S8_INITIALIZER("fcvtms v0.2s, v1.2s, v2.2s\n"),
        S8_INITIALIZER("fcvtmu v0.2s\n"),
        S8_INITIALIZER("fcvtns q0, q1\n"),
        S8_INITIALIZER("fcvtnu v0.1d, v1.1d\n"),
        S8_INITIALIZER("fcvtps v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fcvtpu v0.2s\n"),
        S8_INITIALIZER("fcvtzs v0.2s, v1.2s, v2.2s\n"),
        S8_INITIALIZER("fcvtzu v0.1d, v1.1d\n"),
        S8_INITIALIZER("fneg v0.1d, v1.1d\n"),
        S8_INITIALIZER("frecpe v0.2d\n"),
        S8_INITIALIZER("frinta v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("frinti v32.2d, v1.2d\n"),
        S8_INITIALIZER("frintm v0.1d, v1.1d\n"),
        S8_INITIALIZER("frintn v0.2d\n"),
        S8_INITIALIZER("frintp v0.2s, v1.2d\n"),
        S8_INITIALIZER("frintx v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("frintz q0, q1\n"),
        S8_INITIALIZER("frsqrte v0.2d\n"),
        S8_INITIALIZER("fsqrt v0.1d, v1.1d\n"),
        S8_INITIALIZER("scvtf v0.2d\n"),
        S8_INITIALIZER("scvtf v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("ucvtf v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fabd v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("fabd v0.2d, v1.2d\n"),
        S8_INITIALIZER("fabd v32.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fabd v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("facge v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("facgt v0.4h, v1.4h\n"),
        S8_INITIALIZER("faddp v0.2d, v1.2d\n"),
        S8_INITIALIZER("fadd v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fadd v0.2s, v1.4s, v2.2s\n"),
        S8_INITIALIZER("fcmeq v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fcmge v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("fcmgt q0, q1, q2\n"),
        S8_INITIALIZER("fdiv v0.2d, v1.2d\n"),
        S8_INITIALIZER("fmaxnmp v0.4h, v1.4h\n"),
        S8_INITIALIZER("fmaxnm v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fmaxp v0.2d, v1.2d\n"),
        S8_INITIALIZER("fmax v32.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("fminnmp v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("fminnm v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("fminp v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fmin v0.2d, v1.2d\n"),
        S8_INITIALIZER("fmla v0.4h, v1.4h\n"),
        S8_INITIALIZER("fmla v0.4s, v1.4s, v2.s[0]\n"),
        S8_INITIALIZER("fmls v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fmulx v0.1d, v1.1d, v2.1d\n"),
        S8_INITIALIZER("fmul v0.2d, v1.2d\n"),
        S8_INITIALIZER("frecps v0.8b, v1.8b, v2.8b\n"),
        S8_INITIALIZER("frsqrts v0.2d, v1.2d, v2.2d, v3.2d\n"),
        S8_INITIALIZER("fsub v0.2d, v1.2d\n"),
        S8_INITIALIZER("fabd b0, b1, b2\n"),
        S8_INITIALIZER("srhadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("urhadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("uhadd v0.2d, v1.2d, v2.2d\n"),
        S8_INITIALIZER("uhsub v0.2d, v1.2d, v2.2d\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_advsimd_transform); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_transform_case = assembly_encode(
            arguments->arena, invalid_aarch64_advsimd_transform[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_transform_case.diagnostic_count == 1 && invalid_transform_case.bytes.length == 0 &&
                                   invalid_transform_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    for (u32 neon_index = 0; neon_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_neon_cases); neon_index += 1)
    {
        AssemblyA64DirectSIMDNeonCase neon_case = assembly_a64_direct_simd_neon_cases[neon_index];
        AssemblyEncodeResult representative = assembly_encode(
            arguments->arena, neon_case.representative, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        AssemblyEncodeResult boundary = assembly_encode(
            arguments->arena, neon_case.boundary, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, representative.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(representative.bytes, neon_case.representative_bytes, 4));
        BUSTER_TEST(arguments, boundary.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(boundary.bytes, neon_case.boundary_bytes, 4));
    }
    AssemblyEncodeResult aarch64_direct_simd_neon_case = assembly_encode(
        arguments->arena, S8("CMEQ D0, D1, D2\nFCVTXN S0, D1\nFMAXV S0, V1.4S\n"),
        (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
    BUSTER_TEST(arguments, aarch64_direct_simd_neon_case.diagnostic_count == 0);
    Target aarch64_no_advsimd_neon_tranche = aarch64_advsimd_target;
    aarch64_no_advsimd_neon_tranche.cpu_features =
        target_cpu_features_remove(aarch64_no_advsimd_neon_tranche.cpu_features, TARGET_CPU_FEATURE_AARCH64_NEON);
    AssemblyEncodeResult aarch64_direct_simd_neon_without_feature = assembly_encode(
        arguments->arena, S8("addp d0, v1.2d\n"), (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon_tranche});
    BUSTER_TEST(arguments, aarch64_direct_simd_neon_without_feature.diagnostic_count == 1 &&
                               aarch64_direct_simd_neon_without_feature.bytes.length == 0 &&
                               aarch64_direct_simd_neon_without_feature.diagnostics[0].kind ==
                                   ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_tbl_tbx_spellings) == 8);
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_tbl_tbx_spellings); expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_tbl_tbx_spellings[expected_index];
        bool found = false;
        for (u32 spelling_index = 0; spelling_index < assembly_test_aarch64_direct_simd_spelling_count(); spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (!assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) ||
                !string_equal(spelling.semantic_id, expected.semantic_id))
            {
                continue;
            }
            found = spelling.source_digest == expected.source_digest && spelling.operand_count == expected.operand_count &&
                    spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON;
            for (u32 arrangement_index = 0; arrangement_index < 4; arrangement_index += 1)
            {
                found = found && spelling.arrangements[arrangement_index] == expected.arrangements[arrangement_index];
            }
            break;
        }
        BUSTER_TEST(arguments, found);
    }
    for (u32 tbl_tbx_index = 0; tbl_tbx_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_tbl_tbx_cases); tbl_tbx_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase tbl_tbx_case = assembly_a64_direct_simd_tbl_tbx_cases[tbl_tbx_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, tbl_tbx_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, tbl_tbx_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, tbl_tbx_case.source, (AssemblyEncodeOptions){.target = aarch64_no_advsimd_neon_tranche});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 tbl_tbx_index = 0; tbl_tbx_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_tbl_tbx_boundary_cases); tbl_tbx_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase tbl_tbx_case = assembly_a64_direct_simd_tbl_tbx_boundary_cases[tbl_tbx_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, tbl_tbx_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, tbl_tbx_case.bytes, 4));
    }
    for (u32 tbl_tbx_index = 0; tbl_tbx_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_tbl_tbx_wrap_cases); tbl_tbx_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase tbl_tbx_case = assembly_a64_direct_simd_tbl_tbx_wrap_cases[tbl_tbx_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, tbl_tbx_case.source, (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, tbl_tbx_case.bytes, 4));
    }
    static String8 const malformed_aarch64_tbl_tbx[] = {
        S8_INITIALIZER("tbl v0.16b, {}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b, v2.16b, v3.16b, v4.16b, v5.16b}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.8b}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b, v3.16b}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b}, v9.8b\n"),
        S8_INITIALIZER("tbl v0.8b, {v1.16b}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b}, v9.8b[0]\n"),
        S8_INITIALIZER("tbl q0, {v1.16b}, q9\n"),
        S8_INITIALIZER("tbl v0.16b, {d1}, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b}, v32.16b\n"),
        S8_INITIALIZER("tbl v0.16b, v1.16b, v9.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b}, v9.16b, v10.16b\n"),
        S8_INITIALIZER("tbl v0.16b, {v1.16b}, z9.b\n"),
        S8_INITIALIZER("tbx v0.16b, {v1.16b, v3.16b, v4.16b}, v9.16b\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_tbl_tbx); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_tbl_tbx[malformed_index], (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_final3_cases) == 11);
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_final3_boundary_cases) == 3);
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_final3_spellings); expected_index += 1)
    {
        AssemblyA64DirectSIMDSpellingExpectation expected = assembly_a64_direct_simd_final3_spellings[expected_index];
        u32 found_count = 0;
        for (u32 spelling_index = 0; spelling_index < assembly_test_aarch64_direct_simd_spelling_count(); spelling_index += 1)
        {
            AssemblyAarch64DirectSIMDSpellingTest spelling = {0};
            if (assembly_test_aarch64_direct_simd_spelling_at(spelling_index, &spelling) && spelling.source_digest == expected.source_digest)
            {
                found_count += 1;
                BUSTER_TEST(arguments, string_equal(spelling.semantic_id, expected.semantic_id) &&
                                           spelling.operand_count == expected.operand_count &&
                                           spelling.requirement == BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON &&
                                           memcmp(spelling.arrangements, expected.arrangements, sizeof(expected.arrangements)) == 0);
            }
        }
        BUSTER_TEST(arguments, found_count == 1);
    }
    for (u32 final3_index = 0; final3_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_final3_cases); final3_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase final3_case = assembly_a64_direct_simd_final3_cases[final3_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, final3_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, final3_case.bytes, 4));
        AssemblyEncodeResult without_neon = assembly_encode(
            arguments->arena, final3_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_no_neon});
        BUSTER_TEST(arguments, without_neon.diagnostic_count == 1 && without_neon.bytes.length == 0 &&
                                   without_neon.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    for (u32 final3_index = 0; final3_index < BUSTER_ARRAY_LENGTH(assembly_a64_direct_simd_final3_boundary_cases); final3_index += 1)
    {
        AssemblyA64DirectSIMDEncodingCase final3_case = assembly_a64_direct_simd_final3_boundary_cases[final3_index];
        AssemblyEncodeResult encoded = assembly_encode(
            arguments->arena, final3_case.source, (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, final3_case.bytes, 4));
    }
    /* ORR remains canonical when the MOV alias condition Rm == Rn holds. */
    AssemblyEncodeResult orr_alias_condition = assembly_encode(
        arguments->arena, S8("orr v0.8b, v1.8b, v1.8b\n"), (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
    static u8 const expected_orr_alias_condition[] = {0x20, 0x1c, 0xa1, 0x0e};
    BUSTER_TEST(arguments, orr_alias_condition.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(orr_alias_condition.bytes, expected_orr_alias_condition, 4));
    /* MVN/MOV aliases stay outside the direct canonical spelling table. */
    AssemblyEncodeResult mvn_alias = assembly_encode(
        arguments->arena, S8("mvn v0.8b, v1.8b\n"), (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
    AssemblyEncodeResult mov_alias = assembly_encode(
        arguments->arena, S8("mov v0.8b, v1.8b\n"), (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
    BUSTER_TEST(arguments, mvn_alias.diagnostic_count == 1 && mvn_alias.bytes.length == 0);
    BUSTER_TEST(arguments, mov_alias.diagnostic_count == 1 && mov_alias.bytes.length == 0);
    static String8 const malformed_aarch64_direct_simd_final3[] = {
        S8_INITIALIZER("dup v0.2d, w1\n"),
        S8_INITIALIZER("dup v0.2d, wzr\n"),
        S8_INITIALIZER("dup v0.8b, x1\n"),
        S8_INITIALIZER("dup v0.8b, sp\n"),
        S8_INITIALIZER("dup v0.2d, sp\n"),
        S8_INITIALIZER("dup v32.8b, w1\n"),
        S8_INITIALIZER("dup v0.8b, w32\n"),
        S8_INITIALIZER("dup v0.8b, {v1.8b}\n"),
        S8_INITIALIZER("dup v0.8b, w1, v2.8b\n"),
        S8_INITIALIZER("dup v0.8b\n"),
        S8_INITIALIZER("not v0.8b, v1.8b[0]\n"),
        S8_INITIALIZER("not v0.8b\n"),
        S8_INITIALIZER("not v32.8b, v1.8b\n"),
        S8_INITIALIZER("orr v0.4h, v1.4h, v2.4h\n"),
        S8_INITIALIZER("orr v0.8b, v1.8b\n"),
        S8_INITIALIZER("orr v0.8b, v1.8b, v32.8b\n"),
        S8_INITIALIZER("orr v0.8b, {v1.8b}, v2.8b\n"),
    };
    for (u32 malformed_index = 0; malformed_index < BUSTER_ARRAY_LENGTH(malformed_aarch64_direct_simd_final3); malformed_index += 1)
    {
        AssemblyEncodeResult malformed = assembly_encode(
            arguments->arena, malformed_aarch64_direct_simd_final3[malformed_index],
            (AssemblyEncodeOptions){.target = aarch64_fcvt_suffix_target});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.bytes.length == 0 &&
                                   malformed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    static String8 const invalid_aarch64_direct_simd_neon[] = {
        S8_INITIALIZER("addp d0, v1.4s\n"),
        S8_INITIALIZER("addp d0, v1\n"),
        S8_INITIALIZER("cmeq d0, d1\n"),
        S8_INITIALIZER("fcvtxn v0.4s, d1\n"),
        S8_INITIALIZER("fmaxv s0, v1.2d\n"),
        S8_INITIALIZER("ushl d32, d1, d2\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_direct_simd_neon); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_neon_case = assembly_encode(
            arguments->arena, invalid_aarch64_direct_simd_neon[invalid_index],
            (AssemblyEncodeOptions){.target = aarch64_advsimd_target});
        BUSTER_TEST(arguments, invalid_neon_case.diagnostic_count == 1 && invalid_neon_case.bytes.length == 0 &&
                                   invalid_neon_case.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    BusterAarch64SyntaxMnemonicRange csel_range = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_mnemonic_lookup(S8("csel"), &csel_range) && csel_range.candidate_count > 0);
    u32 csel_row = UINT32_MAX;
    BUSTER_TEST(arguments, buster_aarch64_syntax_mnemonic_candidate(csel_range, 0, &csel_row) && csel_row < 1695);
    AssemblyEncodeResult aarch64_control = assembly_encode(
        arguments->arena,
        S8("csel w0, w1, w2, eq\n"
           "target:\n"
           "cbz w0, target\n"
           "tbz x0, #3, target\n"
           "b.ne target\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_control.diagnostic_count == 0 && aarch64_control.bytes.length == 16);
    BUSTER_TEST(arguments, aarch64_control.relocation_count == 0);
    AssemblyEncodeResult aarch64_control_condition_aliases = assembly_encode(
        arguments->arena, S8("csel w0, w1, w2, hs\ncsel w0, w1, w2, lo\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_control_condition_aliases[] = {
        0x20, 0x20, 0x82, 0x1a,
        0x20, 0x30, 0x82, 0x1a,
    };
    BUSTER_TEST(arguments, aarch64_control_condition_aliases.diagnostic_count == 0 &&
                               aarch64_control_condition_aliases.relocation_count == 0 &&
                               aarch64_control_condition_aliases.bytes.length == sizeof(expected_aarch64_control_condition_aliases) &&
                               memcmp(aarch64_control_condition_aliases.bytes.pointer, expected_aarch64_control_condition_aliases,
                                      sizeof(expected_aarch64_control_condition_aliases)) == 0);
    AssemblyEncodeResult aarch64_branch_condition_aliases = assembly_encode(
        arguments->arena, S8("b.hs target\nb.lo target\ntarget:\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_branch_condition_aliases.diagnostic_count == 0 &&
                               aarch64_branch_condition_aliases.bytes.length == 8 &&
                               aarch64_branch_condition_aliases.relocation_count == 0);
    AssemblyEncodeResult unsupported_control = assembly_encode(
        arguments->arena, S8("ld1 {v0.4s}, [x0]\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, unsupported_control.diagnostic_count == 1 && unsupported_control.bytes.length == 0);

    Target aarch64_m1_no_crc = aarch64_m1_explicit_target;
    aarch64_m1_no_crc.cpu_features = target_cpu_features_remove(aarch64_m1_no_crc.cpu_features, TARGET_CPU_FEATURE_AARCH64_CRC);
    AssemblyEncodeResult aarch64_direct_gpr_no_crc = assembly_encode(
        arguments->arena, S8("crc32w w1, w2, w3\n"), (AssemblyEncodeOptions){.target = aarch64_m1_no_crc});
    BUSTER_TEST(arguments, aarch64_direct_gpr_no_crc.diagnostic_count == 1 &&
                               aarch64_direct_gpr_no_crc.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult aarch64_direct_gpr_no_pauth = assembly_encode(
        arguments->arena, S8("blraa x1, sp\n"), (AssemblyEncodeOptions){.target = aarch64_m1_no_pauth});
    BUSTER_TEST(arguments, aarch64_direct_gpr_no_pauth.diagnostic_count == 1 &&
                               aarch64_direct_gpr_no_pauth.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult aarch64_direct_gpr_generic = assembly_encode(
        arguments->arena, S8("adcs w1, w2, w3\n"), (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_direct_gpr_generic.diagnostic_count == 1 &&
                               aarch64_direct_gpr_generic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);

    AssemblyEncodeResult aarch64_system = assembly_encode(
        arguments->arena,
        S8("brk #1\n"
           "clrex\n"
           "dmb ish\n"
           "dsb #15\n"
           "isb sy\n"
           "hint #0\n"
           "mrs x0, nzcv\n"
           "msr nzcv, x0\n"
           "msr daifset, #15\n"
           "sys #0, c7, c8, #1\n"
           "sys #0, c7, c8, #1, x0\n"
           "sysl x0, #0, c7, c8, #1\n"
           "svc #2\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_system[] = {
        0x20, 0x00, 0x20, 0xd4, 0x5f, 0x3f, 0x03, 0xd5, 0xbf, 0x3b, 0x03, 0xd5,
        0x9f, 0x3f, 0x03, 0xd5, 0xdf, 0x3f, 0x03, 0xd5, 0x1f, 0x20, 0x03, 0xd5,
        0x00, 0x42, 0x3b, 0xd5, 0x00, 0x42, 0x1b, 0xd5, 0xdf, 0x4f, 0x03, 0xd5,
        0x3f, 0x78, 0x08, 0xd5, 0x20, 0x78, 0x08, 0xd5, 0x20, 0x78, 0x28, 0xd5,
        0x41, 0x00, 0x00, 0xd4,
    };
    BUSTER_TEST(arguments, aarch64_system.diagnostic_count == 0 &&
                               aarch64_system.bytes.length == sizeof(expected_aarch64_system) &&
                               memcmp(aarch64_system.bytes.pointer, expected_aarch64_system, sizeof(expected_aarch64_system)) == 0);
    AssemblyEncodeResult aarch64_system_raw = assembly_encode(
        arguments->arena,
        S8("mrs x0, S2_0_C0_C0_0\n"
           "msr S2_0_C0_C0_0, x0\n"
           "mrs x1, S3_3_C7_C8_1\n"
           "msr S3_3_C7_C8_1, x1\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_system_raw[] = {
        0x00, 0x00, 0x30, 0xd5,
        0x00, 0x00, 0x10, 0xd5,
        0x21, 0x78, 0x3b, 0xd5,
        0x21, 0x78, 0x1b, 0xd5,
    };
    BUSTER_TEST(arguments, aarch64_system_raw.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_system_raw.bytes, expected_aarch64_system_raw,
                                                         sizeof(expected_aarch64_system_raw)));
    static String8 const invalid_aarch64_system_raw[] = {
        S8_INITIALIZER("mrs x0, S0_0_C0_C0_0\n"),
        S8_INITIALIZER("mrs x0, S1_0_C0_C0_0\n"),
        S8_INITIALIZER("mrs x0, S4_0_C0_C0_0\n"),
        S8_INITIALIZER("mrs x0, S2_0_C0_C0\n"),
        S8_INITIALIZER("mrs x0, S2_0_C0_C0_0x\n"),
        S8_INITIALIZER("mrs x0, {S2_0_C0_C0_0}\n"),
        S8_INITIALIZER("mrs x0, S3_7_C15_C15_7\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_system_raw); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_raw = assembly_encode(
            arguments->arena, invalid_aarch64_system_raw[invalid_index], (AssemblyEncodeOptions){.target = aarch64_m1_target});
        BUSTER_TEST(arguments, invalid_raw.diagnostic_count == 1 && invalid_raw.bytes.length == 0);
    }
    AssemblyEncodeResult aarch64_system_raw_transaction = assembly_encode(
        arguments->arena,
        S8("mrs x0, S2_0_C0_C0_0\n"
           "mrs x0, {S2_0_C0_C0_0}\n"
           "msr S3_3_C7_C8_1, x1\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_system_raw_transaction[] = {
        0x00, 0x00, 0x30, 0xd5,
        0x21, 0x78, 0x1b, 0xd5,
    };
    BUSTER_TEST(arguments, aarch64_system_raw_transaction.diagnostic_count == 1 &&
                               assembly_test_bytes_equal(aarch64_system_raw_transaction.bytes,
                                                         expected_aarch64_system_raw_transaction,
                                                         sizeof(expected_aarch64_system_raw_transaction)));
    AssemblyEncodeResult aarch64_system_bad = assembly_encode(
        arguments->arena, S8("mrs w0, nzcv\nmsr nzcv, w0\ndmb #0\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_system_bad.diagnostic_count == 3 && aarch64_system_bad.bytes.length == 0);
    AssemblyEncodeResult aarch64_system_exceptions = assembly_encode(
        arguments->arena,
        S8("dcps1 #1\n"
           "dcps2 #2\n"
           "dcps3 #3\n"
           "hlt #4\n"
           "hvc #5\n"
           "smc #6\n"
           "msr spsel, #0\n"
           "msr daifclr, #7\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_system_exceptions[] = {
        0x21, 0x00, 0xa0, 0xd4, 0x42, 0x00, 0xa0, 0xd4, 0x63, 0x00, 0xa0, 0xd4,
        0x80, 0x00, 0x40, 0xd4, 0xa2, 0x00, 0x00, 0xd4, 0xc3, 0x00, 0x00, 0xd4,
        0xbf, 0x40, 0x00, 0xd5, 0xdf, 0x47, 0x03, 0xd5,
    };
    BUSTER_TEST(arguments, aarch64_system_exceptions.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_system_exceptions.bytes, expected_aarch64_system_exceptions,
                                                         sizeof(expected_aarch64_system_exceptions)));
    AssemblyEncodeResult aarch64_system_invalid = assembly_encode(
        arguments->arena, S8("isb ish\nbrk #65536\nhlt #-1\nsys #8, c0, c0, #0\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_system_invalid.diagnostic_count == 4 && aarch64_system_invalid.bytes.length == 0);
    AssemblyEncodeResult aarch64_system_transaction = assembly_encode(
        arguments->arena, S8("brk #1\nhlt #65536\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_system_transaction[] = {0x20, 0x00, 0x20, 0xd4};
    BUSTER_TEST(arguments, aarch64_system_transaction.diagnostic_count == 1 &&
                               assembly_test_bytes_equal(aarch64_system_transaction.bytes, expected_aarch64_system_transaction,
                                                         sizeof(expected_aarch64_system_transaction)));
    AssemblyEncodeResult aarch64_system_generic = assembly_encode(
        arguments->arena, S8("brk #1\nmrs x0, nzcv\n"), (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_system_generic.diagnostic_count == 2 && aarch64_system_generic.bytes.length == 0);
    Aarch64SystemRegisterLookup nzcv_lookup = {0};
    BUSTER_TEST(arguments, aarch64_system_register_lookup_name(S8("nzcv"), &nzcv_lookup) &&
                               nzcv_lookup.packed_encoding == UINT16_C(0xda10) && nzcv_lookup.mode == AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE);

    AssemblyEncodeResult aarch64_scalar_integer = assembly_encode(
        arguments->arena,
        S8("add x0, x1, #4095\n"
           "add w0, w1, w2, lsl #31\n"
           "and x0, x1, #0xff00ff00ff00ff\n"
           "orr w0, w1, w2, ror #31\n"
           "ccmn x1, #31, #15, eq\n"
           "rmif x1, #63, #15\n"
           "udf #65535\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_scalar_integer[] = {
        0x20, 0xfc, 0x3f, 0x91,
        0x20, 0x7c, 0x02, 0x0b,
        0x20, 0x9c, 0x00, 0x92,
        0x20, 0x7c, 0xc2, 0x2a,
        0x2f, 0x08, 0x5f, 0xba,
        0x2f, 0x84, 0x1f, 0xba,
        0xff, 0xff, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, aarch64_scalar_integer.diagnostic_count == 0 &&
                               aarch64_scalar_integer.bytes.length == sizeof(expected_aarch64_scalar_integer) &&
                               memcmp(aarch64_scalar_integer.bytes.pointer, expected_aarch64_scalar_integer,
                                      sizeof(expected_aarch64_scalar_integer)) == 0);
    AssemblyEncodeResult aarch64_m1_gpr_collision_regression = assembly_encode(
        arguments->arena,
        S8("add w0, w1, w2\n"
           "add x0, x1, x2\n"
           "sub w0, w1, w2\n"
           "sub x0, x1, x2\n"
           "and wzr, w1, w2, lsr #3\n"
           "bic wzr, w1, w2, lsr #3\n"
           "eor wzr, w1, w2, lsr #3\n"
           "orn wzr, w1, w2, lsr #3\n"
           "orr wzr, w1, w2, lsr #3\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_m1_gpr_collision_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_m1_gpr_collision_regression.bytes,
                                                         (u8 const[]){0x20, 0x00, 0x02, 0x0b,
                                                                      0x20, 0x00, 0x02, 0x8b,
                                                                      0x20, 0x00, 0x02, 0x4b,
                                                                      0x20, 0x00, 0x02, 0xcb,
                                                                      0x3f, 0x0c, 0x42, 0x0a,
                                                                      0x3f, 0x0c, 0x62, 0x0a,
                                                                      0x3f, 0x0c, 0x42, 0x4a,
                                                                      0x3f, 0x0c, 0x62, 0x2a,
                                                                      0x3f, 0x0c, 0x42, 0x2a},
                                                         36));
    AssemblyEncodeResult aarch64_m1_unary_gpr_collision_regression = assembly_encode(
        arguments->arena,
        S8("cls w0, w1\n"
           "cls x0, x1\n"
           "clz w0, w1\n"
           "clz x0, x1\n"
           "rbit w0, w1\n"
           "rbit x0, x1\n"
           "rev16 w0, w1\n"
           "rev16 x0, x1\n"
           "rev32 x0, x1\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_m1_unary_gpr_collision_regression.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(aarch64_m1_unary_gpr_collision_regression.bytes,
                                                         (u8 const[]){0x20, 0x14, 0xc0, 0x5a,
                                                                      0x20, 0x14, 0xc0, 0xda,
                                                                      0x20, 0x10, 0xc0, 0x5a,
                                                                      0x20, 0x10, 0xc0, 0xda,
                                                                      0x20, 0x00, 0xc0, 0x5a,
                                                                      0x20, 0x00, 0xc0, 0xda,
                                                                      0x20, 0x04, 0xc0, 0x5a,
                                                                      0x20, 0x04, 0xc0, 0xda,
                                                                      0x20, 0x08, 0xc0, 0xda},
                                                         36));
    static String8 const invalid_aarch64_scalar_integer[] = {
        S8_INITIALIZER("add lsl #1, x0, x1, x2\n"),
        S8_INITIALIZER("add x0, lsl #1, x1, x2\n"),
        S8_INITIALIZER("add x0, x1, x2, lsl\n"),
        S8_INITIALIZER("ccmn x1, #31, #15, #0\n"),
    };
    for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_aarch64_scalar_integer); invalid_index += 1)
    {
        AssemblyEncodeResult invalid_scalar = assembly_encode(
            arguments->arena, invalid_aarch64_scalar_integer[invalid_index], (AssemblyEncodeOptions){.target = aarch64_m1_target});
        BUSTER_TEST(arguments, invalid_scalar.diagnostic_count > 0 && invalid_scalar.bytes.length == 0);
    }
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(assembly_a64_m1_gpr_corpus) == 80);
    for (u32 corpus_index = 0; corpus_index < BUSTER_ARRAY_LENGTH(assembly_a64_m1_gpr_corpus); corpus_index += 1)
    {
        AssemblyA64M1GprCorpusCase const* test_case = assembly_a64_m1_gpr_corpus + corpus_index;
        AssemblyEncodeResult encoded = assembly_encode(arguments->arena, test_case->source, (AssemblyEncodeOptions){.target = aarch64_m1_target});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && assembly_test_bytes_equal(encoded.bytes, test_case->bytes, sizeof(test_case->bytes)));
    }
    BUSTER_TEST(arguments, BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT == 72);
    for (u32 corpus_index = 0; corpus_index < BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT; corpus_index += 1)
    {
        BusterAarch64ScalarIntegerCorpusCase const* test_case = buster_aarch64_scalar_integer_corpus + corpus_index;
        String8 source = string_format(arguments->arena, S8("{S8}\n"), test_case->source);
        AssemblyEncodeResult encoded = assembly_encode(arguments->arena, source, (AssemblyEncodeOptions){.target = aarch64_m1_target});
        u32 word = 0;
        if (encoded.bytes.length == 4 && encoded.bytes.pointer)
        {
            word = (u32)encoded.bytes.pointer[0] | ((u32)encoded.bytes.pointer[1] << 8) |
                   ((u32)encoded.bytes.pointer[2] << 16) | ((u32)encoded.bytes.pointer[3] << 24);
        }
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 && encoded.bytes.length == 4 && word == test_case->word);
    }

    String8 split_operands[6] = {0};
    u32 split_operand_count = 0;
    bool split_lists = assembly_test_split_operands(
        S8("{v0.4s, v1.4s}, [x2, x3], (x4, x5), :lo12:symbol"),
        split_operands, BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_lists && split_operand_count == 4);
    if (split_lists && split_operand_count == 4)
    {
        BUSTER_STRING_TEST(arguments, split_operands[0], S8("{v0.4s, v1.4s}"));
        BUSTER_STRING_TEST(arguments, split_operands[1], S8("[x2, x3]"));
        BUSTER_STRING_TEST(arguments, split_operands[2], S8("(x4, x5)"));
        BUSTER_STRING_TEST(arguments, split_operands[3], S8(":lo12:symbol"));
    }
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{v0.4s, v1.4s], x0"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{v0.4s, v1.4s, x0"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("([x0, x1)]"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{[x0, x1}]"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    bool split_nested = assembly_test_split_operands(
        S8("({[x0, x1]}), x2"), split_operands,
        BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_nested && split_operand_count == 2);
    if (split_nested && split_operand_count == 2)
    {
        BUSTER_STRING_TEST(arguments, split_operands[0], S8("({[x0, x1]})"));
        BUSTER_STRING_TEST(arguments, split_operands[1], S8("x2"));
    }

    bool split_six = assembly_test_split_operands(
        S8("x0, x1, x2, x3, x4, x5"), split_operands,
        BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_six && split_operand_count == BUSTER_ARRAY_LENGTH(split_operands));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("x0, x1, x2, x3, x4, x5"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands) - 1, &split_operand_count));

    AssemblyEncodeResult aarch64_same_line_label = assembly_encode(
        arguments->arena, S8("leading_label : b leading_label\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_same_line_label.diagnostic_count == 0 &&
                               aarch64_same_line_label.symbol_count == 1 &&
                               aarch64_same_line_label.symbols[0].defined &&
                               aarch64_same_line_label.symbols[0].offset == 0 &&
                               string_equal(aarch64_same_line_label.symbols[0].name, S8("leading_label")) &&
                               aarch64_same_line_label.bytes.length == sizeof(expected_aarch64_jump) &&
                               memcmp(aarch64_same_line_label.bytes.pointer, expected_aarch64_jump,
                                      sizeof(expected_aarch64_jump)) == 0);
    AssemblyEncodeResult aarch64_separated_label_without_space = assembly_encode(
        arguments->arena, S8("leading_label_without_space :nop\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_separated_label_without_space.diagnostic_count == 0 &&
                               aarch64_separated_label_without_space.symbol_count == 1 &&
                               aarch64_separated_label_without_space.symbols[0].defined &&
                               aarch64_separated_label_without_space.symbols[0].offset == 0 &&
                               string_equal(aarch64_separated_label_without_space.symbols[0].name,
                                            S8("leading_label_without_space")) &&
                               aarch64_separated_label_without_space.bytes.length == 4);
    AssemblyEncodeResult aarch64_modifier_operand = assembly_encode(
        arguments->arena, S8("b :lo12:target\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_modifier_operand.diagnostic_count == 1 &&
                               aarch64_modifier_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_modifier_operand.symbol_count == 0 && aarch64_modifier_operand.bytes.length == 0);
    AssemblyEncodeResult aarch64_trailing_modifier_operand = assembly_encode(
        arguments->arena, S8("b target, :lo12:other\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_trailing_modifier_operand.diagnostic_count == 1 &&
                               aarch64_trailing_modifier_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_trailing_modifier_operand.symbol_count == 1 &&
                               string_equal(aarch64_trailing_modifier_operand.symbols[0].name, S8("target")) &&
                               aarch64_trailing_modifier_operand.bytes.length == 0);
    AssemblyEncodeResult invalid_leading_label = assembly_encode(
        arguments->arena, S8("bad-label: nop\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, invalid_leading_label.diagnostic_count == 1 &&
                               invalid_leading_label.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT &&
                               invalid_leading_label.symbol_count == 0 && invalid_leading_label.bytes.length == 4);

    AssemblyEncodeResult aarch64_six_operands = assembly_encode(
        arguments->arena, S8("b one, two, three, four, five, six\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_six_operands.diagnostic_count == 1 &&
                               aarch64_six_operands.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_six_operands.bytes.length == 0);
    AssemblyEncodeResult unchanged_x86_diagnostic = assembly_encode(
        arguments->arena, S8("mov rax, xmm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult unchanged_aarch64_diagnostic = assembly_encode(
        arguments->arena, S8("ret x0\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, unchanged_x86_diagnostic.diagnostic_count == 1 &&
                               unchanged_x86_diagnostic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               unchanged_x86_diagnostic.bytes.length == 0);
    BUSTER_TEST(arguments, unchanged_aarch64_diagnostic.diagnostic_count == 1 &&
                               unchanged_aarch64_diagnostic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               unchanged_aarch64_diagnostic.bytes.length == 0);

    AssemblyEncodeResult invalid_syntax = assembly_encode(arguments->arena, S8("nop"),
                                                           (AssemblyEncodeOptions){
                                                               .target = aarch64_target,
                                                               .syntax = ASSEMBLY_SYNTAX_ATT,
                                                           });
    BUSTER_TEST(arguments, invalid_syntax.diagnostic_count == 1 &&
                               invalid_syntax.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);
    Target advanced_target = x86_target;
    advanced_target.cpu_features_explicit = true;
    advanced_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL,
        TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512DQ,
        TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_AMX_TILE,
        TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_INT8}, 10);
    AssemblyEncodeResult advanced_evex = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {k1}{z}, zmm2, zmm3\n"
           "vaddps zmm0 {k1}, zmm1, dword ptr [rax]{1to16}\n"
           "vcmpps k1, zmm2, zmm3, 7\n"
           "vmovdqa64 zmm31 {k7}{z}, zmmword ptr [r15+64]\n"
           "vpcmpq k7, zmm30, zmm31, 7\n"
           "kmovw k1, k2\n"
           "kmovd k1, k2\n"
           "kmovq k1, k2\n"
           "kaddw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex[] = {
        0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xc3,
        0x62, 0xf1, 0x74, 0x59, 0x58, 0x00,
        0x62, 0xf1, 0x6c, 0x48, 0xc2, 0xcb, 0x07,
        0x62, 0x41, 0xfd, 0xcf, 0x6f, 0x7f, 0x01,
        0x62, 0x93, 0x8d, 0x40, 0x1f, 0xff, 0x07,
        0xc5, 0xf8, 0x90, 0xca,
        0xc4, 0xe1, 0xf9, 0x90, 0xca,
        0xc4, 0xe1, 0xf8, 0x90, 0xca,
        0xc5, 0xec, 0x4a, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_evex.diagnostic_count == 0 &&
                               advanced_evex.bytes.length == sizeof(expected_advanced_evex) &&
                               memcmp(advanced_evex.bytes.pointer, expected_advanced_evex, sizeof(expected_advanced_evex)) == 0);

    AssemblyEncodeResult advanced_vmovdqu = assembly_encode(
        arguments->arena,
        S8("vmovdqu8 zmm1, zmm2\n"
           "vmovdqu16 zmm1, zmm2\n"
           "vmovdqu16 zmmword ptr [rax], zmm1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vmovdqu[] = {
        0x62, 0xf1, 0x7f, 0x48, 0x6f, 0xca,
        0x62, 0xf1, 0xff, 0x48, 0x6f, 0xca,
        0x62, 0xf1, 0xff, 0x48, 0x7f, 0x08,
    };
    BUSTER_TEST(arguments, advanced_vmovdqu.diagnostic_count == 0 &&
                               advanced_vmovdqu.bytes.length == sizeof(expected_advanced_vmovdqu) &&
                               memcmp(advanced_vmovdqu.bytes.pointer, expected_advanced_vmovdqu,
                                      sizeof(expected_advanced_vmovdqu)) == 0);

    AssemblyEncodeResult advanced_opmask_binary = assembly_encode(
        arguments->arena, S8("kandw k1, k2, k3\nkorw k1, k2, k3\nkxorw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_opmask_binary[] = {
        0xc5, 0xec, 0x41, 0xcb,
        0xc5, 0xec, 0x45, 0xcb,
        0xc5, 0xec, 0x47, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_opmask_binary.diagnostic_count == 0 &&
                               advanced_opmask_binary.bytes.length == sizeof(expected_advanced_opmask_binary) &&
                               memcmp(advanced_opmask_binary.bytes.pointer, expected_advanced_opmask_binary,
                                      sizeof(expected_advanced_opmask_binary)) == 0);

    AssemblyEncodeResult advanced_evex_memory = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r15+r14*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_memory[] = {0x62, 0x91, 0x74, 0x48, 0x58, 0x44, 0xb7, 0x01};
    BUSTER_TEST(arguments, advanced_evex_memory.diagnostic_count == 0 &&
                               advanced_evex_memory.bytes.length == sizeof(expected_advanced_evex_memory) &&
                               memcmp(advanced_evex_memory.bytes.pointer, expected_advanced_evex_memory,
                                      sizeof(expected_advanced_evex_memory)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_base = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r16]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_base[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
    BUSTER_TEST(arguments, advanced_evex_egpr_base.diagnostic_count == 0 &&
                               advanced_evex_egpr_base.bytes.length == sizeof(expected_advanced_evex_egpr_base) &&
                               memcmp(advanced_evex_egpr_base.bytes.pointer, expected_advanced_evex_egpr_base,
                                      sizeof(expected_advanced_evex_egpr_base)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_sib = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_sib[] = {0x62, 0x99, 0x70, 0x48, 0x58, 0x44, 0x88, 0x01};
    BUSTER_TEST(arguments, advanced_evex_egpr_sib.diagnostic_count == 0 &&
                               advanced_evex_egpr_sib.bytes.length == sizeof(expected_advanced_evex_egpr_sib) &&
                               memcmp(advanced_evex_egpr_sib.bytes.pointer, expected_advanced_evex_egpr_sib,
                                      sizeof(expected_advanced_evex_egpr_sib)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_index = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_index[] = {
        0x62, 0xb1, 0x70, 0x48, 0x58, 0x04, 0x8d, 0x40, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, advanced_evex_egpr_index.diagnostic_count == 0 &&
                               advanced_evex_egpr_index.bytes.length == sizeof(expected_advanced_evex_egpr_index) &&
                               memcmp(advanced_evex_egpr_index.bytes.pointer, expected_advanced_evex_egpr_index,
                                      sizeof(expected_advanced_evex_egpr_index)) == 0);

    AssemblyEncodeResult advanced_evex_rip = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [rip+external]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_rip[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x05, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, advanced_evex_rip.diagnostic_count == 0 &&
                               advanced_evex_rip.bytes.length == sizeof(expected_advanced_evex_rip) &&
                               memcmp(advanced_evex_rip.bytes.pointer, expected_advanced_evex_rip,
                                      sizeof(expected_advanced_evex_rip)) == 0 &&
                               advanced_evex_rip.relocation_count == 1 && advanced_evex_rip.relocations[0].offset == 6 &&
                               advanced_evex_rip.relocations[0].addend == -4 &&
                               advanced_evex_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmp_rip = assembly_encode(
        arguments->arena, S8("vcmpps k1, zmm2, zmmword ptr [rip+cmp_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_cmp_rip[] = {0x62, 0xf1, 0x6c, 0x48, 0xc2, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_cmp_rip.diagnostic_count == 0 &&
                               advanced_evex_cmp_rip.bytes.length == sizeof(expected_advanced_evex_cmp_rip) &&
                               memcmp(advanced_evex_cmp_rip.bytes.pointer, expected_advanced_evex_cmp_rip,
                                      sizeof(expected_advanced_evex_cmp_rip)) == 0 &&
                               advanced_evex_cmp_rip.relocation_count == 1 && advanced_evex_cmp_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmp_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmp_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmpd_rip = assembly_encode(
        arguments->arena, S8("vcmppd k1, zmm2, zmmword ptr [rip+cmpd_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_cmpd_rip[] = {0x62, 0xf1, 0xed, 0x48, 0xc2, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_cmpd_rip.diagnostic_count == 0 &&
                               advanced_evex_cmpd_rip.bytes.length == sizeof(expected_advanced_evex_cmpd_rip) &&
                               memcmp(advanced_evex_cmpd_rip.bytes.pointer, expected_advanced_evex_cmpd_rip,
                                      sizeof(expected_advanced_evex_cmpd_rip)) == 0 &&
                               advanced_evex_cmpd_rip.relocation_count == 1 && advanced_evex_cmpd_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmpd_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmpd_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_pcmpq_rip = assembly_encode(
        arguments->arena, S8("vpcmpq k1, zmm2, zmmword ptr [rip+pcmpq_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_pcmpq_rip[] = {0x62, 0xf3, 0xed, 0x48, 0x1f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_pcmpq_rip.diagnostic_count == 0 &&
                               advanced_evex_pcmpq_rip.bytes.length == sizeof(expected_advanced_evex_pcmpq_rip) &&
                               memcmp(advanced_evex_pcmpq_rip.bytes.pointer, expected_advanced_evex_pcmpq_rip,
                                      sizeof(expected_advanced_evex_pcmpq_rip)) == 0 &&
                               advanced_evex_pcmpq_rip.relocation_count == 1 && advanced_evex_pcmpq_rip.relocations[0].offset == 6 &&
                               advanced_evex_pcmpq_rip.relocations[0].addend == -5 &&
                               advanced_evex_pcmpq_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_pcmpud_rip = assembly_encode(
        arguments->arena, S8("vpcmpud k1, zmm2, zmmword ptr [rip+pcmpud_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_pcmpud_rip[] = {0x62, 0xf3, 0x6d, 0x48, 0x1e, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_pcmpud_rip.diagnostic_count == 0 &&
                               advanced_evex_pcmpud_rip.bytes.length == sizeof(expected_advanced_evex_pcmpud_rip) &&
                               memcmp(advanced_evex_pcmpud_rip.bytes.pointer, expected_advanced_evex_pcmpud_rip,
                                      sizeof(expected_advanced_evex_pcmpud_rip)) == 0 &&
                               advanced_evex_pcmpud_rip.relocation_count == 1 && advanced_evex_pcmpud_rip.relocations[0].offset == 6 &&
                               advanced_evex_pcmpud_rip.relocations[0].addend == -5 &&
                               advanced_evex_pcmpud_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_round_rip = assembly_encode(
        arguments->arena, S8("vrndscaleps zmm0, zmmword ptr [rip+round_external], 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_round_rip[] = {0x62, 0xf3, 0x7d, 0x48, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04};
    BUSTER_TEST(arguments, advanced_evex_round_rip.diagnostic_count == 0 &&
                               advanced_evex_round_rip.bytes.length == sizeof(expected_advanced_evex_round_rip) &&
                               memcmp(advanced_evex_round_rip.bytes.pointer, expected_advanced_evex_round_rip,
                                      sizeof(expected_advanced_evex_round_rip)) == 0 &&
                               advanced_evex_round_rip.relocation_count == 1 && advanced_evex_round_rip.relocations[0].offset == 6 &&
                               advanced_evex_round_rip.relocations[0].addend == -5 &&
                               advanced_evex_round_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_roundd_rip = assembly_encode(
        arguments->arena, S8("vrndscalepd zmm0, zmmword ptr [rip+roundd_external], 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_roundd_rip[] = {0x62, 0xf3, 0xfd, 0x48, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04};
    BUSTER_TEST(arguments, advanced_evex_roundd_rip.diagnostic_count == 0 &&
                               advanced_evex_roundd_rip.bytes.length == sizeof(expected_advanced_evex_roundd_rip) &&
                               memcmp(advanced_evex_roundd_rip.bytes.pointer, expected_advanced_evex_roundd_rip,
                                      sizeof(expected_advanced_evex_roundd_rip)) == 0 &&
                               advanced_evex_roundd_rip.relocation_count == 1 && advanced_evex_roundd_rip.relocations[0].offset == 6 &&
                               advanced_evex_roundd_rip.relocations[0].addend == -5 &&
                               advanced_evex_roundd_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmp_att_rip = assembly_encode(
        arguments->arena, S8("vcmpps $7, cmp_att_external(%rip), %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_cmp_att_rip.diagnostic_count == 0 &&
                               advanced_evex_cmp_att_rip.bytes.length == sizeof(expected_advanced_evex_cmp_rip) &&
                               memcmp(advanced_evex_cmp_att_rip.bytes.pointer, expected_advanced_evex_cmp_rip,
                                      sizeof(expected_advanced_evex_cmp_rip)) == 0 &&
                               advanced_evex_cmp_att_rip.relocation_count == 1 &&
                               advanced_evex_cmp_att_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmp_att_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmp_att_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_att = assembly_encode(
        arguments->arena,
        S8("vaddps %zmm3, %zmm2, %zmm0\n"
           "vcmpPS $7, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, %zmm30, %zmm31\n"
           "vaddps {rn-sae}, %zmm2, %zmm1, %zmm0 {%k1}{z}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_att[] = {
        0x62, 0xf1, 0x6c, 0x48, 0x58, 0xc3,
        0x62, 0xf1, 0x6c, 0x48, 0xc2, 0xcb, 0x07,
        0x62, 0x03, 0x7d, 0x48, 0x08, 0xfe, 0x04,
        0x62, 0xf1, 0x74, 0x99, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_att.diagnostic_count == 0 && advanced_att.bytes.length == sizeof(expected_advanced_att) &&
                               memcmp(advanced_att.bytes.pointer, expected_advanced_att, sizeof(expected_advanced_att)) == 0);

    AssemblyEncodeResult advanced_evex_masked_forms = assembly_encode(
        arguments->arena,
        S8("vcmpps k1 {k2}, zmm2, zmm3, 7\n"
           "vcmpps k1 {k2}, zmm2, zmmword ptr [rax+64], 7\n"
           "vmovdqa64 zmmword ptr [rax] {k1}, zmm2\n"
           "vmovdqa64 zmmword ptr [rax+r8*4+64] {k3}, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_masked_forms[] = {
        0x62, 0xf1, 0x6c, 0x4a, 0xc2, 0xcb, 0x07,
        0x62, 0xf1, 0x6c, 0x4a, 0xc2, 0x48, 0x01, 0x07,
        0x62, 0xf1, 0xfd, 0x49, 0x7f, 0x10,
        0x62, 0xb1, 0xfd, 0x4b, 0x7f, 0x54, 0x80, 0x01,
    };
    BUSTER_TEST(arguments, advanced_evex_masked_forms.diagnostic_count == 0 &&
                               advanced_evex_masked_forms.bytes.length == sizeof(expected_advanced_evex_masked_forms) &&
                               memcmp(advanced_evex_masked_forms.bytes.pointer, expected_advanced_evex_masked_forms,
                                      sizeof(expected_advanced_evex_masked_forms)) == 0);

    AssemblyEncodeResult advanced_evex_masked_forms_att = assembly_encode(
        arguments->arena,
        S8("vcmpps $7, %zmm3, %zmm2, %k1 {%k2}\n"
           "vcmpps $7, 64(%rax), %zmm2, %k1 {%k2}\n"
           "vmovdqa64 %zmm2, (%rax) {%k1}\n"
           "vmovdqa64 %zmm2, 64(%rax,%r8,4) {%k3}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_masked_forms_att.diagnostic_count == 0 &&
                               advanced_evex_masked_forms_att.bytes.length == sizeof(expected_advanced_evex_masked_forms) &&
                               memcmp(advanced_evex_masked_forms_att.bytes.pointer, expected_advanced_evex_masked_forms,
                                      sizeof(expected_advanced_evex_masked_forms)) == 0);

    AssemblyEncodeResult advanced_evex_integer_compare_masks = assembly_encode(
        arguments->arena,
        S8("vpcmpeqb k1, zmm2, zmm3\n"
           "vpcmpgtq k1, zmm2, zmm3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_integer_compare_masks[] = {
        0x62, 0xf1, 0x6d, 0x48, 0x74, 0xcb,
        0x62, 0xf2, 0xed, 0x48, 0x37, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_evex_integer_compare_masks.diagnostic_count == 0 &&
                               advanced_evex_integer_compare_masks.bytes.length == sizeof(expected_advanced_evex_integer_compare_masks) &&
                               memcmp(advanced_evex_integer_compare_masks.bytes.pointer,
                                      expected_advanced_evex_integer_compare_masks,
                                      sizeof(expected_advanced_evex_integer_compare_masks)) == 0);

    AssemblyEncodeResult advanced_evex_integer_compare_masks_att = assembly_encode(
        arguments->arena,
        S8("vpcmpeqb %zmm3, %zmm2, %k1\n"
           "vpcmpgtq %zmm3, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_integer_compare_masks_att.diagnostic_count == 0 &&
                               advanced_evex_integer_compare_masks_att.bytes.length == sizeof(expected_advanced_evex_integer_compare_masks) &&
                               memcmp(advanced_evex_integer_compare_masks_att.bytes.pointer,
                                      expected_advanced_evex_integer_compare_masks,
                                      sizeof(expected_advanced_evex_integer_compare_masks)) == 0);

    AssemblyEncodeResult advanced_evex_low_mask_compare = assembly_encode(
        arguments->arena,
        S8("vpcmpeqd k1, xmm2, xmm3\n"
           "vpcmpgtq k1, ymm2, ymm3\n"
           "vpcmpw k1, zmm2, zmm3, 7\n"
           "vxorps zmm0, zmm1, dword ptr [rax]{1to16}\n"
           "vxorpd zmm0, zmm1, qword ptr [rax]{1to8}\n"
           "vpaddd zmm0, zmm1, dword ptr [rax]{1to16}\n"
           "vpcmpgtq k1, zmm2, qword ptr [rax]{1to8}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_low_mask_compare[] = {
        0x62, 0xf1, 0x6d, 0x08, 0x76, 0xcb,
        0x62, 0xf2, 0xed, 0x28, 0x37, 0xcb,
        0x62, 0xf3, 0xed, 0x48, 0x3f, 0xcb, 0x07,
        0x62, 0xf1, 0x74, 0x58, 0x57, 0x00,
        0x62, 0xf1, 0xf5, 0x58, 0x57, 0x00,
        0x62, 0xf1, 0x75, 0x58, 0xfe, 0x00,
        0x62, 0xf2, 0xed, 0x58, 0x37, 0x08,
    };
    BUSTER_TEST(arguments, advanced_evex_low_mask_compare.diagnostic_count == 0 &&
                               advanced_evex_low_mask_compare.bytes.length == sizeof(expected_advanced_evex_low_mask_compare) &&
                               memcmp(advanced_evex_low_mask_compare.bytes.pointer, expected_advanced_evex_low_mask_compare,
                                      sizeof(expected_advanced_evex_low_mask_compare)) == 0);

    AssemblyEncodeResult advanced_evex_low_mask_compare_att = assembly_encode(
        arguments->arena,
        S8("vpcmpeqd %xmm3, %xmm2, %k1\n"
           "vpcmpgtq %ymm3, %ymm2, %k1\n"
           "vpcmpw $7, %zmm3, %zmm2, %k1\n"
           "vxorps (%rax){1to16}, %zmm1, %zmm0\n"
           "vxorpd (%rax){1to8}, %zmm1, %zmm0\n"
           "vpaddd (%rax){1to16}, %zmm1, %zmm0\n"
           "vpcmpgtq (%rax){1to8}, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_low_mask_compare_att.diagnostic_count == 0 &&
                               advanced_evex_low_mask_compare_att.bytes.length == sizeof(expected_advanced_evex_low_mask_compare) &&
                               memcmp(advanced_evex_low_mask_compare_att.bytes.pointer, expected_advanced_evex_low_mask_compare,
                                      sizeof(expected_advanced_evex_low_mask_compare)) == 0);

    AssemblyEncodeResult advanced_evex_canonical_decorators = assembly_encode(
        arguments->arena,
        S8("vcmpps k1, zmm2, zmm3, {sae}, 7\n"
           "vcmppd k1, zmm2, zmm3, {sae}, 7\n"
           "vrndscaleps zmm0, zmm1, {sae}, 4\n"
           "vrndscalepd zmm0, zmm1, {sae}, 4\n"
           "vaddps zmm0, zmm1, zmm2, {rn-sae}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_canonical_decorators[] = {
        0x62, 0xf1, 0x6c, 0x18, 0xc2, 0xcb, 0x07,
        0x62, 0xf1, 0xed, 0x18, 0xc2, 0xcb, 0x07,
        0x62, 0xf3, 0x7d, 0x18, 0x08, 0xc1, 0x04,
        0x62, 0xf3, 0xfd, 0x18, 0x09, 0xc1, 0x04,
        0x62, 0xf1, 0x74, 0x18, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_evex_canonical_decorators.diagnostic_count == 0 &&
                               advanced_evex_canonical_decorators.bytes.length == sizeof(expected_advanced_evex_canonical_decorators) &&
                               memcmp(advanced_evex_canonical_decorators.bytes.pointer, expected_advanced_evex_canonical_decorators,
                                      sizeof(expected_advanced_evex_canonical_decorators)) == 0);

    AssemblyEncodeResult advanced_evex_canonical_decorators_att = assembly_encode(
        arguments->arena,
        S8("vcmpps $7, {sae}, %zmm3, %zmm2, %k1\n"
           "vcmppd $7, {sae}, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, {sae}, %zmm1, %zmm0\n"
           "vrndscalepd $4, {sae}, %zmm1, %zmm0\n"
           "vaddps {rn-sae}, %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_canonical_decorators_att.diagnostic_count == 0 &&
                               advanced_evex_canonical_decorators_att.bytes.length == sizeof(expected_advanced_evex_canonical_decorators) &&
                               memcmp(advanced_evex_canonical_decorators_att.bytes.pointer, expected_advanced_evex_canonical_decorators,
                                      sizeof(expected_advanced_evex_canonical_decorators)) == 0);

    AssemblyEncodeResult advanced_vectors = assembly_encode(
        arguments->arena,
        S8("vaddps xmm16, xmm17, xmm18\n"
           "vaddps ymm16, ymm17, ymm18\n"
           "vaddps zmm16, zmm17, zmm18\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vectors[] = {
        0x62, 0xa1, 0x74, 0x00, 0x58, 0xc2,
        0x62, 0xa1, 0x74, 0x20, 0x58, 0xc2,
        0x62, 0xa1, 0x74, 0x40, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_vectors.diagnostic_count == 0 &&
                               advanced_vectors.bytes.length == sizeof(expected_advanced_vectors) &&
                               memcmp(advanced_vectors.bytes.pointer, expected_advanced_vectors, sizeof(expected_advanced_vectors)) == 0);

    AssemblyEncodeResult advanced_apx = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, r18d\n"
           "{nf} add r16d, r17d\n"
           "{nf} add dword ptr [r16], r17d\n"
           "{nf} add dword ptr [r16+r17*4], r18d\n"
           "{nf} add dword ptr [r24+r25*4+64], r26d\n"
           "add r24, r25, r26\n"
           "mov r16d, r17d\n"
           "add r16d, dword ptr [r17]\n"
           "push r16\n"
           "pop r17\n"
           "push2 r16, r17\n"
           "pop2 r16, r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx[] = {
        0x62, 0xec, 0x7c, 0x10, 0x01, 0xd1,
        0x62, 0xec, 0x7c, 0x0c, 0x01, 0xc8,
        0x62, 0xec, 0x7c, 0x0c, 0x01, 0x08,
        0x62, 0xec, 0x78, 0x0c, 0x01, 0x14, 0x88,
        0x62, 0x0c, 0x78, 0x0c, 0x01, 0x54, 0x88, 0x40,
        0x62, 0x4c, 0xbc, 0x10, 0x01, 0xd1,
        0xd5, 0x50, 0x89, 0xc8,
        0xd5, 0x50, 0x03, 0x01,
        0xd5, 0x10, 0x50,
        0xd5, 0x10, 0x59,
        0x62, 0xfc, 0x7c, 0x10, 0xff, 0xf1,
        0x62, 0xfc, 0x7c, 0x10, 0x8f, 0xc1,
    };
    BUSTER_TEST(arguments, advanced_apx.diagnostic_count == 0 && advanced_apx.bytes.length == sizeof(expected_advanced_apx) &&
                               memcmp(advanced_apx.bytes.pointer, expected_advanced_apx, sizeof(expected_advanced_apx)) == 0);

    AssemblyEncodeResult advanced_apx_att_nf = assembly_encode(
        arguments->arena,
        S8("{nf} addl %r18d, (%r16,%r17,4)\n"
           "{nf} addl %r26d, 64(%r24,%r25,4)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_apx_att_nf[] = {
        0x62, 0xec, 0x78, 0x0c, 0x01, 0x14, 0x88,
        0x62, 0x0c, 0x78, 0x0c, 0x01, 0x54, 0x88, 0x40,
    };
    BUSTER_TEST(arguments, advanced_apx_att_nf.diagnostic_count == 0 &&
                               advanced_apx_att_nf.bytes.length == sizeof(expected_advanced_apx_att_nf) &&
                               memcmp(advanced_apx_att_nf.bytes.pointer, expected_advanced_apx_att_nf,
                                      sizeof(expected_advanced_apx_att_nf)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_memory_immediate = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [r18]\n"
           "add r16d, dword ptr [r18], 5\n"
           "add r16d, r17d, 5\n"
           "{nf} add r16d, 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_memory_immediate[] = {
        0x62, 0xec, 0x7c, 0x10, 0x03, 0x0a,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0x02, 0x05,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0xc1, 0x05,
        0x62, 0xfc, 0x7c, 0x0c, 0x83, 0xc0, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_memory_immediate.diagnostic_count == 0 &&
                               advanced_apx_ndd_memory_immediate.bytes.length == sizeof(expected_advanced_apx_ndd_memory_immediate) &&
                               memcmp(advanced_apx_ndd_memory_immediate.bytes.pointer, expected_advanced_apx_ndd_memory_immediate,
                                      sizeof(expected_advanced_apx_ndd_memory_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_egpr_sib = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_egpr_sib[] = {0x62, 0x8c, 0x78, 0x10, 0x03, 0x4c, 0x88, 0x40};
    BUSTER_TEST(arguments, advanced_apx_ndd_egpr_sib.diagnostic_count == 0 &&
                               advanced_apx_ndd_egpr_sib.bytes.length == sizeof(expected_advanced_apx_ndd_egpr_sib) &&
                               memcmp(advanced_apx_ndd_egpr_sib.bytes.pointer, expected_advanced_apx_ndd_egpr_sib,
                                      sizeof(expected_advanced_apx_ndd_egpr_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_immediate_sib = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [r24+r25*4+64], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_immediate_sib[] = {
        0x62, 0x9c, 0x78, 0x10, 0x83, 0x44, 0x88, 0x40, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_sib.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_sib.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_sib) &&
                               memcmp(advanced_apx_ndd_immediate_sib.bytes.pointer, expected_advanced_apx_ndd_immediate_sib,
                                      sizeof(expected_advanced_apx_ndd_immediate_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_immediate_sib_att = assembly_encode(
        arguments->arena,
        S8("addl $5, 64(%r24,%r25,4), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_sib_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_sib_att.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_sib) &&
                               memcmp(advanced_apx_ndd_immediate_sib_att.bytes.pointer, expected_advanced_apx_ndd_immediate_sib,
                                      sizeof(expected_advanced_apx_ndd_immediate_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_relocation = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [rip+apx_ndd_external]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_relocation[] = {0x62, 0xe4, 0x7c, 0x10, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.diagnostic_count == 0);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.bytes.length == sizeof(expected_advanced_apx_ndd_relocation));
    BUSTER_TEST(arguments, memcmp(advanced_apx_ndd_relocation.bytes.pointer, expected_advanced_apx_ndd_relocation,
                                  sizeof(expected_advanced_apx_ndd_relocation)) == 0);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocation_count == 1);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].offset == 6);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].addend == -4);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_immediate_relocation = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [rip+apx_ndd_immediate_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_immediate_relocation[] = {
        0x62, 0xf4, 0x7c, 0x10, 0x83, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_relocation.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_relocation.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_relocation) &&
                               memcmp(advanced_apx_ndd_immediate_relocation.bytes.pointer,
                                      expected_advanced_apx_ndd_immediate_relocation,
                                      sizeof(expected_advanced_apx_ndd_immediate_relocation)) == 0 &&
                               advanced_apx_ndd_immediate_relocation.relocation_count == 1 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].offset == 6 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].addend == -5 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_immediate_relocation_att = assembly_encode(
        arguments->arena,
        S8("addl $5, apx_ndd_immediate_att_external(%rip), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_relocation_att.bytes.length ==
                                   sizeof(expected_advanced_apx_ndd_immediate_relocation) &&
                               memcmp(advanced_apx_ndd_immediate_relocation_att.bytes.pointer,
                                      expected_advanced_apx_ndd_immediate_relocation,
                                      sizeof(expected_advanced_apx_ndd_immediate_relocation)) == 0 &&
                               advanced_apx_ndd_immediate_relocation_att.relocation_count == 1 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_memory_immediate_relocation = assembly_encode(
        arguments->arena,
        S8("{nf} add dword ptr [rip+apx_nf_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_memory_immediate_relocation[] = {
        0x62, 0xf4, 0x7c, 0x0c, 0x83, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_memory_immediate_relocation.diagnostic_count == 0 &&
                               advanced_apx_nf_memory_immediate_relocation.bytes.length ==
                                   sizeof(expected_advanced_apx_nf_memory_immediate_relocation) &&
                               memcmp(advanced_apx_nf_memory_immediate_relocation.bytes.pointer,
                                      expected_advanced_apx_nf_memory_immediate_relocation,
                                      sizeof(expected_advanced_apx_nf_memory_immediate_relocation)) == 0 &&
                               advanced_apx_nf_memory_immediate_relocation.relocation_count == 1 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].offset == 6 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].addend == -5 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_memory_immediate_att = assembly_encode(
        arguments->arena,
        S8("addl (%r18), %r17d, %r16d\n"
           "addl $5, (%r18), %r16d\n"
           "addl $5, %r17d, %r16d\n"
           "{nf} addl $5, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_memory_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_memory_immediate_att.bytes.length == sizeof(expected_advanced_apx_ndd_memory_immediate) &&
                               memcmp(advanced_apx_ndd_memory_immediate_att.bytes.pointer, expected_advanced_apx_ndd_memory_immediate,
                                      sizeof(expected_advanced_apx_ndd_memory_immediate)) == 0);

    AssemblyEncodeResult invalid_apx_att_immediate_order = assembly_encode(
        arguments->arena,
        S8("addl %r17d, $5, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_att_immediate_order.diagnostic_count == 1 &&
                               invalid_apx_att_immediate_order.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult advanced_apx_ndd_carry = assembly_encode(
        arguments->arena,
        S8("adc r16d, r17d, r18d\n"
           "sbb r16d, r17d, 5\n"
           "adc r16d, r17d, dword ptr [r18]\n"
           "sbb r16d, dword ptr [r18], r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_carry[] = {
        0x62, 0xec, 0x7c, 0x10, 0x11, 0xd1,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0xd9, 0x05,
        0x62, 0xec, 0x7c, 0x10, 0x13, 0x0a,
        0x62, 0xec, 0x7c, 0x10, 0x19, 0x0a,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_carry.diagnostic_count == 0 &&
                               advanced_apx_ndd_carry.bytes.length == sizeof(expected_advanced_apx_ndd_carry) &&
                               memcmp(advanced_apx_ndd_carry.bytes.pointer, expected_advanced_apx_ndd_carry,
                                      sizeof(expected_advanced_apx_ndd_carry)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_carry_att = assembly_encode(
        arguments->arena,
        S8("adcl %r18d, %r17d, %r16d\n"
           "sbbl $5, %r17d, %r16d\n"
           "adcl (%r18), %r17d, %r16d\n"
           "sbbl %r17d, (%r18), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_carry_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_carry_att.bytes.length == sizeof(expected_advanced_apx_ndd_carry) &&
                               memcmp(advanced_apx_ndd_carry_att.bytes.pointer, expected_advanced_apx_ndd_carry,
                                      sizeof(expected_advanced_apx_ndd_carry)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_nf = assembly_encode(
        arguments->arena,
        S8("{nf} add r16d, r17d, r18d\n"
           "{nf} sub r24, r25, qword ptr [r26+r27*8+64]\n"
           "{nf} xor r16b, r17b, 255\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_nf[] = {
        0x62, 0xec, 0x7c, 0x14, 0x01, 0xd1,
        0x62, 0x0c, 0xb8, 0x14, 0x2b, 0x4c, 0xda, 0x40,
        0x62, 0xfc, 0x7c, 0x14, 0x80, 0xf1, 0xff,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_nf.diagnostic_count == 0 &&
                               advanced_apx_ndd_nf.bytes.length == sizeof(expected_advanced_apx_ndd_nf) &&
                               memcmp(advanced_apx_ndd_nf.bytes.pointer, expected_advanced_apx_ndd_nf,
                                      sizeof(expected_advanced_apx_ndd_nf)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_nf_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl %r18d, %r17d, %r16d\n"
           "{nf} subq 64(%r26,%r27,8), %r25, %r24\n"
           "{nf} xorb $255, %r17b, %r16b\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_nf_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_nf_att.bytes.length == sizeof(expected_advanced_apx_ndd_nf) &&
                               memcmp(advanced_apx_ndd_nf_att.bytes.pointer, expected_advanced_apx_ndd_nf,
                                      sizeof(expected_advanced_apx_ndd_nf)) == 0);

    AssemblyEncodeResult invalid_apx_nf_carry = assembly_encode(
        arguments->arena,
        S8("{nf} adc r16d, r17d\n"
           "{nf} sbb r16d, r17d\n"
           "{nf} adc r16d, r17d, r18d\n"
           "{nf} sbb r16d, r17d, r18d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_nf_carry.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_nf_carry.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_nf_carry.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_apx_nf_carry_att = assembly_encode(
        arguments->arena,
        S8("{nf} adcl %r17d, %r16d\n"
           "{nf} sbbl %r17d, %r16d\n"
           "{nf} adcl %r18d, %r17d, %r16d\n"
           "{nf} sbbl %r18d, %r17d, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_nf_carry_att.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_nf_carry_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_nf_carry_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_apx_nf_memory_immediate_relocation_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl $5, apx_nf_att_external(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_memory_immediate_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_nf_memory_immediate_relocation_att.bytes.length ==
                                   sizeof(expected_advanced_apx_nf_memory_immediate_relocation) &&
                               memcmp(advanced_apx_nf_memory_immediate_relocation_att.bytes.pointer,
                                      expected_advanced_apx_nf_memory_immediate_relocation,
                                      sizeof(expected_advanced_apx_nf_memory_immediate_relocation)) == 0 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocation_count == 1 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_shift_relocation = assembly_encode(
        arguments->arena,
        S8("{nf} shl dword ptr [rip+apx_nf_shift_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_shift_relocation[] = {
        0x62, 0xf4, 0x7c, 0x0c, 0xc1, 0x25, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_shift_relocation.diagnostic_count == 0 &&
                               advanced_apx_nf_shift_relocation.bytes.length == sizeof(expected_advanced_apx_nf_shift_relocation) &&
                               memcmp(advanced_apx_nf_shift_relocation.bytes.pointer, expected_advanced_apx_nf_shift_relocation,
                                      sizeof(expected_advanced_apx_nf_shift_relocation)) == 0 &&
                               advanced_apx_nf_shift_relocation.relocation_count == 1 &&
                               advanced_apx_nf_shift_relocation.relocations[0].offset == 6 &&
                               advanced_apx_nf_shift_relocation.relocations[0].addend == -5 &&
                               advanced_apx_nf_shift_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_shift_relocation_att = assembly_encode(
        arguments->arena,
        S8("{nf} shll $5, apx_nf_shift_att_external(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_shift_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_nf_shift_relocation_att.bytes.length == sizeof(expected_advanced_apx_nf_shift_relocation) &&
                               memcmp(advanced_apx_nf_shift_relocation_att.bytes.pointer, expected_advanced_apx_nf_shift_relocation,
                                      sizeof(expected_advanced_apx_nf_shift_relocation)) == 0 &&
                               advanced_apx_nf_shift_relocation_att.relocation_count == 1 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_apx_ndd_memory_immediate = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [r17], dword ptr [r18]\n"
           "add r16d, 4294967296\n"
           "{nf} add dword ptr [r16], 4294967296\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_ndd_memory_immediate.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_ndd_memory_immediate.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_ndd_memory_immediate.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_vrndscale_signed_immediate = assembly_encode(
        arguments->arena,
        S8("vrndscaleps zmm0, zmm1, -1\n"
           "vrndscaleps zmm0, zmm1, -128\n"
           "vrndscaleps zmm0, zmm1, 255\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vrndscale_signed_immediate[] = {
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0xff,
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0x80,
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0xff,
    };
    BUSTER_TEST(arguments, advanced_vrndscale_signed_immediate.diagnostic_count == 0 &&
                               advanced_vrndscale_signed_immediate.bytes.length == sizeof(expected_advanced_vrndscale_signed_immediate) &&
                               memcmp(advanced_vrndscale_signed_immediate.bytes.pointer, expected_advanced_vrndscale_signed_immediate,
                                      sizeof(expected_advanced_vrndscale_signed_immediate)) == 0);

    AssemblyEncodeResult advanced_vrndscale_signed_immediate_att = assembly_encode(
        arguments->arena,
        S8("vrndscaleps $-1, %zmm1, %zmm0\n"
           "vrndscaleps $-128, %zmm1, %zmm0\n"
           "vrndscaleps $255, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_vrndscale_signed_immediate_att.diagnostic_count == 0 &&
                               advanced_vrndscale_signed_immediate_att.bytes.length == sizeof(expected_advanced_vrndscale_signed_immediate) &&
                               memcmp(advanced_vrndscale_signed_immediate_att.bytes.pointer, expected_advanced_vrndscale_signed_immediate,
                                      sizeof(expected_advanced_vrndscale_signed_immediate)) == 0);

    AssemblyEncodeResult invalid_vrndscale_signed_immediate = assembly_encode(
        arguments->arena,
        S8("vrndscaleps zmm0, zmm1, -129\n"
           "vrndscaleps zmm0, zmm1, 256\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vrndscale_signed_immediate.diagnostic_count == 2);
    AssemblyEncodeResult invalid_vrndscale_signed_immediate_att = assembly_encode(
        arguments->arena,
        S8("vrndscaleps $-129, %zmm1, %zmm0\n"
           "vrndscaleps $256, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_vrndscale_signed_immediate_att.diagnostic_count == 2);

    AssemblyEncodeResult invalid_nf_aliases = assembly_encode(
        arguments->arena,
        S8("{nf} add{nf} r16d, r17d\n"
           "{nf} addnf r16d, r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_nf_aliases.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_nf_aliases.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_nf_aliases.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }
    AssemblyEncodeResult invalid_nf_aliases_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl{nf} %r17d, %r16d\n"
           "{nf} addnfl %r17d, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_nf_aliases_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_nf_aliases_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_nf_aliases_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }

    AssemblyEncodeResult advanced_apx_legacy_memory = assembly_encode(
        arguments->arena,
        S8("mov qword ptr [r16], r17\n"
           "add qword ptr [r16], r17\n"
           "mov qword ptr [r24+r25*4+64], r26\n"
           "add qword ptr [r24+r25*4+64], r26\n"
           "mov qword ptr [r16+apx_external], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_memory[] = {
        0xd5, 0x58, 0x89, 0x08,
        0xd5, 0x58, 0x01, 0x08,
        0xd5, 0x7f, 0x89, 0x54, 0x88, 0x40,
        0xd5, 0x7f, 0x01, 0x54, 0x88, 0x40,
        0xd5, 0x58, 0x89, 0x88, 0x00, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, advanced_apx_legacy_memory.diagnostic_count == 0 &&
                               advanced_apx_legacy_memory.bytes.length == sizeof(expected_advanced_apx_legacy_memory) &&
                               memcmp(advanced_apx_legacy_memory.bytes.pointer, expected_advanced_apx_legacy_memory,
                                      sizeof(expected_advanced_apx_legacy_memory)) == 0 &&
                               advanced_apx_legacy_memory.relocation_count == 1 &&
                               advanced_apx_legacy_memory.relocations[0].offset == 24 &&
                               advanced_apx_legacy_memory.relocations[0].addend == 0 &&
                               advanced_apx_legacy_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               string_equal(advanced_apx_legacy_memory.symbols[advanced_apx_legacy_memory.relocations[0].symbol].name,
                                            S8("apx_external")));

    AssemblyEncodeResult advanced_apx_legacy_memory_att = assembly_encode(
        arguments->arena,
        S8("movq %r17, (%r16)\n"
           "addq %r17, (%r16)\n"
           "movq %r26, 64(%r24,%r25,4)\n"
           "addq %r26, 64(%r24,%r25,4)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_apx_legacy_memory_att[] = {
        0xd5, 0x58, 0x89, 0x08,
        0xd5, 0x58, 0x01, 0x08,
        0xd5, 0x7f, 0x89, 0x54, 0x88, 0x40,
        0xd5, 0x7f, 0x01, 0x54, 0x88, 0x40,
    };
    BUSTER_TEST(arguments, advanced_apx_legacy_memory_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_memory_att.bytes.length == sizeof(expected_advanced_apx_legacy_memory_att) &&
                               memcmp(advanced_apx_legacy_memory_att.bytes.pointer, expected_advanced_apx_legacy_memory_att,
                                      sizeof(expected_advanced_apx_legacy_memory_att)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_families = assembly_encode(
        arguments->arena,
        S8("lea r16, [r17+r18*4+64]\n"
           "call r16\n"
           "jmp qword ptr [r19]\n"
           "movaps xmm0, [r16]\n"
           "movdqa xmm1, [r17]\n"
           "movdqu xmm0, [r16]\n"
           "addss xmm2, dword ptr [r18]\n"
           "addsd xmm3, qword ptr [r19]\n"
           "adc r16d, r17d\n"
           "sbb r18d, dword ptr [r19]\n"
           "imul r20d, r21d\n"
           "shl r22d, 3\n"
           "shl r23d, cl\n"
           "mov r16d, 5\n"
           "mov qword ptr [r16], 5\n"
           "add r16d, 5\n"
           "imul r16d, r17d, 5\n"
           "imul r16d, dword ptr [r18], 5\n"
           "imul r24\n"
           "imul qword ptr [r25]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_families[] = {
        0xd5, 0x78, 0x8d, 0x44, 0x91, 0x40,
        0xd5, 0x10, 0xff, 0xd0,
        0xd5, 0x10, 0xff, 0x23,
        0xd5, 0x90, 0x28, 0x00,
        0x66, 0xd5, 0x90, 0x6f, 0x09,
        0xf3, 0xd5, 0x90, 0x6f, 0x00,
        0xf3, 0xd5, 0x90, 0x58, 0x12,
        0xf2, 0xd5, 0x90, 0x58, 0x1b,
        0xd5, 0x50, 0x11, 0xc8,
        0xd5, 0x50, 0x1b, 0x13,
        0xd5, 0xd0, 0xaf, 0xe5,
        0xd5, 0x10, 0xc1, 0xe6, 0x03,
        0xd5, 0x10, 0xd3, 0xe7,
        0xd5, 0x10, 0xb8, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xc7, 0x00, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x10, 0x83, 0xc0, 0x05,
        0xd5, 0x50, 0x6b, 0xc1, 0x05,
        0xd5, 0x50, 0x6b, 0x02, 0x05,
        0xd5, 0x19, 0xf7, 0xe8,
        0xd5, 0x19, 0xf7, 0x29,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_families.diagnostic_count == 0 &&
                               advanced_apx_rex2_families.bytes.length == sizeof(expected_apx_rex2_families) &&
                               memcmp(advanced_apx_rex2_families.bytes.pointer, expected_apx_rex2_families,
                                      sizeof(expected_apx_rex2_families)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_families_att = assembly_encode(
        arguments->arena,
        S8("leaq 64(%r17,%r18,4), %r16\n"
           "call *%r16\n"
           "jmp *(%r19)\n"
           "movaps (%r16), %xmm0\n"
           "movdqa (%r17), %xmm1\n"
           "movdqu (%r16), %xmm0\n"
           "addss (%r18), %xmm2\n"
           "addsd (%r19), %xmm3\n"
           "adcl %r17d, %r16d\n"
           "sbbl (%r19), %r18d\n"
           "imull %r21d, %r20d\n"
           "shll $3, %r22d\n"
           "shll %cl, %r23d\n"
           "movl $5, %r16d\n"
           "movq $5, (%r16)\n"
           "addl $5, %r16d\n"
           "imull $5, %r17d, %r16d\n"
           "imull $5, (%r18), %r16d\n"
           "imulq %r24\n"
           "imulq (%r25)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_families_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_families_att.bytes.length == sizeof(expected_apx_rex2_families) &&
                               memcmp(advanced_apx_rex2_families_att.bytes.pointer, expected_apx_rex2_families,
                                      sizeof(expected_apx_rex2_families)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_unary = assembly_encode(
        arguments->arena,
        S8("inc r16b\n"
           "dec byte ptr [r17]\n"
           "neg r16b\n"
           "not byte ptr [r17]\n"
           "inc r16d\n"
           "dec dword ptr [r17]\n"
           "neg r16d\n"
           "not dword ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_unary[] = {
        0xd5, 0x10, 0xfe, 0xc0,
        0xd5, 0x10, 0xfe, 0x09,
        0xd5, 0x10, 0xf6, 0xd8,
        0xd5, 0x10, 0xf6, 0x11,
        0xd5, 0x10, 0xff, 0xc0,
        0xd5, 0x10, 0xff, 0x09,
        0xd5, 0x10, 0xf7, 0xd8,
        0xd5, 0x10, 0xf7, 0x11,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_unary.diagnostic_count == 0 &&
                               advanced_apx_rex2_unary.bytes.length == sizeof(expected_apx_rex2_unary) &&
                               memcmp(advanced_apx_rex2_unary.bytes.pointer, expected_apx_rex2_unary,
                                      sizeof(expected_apx_rex2_unary)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_unary_att = assembly_encode(
        arguments->arena,
        S8("incb %r16b\n"
           "decb (%r17)\n"
           "negb %r16b\n"
           "notb (%r17)\n"
           "incl %r16d\n"
           "decl (%r17)\n"
           "negl %r16d\n"
           "notl (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_unary_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_unary_att.bytes.length == sizeof(expected_apx_rex2_unary) &&
                               memcmp(advanced_apx_rex2_unary_att.bytes.pointer, expected_apx_rex2_unary,
                                      sizeof(expected_apx_rex2_unary)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_byte_imul = assembly_encode(
        arguments->arena, S8("imul r16b\nimul byte ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_byte_imul[] = {
        0xd5, 0x10, 0xf6, 0xe8,
        0xd5, 0x10, 0xf6, 0x29,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_byte_imul.diagnostic_count == 0 &&
                               advanced_apx_rex2_byte_imul.bytes.length == sizeof(expected_apx_rex2_byte_imul) &&
                               memcmp(advanced_apx_rex2_byte_imul.bytes.pointer, expected_apx_rex2_byte_imul,
                                      sizeof(expected_apx_rex2_byte_imul)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_byte_imul_att = assembly_encode(
        arguments->arena, S8("imulb %r16b\nimulb (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_byte_imul_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_byte_imul_att.bytes.length == sizeof(expected_apx_rex2_byte_imul) &&
                               memcmp(advanced_apx_rex2_byte_imul_att.bytes.pointer, expected_apx_rex2_byte_imul,
                                      sizeof(expected_apx_rex2_byte_imul)) == 0);

    AssemblyEncodeResult invalid_apx_rex2_byte_imul = assembly_encode(
        arguments->arena,
        S8("imul r16b, r17b\n"
           "imul r16b, r17b, 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_rex2_byte_imul.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_apx_rex2_byte_imul_att = assembly_encode(
        arguments->arena,
        S8("imulb %r17b, %r16b\n"
           "imulb $5, %r17b, %r16b\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_rex2_byte_imul_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_apx_rex2_byte_imul_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_apx_rex2_mov_immediate = assembly_encode(
        arguments->arena,
        S8("mov r16b, 5\n"
           "mov r16w, 5\n"
           "mov r16d, 5\n"
           "mov r16, 5\n"
           "mov r16, 0x1122334455667788\n"
           "mov byte ptr [r16], 5\n"
           "mov word ptr [r16], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_mov_immediate[] = {
        0xd5, 0x10, 0xb0, 0x05,
        0x66, 0xd5, 0x10, 0xb8, 0x05, 0x00,
        0xd5, 0x10, 0xb8, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xc7, 0xc0, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xd5, 0x10, 0xc6, 0x00, 0x05,
        0x66, 0xd5, 0x10, 0xc7, 0x00, 0x05, 0x00,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_mov_immediate.diagnostic_count == 0 &&
                               advanced_apx_rex2_mov_immediate.bytes.length == sizeof(expected_apx_rex2_mov_immediate) &&
                               memcmp(advanced_apx_rex2_mov_immediate.bytes.pointer, expected_apx_rex2_mov_immediate,
                                      sizeof(expected_apx_rex2_mov_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_mov_immediate_att = assembly_encode(
        arguments->arena,
        S8("movb $5, %r16b\n"
           "movw $5, %r16w\n"
           "movl $5, %r16d\n"
           "movq $5, %r16\n"
           "movq $0x1122334455667788, %r16\n"
           "movb $5, (%r16)\n"
           "movw $5, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_mov_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_mov_immediate_att.bytes.length == sizeof(expected_apx_rex2_mov_immediate) &&
                               memcmp(advanced_apx_rex2_mov_immediate_att.bytes.pointer, expected_apx_rex2_mov_immediate,
                                      sizeof(expected_apx_rex2_mov_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_evex_ndd_families = assembly_encode(
        arguments->arena,
        S8("imul r16d, r17d, r18d\n"
           "{nf} imul r16d, r17d, r18d\n"
           "shl r16d, r17d, 3\n"
           "{nf} shl r16d, r17d, 3\n"
           "shl r16d, r17d, cl\n"
           "{nf} inc r16d\n"
           "{nf} dec dword ptr [r17]\n"
           "{nf} neg r18d\n"
           "{nf} imul r19d, r20d\n"
           "{nf} shl r21d, 3\n"
           "{nf} shr r22d, cl\n"
           "{nf} inc r16b\n"
           "{nf} dec byte ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_evex_ndd_families[] = {
        0x62, 0xec, 0x7c, 0x10, 0xaf, 0xca,
        0x62, 0xec, 0x7c, 0x14, 0xaf, 0xca,
        0x62, 0xfc, 0x7c, 0x10, 0xc1, 0xe1, 0x03,
        0x62, 0xfc, 0x7c, 0x14, 0xc1, 0xe1, 0x03,
        0x62, 0xfc, 0x7c, 0x10, 0xd3, 0xe1,
        0x62, 0xfc, 0x7c, 0x0c, 0xff, 0xc0,
        0x62, 0xfc, 0x7c, 0x0c, 0xff, 0x09,
        0x62, 0xfc, 0x7c, 0x0c, 0xf7, 0xda,
        0x62, 0xec, 0x7c, 0x0c, 0xaf, 0xdc,
        0x62, 0xfc, 0x7c, 0x0c, 0xc1, 0xe5, 0x03,
        0x62, 0xfc, 0x7c, 0x0c, 0xd3, 0xee,
        0x62, 0xfc, 0x7c, 0x0c, 0xfe, 0xc0,
        0x62, 0xfc, 0x7c, 0x0c, 0xfe, 0x09,
    };
    BUSTER_TEST(arguments, advanced_apx_evex_ndd_families.diagnostic_count == 0 &&
                               advanced_apx_evex_ndd_families.bytes.length == sizeof(expected_apx_evex_ndd_families) &&
                               memcmp(advanced_apx_evex_ndd_families.bytes.pointer, expected_apx_evex_ndd_families,
                                      sizeof(expected_apx_evex_ndd_families)) == 0);

    AssemblyEncodeResult advanced_apx_evex_ndd_families_att = assembly_encode(
        arguments->arena,
        S8("imull %r18d, %r17d, %r16d\n"
           "{nf} imull %r18d, %r17d, %r16d\n"
           "shll $3, %r17d, %r16d\n"
           "{nf} shll $3, %r17d, %r16d\n"
           "shll %cl, %r17d, %r16d\n"
           "{nf} incl %r16d\n"
           "{nf} decl (%r17)\n"
           "{nf} negl %r18d\n"
           "{nf} imull %r20d, %r19d\n"
           "{nf} shll $3, %r21d\n"
           "{nf} shrl %cl, %r22d\n"
           "{nf} incb %r16b\n"
           "{nf} decb (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_evex_ndd_families_att.diagnostic_count == 0 &&
                               advanced_apx_evex_ndd_families_att.bytes.length == sizeof(expected_apx_evex_ndd_families) &&
                               memcmp(advanced_apx_evex_ndd_families_att.bytes.pointer, expected_apx_evex_ndd_families,
                                      sizeof(expected_apx_evex_ndd_families)) == 0);

    AssemblyEncodeResult advanced_apx_nf_immediate_imul = assembly_encode(
        arguments->arena,
        S8("{nf} imul r16d, r17d, 5\n"
           "{nf} imul r16d, dword ptr [r18], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_immediate_imul[] = {
        0x62, 0xec, 0x7c, 0x0c, 0x6b, 0xc1, 0x05,
        0x62, 0xec, 0x7c, 0x0c, 0x6b, 0x02, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_immediate_imul.diagnostic_count == 0 &&
                               advanced_apx_nf_immediate_imul.bytes.length == sizeof(expected_advanced_apx_nf_immediate_imul) &&
                               memcmp(advanced_apx_nf_immediate_imul.bytes.pointer, expected_advanced_apx_nf_immediate_imul,
                                      sizeof(expected_advanced_apx_nf_immediate_imul)) == 0);

    AssemblyEncodeResult advanced_apx_nf_immediate_imul_att = assembly_encode(
        arguments->arena,
        S8("{nf} imull $5, %r17d, %r16d\n"
           "{nf} imull $5, (%r18), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_immediate_imul_att.diagnostic_count == 0 &&
                               advanced_apx_nf_immediate_imul_att.bytes.length == sizeof(expected_advanced_apx_nf_immediate_imul) &&
                               memcmp(advanced_apx_nf_immediate_imul_att.bytes.pointer, expected_advanced_apx_nf_immediate_imul,
                                      sizeof(expected_advanced_apx_nf_immediate_imul)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock = assembly_encode(
        arguments->arena, S8("lock add qword ptr [r16], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_lock[] = {0xf0, 0xd5, 0x58, 0x01, 0x08};
    BUSTER_TEST(arguments, advanced_apx_legacy_lock.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock.bytes.length == sizeof(expected_advanced_apx_legacy_lock) &&
                               memcmp(advanced_apx_legacy_lock.bytes.pointer, expected_advanced_apx_legacy_lock,
                                      sizeof(expected_advanced_apx_legacy_lock)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_att = assembly_encode(
        arguments->arena, S8("lock addq %r17, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_att.bytes.length == sizeof(expected_advanced_apx_legacy_lock) &&
                               memcmp(advanced_apx_legacy_lock_att.bytes.pointer, expected_advanced_apx_legacy_lock,
                                      sizeof(expected_advanced_apx_legacy_lock)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_immediate = assembly_encode(
        arguments->arena, S8("lock add dword ptr [r17], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_lock_immediate[] = {0xf0, 0xd5, 0x10, 0x83, 0x01, 0x05};
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_immediate.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_immediate.bytes.length == sizeof(expected_advanced_apx_legacy_lock_immediate) &&
                               memcmp(advanced_apx_legacy_lock_immediate.bytes.pointer, expected_advanced_apx_legacy_lock_immediate,
                                      sizeof(expected_advanced_apx_legacy_lock_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_immediate_att = assembly_encode(
        arguments->arena, S8("lock addl $5, (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_immediate_att.bytes.length == sizeof(expected_advanced_apx_legacy_lock_immediate) &&
                               memcmp(advanced_apx_legacy_lock_immediate_att.bytes.pointer, expected_advanced_apx_legacy_lock_immediate,
                                      sizeof(expected_advanced_apx_legacy_lock_immediate)) == 0);

    AssemblyEncodeResult invalid_apx_nf_lock = assembly_encode(
        arguments->arena, S8("lock {nf} add qword ptr [r16], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_nf_lock.diagnostic_count == 1 &&
                               invalid_apx_nf_lock.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_apx_nf_lock_att = assembly_encode(
        arguments->arena, S8("lock {nf} addq %r17, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_nf_lock_att.diagnostic_count == 1 &&
                               invalid_apx_nf_lock_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_apx_legacy_lock = assembly_encode(
        arguments->arena,
        S8("lock mov qword ptr [r16], r17\n"
           "lock add r16, r17\n"
           "lock {nf} add r16, r17\n"
           "lock vaddps zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_legacy_lock.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_legacy_lock.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_legacy_lock.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_apx_legacy_memory = assembly_encode(
        arguments->arena,
        S8("mov qword ptr [r16], r17d\n"
           "mov qword ptr [r16], ah\n"
           "add qword ptr [r16], dh\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_legacy_memory.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_legacy_memory.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_legacy_memory.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_amx = assembly_encode(
        arguments->arena,
        S8("ldtilecfg [rax]\n"
           "sttilecfg [rax]\n"
           "tileloadd tmm1, [rax+rbx*4]\n"
           "tileloaddt1 tmm2, [rax]\n"
           "tilestored [r14], tmm7\n"
           "tilezero tmm0\n"
           "tdpbf16ps tmm0, tmm1, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm2\n"
           "tdpbsud tmm0, tmm1, tmm2\n"
           "tdpbusd tmm0, tmm1, tmm2\n"
           "tdpbuud tmm0, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx[] = {
        0xc4, 0xe2, 0x78, 0x49, 0x00,
        0xc4, 0xe2, 0x79, 0x49, 0x00,
        0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x98,
        0xc4, 0xe2, 0x79, 0x4b, 0x14, 0x20,
        0xc4, 0xc2, 0x7a, 0x4b, 0x3c, 0x26,
        0xc4, 0xe2, 0x7b, 0x49, 0xc0,
        0xc4, 0xe2, 0x6a, 0x5c, 0xc1,
        0xc4, 0xe2, 0x6b, 0x5e, 0xc1,
        0xc4, 0xe2, 0x6a, 0x5e, 0xc1,
        0xc4, 0xe2, 0x69, 0x5e, 0xc1,
        0xc4, 0xe2, 0x68, 0x5e, 0xc1,
    };
    BUSTER_TEST(arguments, advanced_amx.diagnostic_count == 0 && advanced_amx.bytes.length == sizeof(expected_advanced_amx) &&
                               memcmp(advanced_amx.bytes.pointer, expected_advanced_amx, sizeof(expected_advanced_amx)) == 0);

    AssemblyEncodeResult advanced_amx_egpr = assembly_encode(
        arguments->arena, S8("tileloadd tmm0, [r16]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx_egpr[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
    BUSTER_TEST(arguments, advanced_amx_egpr.diagnostic_count == 0 &&
                               advanced_amx_egpr.bytes.length == sizeof(expected_advanced_amx_egpr) &&
                               memcmp(advanced_amx_egpr.bytes.pointer, expected_advanced_amx_egpr,
                                      sizeof(expected_advanced_amx_egpr)) == 0);

    AssemblyEncodeResult advanced_amx_att = assembly_encode(
        arguments->arena, S8("tileloadd (%r16), %tmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_amx_att[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
    BUSTER_TEST(arguments, advanced_amx_att.diagnostic_count == 0 &&
                               advanced_amx_att.bytes.length == sizeof(expected_advanced_amx_att) &&
                               memcmp(advanced_amx_att.bytes.pointer, expected_advanced_amx_att,
                                      sizeof(expected_advanced_amx_att)) == 0);

    AssemblyEncodeResult advanced_amx_egpr_sib = assembly_encode(
        arguments->arena, S8("tileloadd tmm0, [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx_egpr_sib[] = {0x62, 0x9a, 0x7b, 0x08, 0x4b, 0x44, 0x88, 0x40};
    BUSTER_TEST(arguments, advanced_amx_egpr_sib.diagnostic_count == 0 &&
                               advanced_amx_egpr_sib.bytes.length == sizeof(expected_advanced_amx_egpr_sib) &&
                               memcmp(advanced_amx_egpr_sib.bytes.pointer, expected_advanced_amx_egpr_sib,
                                      sizeof(expected_advanced_amx_egpr_sib)) == 0);

    AssemblyEncodeResult invalid_amx_rip = assembly_encode(
        arguments->arena,
        S8("tileloadd tmm0, [rip+tile_external]\n"
           "tileloaddt1 tmm0, [rip+tile_external_t1]\n"
           "tilestored [rip+tile_external_store], tmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_rip.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_rip.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_rip.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_amx_rip_att = assembly_encode(
        arguments->arena,
        S8("tileloadd tile_att_external(%rip), %tmm0\n"
           "tileloaddt1 tile_att_external_t1(%rip), %tmm0\n"
           "tilestored %tmm0, tile_att_external_store(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_amx_rip_att.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_rip_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_rip_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    {
        Target amx_movrs_target = advanced_target;
        amx_movrs_target.cpu_features = target_cpu_features_add(amx_movrs_target.cpu_features,
                                                                TARGET_CPU_FEATURE_X86_AMX_MOVRS);
        u8 expected_tileloaddrs_apx[] = {0x62, 0xfa, 0x7f, 0x08, 0x4a, 0x04, 0x20};
        u8 expected_tileloaddrst1_apx[] = {0x62, 0xfa, 0x7d, 0x08, 0x4a, 0x04, 0x20};
        AssemblyEncodeResult tileloaddrs_apx_intel = assembly_encode(
            arguments->arena, S8("tileloaddrs tmm0, dword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult tileloaddrs_apx_att = assembly_encode(
            arguments->arena, S8("tileloaddrs (%r16), %tmm0\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult tileloaddrst1_apx_intel = assembly_encode(
            arguments->arena, S8("tileloaddrst1 tmm0, dword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult tileloaddrst1_apx_att = assembly_encode(
            arguments->arena, S8("tileloaddrst1 (%r16), %tmm0\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, tileloaddrs_apx_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrs_apx_intel.bytes, expected_tileloaddrs_apx,
                                                             sizeof(expected_tileloaddrs_apx)));
        BUSTER_TEST(arguments, tileloaddrs_apx_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrs_apx_att.bytes, expected_tileloaddrs_apx,
                                                             sizeof(expected_tileloaddrs_apx)));
        BUSTER_TEST(arguments, tileloaddrst1_apx_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrst1_apx_intel.bytes, expected_tileloaddrst1_apx,
                                                             sizeof(expected_tileloaddrst1_apx)));
        BUSTER_TEST(arguments, tileloaddrst1_apx_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrst1_apx_att.bytes, expected_tileloaddrst1_apx,
                                                             sizeof(expected_tileloaddrst1_apx)));
        AssemblyEncodeResult tileloaddrs_missing_movrs = assembly_encode(
            arguments->arena, S8("tileloaddrs tmm0, dword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, tileloaddrs_missing_movrs.bytes.length == 0);
        BUSTER_TEST(arguments, tileloaddrs_missing_movrs.diagnostic_count == 1);
        BUSTER_TEST(arguments,
                    tileloaddrs_missing_movrs.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        Target amx_movrs_without_apx = amx_movrs_target;
        amx_movrs_without_apx.cpu_features =
            target_cpu_features_remove(amx_movrs_without_apx.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        amx_movrs_without_apx.cpu_features = target_cpu_features_remove(amx_movrs_without_apx.cpu_features,
                                                                        TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
        u8 expected_tileloaddrs_vex[] = {0xc4, 0xe2, 0x7b, 0x4a, 0x04, 0x20};
        u8 expected_tileloaddrst1_vex[] = {0xc4, 0xe2, 0x79, 0x4a, 0x04, 0x20};
        AssemblyEncodeResult tileloaddrs_att_without_apx = assembly_encode(
            arguments->arena, S8("tileloaddrs (%rax), %tmm0\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_without_apx, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult tileloaddrst1_att_without_apx = assembly_encode(
            arguments->arena, S8("tileloaddrst1 (%rax), %tmm0\n"),
            (AssemblyEncodeOptions){.target = amx_movrs_without_apx, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, tileloaddrs_att_without_apx.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrs_att_without_apx.bytes, expected_tileloaddrs_vex,
                                                             sizeof(expected_tileloaddrs_vex)));
        BUSTER_TEST(arguments, tileloaddrst1_att_without_apx.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(tileloaddrst1_att_without_apx.bytes, expected_tileloaddrst1_vex,
                                                             sizeof(expected_tileloaddrst1_vex)));
    }

    AssemblyEncodeResult invalid_amx_repeated_tiles = assembly_encode(
        arguments->arena,
        S8("tdpbf16ps tmm0, tmm0, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm0\n"
           "tdpbsud tmm1, tmm1, tmm2\n"
           "tdpbusd tmm0, tmm1, tmm1\n"
           "tdpbuud tmm2, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_repeated_tiles.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_repeated_tiles.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_amx_repeated_tiles.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target avx10_1_target = x86_target;
    avx10_1_target.cpu_features_explicit = true;
    avx10_1_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX10_1}, 3);
    AssemblyEncodeResult avx10_1 = assembly_encode(arguments->arena, S8("vmovdqa32 ymm0, ymm1\nvmovdqa32 zmm0, zmm1\n"),
                                                     (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_1[] = {0x62, 0xf1, 0x7d, 0x28, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_1.diagnostic_count == 1 && avx10_1.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               avx10_1.bytes.length == sizeof(expected_avx10_1) &&
                               memcmp(avx10_1.bytes.pointer, expected_avx10_1, sizeof(expected_avx10_1)) == 0);
    avx10_1_target.cpu_features = target_cpu_features_add(avx10_1_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX10_512);
    AssemblyEncodeResult avx10_512 = assembly_encode(arguments->arena, S8("vmovdqa32 zmm0, zmm1\n"),
                                                      (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_512[] = {0x62, 0xf1, 0x7d, 0x48, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_512.diagnostic_count == 0 && avx10_512.bytes.length == sizeof(expected_avx10_512) &&
                               memcmp(avx10_512.bytes.pointer, expected_avx10_512, sizeof(expected_avx10_512)) == 0);

    Target avx10_aux_target = x86_target;
    avx10_aux_target.cpu_features_explicit = true;
    avx10_aux_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX10_2,
        TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_AVX10_512}, 5);
    String8 avx10_aux_intel_source =
        S8("vcvtbf42hf8 xmm0, qword ptr [rax]\n"
           "vcvtbf42hf8 ymm0, xmmword ptr [rax]\n"
           "vcvtbf42hf8 zmm0, ymmword ptr [rax]\n"
           "vcvtbf42hf8 xmm0 {k1}, qword ptr [rax]\n"
           "vcvtbf42hf8 zmm0 {k1}, ymmword ptr [rax]\n"
           "vcvtbf42hf8 xmm0, qword ptr [rax + 4]\n");
    u8 expected_avx10_aux_intel[] = {
        0x62, 0xf5, 0x7c, 0x08, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x28, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x48, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x09, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x49, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x08, 0x37, 0x80, 0x04, 0x00, 0x00, 0x00,
    };
    AssemblyEncodeResult avx10_aux_intel = assembly_encode(
        arguments->arena, avx10_aux_intel_source,
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, avx10_aux_intel.diagnostic_count == 0 && avx10_aux_intel.bytes.length == sizeof(expected_avx10_aux_intel) &&
                               memcmp(avx10_aux_intel.bytes.pointer, expected_avx10_aux_intel,
                                      sizeof(expected_avx10_aux_intel)) == 0);

    String8 avx10_aux_att_source =
        S8("vcvtbf42hf8 (%rax), %xmm0\n"
           "vcvtbf42hf8 (%rax), %ymm0\n"
           "vcvtbf42hf8 (%rax), %zmm0\n"
           "vcvtbf42hf8 (%rax), %xmm0 {%k1}\n"
           "vcvtbf42hf8 (%rax), %zmm0 {%k1}\n"
           "vcvtbf42hf8 4(%rax), %xmm0\n");
    AssemblyEncodeResult avx10_aux_att = assembly_encode(
        arguments->arena, avx10_aux_att_source,
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, avx10_aux_att.diagnostic_count == 0 && avx10_aux_att.bytes.length == sizeof(expected_avx10_aux_intel) &&
                               memcmp(avx10_aux_att.bytes.pointer, expected_avx10_aux_intel,
                                      sizeof(expected_avx10_aux_intel)) == 0);

    AssemblyEncodeResult mem128_intel = assembly_encode(
        arguments->arena,
        S8("vpslld ymm0 {k1}, ymm1, xmmword ptr [rax + 16]\n"
           "vpsrld zmm0 {k1}, zmm1, xmmword ptr [rax + 32]\n"
           "vpslld ymm0 {k1}, ymm1, xmmword ptr [rax + 17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_mem128[] = {
        0x62, 0xf1, 0x75, 0x29, 0xf2, 0x40, 0x01,
        0x62, 0xf1, 0x75, 0x49, 0xd2, 0x40, 0x02,
        0x62, 0xf1, 0x75, 0x29, 0xf2, 0x80, 0x11, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, mem128_intel.diagnostic_count == 0 && mem128_intel.bytes.length == sizeof(expected_mem128) &&
                               memcmp(mem128_intel.bytes.pointer, expected_mem128, sizeof(expected_mem128)) == 0);

    AssemblyEncodeResult mem128_att = assembly_encode(
        arguments->arena,
        S8("vpslld 16(%rax), %ymm1, %ymm0 {%k1}\n"
           "vpsrld 32(%rax), %zmm1, %zmm0 {%k1}\n"
           "vpslld 17(%rax), %ymm1, %ymm0 {%k1}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, mem128_att.diagnostic_count == 0 && mem128_att.bytes.length == sizeof(expected_mem128) &&
                               memcmp(mem128_att.bytes.pointer, expected_mem128, sizeof(expected_mem128)) == 0);

    AssemblyEncodeResult invalid_avx10_aux_widths = assembly_encode(
        arguments->arena,
        S8("vcvtbf42hf8 xmm0, xmmword ptr [rax]\n"
           "vcvtbf42hf8 ymm0, ymmword ptr [rax]\n"
           "vcvtbf42hf8 zmm0, zmmword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_avx10_aux_widths.diagnostic_count == 3 && invalid_avx10_aux_widths.bytes.length == 0);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_avx10_aux_widths.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_avx10_aux_widths.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target fixed_round_len_target = advanced_target;
    fixed_round_len_target.cpu_features = target_cpu_features_add(fixed_round_len_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512FP16);
    AssemblyEncodeResult fixed_round_len512_intel = assembly_encode(
        arguments->arena,
        S8("vaddph zmm0, zmm1, zmm2\n"
           "vaddph {rn-sae}, zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_fixed_round_len512[] = {
        0x62, 0xf5, 0x74, 0x48, 0x58, 0xc2,
        0x62, 0xf5, 0x74, 0x18, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, fixed_round_len512_intel.diagnostic_count == 0 &&
                               fixed_round_len512_intel.bytes.length == sizeof(expected_fixed_round_len512) &&
                               memcmp(fixed_round_len512_intel.bytes.pointer, expected_fixed_round_len512,
                                      sizeof(expected_fixed_round_len512)) == 0);
    AssemblyEncodeResult fixed_round_len512_att = assembly_encode(
        arguments->arena,
        S8("vaddph %zmm2, %zmm1, %zmm0\n"
           "vaddph {rn-sae}, %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, fixed_round_len512_att.diagnostic_count == 0 &&
                               fixed_round_len512_att.bytes.length == sizeof(expected_fixed_round_len512) &&
                               memcmp(fixed_round_len512_att.bytes.pointer, expected_fixed_round_len512,
                                      sizeof(expected_fixed_round_len512)) == 0);
    AssemblyEncodeResult invalid_fixed_round_len = assembly_encode(
        arguments->arena, S8("vaddph {rn-sae}, ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_fixed_round_len.diagnostic_count == 1 &&
                               invalid_fixed_round_len.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               invalid_fixed_round_len.bytes.length == 0);

    AssemblyEncodeResult invalid_advanced_features = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0, zmm1, zmm2\n"
           "vmovdqa32 xmm0, xmm1\n"
           "kmovw k1, k2\n"
           "add r16d, r17d, r18d\n"
           "ldtilecfg [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_features.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    AssemblyEncodeResult invalid_advanced_operands = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {z}, zmm1, zmm2\n"
           "vaddps zmm0, zmm1 {k1}, zmm2\n"
           "vaddps zmm0, zmm1, dword ptr [rax]{1to8}\n"
           "vcmpps k1, zmm2, zmm3, 32\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_operands.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_operands.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_operands.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_decorators = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {k1}{k2}, zmm1, zmm2\n"
           "vaddps zmm0 {z}{z}, zmm1, zmm2\n"
           "vaddps zmm0 {1to16}, zmm1, zmm2\n"
           "vaddps zmm0 {sae}, zmm1, zmm2\n"
           "vxorps zmm0 {rn-sae}, zmm1, zmm2\n"
           "vaddps zmm0, zmm1, zmmword ptr [rax]{1to16}{1to8}\n"
           "vcmpps k1 {z}, zmm2, zmm3, 7\n"
           "vcmpps k1, zmm2 {k2}, zmm3, 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_decorators.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_broadcasts = assembly_encode(
        arguments->arena,
        S8("vpaddb zmm0, zmm1, byte ptr [rax]{1to64}\n"
           "vpmullw zmm0, zmm1, word ptr [rax]{1to32}\n"
           "vpcmpeqb k1, zmm2, byte ptr [rax]{1to64}\n"
           "vpcmpw k1, zmm2, word ptr [rax]{1to32}, 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_broadcasts.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_broadcasts.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_broadcasts.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_advanced_broadcasts_att = assembly_encode(
        arguments->arena,
        S8("vpaddb (%rax){1to64}, %zmm1, %zmm0\n"
           "vpmullw (%rax){1to32}, %zmm1, %zmm0\n"
           "vpcmpeqb (%rax){1to64}, %zmm2, %k1\n"
           "vpcmpw $7, (%rax){1to32}, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_advanced_broadcasts_att.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_broadcasts_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_broadcasts_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    // XED's EMX_BROADCAST_* pseudo operands are implicit instruction
    // semantics, so ordinary source syntax has no {1toN} decorator.  Keep
    // exact-byte checks for one AVX, one AVX2/NE-convert, and one masked EVEX
    // form; the EVEX byte also proves that EMX did not set EVEX.b.
    AssemblyEncodeResult emx_avx = assembly_encode(
        arguments->arena, S8("vbroadcastss ymm0, dword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx[] = {0xc4, 0xe2, 0x7d, 0x18, 0x00};
    BUSTER_TEST(arguments, emx_avx.diagnostic_count == 0 && emx_avx.bytes.length == sizeof(expected_emx_avx) &&
                               memcmp(emx_avx.bytes.pointer, expected_emx_avx, sizeof(expected_emx_avx)) == 0);

    AssemblyEncodeResult emx_avx2 = assembly_encode(
        arguments->arena, S8("vpbroadcastb ymm0, byte ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx2[] = {0xc4, 0xe2, 0x7d, 0x78, 0x00};
    BUSTER_TEST(arguments, emx_avx2.diagnostic_count == 0 && emx_avx2.bytes.length == sizeof(expected_emx_avx2) &&
                               memcmp(emx_avx2.bytes.pointer, expected_emx_avx2, sizeof(expected_emx_avx2)) == 0);

    Target x86_avx_ne_target = x86_avx2_target;
    x86_avx_ne_target.cpu_features = target_cpu_features_add(x86_avx_ne_target.cpu_features,
                                                               TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT);
    AssemblyEncodeResult emx_avx_ne = assembly_encode(
        arguments->arena, S8("vbcstnebf162ps ymm0, word ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx_ne_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx_ne[] = {0xc4, 0xe2, 0x7e, 0xb1, 0x00};
    BUSTER_TEST(arguments, emx_avx_ne.diagnostic_count == 0 && emx_avx_ne.bytes.length == sizeof(expected_emx_avx_ne) &&
                               memcmp(emx_avx_ne.bytes.pointer, expected_emx_avx_ne, sizeof(expected_emx_avx_ne)) == 0);

    AssemblyEncodeResult emx_evex = assembly_encode(
        arguments->arena, S8("vbroadcastss ymm0 {k1}, dword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_evex[] = {0x62, 0xf2, 0x7d, 0x29, 0x18, 0x00};
    BUSTER_TEST(arguments, emx_evex.diagnostic_count == 0 && emx_evex.bytes.length == sizeof(expected_emx_evex) &&
                               memcmp(emx_evex.bytes.pointer, expected_emx_evex, sizeof(expected_emx_evex)) == 0);

    AssemblyEncodeResult invalid_advanced_rounding = assembly_encode(
        arguments->arena,
        S8("vaddps {rn-sae}, xmm0, xmm1, xmm2\n"
           "vaddps {rn-sae}, ymm0, ymm1, ymm2\n"
           "vcmpps k1, xmm2, xmm3, {sae}, 7\n"
           "vcmpps k1, ymm2, ymm3, {sae}, 7\n"
           "vrndscaleps xmm0, xmm1, {sae}, 4\n"
           "vrndscaleps ymm0, ymm1, {sae}, 4\n"
           "vcmpps k1, zmm2, zmm3, {rn-sae}, 7\n"
           "vrndscaleps zmm0, zmm1, {rn-sae}, 4\n"
           "vrndscalepd zmm0, zmm1, {rd-sae}, 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_rounding.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_rounding.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_rounding.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_advanced_rounding_att = assembly_encode(
        arguments->arena,
        S8("vaddps {rn-sae}, %xmm2, %xmm1, %xmm0\n"
           "vaddps {rn-sae}, %ymm2, %ymm1, %ymm0\n"
           "vcmpps $7, {sae}, %xmm3, %xmm2, %k1\n"
           "vcmpps $7, {sae}, %ymm3, %ymm2, %k1\n"
           "vrndscaleps $4, {sae}, %xmm1, %xmm0\n"
           "vrndscaleps $4, {sae}, %ymm1, %ymm0\n"
           "vcmpps $7, {rn-sae}, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, {rn-sae}, %zmm1, %zmm0\n"
           "vrndscalepd $4, {rd-sae}, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_advanced_rounding_att.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_rounding_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_rounding_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target scalar_evex_target = advanced_target;
    scalar_evex_target.cpu_features = target_cpu_features_remove(scalar_evex_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512VL);
    AssemblyEncodeResult scalar_evex = assembly_encode(
        arguments->arena,
        S8("vaddss xmm1 {k1}, xmm2, xmm3\n"
           "vaddsd xmm1 {k1}, xmm2, xmm3\n"),
        (AssemblyEncodeOptions){.target = scalar_evex_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_scalar_evex[] = {
        0x62, 0xf1, 0x6e, 0x09, 0x58, 0xcb,
        0x62, 0xf1, 0xef, 0x09, 0x58, 0xcb,
    };
    BUSTER_TEST(arguments, scalar_evex.diagnostic_count == 0 && scalar_evex.bytes.length == sizeof(expected_scalar_evex) &&
                               memcmp(scalar_evex.bytes.pointer, expected_scalar_evex, sizeof(expected_scalar_evex)) == 0);

    Target missing_vxor_dq_target = advanced_target;
    missing_vxor_dq_target.cpu_features = target_cpu_features_remove(missing_vxor_dq_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512DQ);
    AssemblyEncodeResult invalid_vxor_dq = assembly_encode(
        arguments->arena,
        S8("vxorps zmm0, zmm1, zmm2\n"
           "vxorpd zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = missing_vxor_dq_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vxor_dq.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vxor_dq.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vxor_dq.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult invalid_vxor_dq_att = assembly_encode(
        arguments->arena,
        S8("vxorps %zmm2, %zmm1, %zmm0\n"
           "vxorpd %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = missing_vxor_dq_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_vxor_dq_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vxor_dq_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vxor_dq_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    Target avx10_vxor_target = x86_target;
    avx10_vxor_target.cpu_features_explicit = true;
    avx10_vxor_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512}, 4);
    AssemblyEncodeResult avx10_vxor = assembly_encode(
        arguments->arena, S8("vxorps zmm0, zmm1, dword ptr [rax]{1to16}\n"),
        (AssemblyEncodeOptions){.target = avx10_vxor_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_vxor[] = {0x62, 0xf1, 0x74, 0x58, 0x57, 0x00};
    BUSTER_TEST(arguments, avx10_vxor.diagnostic_count == 0 && avx10_vxor.bytes.length == sizeof(expected_avx10_vxor) &&
                               memcmp(avx10_vxor.bytes.pointer, expected_avx10_vxor, sizeof(expected_avx10_vxor)) == 0);

    AssemblyEncodeResult invalid_pop2_same_register = assembly_encode(
        arguments->arena, S8("pop2 r16, r16\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_pop2_same_register.diagnostic_count == 1 &&
                               invalid_pop2_same_register.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    AssemblyEncodeResult invalid_pop2_same_register_att = assembly_encode(
        arguments->arena, S8("pop2q %r16, %r16\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_pop2_same_register_att.diagnostic_count == 1 &&
                               invalid_pop2_same_register_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_operand_nf = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d {nf}\n"
           "add r16d {nf}, r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_operand_nf.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_operand_nf.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_operand_nf.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_operand_nf_att = assembly_encode(
        arguments->arena,
        S8("addl %r17d, %r16d {nf}\n"
           "addl %r17d {nf}, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_operand_nf_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_operand_nf_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_operand_nf_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult numbered_register_boundaries = assembly_encode(
        arguments->arena,
        S8("mov r16, r17\n"
           "vaddps xmm16, xmm17, xmm18\n"
           "vaddps zmm31, zmm30, zmm29\n"
           "kmovw k7, k1\n"
           "tilezero tmm7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, numbered_register_boundaries.diagnostic_count == 0);
    AssemblyEncodeResult invalid_numbered_registers = assembly_encode(
        arguments->arena,
        S8("mov r016, r17\n"
           "vaddps xmm00, xmm1, xmm2\n"
           "vaddps zmm000, zmm1, zmm2\n"
           "kmovw k00, k1\n"
           "tilezero tmm00\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_numbered_registers.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_numbered_registers.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_numbered_registers.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_numbered_registers_att = assembly_encode(
        arguments->arena,
        S8("movq %r016, %r17\n"
           "vaddps %xmm00, %xmm1, %xmm2\n"
           "vaddps %zmm000, %zmm1, %zmm2\n"
           "kmovw %k00, %k1\n"
           "tilezero %tmm00\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_numbered_registers_att.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_numbered_registers_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_numbered_registers_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_alias = assembly_encode(
        arguments->arena, S8("vroundscaleps zmm0, zmm1, 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_alias.diagnostic_count == 1 &&
                               invalid_advanced_alias.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);

    AssemblyEncodeResult invalid_vmovdqa_aliases = assembly_encode(
        arguments->arena,
        S8("vmovdqa8 zmm0, zmm1\n"
           "vmovdqa16 zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vmovdqa_aliases.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vmovdqa_aliases.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vmovdqa_aliases.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }

    AssemblyEncodeResult invalid_vex_only_evex = assembly_encode(
        arguments->arena,
        S8("vmovdqa zmm0, zmm1\n"
           "vmovdqa xmm16, xmm17\n"
           "vpand zmm0, zmm1, zmm2\n"
           "vpand xmm16, xmm17, xmm18\n"
           "vpcmpeqb zmm0, zmm1, zmm2\n"
           "vpcmpeqb xmm16, xmm17, xmm18\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vex_only_evex.diagnostic_count == 6);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vex_only_evex.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vex_only_evex.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_evex_masks = assembly_encode(
        arguments->arena,
        S8("vcmpps k1 {k0}, zmm2, zmm3, 7\n"
           "vcmpps k1 {z}, zmm2, zmm3, 7\n"
           "vmovdqa64 zmmword ptr [rax] {z}, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_evex_masks.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_evex_masks.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_evex_masks.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target missing_kmov_width_target = advanced_target;
    missing_kmov_width_target.cpu_features = target_cpu_features_remove(missing_kmov_width_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512BW);
    AssemblyEncodeResult invalid_kmov_width_features = assembly_encode(
        arguments->arena,
        S8("kmovd k1, k2\n"
           "kmovq k1, k2\n"),
        (AssemblyEncodeOptions){.target = missing_kmov_width_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_kmov_width_features.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_kmov_width_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_kmov_width_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    Target missing_kadd_width_target = advanced_target;
    missing_kadd_width_target.cpu_features = target_cpu_features_remove(missing_kadd_width_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512DQ);
    AssemblyEncodeResult invalid_kadd_width_features = assembly_encode(
        arguments->arena, S8("kaddw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = missing_kadd_width_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_kadd_width_features.diagnostic_count == 1 &&
                               invalid_kadd_width_features.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

    Target amx_missing_target = advanced_target;
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_TILE);
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_BF16);
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_INT8);
    AssemblyEncodeResult invalid_amx_features = assembly_encode(
        arguments->arena,
        S8("tilezero tmm0\n"
           "tdpbf16ps tmm0, tmm1, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = amx_missing_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_features.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    AssemblyEncodeResult invalid_apx_high_byte = assembly_encode(
        arguments->arena,
        S8("mov r16b, ah\n"
           "mov byte ptr [r16], ah\n"
           "add r16b, dh\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_high_byte.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_high_byte.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_high_byte.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target amd_target = x86_target;
    amd_target.cpu_features_explicit = true;
    amd_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_XOP,
        TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_LWP,
        TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA}, 8);
    String8 amd_intel_source =
        S8("vfrczps xmm1, xmm2\n"
           "vfrczps xmm8, xmm9\n"
           "vfrczps xmm1, xmmword ptr [rax + 127]\n"
           "vpshab xmm1, xmm2, xmm3\n"
           "vpshab xmm1, xmm2, xmmword ptr [rax]\n"
           "vprotb xmm1, xmm2, 0x55\n"
           "vprotb xmm1, xmm2, xmm3\n"
           "vprotb xmm1, xmm2, xmmword ptr [rax]\n"
           "vpcmov xmm1, xmm2, xmm3, xmm4\n"
           "vpcmov xmm1, xmm2, xmm3, xmmword ptr [rax]\n"
           "vpcmov xmm1, xmm2, xmmword ptr [rax], xmm4\n"
           "vpperm xmm1, xmm2, xmm3, xmmword ptr [rax + 127]\n"
           "vfmaddps xmm1, xmm2, xmm3, xmm4\n"
           "vfmaddps xmm1, xmm2, xmm3, xmmword ptr [rax]\n"
           "vfmaddps xmm1, xmm2, xmmword ptr [rax], xmm4\n"
           "vfmaddss xmm1, xmm2, xmm3, xmm4\n"
           "bextr r8, r9, 0x11223344\n"
           "bextr eax, ecx, 0x11223344\n"
           "blcfill r8, r9\n"
           "blcfill r8, qword ptr [rax + 127]\n"
           "llwpcb r8\n"
           "slwpcb r9\n"
           "lwpins r8, ecx, 0x11223344\n"
           "lwpins r8, dword ptr [rax + 127], 0x11223344\n"
           "femms\n"
           "pi2fw mm0, mm1\n"
           "pfadd mm0, qword ptr [rax + 127]\n");
    u8 expected_amd_intel[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xca,
        0x8f, 0x49, 0x78, 0x80, 0xc1,
        0x8f, 0xe9, 0x78, 0x80, 0x48, 0x7f,
        0x8f, 0xe9, 0x60, 0x98, 0xca,
        0x8f, 0xe9, 0xe8, 0x98, 0x08,
        0x8f, 0xe8, 0x78, 0xc0, 0xca, 0x55,
        0x8f, 0xe9, 0x60, 0x90, 0xca,
        0x8f, 0xe9, 0xe8, 0x90, 0x08,
        0x8f, 0xe8, 0x68, 0xa2, 0xcb, 0x40,
        0x8f, 0xe8, 0xe8, 0xa2, 0x08, 0x30,
        0x8f, 0xe8, 0x68, 0xa2, 0x08, 0x40,
        0x8f, 0xe8, 0xe8, 0xa3, 0x48, 0x7f, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0xcc, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0x08, 0x30,
        0xc4, 0xe3, 0x69, 0x68, 0x08, 0x40,
        0xc4, 0xe3, 0xe9, 0x6a, 0xcc, 0x30,
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xea, 0x78, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xc9, 0xb8, 0x01, 0xc9,
        0x8f, 0xe9, 0xb8, 0x01, 0x48, 0x7f,
        0x8f, 0xc9, 0xf8, 0x12, 0xc0,
        0x8f, 0xc9, 0xf8, 0x12, 0xc9,
        0x8f, 0xea, 0xb8, 0x12, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xea, 0xb8, 0x12, 0x40, 0x7f, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0e,
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0x40, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_intel = assembly_encode(arguments->arena, amd_intel_source,
                                                      (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_intel.diagnostic_count == 0 && amd_intel.bytes.length == sizeof(expected_amd_intel) &&
                               memcmp(amd_intel.bytes.pointer, expected_amd_intel, sizeof(expected_amd_intel)) == 0);

    String8 amd_att_source =
        S8("vfrczps %xmm2, %xmm1\n"
           "vpshab %xmm3, %xmm2, %xmm1\n"
           "vprotb $0x55, %xmm2, %xmm1\n"
           "vpcmov %xmm4, %xmm3, %xmm2, %xmm1\n"
           "vpcmov (%rax), %xmm3, %xmm2, %xmm1\n"
           "vfmaddps %xmm4, %xmm3, %xmm2, %xmm1\n"
           "bextrq $0x11223344, %r9, %r8\n"
           "pfadd 127(%rax), %mm0\n");
    u8 expected_amd_att[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xca,
        0x8f, 0xe9, 0x60, 0x98, 0xca,
        0x8f, 0xe8, 0x78, 0xc0, 0xca, 0x55,
        0x8f, 0xe8, 0x68, 0xa2, 0xcb, 0x40,
        0x8f, 0xe8, 0xe8, 0xa2, 0x08, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0xcc, 0x30,
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0f, 0x40, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_att = assembly_encode(arguments->arena, amd_att_source,
                                                    (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, amd_att.diagnostic_count == 0 && amd_att.bytes.length == sizeof(expected_amd_att) &&
                               memcmp(amd_att.bytes.pointer, expected_amd_att, sizeof(expected_amd_att)) == 0);

    AssemblyEncodeResult amd_att_shapes = assembly_encode(
        arguments->arena,
        S8("vpcomb $3, %xmm2, %xmm1, %xmm0\n"
           "vpcomub $3, %xmm2, %xmm1, %xmm0\n"
           "vpshlb (%rax), %xmm1, %xmm0\n"
           "vprotb %xmm2, %xmm1, %xmm0\n"
           "vpcmov (%rax), %xmm2, %xmm1, %xmm0\n"
           "vpperm %xmm3, %xmm2, %xmm1, %xmm0\n"
           "vpermil2ps $3, %xmm3, %xmm2, %xmm1, %xmm0\n"
           "vfmaddps (%rax), %xmm2, %xmm1, %xmm0\n"
           "vfmaddps %xmm3, (%rax), %xmm1, %xmm0\n"
           "lwpval $3, %edx, %r9\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_amd_att_shapes[] = {
        0x8f, 0xe8, 0x70, 0xcc, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xec, 0xc2, 0x03,
        0x8f, 0xe9, 0xf0, 0x94, 0x00,
        0x8f, 0xe9, 0x68, 0x90, 0xc1,
        0x8f, 0xe8, 0xf0, 0xa2, 0x00, 0x20,
        0x8f, 0xe8, 0x70, 0xa3, 0xc2, 0x30,
        0xc4, 0xe3, 0x71, 0x48, 0xc2, 0x33,
        0xc4, 0xe3, 0xf1, 0x68, 0x00, 0x20,
        0xc4, 0xe3, 0x71, 0x68, 0x00, 0x30,
        0x8f, 0xea, 0xb0, 0x12, 0xca, 0x03, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, amd_att_shapes.diagnostic_count == 0 && amd_att_shapes.bytes.length == sizeof(expected_amd_att_shapes) &&
                               memcmp(amd_att_shapes.bytes.pointer, expected_amd_att_shapes, sizeof(expected_amd_att_shapes)) == 0);

    String8 amd_xop_inventory_source =
        S8("vfrczps xmm0, xmm1\n"
           "vfrczpd xmm0, xmm1\n"
           "vfrczss xmm0, xmm1\n"
           "vfrczsd xmm0, xmm1\n"
           "vphaddbw xmm0, xmm1\n"
           "vphaddbd xmm0, xmm1\n"
           "vphaddbq xmm0, xmm1\n"
           "vphaddwd xmm0, xmm1\n"
           "vphaddwq xmm0, xmm1\n"
           "vphaddubw xmm0, xmm1\n"
           "vphaddubd xmm0, xmm1\n"
           "vphaddubq xmm0, xmm1\n"
           "vphadduwd xmm0, xmm1\n"
           "vphadduwq xmm0, xmm1\n"
           "vphsubbw xmm0, xmm1\n"
           "vphsubwd xmm0, xmm1\n"
           "vphsubdq xmm0, xmm1\n"
           "vphadddq xmm0, xmm1\n"
           "vphaddudq xmm0, xmm1\n"
           "vprotb xmm0, xmm1, 3\n"
           "vprotw xmm0, xmm1, 3\n"
           "vprotd xmm0, xmm1, 3\n"
           "vprotq xmm0, xmm1, 3\n"
           "vpcomb xmm0, xmm1, xmm2, 3\n"
           "vpcomw xmm0, xmm1, xmm2, 3\n"
           "vpcomd xmm0, xmm1, xmm2, 3\n"
           "vpcomq xmm0, xmm1, xmm2, 3\n"
           "vpcomub xmm0, xmm1, xmm2, 3\n"
           "vpcomuw xmm0, xmm1, xmm2, 3\n"
           "vpcomud xmm0, xmm1, xmm2, 3\n"
           "vpcomuq xmm0, xmm1, xmm2, 3\n"
           "vprotb xmm0, xmm1, xmm2\n"
           "vprotw xmm0, xmm1, xmm2\n"
           "vprotd xmm0, xmm1, xmm2\n"
           "vprotq xmm0, xmm1, xmm2\n"
           "vpshlb xmm0, xmm1, xmm2\n"
           "vpshlw xmm0, xmm1, xmm2\n"
           "vpshld xmm0, xmm1, xmm2\n"
           "vpshlq xmm0, xmm1, xmm2\n"
           "vpshab xmm0, xmm1, xmm2\n"
           "vpshaw xmm0, xmm1, xmm2\n"
           "vpshad xmm0, xmm1, xmm2\n"
           "vpshaq xmm0, xmm1, xmm2\n"
           "vpmacssww xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdql xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsww xmm0, xmm1, xmm2, xmm3\n"
           "vpmacswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdql xmm0, xmm1, xmm2, xmm3\n"
           "vpcmov xmm0, xmm1, xmm2, xmm3\n"
           "vpperm xmm0, xmm1, xmm2, xmm3\n"
           "vpmadcsswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmadcswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdqh xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdqh xmm0, xmm1, xmm2, xmm3\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 3\n"
           "vpermil2pd xmm0, xmm1, xmm2, xmm3, 3\n"
           "llwpcb r8\n"
           "slwpcb r9\n"
           "lwpins r8, ecx, 3\n"
           "lwpval r9, edx, 3\n");
    u8 expected_amd_xop_inventory[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xc1,
        0x8f, 0xe9, 0x78, 0x81, 0xc1,
        0x8f, 0xe9, 0x78, 0x82, 0xc1,
        0x8f, 0xe9, 0x78, 0x83, 0xc1,
        0x8f, 0xe9, 0x78, 0xc1, 0xc1,
        0x8f, 0xe9, 0x78, 0xc2, 0xc1,
        0x8f, 0xe9, 0x78, 0xc3, 0xc1,
        0x8f, 0xe9, 0x78, 0xc6, 0xc1,
        0x8f, 0xe9, 0x78, 0xc7, 0xc1,
        0x8f, 0xe9, 0x78, 0xd1, 0xc1,
        0x8f, 0xe9, 0x78, 0xd2, 0xc1,
        0x8f, 0xe9, 0x78, 0xd3, 0xc1,
        0x8f, 0xe9, 0x78, 0xd6, 0xc1,
        0x8f, 0xe9, 0x78, 0xd7, 0xc1,
        0x8f, 0xe9, 0x78, 0xe1, 0xc1,
        0x8f, 0xe9, 0x78, 0xe2, 0xc1,
        0x8f, 0xe9, 0x78, 0xe3, 0xc1,
        0x8f, 0xe9, 0x78, 0xcb, 0xc1,
        0x8f, 0xe9, 0x78, 0xdb, 0xc1,
        0x8f, 0xe8, 0x78, 0xc0, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc1, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc2, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc3, 0xc1, 0x03,
        0x8f, 0xe8, 0x70, 0xcc, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xcd, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xce, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xcf, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xec, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xed, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xee, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xef, 0xc2, 0x03,
        0x8f, 0xe9, 0x68, 0x90, 0xc1,
        0x8f, 0xe9, 0x68, 0x91, 0xc1,
        0x8f, 0xe9, 0x68, 0x92, 0xc1,
        0x8f, 0xe9, 0x68, 0x93, 0xc1,
        0x8f, 0xe9, 0x68, 0x94, 0xc1,
        0x8f, 0xe9, 0x68, 0x95, 0xc1,
        0x8f, 0xe9, 0x68, 0x96, 0xc1,
        0x8f, 0xe9, 0x68, 0x97, 0xc1,
        0x8f, 0xe9, 0x68, 0x98, 0xc1,
        0x8f, 0xe9, 0x68, 0x99, 0xc1,
        0x8f, 0xe9, 0x68, 0x9a, 0xc1,
        0x8f, 0xe9, 0x68, 0x9b, 0xc1,
        0x8f, 0xe8, 0x70, 0x85, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x86, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x87, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x95, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x96, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x97, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa2, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa3, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa6, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xb6, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x8e, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x8f, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x9e, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x9f, 0xc2, 0x30,
        0xc4, 0xe3, 0x71, 0x48, 0xc2, 0x33,
        0xc4, 0xe3, 0x71, 0x49, 0xc2, 0x33,
        0x8f, 0xc9, 0xf8, 0x12, 0xc0,
        0x8f, 0xc9, 0xf8, 0x12, 0xc9,
        0x8f, 0xea, 0xb8, 0x12, 0xc1, 0x03, 0x00, 0x00, 0x00,
        0x8f, 0xea, 0xb0, 0x12, 0xca, 0x03, 0x00, 0x00, 0x00,
    };
    AssemblyEncodeResult amd_xop_inventory = assembly_encode(arguments->arena, amd_xop_inventory_source,
                                                              (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_xop_inventory.diagnostic_count == 0 && amd_xop_inventory.bytes.length == sizeof(expected_amd_xop_inventory) &&
                               memcmp(amd_xop_inventory.bytes.pointer, expected_amd_xop_inventory, sizeof(expected_amd_xop_inventory)) == 0);

    String8 amd_fma4_inventory_source =
        S8("vfmaddsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddss xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddsd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubss xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubsd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddss xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddsd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubss xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubsd xmm0, xmm1, xmm2, xmm3\n");
    u8 expected_amd_fma4_inventory[] = {
        0xc4, 0xe3, 0xf1, 0x5c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5f, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x68, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x69, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6a, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6b, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6f, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x78, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x79, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7a, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7b, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7f, 0xc3, 0x20,
    };
    AssemblyEncodeResult amd_fma4_inventory = assembly_encode(arguments->arena, amd_fma4_inventory_source,
                                                               (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_fma4_inventory.diagnostic_count == 0 && amd_fma4_inventory.bytes.length == sizeof(expected_amd_fma4_inventory) &&
                               memcmp(amd_fma4_inventory.bytes.pointer, expected_amd_fma4_inventory, sizeof(expected_amd_fma4_inventory)) == 0);

    String8 amd_tbm_inventory_source =
        S8("bextr r8, r9, 3\n"
           "blcfill r8, r9\n"
           "blci r8, r9\n"
           "blcic r8, r9\n"
           "blcmsk r8, r9\n"
           "blcs r8, r9\n"
           "blsfill r8, r9\n"
           "blsic r8, r9\n"
           "t1mskc r8, r9\n"
           "tzmsk r8, r9\n");
    u8 expected_amd_tbm_inventory[] = {
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x03, 0x00, 0x00, 0x00,
        0x8f, 0xc9, 0xb8, 0x01, 0xc9,
        0x8f, 0xc9, 0xb8, 0x02, 0xf1,
        0x8f, 0xc9, 0xb8, 0x01, 0xe9,
        0x8f, 0xc9, 0xb8, 0x02, 0xc9,
        0x8f, 0xc9, 0xb8, 0x01, 0xd9,
        0x8f, 0xc9, 0xb8, 0x01, 0xd1,
        0x8f, 0xc9, 0xb8, 0x01, 0xf1,
        0x8f, 0xc9, 0xb8, 0x01, 0xf9,
        0x8f, 0xc9, 0xb8, 0x01, 0xe1,
    };
    AssemblyEncodeResult amd_tbm_inventory = assembly_encode(arguments->arena, amd_tbm_inventory_source,
                                                              (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_tbm_inventory.diagnostic_count == 0 && amd_tbm_inventory.bytes.length == sizeof(expected_amd_tbm_inventory) &&
                               memcmp(amd_tbm_inventory.bytes.pointer, expected_amd_tbm_inventory, sizeof(expected_amd_tbm_inventory)) == 0);

    String8 amd_3dnow_inventory_source =
        S8("femms\n"
           "pi2fw mm0, mm1\n"
           "pi2fd mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pf2id mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pfcmpge mm0, mm1\n"
           "pfmin mm0, mm1\n"
           "pfrcp mm0, mm1\n"
           "pfrsqrt mm0, mm1\n"
           "pfsub mm0, mm1\n"
           "pfadd mm0, mm1\n"
           "pfcmpgt mm0, mm1\n"
           "pfmax mm0, mm1\n"
           "pfrcpit1 mm0, mm1\n"
           "pfrsqit1 mm0, mm1\n"
           "pfsubr mm0, mm1\n"
           "pfacc mm0, mm1\n"
           "pfcmpeq mm0, mm1\n"
           "pfmul mm0, mm1\n"
           "pfrcpit2 mm0, mm1\n"
           "pmulhrw mm0, mm1\n"
           "pswapd mm0, mm1\n"
           "pavgusb mm0, mm1\n");
    u8 expected_amd_3dnow_inventory[] = {
        0x0f, 0x0e,
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0xc1, 0x0d,
        0x0f, 0x0f, 0xc1, 0x1c,
        0x0f, 0x0f, 0xc1, 0x1d,
        0x0f, 0x0f, 0xc1, 0x8a,
        0x0f, 0x0f, 0xc1, 0x8e,
        0x0f, 0x0f, 0xc1, 0x90,
        0x0f, 0x0f, 0xc1, 0x94,
        0x0f, 0x0f, 0xc1, 0x96,
        0x0f, 0x0f, 0xc1, 0x97,
        0x0f, 0x0f, 0xc1, 0x9a,
        0x0f, 0x0f, 0xc1, 0x9e,
        0x0f, 0x0f, 0xc1, 0xa0,
        0x0f, 0x0f, 0xc1, 0xa4,
        0x0f, 0x0f, 0xc1, 0xa6,
        0x0f, 0x0f, 0xc1, 0xa7,
        0x0f, 0x0f, 0xc1, 0xaa,
        0x0f, 0x0f, 0xc1, 0xae,
        0x0f, 0x0f, 0xc1, 0xb0,
        0x0f, 0x0f, 0xc1, 0xb4,
        0x0f, 0x0f, 0xc1, 0xb6,
        0x0f, 0x0f, 0xc1, 0xb7,
        0x0f, 0x0f, 0xc1, 0xbb,
        0x0f, 0x0f, 0xc1, 0xbf,
    };
    AssemblyEncodeResult amd_3dnow_inventory = assembly_encode(arguments->arena, amd_3dnow_inventory_source,
                                                                (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_3dnow_inventory.diagnostic_count == 0 && amd_3dnow_inventory.bytes.length == sizeof(expected_amd_3dnow_inventory) &&
                               memcmp(amd_3dnow_inventory.bytes.pointer, expected_amd_3dnow_inventory, sizeof(expected_amd_3dnow_inventory)) == 0);

    String8 amd_memory_source =
        S8("vfrczps ymm8, ymm9\n"
           "vfrczpd ymm8, ymm9\n"
           "vpcmov ymm8, ymm9, ymm10, ymm11\n"
           "vpcmov ymm8, ymm9, ymm10, ymmword ptr [rax]\n"
           "vpcmov ymm8, ymm9, ymmword ptr [rax], ymm11\n"
           "vfrczps xmm8, xmmword ptr [r12 + r9*4 + 0x12345678]\n"
           "vprotb xmm8, xmm9, xmmword ptr [r12 + r9*8 - 128]\n"
           "vpshlq xmm8, xmm9, xmmword ptr [r12 + r9*8 + 127]\n"
           "vpcmov xmm8, xmm9, xmm10, xmmword ptr [r12 + r9*4 + 0x12345678]\n"
           "vpperm xmm8, xmm9, xmm10, xmmword ptr [r12 + r9*8 + 127]\n"
           "vpperm xmm8, xmm9, xmmword ptr [r12 + r9*8 + 127], xmm10\n"
           "vpmacssww xmm8, xmm9, xmmword ptr [r12 + r9*2 + 127], xmm10\n"
           "vpermil2ps ymm8, ymm9, ymmword ptr [r12 + r9*4 - 128], ymm10, 7\n"
           "vfmaddpd ymm8, ymm9, ymmword ptr [r12 + r9*8 + 0x12345678], ymm10\n"
           "vfmaddsubps ymm8, ymm9, ymm10, ymmword ptr [r12 + r9*8 - 128]\n"
           "bextr r8, qword ptr [r12 + r9*8 + 0x12345678], 0x11223344\n"
           "blci r8, qword ptr [r12 + r9*8 + 127]\n"
           "lwpins r8, dword ptr [r12 + r9*4 + 0x12345678], 0x11223344\n"
           "pfadd mm0, qword ptr [r12 + r9*8 + 127]\n"
           "vfrczps xmm0, xmmword ptr [0x12345678]\n"
           "pfadd mm0, qword ptr [0x12345678]\n"
           "blcfill r8, qword ptr [0x12345678]\n");
    u8 expected_amd_memory[] = {
        0x8f, 0x49, 0x7c, 0x80, 0xc1,
        0x8f, 0x49, 0x7c, 0x81, 0xc1,
        0x8f, 0x48, 0x34, 0xa2, 0xc2, 0xb0,
        0x8f, 0x68, 0xb4, 0xa2, 0x00, 0xa0,
        0x8f, 0x68, 0x34, 0xa2, 0x00, 0xb0,
        0x8f, 0x09, 0x78, 0x80, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12,
        0x8f, 0x09, 0xb0, 0x90, 0x44, 0xcc, 0x80,
        0x8f, 0x09, 0xb0, 0x97, 0x44, 0xcc, 0x7f,
        0x8f, 0x08, 0xb0, 0xa2, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0x8f, 0x08, 0xb0, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0x85, 0x44, 0x4c, 0x7f, 0xa0,
        0xc4, 0x03, 0x35, 0x48, 0x44, 0x8c, 0x80, 0xa7,
        0xc4, 0x03, 0x35, 0x69, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0xc4, 0x03, 0xb5, 0x5c, 0x44, 0xcc, 0x80, 0xa0,
        0x8f, 0x0a, 0xf8, 0x10, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0x89, 0xb8, 0x02, 0x74, 0xcc, 0x7f,
        0x8f, 0x8a, 0xb8, 0x12, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x43, 0x0f, 0x0f, 0x44, 0xcc, 0x7f, 0x9e,
        0x8f, 0xe9, 0x78, 0x80, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x0f, 0x0f, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12, 0x9e,
        0x8f, 0xe9, 0xb8, 0x01, 0x0c, 0x25, 0x78, 0x56, 0x34, 0x12,
    };
    AssemblyEncodeResult amd_memory = assembly_encode(arguments->arena, amd_memory_source,
                                                       (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_memory.diagnostic_count == 0 && amd_memory.bytes.length == sizeof(expected_amd_memory) &&
                               memcmp(amd_memory.bytes.pointer, expected_amd_memory, sizeof(expected_amd_memory)) == 0);

    String8 amd_memory_att_source =
        S8("vfrczps %ymm9, %ymm8\n"
           "vfrczpd %ymm9, %ymm8\n"
           "vpcmov %ymm11, %ymm10, %ymm9, %ymm8\n"
           "vpcmov (%rax), %ymm10, %ymm9, %ymm8\n"
           "vpcmov %ymm11, (%rax), %ymm9, %ymm8\n"
           "vfrczps 305419896(%r12,%r9,4), %xmm8\n"
           "vprotb -128(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpshlq 127(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpcmov 305419896(%r12,%r9,4), %xmm10, %xmm9, %xmm8\n"
           "vpperm 127(%r12,%r9,8), %xmm10, %xmm9, %xmm8\n"
           "vpperm %xmm10, 127(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpmacssww %xmm10, 127(%r12,%r9,2), %xmm9, %xmm8\n"
           "vpermil2ps $7, %ymm10, -128(%r12,%r9,4), %ymm9, %ymm8\n"
           "vfmaddpd %ymm10, 305419896(%r12,%r9,8), %ymm9, %ymm8\n"
           "vfmaddsubps -128(%r12,%r9,8), %ymm10, %ymm9, %ymm8\n"
           "bextrq $287454020, 305419896(%r12,%r9,8), %r8\n"
           "blciq 127(%r12,%r9,8), %r8\n"
           "lwpins $287454020, 305419896(%r12,%r9,4), %r8\n"
           "pfadd 127(%r12,%r9,8), %mm0\n");
    u8 expected_amd_memory_att[] = {
        0x8f, 0x49, 0x7c, 0x80, 0xc1,
        0x8f, 0x49, 0x7c, 0x81, 0xc1,
        0x8f, 0x48, 0x34, 0xa2, 0xc2, 0xb0,
        0x8f, 0x68, 0xb4, 0xa2, 0x00, 0xa0,
        0x8f, 0x68, 0x34, 0xa2, 0x00, 0xb0,
        0x8f, 0x09, 0x78, 0x80, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12,
        0x8f, 0x09, 0xb0, 0x90, 0x44, 0xcc, 0x80,
        0x8f, 0x09, 0xb0, 0x97, 0x44, 0xcc, 0x7f,
        0x8f, 0x08, 0xb0, 0xa2, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0x8f, 0x08, 0xb0, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0x85, 0x44, 0x4c, 0x7f, 0xa0,
        0xc4, 0x03, 0x35, 0x48, 0x44, 0x8c, 0x80, 0xa7,
        0xc4, 0x03, 0x35, 0x69, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0xc4, 0x03, 0xb5, 0x5c, 0x44, 0xcc, 0x80, 0xa0,
        0x8f, 0x0a, 0xf8, 0x10, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0x89, 0xb8, 0x02, 0x74, 0xcc, 0x7f,
        0x8f, 0x8a, 0xb8, 0x12, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x43, 0x0f, 0x0f, 0x44, 0xcc, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_memory_att = assembly_encode(arguments->arena, amd_memory_att_source,
                                                           (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, amd_memory_att.diagnostic_count == 0 && amd_memory_att.bytes.length == sizeof(expected_amd_memory_att) &&
                               memcmp(amd_memory_att.bytes.pointer, expected_amd_memory_att, sizeof(expected_amd_memory_att)) == 0);

    AssemblyEncodeResult amd_rip_relocations = assembly_encode(
        arguments->arena,
        S8("vfrczps xmm0, xmmword ptr [rip + amd_external]\n"
           "vpcmov xmm0, xmm1, xmm2, xmmword ptr [rip + amd_external]\n"
           "vfmaddps xmm0, xmm1, xmm2, xmmword ptr [rip + amd_external]\n"
           "bextr r8, qword ptr [rip + amd_external], 0x11223344\n"
           "pfadd mm0, qword ptr [rip + amd_external]\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_amd_rip_relocations[] = {
        0x8f, 0xe9, 0x78, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x8f, 0xe8, 0xf0, 0xa2, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20,
        0xc4, 0xe3, 0xf1, 0x68, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20,
        0x8f, 0x6a, 0xf8, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0f, 0x05, 0x00, 0x00, 0x00, 0x00, 0x9e,
    };
    BUSTER_TEST(arguments, amd_rip_relocations.diagnostic_count == 0 && amd_rip_relocations.bytes.length == sizeof(expected_amd_rip_relocations) &&
                               memcmp(amd_rip_relocations.bytes.pointer, expected_amd_rip_relocations, sizeof(expected_amd_rip_relocations)) == 0);
    BUSTER_TEST(arguments, amd_rip_relocations.symbol_count == 1 && !amd_rip_relocations.symbols[0].defined &&
                               string_equal(amd_rip_relocations.symbols[0].name, S8("amd_external")) && amd_rip_relocations.relocation_count == 5);
    u64 amd_rip_relocation_offsets[] = {5, 14, 24, 34, 45};
    s64 amd_rip_relocation_addends[] = {-4, -5, -5, -8, -5};
    for (u32 relocation_index = 0; relocation_index < 5; relocation_index += 1)
    {
        BUSTER_TEST(arguments, amd_rip_relocations.relocations[relocation_index].offset == amd_rip_relocation_offsets[relocation_index] &&
                                   amd_rip_relocations.relocations[relocation_index].symbol == 0 &&
                                   amd_rip_relocations.relocations[relocation_index].addend == amd_rip_relocation_addends[relocation_index] &&
                                   amd_rip_relocations.relocations[relocation_index].kind == ASSEMBLY_RELOCATION_X86_PC32);
    }

    Target amd_no_xop = amd_target;
    amd_no_xop.cpu_features = target_cpu_features_remove(amd_no_xop.cpu_features, TARGET_CPU_FEATURE_X86_XOP);
    AssemblyEncodeResult unsupported_amd_xop = assembly_encode(arguments->arena, S8("vfrczps xmm0, xmm1\n"),
                                                                 (AssemblyEncodeOptions){.target = amd_no_xop, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_xop.diagnostic_count == 1 &&
                               unsupported_amd_xop.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_xop.diagnostics[0].message, S8("instruction requires the xop target feature")));
    Target amd_no_fma4 = amd_target;
    amd_no_fma4.cpu_features = target_cpu_features_remove(amd_no_fma4.cpu_features, TARGET_CPU_FEATURE_X86_FMA4);
    AssemblyEncodeResult unsupported_amd_fma4 = assembly_encode(arguments->arena, S8("vfmaddps xmm0, xmm1, xmm2, xmm3\n"),
                                                                  (AssemblyEncodeOptions){.target = amd_no_fma4, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_fma4.diagnostic_count == 1 &&
                               unsupported_amd_fma4.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_fma4.diagnostics[0].message, S8("instruction requires the fma4 target feature")));
    Target amd_no_3dnow = amd_target;
    amd_no_3dnow.cpu_features = target_cpu_features_remove(amd_no_3dnow.cpu_features, TARGET_CPU_FEATURE_X86_3DNOW);
    amd_no_3dnow.cpu_features = target_cpu_features_remove(amd_no_3dnow.cpu_features, TARGET_CPU_FEATURE_X86_3DNOWA);
    AssemblyEncodeResult unsupported_amd_3dnow = assembly_encode(arguments->arena, S8("pfadd mm0, mm1\n"),
                                                                   (AssemblyEncodeOptions){.target = amd_no_3dnow, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnow.diagnostic_count == 1 &&
                               unsupported_amd_3dnow.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_3dnow.diagnostics[0].message, S8("instruction requires the 3dnow target feature")));
    Target amd_base_3dnow_only = amd_target;
    amd_base_3dnow_only.cpu_features = target_cpu_features_remove(amd_base_3dnow_only.cpu_features, TARGET_CPU_FEATURE_X86_3DNOWA);
    AssemblyEncodeResult unsupported_amd_3dnowa = assembly_encode(arguments->arena, S8("pi2fw mm0, mm1\n"),
                                                                    (AssemblyEncodeOptions){.target = amd_base_3dnow_only, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnowa.diagnostic_count == 1 &&
                               unsupported_amd_3dnowa.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_3dnowa.diagnostics[0].message, S8("instruction requires the 3dnowa target feature")));
    AssemblyEncodeResult enhanced_amd_3dnow = assembly_encode(
        arguments->arena,
        S8("pi2fw mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pswapd mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_enhanced_amd_3dnow[] = {
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0xc1, 0x1c,
        0x0f, 0x0f, 0xc1, 0x8a,
        0x0f, 0x0f, 0xc1, 0x8e,
        0x0f, 0x0f, 0xc1, 0xbb,
    };
    BUSTER_TEST(arguments, enhanced_amd_3dnow.diagnostic_count == 0 && enhanced_amd_3dnow.bytes.length == sizeof(expected_enhanced_amd_3dnow) &&
                               memcmp(enhanced_amd_3dnow.bytes.pointer, expected_enhanced_amd_3dnow, sizeof(expected_enhanced_amd_3dnow)) == 0);
    AssemblyEncodeResult unsupported_amd_3dnowa_all = assembly_encode(
        arguments->arena,
        S8("pi2fw mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pswapd mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = amd_base_3dnow_only, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnowa_all.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < unsupported_amd_3dnowa_all.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, unsupported_amd_3dnowa_all.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   string_equal(unsupported_amd_3dnowa_all.diagnostics[diagnostic_index].message,
                                                S8("instruction requires the 3dnowa target feature")));
    }
    Target amd_no_tbm = amd_target;
    amd_no_tbm.cpu_features = target_cpu_features_remove(amd_no_tbm.cpu_features, TARGET_CPU_FEATURE_X86_TBM);
    AssemblyEncodeResult unsupported_amd_tbm = assembly_encode(arguments->arena, S8("bextr rax, rcx, 0x1\n"),
                                                                 (AssemblyEncodeOptions){.target = amd_no_tbm, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_tbm.diagnostic_count == 1 &&
                               unsupported_amd_tbm.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target amd_no_lwp = amd_target;
    amd_no_lwp.cpu_features = target_cpu_features_remove(amd_no_lwp.cpu_features, TARGET_CPU_FEATURE_X86_LWP);
    AssemblyEncodeResult unsupported_amd_lwp = assembly_encode(arguments->arena, S8("llwpcb r8\n"),
                                                                (AssemblyEncodeOptions){.target = amd_no_lwp, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_lwp.diagnostic_count == 1 &&
                               unsupported_amd_lwp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_lwp.diagnostics[0].message, S8("instruction requires the lwp target feature")));

    AssemblyEncodeResult invalid_amd_forms = assembly_encode(
        arguments->arena,
        S8("vfrczps ymm0, xmm1\n"
           "vfmaddss ymm0, ymm1, ymm2, ymm3\n"
           "vpcmov xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpmacssww xmm0, xmm1, xmm2, xmmword ptr [rax]\n"
           "vprotb xmm0, xmm1, 0x100\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 0x10\n"
           "bextr rax, ecx, 0x1\n"
           "lwpins eax, ecx, 0x1\n"
           "pfadd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amd_forms.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amd_forms.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amd_forms.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_amd_bounds = assembly_encode(
        arguments->arena,
        S8("vpcomb xmm0, xmm1, xmm2, 0x100\n"
           "vpcomb xmm0, xmm1, 0x1\n"
           "vpcomw ymm0, ymm1, ymm2, 0\n"
           "vprotb xmm0, xmm1, xmm2, 0\n"
           "vpshlb xmm0, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpcmov xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpmacssww xmm0, xmm1, xmm2, xmmword ptr [rax]\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 16\n"
           "vpermil2pd xmm0, xmm1, xmm2, xmm3, 16\n"
           "vfmaddps xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "blci eax, rcx\n"
           "lwpval r8, r9, 1\n"
           "pi2fw xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amd_bounds.diagnostic_count == 13);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amd_bounds.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amd_bounds.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    {
        u8 expected_ud0[] = {0x0f, 0xff};
        AssemblyEncodeResult metadata_legacy = assembly_encode(arguments->arena, S8("ud0\n"),
                                                                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_legacy.diagnostic_count == 0 && metadata_legacy.relocation_count == 0 &&
                                   metadata_legacy.bytes.length == sizeof(expected_ud0) &&
                                   memcmp(metadata_legacy.bytes.pointer, expected_ud0, sizeof(expected_ud0)) == 0);

        u8 expected_rex[] = {0x41, 0x0f, 0x01, 0xe0};
        AssemblyEncodeResult metadata_rex_intel = assembly_encode(arguments->arena, S8("smsw r8d\n"),
                                                                   (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_rex_att = assembly_encode(arguments->arena, S8("smswl %r8d\n"),
                                                                 (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_rex_intel.diagnostic_count == 0 && metadata_rex_intel.relocation_count == 0 &&
                                   metadata_rex_intel.bytes.length == sizeof(expected_rex) &&
                                   memcmp(metadata_rex_intel.bytes.pointer, expected_rex, sizeof(expected_rex)) == 0);
        BUSTER_TEST(arguments, metadata_rex_att.diagnostic_count == 0 && metadata_rex_att.relocation_count == 0 &&
                                   metadata_rex_att.bytes.length == sizeof(expected_rex) &&
                                   memcmp(metadata_rex_att.bytes.pointer, expected_rex, sizeof(expected_rex)) == 0);

        u8 expected_hlt[] = {0xf4};
        AssemblyEncodeResult metadata_system = assembly_encode(arguments->arena, S8("hlt\n"),
                                                                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_system.diagnostic_count == 0 && metadata_system.relocation_count == 0 &&
                                   metadata_system.bytes.length == sizeof(expected_hlt) &&
                                   memcmp(metadata_system.bytes.pointer, expected_hlt, sizeof(expected_hlt)) == 0);

        u8 expected_smsw[] = {0x0f, 0x01, 0xe0};
        AssemblyEncodeResult metadata_control = assembly_encode(arguments->arena, S8("smsw eax\n"),
                                                                 (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_control.diagnostic_count == 0 && metadata_control.relocation_count == 0 &&
                                   metadata_control.bytes.length == sizeof(expected_smsw) &&
                                   memcmp(metadata_control.bytes.pointer, expected_smsw, sizeof(expected_smsw)) == 0);

        u8 expected_rex2[] = {0xd5, 0x18, 0x50};
        AssemblyEncodeResult metadata_rex2_intel = assembly_encode(arguments->arena, S8("pushp r16\n"),
                                                                    (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_rex2_att = assembly_encode(arguments->arena, S8("pushpq %r16\n"),
                                                                  (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_rex2_intel.diagnostic_count == 0 && metadata_rex2_intel.bytes.length == sizeof(expected_rex2) &&
                                   memcmp(metadata_rex2_intel.bytes.pointer, expected_rex2, sizeof(expected_rex2)) == 0);
        BUSTER_TEST(arguments, metadata_rex2_att.diagnostic_count == 0 && metadata_rex2_att.bytes.length == sizeof(expected_rex2) &&
                                   memcmp(metadata_rex2_att.bytes.pointer, expected_rex2, sizeof(expected_rex2)) == 0);

        u8 expected_apx[] = {0x62, 0xfc, 0x7c, 0x10, 0x83, 0xc1, 0x7b};
        AssemblyEncodeResult metadata_apx = assembly_encode(arguments->arena, S8("addl $123, %r17d, %r16d\n"),
                                                             (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx.diagnostic_count == 0 && metadata_apx.relocation_count == 0 &&
                                   metadata_apx.bytes.length == sizeof(expected_apx) &&
                                   memcmp(metadata_apx.bytes.pointer, expected_apx, sizeof(expected_apx)) == 0);
        AssemblyEncodeResult metadata_apx_missing = assembly_encode(arguments->arena, S8("addl $123, %r17d, %r16d\n"),
                                                                     (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_missing.diagnostic_count == 1 &&
                                   metadata_apx_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        Target apx_scc_target = advanced_target;
        apx_scc_target.cpu_features = target_cpu_features_add(apx_scc_target.cpu_features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
        u8 expected_apx_scc_byte[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0xf2};
        AssemblyEncodeResult metadata_apx_scc_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_att = assembly_encode(
            arguments->arena, S8("ccmpb $2, %r14b, %dl\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});

        // Generated APX disassembly aliases expose the otherwise-hidden NDD
        // forms without changing ordinary IMUL/SETcc selection.  `{nf}` is
        // still explicit, so IMULZU covers both ZU and ZU+NF rows.
        u8 expected_imulzu[] = {0x62, 0xf4, 0xfc, 0x18, 0x6b, 0xc0, 0x00};
        u8 expected_imulzu_nf[] = {0x62, 0xf4, 0xfc, 0x1c, 0x6b, 0xc0, 0x00};
        AssemblyEncodeResult metadata_imulzu = assembly_encode(
            arguments->arena, S8("imulzu rax, rax, 0\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_imulzu_nf = assembly_encode(
            arguments->arena, S8("{nf} imulzu rax, rax, 0\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_imulzu_att = assembly_encode(
            arguments->arena, S8("imulzu $0, %rax, %rax\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_imulzu.diagnostic_count == 0 && metadata_imulzu.bytes.length == sizeof(expected_imulzu) &&
                                   memcmp(metadata_imulzu.bytes.pointer, expected_imulzu, sizeof(expected_imulzu)) == 0);
        BUSTER_TEST(arguments, metadata_imulzu_nf.diagnostic_count == 0 && metadata_imulzu_nf.bytes.length == sizeof(expected_imulzu_nf) &&
                                   memcmp(metadata_imulzu_nf.bytes.pointer, expected_imulzu_nf, sizeof(expected_imulzu_nf)) == 0);
        BUSTER_TEST(arguments, metadata_imulzu_att.diagnostic_count == 0 && metadata_imulzu_att.bytes.length == sizeof(expected_imulzu) &&
                                   memcmp(metadata_imulzu_att.bytes.pointer, expected_imulzu, sizeof(expected_imulzu)) == 0);

        u8 expected_setzub[] = {0x62, 0xf4, 0x7c, 0x18, 0x42, 0xc0};
        u8 expected_setzub_mem[] = {0x62, 0xf4, 0x7c, 0x18, 0x42, 0x00};
        AssemblyEncodeResult metadata_setzub = assembly_encode(
            arguments->arena, S8("setzub al\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_setzub_mem = assembly_encode(
            arguments->arena, S8("setzub byte ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_setzub_att = assembly_encode(
            arguments->arena, S8("setzub %al\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult metadata_setzub_egpr = assembly_encode(
            arguments->arena, S8("setzub r16b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_setzub.diagnostic_count == 0 && metadata_setzub.bytes.length == sizeof(expected_setzub) &&
                                   memcmp(metadata_setzub.bytes.pointer, expected_setzub, sizeof(expected_setzub)) == 0);
        BUSTER_TEST(arguments, metadata_setzub_mem.diagnostic_count == 0 &&
                                   metadata_setzub_mem.bytes.length == sizeof(expected_setzub_mem) &&
                                   memcmp(metadata_setzub_mem.bytes.pointer, expected_setzub_mem, sizeof(expected_setzub_mem)) == 0);
        BUSTER_TEST(arguments, metadata_setzub_att.diagnostic_count == 0 && metadata_setzub_att.bytes.length == sizeof(expected_setzub) &&
                                   memcmp(metadata_setzub_att.bytes.pointer, expected_setzub, sizeof(expected_setzub)) == 0 &&
                                   metadata_setzub_egpr.diagnostic_count == 0 && metadata_setzub_egpr.bytes.length == sizeof(expected_setzub) &&
                                   memcmp(metadata_setzub_egpr.bytes.pointer, (u8[]){0x62, 0xfc, 0x7c, 0x18, 0x42, 0xc0},
                                          sizeof(expected_setzub)) == 0);

        AssemblyEncodeResult metadata_setb_legacy = assembly_encode(
            arguments->arena, S8("setb al\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_setb_legacy[] = {0x0f, 0x92, 0xc0};
        BUSTER_TEST(arguments, metadata_setb_legacy.diagnostic_count == 0 && metadata_setb_legacy.bytes.length == sizeof(expected_setb_legacy) &&
                                   memcmp(metadata_setb_legacy.bytes.pointer, expected_setb_legacy, sizeof(expected_setb_legacy)) == 0);

        AssemblyEncodeResult metadata_setzub_missing = assembly_encode(
            arguments->arena, S8("setzub al\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_imulzu_missing = assembly_encode(
            arguments->arena, S8("imulzu rax, rcx, 0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_setzub_missing.diagnostic_count == 1);
        BUSTER_TEST(arguments, metadata_setzub_missing.diagnostic_count == 1 &&
                                   metadata_setzub_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, metadata_imulzu_missing.diagnostic_count == 1);
        BUSTER_TEST(arguments, metadata_imulzu_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        AssemblyEncodeResult metadata_imulzu_wrong_count = assembly_encode(
            arguments->arena, S8("imulzu rax, rax\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_setzub_nf = assembly_encode(
            arguments->arena, S8("{nf} setzub al\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_imulzu_wrong_count.diagnostic_count == 1 &&
                                   metadata_imulzu_wrong_count.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_setzub_nf.diagnostic_count == 1 &&
                                   metadata_setzub_nf.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
        u8 expected_apx_scc_dfv15[] = {0x62, 0x74, 0x7c, 0x02, 0x38, 0xf2};
        AssemblyEncodeResult metadata_apx_scc_dfv15_intel = assembly_encode(
            arguments->arena, S8("ccmpb 15, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_dfv15_att = assembly_encode(
            arguments->arena, S8("ccmpb $15, %r14b, %dl\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_intel.diagnostic_count == 0 && metadata_apx_scc_intel.relocation_count == 0 &&
                                   metadata_apx_scc_intel.bytes.length == sizeof(expected_apx_scc_byte) &&
                                   memcmp(metadata_apx_scc_intel.bytes.pointer, expected_apx_scc_byte, sizeof(expected_apx_scc_byte)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_att.diagnostic_count == 0 && metadata_apx_scc_att.relocation_count == 0 &&
                                   metadata_apx_scc_att.bytes.length == sizeof(expected_apx_scc_byte) &&
                                   memcmp(metadata_apx_scc_att.bytes.pointer, expected_apx_scc_byte, sizeof(expected_apx_scc_byte)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_dfv15_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_dfv15_intel.relocation_count == 0 &&
                                   metadata_apx_scc_dfv15_intel.bytes.length == sizeof(expected_apx_scc_dfv15) &&
                                   memcmp(metadata_apx_scc_dfv15_intel.bytes.pointer, expected_apx_scc_dfv15,
                                          sizeof(expected_apx_scc_dfv15)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_dfv15_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_dfv15_att.relocation_count == 0 &&
                                   metadata_apx_scc_dfv15_att.bytes.length == sizeof(expected_apx_scc_dfv15) &&
                                   memcmp(metadata_apx_scc_dfv15_att.bytes.pointer, expected_apx_scc_dfv15,
                                          sizeof(expected_apx_scc_dfv15)) == 0);

        u8 expected_apx_scc_rip_dfv2[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0x35, 0x00, 0x00, 0x00, 0x00};
        u8 expected_apx_scc_rip_dfv0[] = {0x62, 0x74, 0x04, 0x02, 0x38, 0x35, 0x00, 0x00, 0x00, 0x00};
        AssemblyEncodeResult metadata_apx_scc_rip_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [rip + ccmp_external], r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_rip_att = assembly_encode(
            arguments->arena, S8("ccmpbb $0, %r14b, ccmp_external(%rip)\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_rip_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_rip_intel.bytes.length == sizeof(expected_apx_scc_rip_dfv2) &&
                                   memcmp(metadata_apx_scc_rip_intel.bytes.pointer, expected_apx_scc_rip_dfv2,
                                          sizeof(expected_apx_scc_rip_dfv2)) == 0 &&
                                   metadata_apx_scc_rip_intel.symbol_count == 1 && !metadata_apx_scc_rip_intel.symbols[0].defined &&
                                   string_equal(metadata_apx_scc_rip_intel.symbols[0].name, S8("ccmp_external")) &&
                                   metadata_apx_scc_rip_intel.relocation_count == 1 &&
                                   metadata_apx_scc_rip_intel.relocations[0].offset == 6 &&
                                   metadata_apx_scc_rip_intel.relocations[0].symbol == 0 &&
                                   metadata_apx_scc_rip_intel.relocations[0].addend == -4 &&
                                   metadata_apx_scc_rip_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
        BUSTER_TEST(arguments, metadata_apx_scc_rip_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_rip_att.bytes.length == sizeof(expected_apx_scc_rip_dfv0) &&
                                   memcmp(metadata_apx_scc_rip_att.bytes.pointer, expected_apx_scc_rip_dfv0,
                                          sizeof(expected_apx_scc_rip_dfv0)) == 0 &&
                                   metadata_apx_scc_rip_att.relocation_count == 1 &&
                                   metadata_apx_scc_rip_att.relocations[0].offset == 6 &&
                                   metadata_apx_scc_rip_att.relocations[0].symbol == 0 &&
                                   metadata_apx_scc_rip_att.relocations[0].addend == -4 &&
                                   metadata_apx_scc_rip_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

        u8 expected_apx_scc_memory_immediate[] = {0x62, 0xf4, 0x14, 0x02, 0x80, 0x38, 0x07};
        AssemblyEncodeResult metadata_apx_scc_memory_immediate_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [rax], 7\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_memory_immediate_att = assembly_encode(
            arguments->arena, S8("ccmpbb $2, $7, (%rax)\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_memory_immediate_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_memory_immediate_intel.relocation_count == 0 &&
                                   metadata_apx_scc_memory_immediate_intel.bytes.length == sizeof(expected_apx_scc_memory_immediate) &&
                                   memcmp(metadata_apx_scc_memory_immediate_intel.bytes.pointer, expected_apx_scc_memory_immediate,
                                          sizeof(expected_apx_scc_memory_immediate)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_memory_immediate_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_memory_immediate_att.relocation_count == 0 &&
                                   metadata_apx_scc_memory_immediate_att.bytes.length == sizeof(expected_apx_scc_memory_immediate) &&
                                   memcmp(metadata_apx_scc_memory_immediate_att.bytes.pointer, expected_apx_scc_memory_immediate,
                                          sizeof(expected_apx_scc_memory_immediate)) == 0);

        u8 expected_apx_scc_egpr_memory[] = {0x62, 0x7c, 0x10, 0x02, 0x38, 0x74, 0x51, 0x08};
        AssemblyEncodeResult metadata_apx_scc_egpr_memory = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [r17+r18*2+8], r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_apx_scc_egpr_memory.diagnostic_count == 0 &&
                                   metadata_apx_scc_egpr_memory.relocation_count == 0 &&
                                   metadata_apx_scc_egpr_memory.bytes.length == sizeof(expected_apx_scc_egpr_memory) &&
                                   memcmp(metadata_apx_scc_egpr_memory.bytes.pointer, expected_apx_scc_egpr_memory,
                                          sizeof(expected_apx_scc_egpr_memory)) == 0);

        u8 expected_apx_ctestz[] = {0x62, 0x74, 0x84, 0x04, 0x85, 0xf2};
        AssemblyEncodeResult metadata_apx_ctestz_intel = assembly_encode(
            arguments->arena, S8("ctestz 0, rdx, r14\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_ctestz_att = assembly_encode(
            arguments->arena, S8("ctestz $0, %r14, %rdx\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_ctestz_intel.diagnostic_count == 0 && metadata_apx_ctestz_intel.relocation_count == 0 &&
                                   metadata_apx_ctestz_intel.bytes.length == sizeof(expected_apx_ctestz) &&
                                   memcmp(metadata_apx_ctestz_intel.bytes.pointer, expected_apx_ctestz, sizeof(expected_apx_ctestz)) == 0);
        BUSTER_TEST(arguments, metadata_apx_ctestz_att.diagnostic_count == 0 && metadata_apx_ctestz_att.relocation_count == 0 &&
                                   metadata_apx_ctestz_att.bytes.length == sizeof(expected_apx_ctestz) &&
                                   memcmp(metadata_apx_ctestz_att.bytes.pointer, expected_apx_ctestz, sizeof(expected_apx_ctestz)) == 0);

        AssemblyEncodeResult metadata_apx_scc_missing_dfv = assembly_encode(
            arguments->arena, S8("ccmpb dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_negative_dfv = assembly_encode(
            arguments->arena, S8("ccmpb -1, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_large_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 16, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_symbol_dfv = assembly_encode(
            arguments->arena, S8("ccmpb ccmp_dfv, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_duplicate_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 2, 3, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_malformed_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 0xffffffffffffffff, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_apx_scc_missing_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_missing_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_missing_dfv.bytes.length == 0 && metadata_apx_scc_missing_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_missing_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_negative_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_negative_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_negative_dfv.bytes.length == 0 && metadata_apx_scc_negative_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_negative_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_large_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_large_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_large_dfv.bytes.length == 0 && metadata_apx_scc_large_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_large_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_symbol_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_symbol_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_symbol_dfv.bytes.length == 0 && metadata_apx_scc_symbol_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_symbol_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_duplicate_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_duplicate_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_duplicate_dfv.bytes.length == 0 && metadata_apx_scc_duplicate_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_duplicate_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_malformed_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_malformed_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_malformed_dfv.bytes.length == 0 && metadata_apx_scc_malformed_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_malformed_dfv.symbol_count == 0);

        u8 expected_evex_r4_scalar[] = {0x62, 0xf1, 0x7f, 0x18, 0x2d, 0xc1};
        AssemblyEncodeResult metadata_evex_r4_intel = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, eax, xmm1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_r4_att = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, %xmm1, %eax\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_r4_intel.diagnostic_count == 0 && metadata_evex_r4_intel.relocation_count == 0 &&
                                   metadata_evex_r4_intel.bytes.length == sizeof(expected_evex_r4_scalar) &&
                                   memcmp(metadata_evex_r4_intel.bytes.pointer, expected_evex_r4_scalar,
                                          sizeof(expected_evex_r4_scalar)) == 0);
        BUSTER_TEST(arguments, metadata_evex_r4_att.diagnostic_count == 0 && metadata_evex_r4_att.relocation_count == 0 &&
                                   metadata_evex_r4_att.bytes.length == sizeof(expected_evex_r4_scalar) &&
                                   memcmp(metadata_evex_r4_att.bytes.pointer, expected_evex_r4_scalar,
                                          sizeof(expected_evex_r4_scalar)) == 0);

        u8 expected_evex_r4_egpr_scalar[] = {0x62, 0xe1, 0x7f, 0x18, 0x2d, 0xc1};
        AssemblyEncodeResult metadata_evex_r4_egpr_intel = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, r16d, xmm1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_r4_egpr_att = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, %xmm1, %r16d\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_r4_egpr_intel.diagnostic_count == 0 &&
                                   metadata_evex_r4_egpr_intel.relocation_count == 0 &&
                                   metadata_evex_r4_egpr_intel.bytes.length == sizeof(expected_evex_r4_egpr_scalar) &&
                                   memcmp(metadata_evex_r4_egpr_intel.bytes.pointer, expected_evex_r4_egpr_scalar,
                                          sizeof(expected_evex_r4_egpr_scalar)) == 0);
        BUSTER_TEST(arguments, metadata_evex_r4_egpr_att.diagnostic_count == 0 &&
                                   metadata_evex_r4_egpr_att.relocation_count == 0 &&
                                   metadata_evex_r4_egpr_att.bytes.length == sizeof(expected_evex_r4_egpr_scalar) &&
                                   memcmp(metadata_evex_r4_egpr_att.bytes.pointer, expected_evex_r4_egpr_scalar,
                                          sizeof(expected_evex_r4_egpr_scalar)) == 0);

        u8 expected_unsigned_immediate[] = {0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        AssemblyEncodeResult metadata_unsigned_immediate_intel = assembly_encode(
            arguments->arena, S8("mov rax, 0xffffffffffffffff\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_unsigned_immediate_att = assembly_encode(
            arguments->arena, S8("movq $0xffffffffffffffff, %rax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_unsigned_immediate_intel.diagnostic_count == 0 &&
                                   metadata_unsigned_immediate_intel.bytes.length == sizeof(expected_unsigned_immediate) &&
                                   memcmp(metadata_unsigned_immediate_intel.bytes.pointer, expected_unsigned_immediate,
                                          sizeof(expected_unsigned_immediate)) == 0);
        BUSTER_TEST(arguments, metadata_unsigned_immediate_att.diagnostic_count == 0 &&
                                   metadata_unsigned_immediate_att.bytes.length == sizeof(expected_unsigned_immediate) &&
                                   memcmp(metadata_unsigned_immediate_att.bytes.pointer, expected_unsigned_immediate,
                                          sizeof(expected_unsigned_immediate)) == 0);

        {
            Target metadata_target = sse4a_target;
            u8 expected_extrq[] = {0x66, 0x0f, 0x78, 0xc0, 0x01, 0x02};
            u8 expected_extrq_att[] = {0x66, 0x0f, 0x78, 0xc0, 0x02, 0x01};
            u8 expected_insertq[] = {0xf2, 0x0f, 0x78, 0xc1, 0x01, 0x02};
            u8 expected_insertq_att[] = {0xf2, 0x0f, 0x78, 0xc8, 0x02, 0x01};
            Target intel_no_sse4a_target = x86_target;
            intel_no_sse4a_target.cpu_model = CPU_MODEL_INTEL_HASWELL;
            intel_no_sse4a_target.cpu_features_explicit = true;
            intel_no_sse4a_target.cpu_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
            AssemblyEncodeResult metadata_sse4a_missing =
                assembly_encode(arguments->arena, S8("extrq xmm0, 1, 2\ninsertq xmm0, xmm1, 1, 2\n"),
                                (AssemblyEncodeOptions){.target = intel_no_sse4a_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_sse4a_missing.diagnostic_count == 2 && metadata_sse4a_missing.bytes.length == 0 &&
                                       metadata_sse4a_missing.relocation_count == 0 && metadata_sse4a_missing.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < metadata_sse4a_missing.diagnostic_count; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_sse4a_missing.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
            }
            AssemblyEncodeResult metadata_extrq_intel = assembly_encode(arguments->arena, S8("extrq xmm0, 1, 2\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_intel = assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, 1, 2\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_extrq_att = assembly_encode(arguments->arena, S8("extrq $1, $2, %xmm0\n"),
                                                                      (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult metadata_insertq_att = assembly_encode(arguments->arena, S8("insertq $1, $2, %xmm0, %xmm1\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, metadata_extrq_intel.diagnostic_count == 0 && metadata_extrq_intel.bytes.length == sizeof(expected_extrq) &&
                                       memcmp(metadata_extrq_intel.bytes.pointer, expected_extrq, sizeof(expected_extrq)) == 0);
            BUSTER_TEST(arguments, metadata_insertq_intel.diagnostic_count == 0 && metadata_insertq_intel.bytes.length == sizeof(expected_insertq) &&
                                       memcmp(metadata_insertq_intel.bytes.pointer, expected_insertq, sizeof(expected_insertq)) == 0);
            BUSTER_TEST(arguments, metadata_extrq_att.diagnostic_count == 0 && metadata_extrq_att.bytes.length == sizeof(expected_extrq_att) &&
                                       memcmp(metadata_extrq_att.bytes.pointer, expected_extrq_att, sizeof(expected_extrq_att)) == 0);
            BUSTER_TEST(arguments, metadata_insertq_att.diagnostic_count == 0 && metadata_insertq_att.bytes.length == sizeof(expected_insertq_att) &&
                                       memcmp(metadata_insertq_att.bytes.pointer, expected_insertq_att, sizeof(expected_insertq_att)) == 0);

            u8 expected_extrq_boundaries[] = {
                0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00, 0x66, 0x0f, 0x78, 0xc0, 0xff, 0xff,
            };
            AssemblyEncodeResult metadata_extrq_boundaries =
                assembly_encode(arguments->arena, S8("extrq xmm0, 0, 0\nextrq xmm0, 255, 255\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments,
                        metadata_extrq_boundaries.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_extrq_boundaries.bytes, expected_extrq_boundaries, sizeof(expected_extrq_boundaries)) &&
                            metadata_extrq_boundaries.relocation_count == 0 && metadata_extrq_boundaries.symbol_count == 0);

            u8 expected_insertq_boundaries[] = {
                0xf2, 0x0f, 0x78, 0xc1, 0x00, 0x00, 0xf2, 0x0f, 0x78, 0xc1, 0xff, 0xff,
            };
            AssemblyEncodeResult metadata_insertq_boundaries =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, 0, 0\ninsertq xmm0, xmm1, 255, 255\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments,
                        metadata_insertq_boundaries.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_insertq_boundaries.bytes, expected_insertq_boundaries, sizeof(expected_insertq_boundaries)) &&
                            metadata_insertq_boundaries.relocation_count == 0 && metadata_insertq_boundaries.symbol_count == 0);

            AssemblyEncodeResult metadata_extrq_invalid = assembly_encode(arguments->arena, S8("extrq xmm0, -1, 0\nextrq xmm0, 0, 256\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_invalid =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, -1, 0\ninsertq xmm0, xmm1, 0, 256\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_invalid.diagnostic_count == 2 && metadata_extrq_invalid.bytes.length == 0 &&
                                       metadata_extrq_invalid.relocation_count == 0 && metadata_extrq_invalid.symbol_count == 0);
            BUSTER_TEST(arguments, metadata_insertq_invalid.diagnostic_count == 2 && metadata_insertq_invalid.bytes.length == 0 &&
                                       metadata_insertq_invalid.relocation_count == 0 && metadata_insertq_invalid.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < 2; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_extrq_invalid.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION);
                BUSTER_TEST(arguments, metadata_insertq_invalid.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION);
            }

            AssemblyEncodeResult metadata_extrq_shape = assembly_encode(arguments->arena, S8("extrq xmm0, 1\nextrq eax, 1, 2\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_shape = assembly_encode(arguments->arena, S8("insertq xmm0, 1, 2\ninsertq rax, xmm1, 1, 2\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_shape.diagnostic_count == 2 && metadata_extrq_shape.bytes.length == 0 &&
                                       metadata_extrq_shape.relocation_count == 0 && metadata_extrq_shape.symbol_count == 0);
            BUSTER_TEST(arguments, metadata_insertq_shape.diagnostic_count == 2 && metadata_insertq_shape.bytes.length == 0 &&
                                       metadata_insertq_shape.relocation_count == 0 && metadata_insertq_shape.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < 2; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_extrq_shape.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
                BUSTER_TEST(arguments, metadata_insertq_shape.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
            }

            u8 expected_extrq_symbols[] = {0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00};
            u8 expected_insertq_symbols[] = {0xf2, 0x0f, 0x78, 0xc1, 0x00, 0x00};
            u8 expected_extrq_symbols_att[] = {0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00};
            u8 expected_insertq_symbols_att[] = {0xf2, 0x0f, 0x78, 0xc8, 0x00, 0x00};
            AssemblyEncodeResult metadata_extrq_symbols = assembly_encode(arguments->arena, S8("extrq xmm0, extrq_lo, extrq_hi\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_symbols =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, insertq_lo, insertq_hi\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_symbols.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(metadata_extrq_symbols.bytes, expected_extrq_symbols, sizeof(expected_extrq_symbols)) &&
                                       metadata_extrq_symbols.symbol_count == 2 && metadata_extrq_symbols.relocation_count == 2);
            BUSTER_TEST(arguments, metadata_insertq_symbols.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(metadata_insertq_symbols.bytes, expected_insertq_symbols, sizeof(expected_insertq_symbols)) &&
                                       metadata_insertq_symbols.symbol_count == 2 && metadata_insertq_symbols.relocation_count == 2);
            BUSTER_TEST(arguments, metadata_extrq_symbols.relocations[0].offset == 4 && metadata_extrq_symbols.relocations[1].offset == 5 &&
                                       metadata_extrq_symbols.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       metadata_extrq_symbols.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       string_equal(metadata_extrq_symbols.symbols[metadata_extrq_symbols.relocations[0].symbol].name, S8("extrq_lo")) &&
                                       string_equal(metadata_extrq_symbols.symbols[metadata_extrq_symbols.relocations[1].symbol].name, S8("extrq_hi")));
            BUSTER_TEST(arguments, metadata_insertq_symbols.relocations[0].offset == 4 && metadata_insertq_symbols.relocations[1].offset == 5 &&
                                       metadata_insertq_symbols.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       metadata_insertq_symbols.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       string_equal(metadata_insertq_symbols.symbols[metadata_insertq_symbols.relocations[0].symbol].name, S8("insertq_lo")) &&
                                       string_equal(metadata_insertq_symbols.symbols[metadata_insertq_symbols.relocations[1].symbol].name, S8("insertq_hi")));

            AssemblyEncodeResult metadata_extrq_symbols_att =
                assembly_encode(arguments->arena, S8("extrq $extrq_att_lo, $extrq_att_hi, %xmm0\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult metadata_insertq_symbols_att =
                assembly_encode(arguments->arena, S8("insertq $insertq_att_lo, $insertq_att_hi, %xmm0, %xmm1\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments,
                        metadata_extrq_symbols_att.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_extrq_symbols_att.bytes, expected_extrq_symbols_att, sizeof(expected_extrq_symbols_att)) &&
                            metadata_extrq_symbols_att.symbol_count == 2 && metadata_extrq_symbols_att.relocation_count == 2);
            BUSTER_TEST(arguments,
                        metadata_insertq_symbols_att.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_insertq_symbols_att.bytes, expected_insertq_symbols_att, sizeof(expected_insertq_symbols_att)) &&
                            metadata_insertq_symbols_att.symbol_count == 2 && metadata_insertq_symbols_att.relocation_count == 2);
            BUSTER_TEST(arguments,
                        metadata_extrq_symbols_att.relocations[0].offset == 4 && metadata_extrq_symbols_att.relocations[1].offset == 5 &&
                            metadata_extrq_symbols_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            metadata_extrq_symbols_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            string_equal(metadata_extrq_symbols_att.symbols[metadata_extrq_symbols_att.relocations[0].symbol].name, S8("extrq_att_hi")) &&
                            string_equal(metadata_extrq_symbols_att.symbols[metadata_extrq_symbols_att.relocations[1].symbol].name, S8("extrq_att_lo")));
            BUSTER_TEST(arguments,
                        metadata_insertq_symbols_att.relocations[0].offset == 4 && metadata_insertq_symbols_att.relocations[1].offset == 5 &&
                            metadata_insertq_symbols_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            metadata_insertq_symbols_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            string_equal(metadata_insertq_symbols_att.symbols[metadata_insertq_symbols_att.relocations[0].symbol].name, S8("insertq_att_hi")) &&
                            string_equal(metadata_insertq_symbols_att.symbols[metadata_insertq_symbols_att.relocations[1].symbol].name, S8("insertq_att_lo")));
        }

        u8 expected_movq_exact[] = {0x0f, 0x6f, 0xc1};
        AssemblyEncodeResult metadata_movq_exact_intel =
            assembly_encode(arguments->arena, S8("movq mm0, mm1\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_movq_exact_att = assembly_encode(
            arguments->arena, S8("movq %mm1, %mm0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_movq_exact_intel.diagnostic_count == 0 &&
                                   metadata_movq_exact_intel.bytes.length == sizeof(expected_movq_exact) &&
                                   memcmp(metadata_movq_exact_intel.bytes.pointer, expected_movq_exact, sizeof(expected_movq_exact)) == 0);
        BUSTER_TEST(arguments, metadata_movq_exact_att.diagnostic_count == 0 &&
                                   metadata_movq_exact_att.bytes.length == sizeof(expected_movq_exact) &&
                                   memcmp(metadata_movq_exact_att.bytes.pointer, expected_movq_exact, sizeof(expected_movq_exact)) == 0);

        AssemblyEncodeResult metadata_invalid_suffix = assembly_encode(
            arguments->arena, S8("hltq\nsmswq %r8d\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_invalid_suffix.diagnostic_count == 2 && metadata_invalid_suffix.bytes.length == 0 &&
                                   metadata_invalid_suffix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                                   metadata_invalid_suffix.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

        u8 expected_relative_literal[] = {0x90, 0xeb, 0xfd};
        AssemblyEncodeResult metadata_relative_literal_intel = assembly_encode(
            arguments->arena, S8("nop\njmp_near 0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_relative_literal_att = assembly_encode(
            arguments->arena, S8("nop\njmp_near $0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_relative_literal_intel.diagnostic_count == 0 &&
                                   metadata_relative_literal_intel.bytes.length == sizeof(expected_relative_literal) &&
                                   memcmp(metadata_relative_literal_intel.bytes.pointer, expected_relative_literal,
                                          sizeof(expected_relative_literal)) == 0);
        BUSTER_TEST(arguments, metadata_relative_literal_att.diagnostic_count == 0 &&
                                   metadata_relative_literal_att.bytes.length == sizeof(expected_relative_literal) &&
                                   memcmp(metadata_relative_literal_att.bytes.pointer, expected_relative_literal,
                                          sizeof(expected_relative_literal)) == 0);

        u8 expected_int_symbol[] = {0xcd, 0x00};
        AssemblyEncodeResult metadata_int_external = assembly_encode(
            arguments->arena, S8("int external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_int_external.diagnostic_count == 0 && metadata_int_external.symbol_count == 1 &&
                                   !metadata_int_external.symbols[0].defined && metadata_int_external.relocation_count == 1 &&
                                   metadata_int_external.relocations[0].offset == 1 && metadata_int_external.relocations[0].symbol == 0 &&
                                   metadata_int_external.relocations[0].addend == 0 &&
                                   metadata_int_external.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                   metadata_int_external.bytes.length == sizeof(expected_int_symbol) &&
                                   memcmp(metadata_int_external.bytes.pointer, expected_int_symbol, sizeof(expected_int_symbol)) == 0);
        AssemblyEncodeResult metadata_int_local = assembly_encode(
            arguments->arena, S8("local:\nint local\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_int_local.diagnostic_count == 0 && metadata_int_local.symbol_count == 1 &&
                                   metadata_int_local.symbols[0].defined && metadata_int_local.symbols[0].offset == 0 &&
                                   metadata_int_local.relocation_count == 0 && metadata_int_local.bytes.length == sizeof(expected_int_symbol) &&
                                   memcmp(metadata_int_local.bytes.pointer, expected_int_symbol, sizeof(expected_int_symbol)) == 0);

        u8 expected_vex_address32[] = {0x67, 0xc5, 0xf0, 0x58, 0x03};
        AssemblyEncodeResult metadata_vex_intel = assembly_encode(
            arguments->arena, S8("vaddps xmm0, xmm1, xmmword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vex_att = assembly_encode(
            arguments->arena, S8("vaddps (%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_vex_intel.diagnostic_count == 0 && metadata_vex_intel.bytes.length == sizeof(expected_vex_address32) &&
                                   memcmp(metadata_vex_intel.bytes.pointer, expected_vex_address32, sizeof(expected_vex_address32)) == 0);
        BUSTER_TEST(arguments, metadata_vex_att.diagnostic_count == 0 && metadata_vex_att.bytes.length == sizeof(expected_vex_address32) &&
                                   memcmp(metadata_vex_att.bytes.pointer, expected_vex_address32, sizeof(expected_vex_address32)) == 0);

        u8 expected_vex_ymm_address32[] = {0x67, 0xc5, 0xf4, 0x58, 0x03};
        AssemblyEncodeResult metadata_vex_ymm_address32 = assembly_encode(
            arguments->arena, S8("vaddps ymm0, ymm1, ymmword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vex_ymm_address32.diagnostic_count == 0 &&
                                   metadata_vex_ymm_address32.bytes.length == sizeof(expected_vex_ymm_address32) &&
                                   memcmp(metadata_vex_ymm_address32.bytes.pointer, expected_vex_ymm_address32,
                                          sizeof(expected_vex_ymm_address32)) == 0);

        u8 expected_evex_zmm_memory[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
        AssemblyEncodeResult metadata_evex_zmm_qualified = assembly_encode(
            arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_evex_zmm_qualified.diagnostic_count == 0 &&
                                   metadata_evex_zmm_qualified.bytes.length == sizeof(expected_evex_zmm_memory) &&
                                   memcmp(metadata_evex_zmm_qualified.bytes.pointer, expected_evex_zmm_memory,
                                          sizeof(expected_evex_zmm_memory)) == 0);
        AssemblyEncodeResult metadata_evex_mismatched_qualifier = assembly_encode(
            arguments->arena, S8("vaddps zmm0, zmm1, xmmword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_evex_mismatched_qualifier.diagnostic_count == 1 &&
                                   metadata_evex_mismatched_qualifier.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_evex_mismatched_qualifier.bytes.length == 0 &&
                                   metadata_evex_mismatched_qualifier.symbol_count == 0 &&
                                   metadata_evex_mismatched_qualifier.relocation_count == 0);

        AssemblyEncodeResult metadata_vex_relocation_intel = assembly_encode(
            arguments->arena, S8("vaddps xmm0, xmm1, xmmword ptr [ebx + external + 8]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vex_relocation_att = assembly_encode(
            arguments->arena, S8("vaddps external+8(%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_vex_relocation[] = {0x67, 0xc5, 0xf0, 0x58, 0x83, 0x00, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, metadata_vex_relocation_intel.diagnostic_count == 0 && metadata_vex_relocation_intel.symbol_count == 1 &&
                                   metadata_vex_relocation_intel.bytes.length == sizeof(expected_vex_relocation) &&
                                   memcmp(metadata_vex_relocation_intel.bytes.pointer, expected_vex_relocation,
                                          sizeof(expected_vex_relocation)) == 0 && metadata_vex_relocation_intel.relocation_count == 1 &&
                                   !metadata_vex_relocation_intel.symbols[0].defined &&
                                   string_equal(metadata_vex_relocation_intel.symbols[0].name, S8("external")) &&
                                   metadata_vex_relocation_intel.relocations[0].offset == 5 &&
                                   metadata_vex_relocation_intel.relocations[0].symbol == 0 &&
                                   metadata_vex_relocation_intel.relocations[0].addend == 8 &&
                                   metadata_vex_relocation_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED);
        BUSTER_TEST(arguments, metadata_vex_relocation_att.diagnostic_count == 0 && metadata_vex_relocation_att.symbol_count == 1 &&
                                   metadata_vex_relocation_att.bytes.length == sizeof(expected_vex_relocation) &&
                                   memcmp(metadata_vex_relocation_att.bytes.pointer, expected_vex_relocation,
                                          sizeof(expected_vex_relocation)) == 0 && metadata_vex_relocation_att.relocation_count == 1 &&
                                   !metadata_vex_relocation_att.symbols[0].defined &&
                                   string_equal(metadata_vex_relocation_att.symbols[0].name, S8("external")) &&
                                   metadata_vex_relocation_att.relocations[0].offset == 5 &&
                                   metadata_vex_relocation_att.relocations[0].symbol == 0 &&
                                   metadata_vex_relocation_att.relocations[0].addend == 8 &&
                                   metadata_vex_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED);

        u8 expected_vpternlogd_two_relocations[] = {
            0x62, 0xf3, 0x75, 0x48, 0x25, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        AssemblyEncodeResult metadata_vpternlogd_no_newline = assembly_encode(
            arguments->arena, S8("vpternlogd zmm0, zmm1, zmmword ptr [mem_symbol], imm_symbol"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vpternlogd_newline = assembly_encode(
            arguments->arena, S8("vpternlogd zmm0, zmm1, zmmword ptr [mem_symbol], imm_symbol\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vpternlogd_no_newline.diagnostic_count == 0 &&
                                   metadata_vpternlogd_no_newline.bytes.length == sizeof(expected_vpternlogd_two_relocations) &&
                                   memcmp(metadata_vpternlogd_no_newline.bytes.pointer, expected_vpternlogd_two_relocations,
                                          sizeof(expected_vpternlogd_two_relocations)) == 0 &&
                                   metadata_vpternlogd_no_newline.symbol_count == 2 &&
                                   string_equal(metadata_vpternlogd_no_newline.symbols[0].name, S8("mem_symbol")) &&
                                   !metadata_vpternlogd_no_newline.symbols[0].defined &&
                                   string_equal(metadata_vpternlogd_no_newline.symbols[1].name, S8("imm_symbol")) &&
                                   !metadata_vpternlogd_no_newline.symbols[1].defined &&
                                   metadata_vpternlogd_no_newline.relocation_count == 2 &&
                                   metadata_vpternlogd_no_newline.relocations[0].offset == 7 &&
                                   metadata_vpternlogd_no_newline.relocations[0].symbol == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[0].addend == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED &&
                                   metadata_vpternlogd_no_newline.relocations[1].offset == 11 &&
                                   metadata_vpternlogd_no_newline.relocations[1].symbol == 1 &&
                                   metadata_vpternlogd_no_newline.relocations[1].addend == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8);
        BUSTER_TEST(arguments, metadata_vpternlogd_newline.diagnostic_count == 0 &&
                                   metadata_vpternlogd_newline.bytes.length == metadata_vpternlogd_no_newline.bytes.length &&
                                   memcmp(metadata_vpternlogd_newline.bytes.pointer, metadata_vpternlogd_no_newline.bytes.pointer,
                                          metadata_vpternlogd_no_newline.bytes.length) == 0 &&
                                   metadata_vpternlogd_newline.symbol_count == metadata_vpternlogd_no_newline.symbol_count &&
                                   metadata_vpternlogd_newline.relocation_count == metadata_vpternlogd_no_newline.relocation_count &&
                                   metadata_vpternlogd_newline.relocations[0].offset == metadata_vpternlogd_no_newline.relocations[0].offset &&
                                   metadata_vpternlogd_newline.relocations[0].symbol == metadata_vpternlogd_no_newline.relocations[0].symbol &&
                                   metadata_vpternlogd_newline.relocations[0].addend == metadata_vpternlogd_no_newline.relocations[0].addend &&
                                   metadata_vpternlogd_newline.relocations[0].kind == metadata_vpternlogd_no_newline.relocations[0].kind &&
                                   metadata_vpternlogd_newline.relocations[1].offset == metadata_vpternlogd_no_newline.relocations[1].offset &&
                                   metadata_vpternlogd_newline.relocations[1].symbol == metadata_vpternlogd_no_newline.relocations[1].symbol &&
                                   metadata_vpternlogd_newline.relocations[1].addend == metadata_vpternlogd_no_newline.relocations[1].addend &&
                                   metadata_vpternlogd_newline.relocations[1].kind == metadata_vpternlogd_no_newline.relocations[1].kind);
        AssemblyEncodeResult metadata_vex_local = assembly_encode(
            arguments->arena, S8("local:\n"
                                 "vaddps xmm0, xmm1, xmmword ptr [ebx + local + 8]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_vex_local[] = {0x67, 0xc5, 0xf0, 0x58, 0x83, 0x08, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, metadata_vex_local.diagnostic_count == 0 && metadata_vex_local.symbol_count == 1 &&
                                   metadata_vex_local.symbols[0].defined && metadata_vex_local.symbols[0].offset == 0 &&
                                   string_equal(metadata_vex_local.symbols[0].name, S8("local")) &&
                                   metadata_vex_local.relocation_count == 0 && metadata_vex_local.bytes.length == sizeof(expected_vex_local) &&
                                   memcmp(metadata_vex_local.bytes.pointer, expected_vex_local, sizeof(expected_vex_local)) == 0);

        u8 expected_xop_address32[] = {0x67, 0x8f, 0xe8, 0x70, 0xa3, 0x03, 0x20};
        AssemblyEncodeResult metadata_xop_intel = assembly_encode(
            arguments->arena, S8("vpperm xmm0, xmm1, xmmword ptr [ebx], xmm2\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_xop_att = assembly_encode(
            arguments->arena, S8("vpperm %xmm2, (%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_xop_intel.diagnostic_count == 0 && metadata_xop_intel.bytes.length == sizeof(expected_xop_address32) &&
                                   memcmp(metadata_xop_intel.bytes.pointer, expected_xop_address32, sizeof(expected_xop_address32)) == 0);
        BUSTER_TEST(arguments, metadata_xop_att.diagnostic_count == 0 && metadata_xop_att.bytes.length == sizeof(expected_xop_address32) &&
                                   memcmp(metadata_xop_att.bytes.pointer, expected_xop_address32, sizeof(expected_xop_address32)) == 0);

        Target gather_target = advanced_target;
        gather_target.cpu_features = target_cpu_features_add(gather_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
        AssemblyEncodeResult metadata_vsib_intel = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + xmm1*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vsib_att = assembly_encode(
            arguments->arena, S8("vgatherdps %xmm2, (%rax,%xmm1,4), %xmm0\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        // The final XMM operand is VEX.vvvv (the distinct destination); it
        // must not be dropped merely because the form also carries VSIB.
        u8 expected_vsib[] = {0xc4, 0xe2, 0x69, 0x92, 0x04, 0x88};
        BUSTER_TEST(arguments, metadata_vsib_intel.diagnostic_count == 0 && metadata_vsib_intel.bytes.length == sizeof(expected_vsib) &&
                                   memcmp(metadata_vsib_intel.bytes.pointer, expected_vsib, sizeof(expected_vsib)) == 0);
        BUSTER_TEST(arguments, metadata_vsib_att.diagnostic_count == 0 && metadata_vsib_att.bytes.length == sizeof(expected_vsib) &&
                                   memcmp(metadata_vsib_att.bytes.pointer, expected_vsib, sizeof(expected_vsib)) == 0);
        AssemblyEncodeResult metadata_vsib_invalid = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + rcx*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vsib_invalid.diagnostic_count == 1 &&
                                   metadata_vsib_invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   string_equal(metadata_vsib_invalid.diagnostics[0].message, S8("metadata instruction form is not encodable")) &&
                                   metadata_vsib_invalid.bytes.length == 0 && metadata_vsib_invalid.symbol_count == 0 &&
                                   metadata_vsib_invalid.relocation_count == 0);
        Target gather_no_avx2 = gather_target;
        gather_no_avx2.cpu_features = target_cpu_features_remove(gather_no_avx2.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
        AssemblyEncodeResult metadata_vsib_missing = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + xmm1*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_no_avx2, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vsib_missing.diagnostic_count == 1 &&
                                   metadata_vsib_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   metadata_vsib_missing.bytes.length == 0 && metadata_vsib_missing.symbol_count == 0 &&
                                   metadata_vsib_missing.relocation_count == 0);

        u8 expected_evex[] = {0x62, 0xf2, 0x6d, 0x49, 0x65, 0xc3};
        AssemblyEncodeResult metadata_evex_intel = assembly_encode(
            arguments->arena, S8("vblendmps zmm0 {k1}, zmm2, zmm3\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_att = assembly_encode(
            arguments->arena, S8("vblendmps %zmm3, %zmm2, %zmm0 {%k1}\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_intel.diagnostic_count == 0 && metadata_evex_intel.bytes.length == sizeof(expected_evex) &&
                                   memcmp(metadata_evex_intel.bytes.pointer, expected_evex, sizeof(expected_evex)) == 0);
        BUSTER_TEST(arguments, metadata_evex_att.diagnostic_count == 0 && metadata_evex_att.bytes.length == sizeof(expected_evex) &&
                                   memcmp(metadata_evex_att.bytes.pointer, expected_evex, sizeof(expected_evex)) == 0);

        u8 expected_amx[] = {0xc4, 0xe2, 0x78, 0x49, 0xc0};
        AssemblyEncodeResult metadata_amx_intel = assembly_encode(arguments->arena, S8("tilerelease\n"),
                                                                   (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_amx_att = assembly_encode(arguments->arena, S8("tilerelease\n"),
                                                                 (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_amx_intel.diagnostic_count == 0 && metadata_amx_intel.bytes.length == sizeof(expected_amx) &&
                                   memcmp(metadata_amx_intel.bytes.pointer, expected_amx, sizeof(expected_amx)) == 0);
        BUSTER_TEST(arguments, metadata_amx_att.diagnostic_count == 0 && metadata_amx_att.bytes.length == sizeof(expected_amx) &&
                                   memcmp(metadata_amx_att.bytes.pointer, expected_amx, sizeof(expected_amx)) == 0);

        u8 expected_segment[] = {0x64, 0x8b, 0x03};
        AssemblyEncodeResult metadata_segment_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_att = assembly_encode(
            arguments->arena, S8("movl %fs:(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_intel.diagnostic_count == 0 && metadata_segment_intel.bytes.length == sizeof(expected_segment) &&
                                   memcmp(metadata_segment_intel.bytes.pointer, expected_segment, sizeof(expected_segment)) == 0);
        BUSTER_TEST(arguments, metadata_segment_att.diagnostic_count == 0 && metadata_segment_att.bytes.length == sizeof(expected_segment) &&
                                   memcmp(metadata_segment_att.bytes.pointer, expected_segment, sizeof(expected_segment)) == 0);
        u8 expected_segment_displacement[] = {0x64, 0x8b, 0x43, 0x08};
        AssemblyEncodeResult metadata_segment_displacement_att = assembly_encode(
            arguments->arena, S8("movl %fs:8(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_displacement_att.diagnostic_count == 0 &&
                                   metadata_segment_displacement_att.bytes.length == sizeof(expected_segment_displacement) &&
                                   memcmp(metadata_segment_displacement_att.bytes.pointer, expected_segment_displacement,
                                          sizeof(expected_segment_displacement)) == 0);

        u8 expected_segment_register[] = {0x66, 0x8c, 0xe0};
        AssemblyEncodeResult metadata_segment_register_intel = assembly_encode(
            arguments->arena, S8("mov ax, fs\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_register_att = assembly_encode(
            arguments->arena, S8("movw %fs, %ax\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_register_intel.diagnostic_count == 0 &&
                                   metadata_segment_register_intel.bytes.length == sizeof(expected_segment_register) &&
                                   memcmp(metadata_segment_register_intel.bytes.pointer, expected_segment_register,
                                          sizeof(expected_segment_register)) == 0);
        BUSTER_TEST(arguments, metadata_segment_register_att.diagnostic_count == 0 &&
                                   metadata_segment_register_att.bytes.length == sizeof(expected_segment_register) &&
                                   memcmp(metadata_segment_register_att.bytes.pointer, expected_segment_register,
                                          sizeof(expected_segment_register)) == 0);

        u8 expected_segment_register_store[] = {0x66, 0x8e, 0xe0};
        AssemblyEncodeResult metadata_segment_register_store_intel = assembly_encode(
            arguments->arena, S8("mov fs, ax\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_register_store_att = assembly_encode(
            arguments->arena, S8("movw %ax, %fs\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_register_store_intel.diagnostic_count == 0 &&
                                   metadata_segment_register_store_intel.bytes.length == sizeof(expected_segment_register_store) &&
                                   memcmp(metadata_segment_register_store_intel.bytes.pointer, expected_segment_register_store,
                                          sizeof(expected_segment_register_store)) == 0);
        BUSTER_TEST(arguments, metadata_segment_register_store_att.diagnostic_count == 0 &&
                                   metadata_segment_register_store_att.bytes.length == sizeof(expected_segment_register_store) &&
                                   memcmp(metadata_segment_register_store_att.bytes.pointer, expected_segment_register_store,
                                          sizeof(expected_segment_register_store)) == 0);

        u8 expected_address32[] = {0x67, 0x8b, 0x03};
        AssemblyEncodeResult metadata_address32_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_address32_att = assembly_encode(
            arguments->arena, S8("movl (%ebx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_address32_intel.diagnostic_count == 0 && metadata_address32_intel.bytes.length == sizeof(expected_address32) &&
                                   memcmp(metadata_address32_intel.bytes.pointer, expected_address32, sizeof(expected_address32)) == 0);
        BUSTER_TEST(arguments, metadata_address32_att.diagnostic_count == 0 && metadata_address32_att.bytes.length == sizeof(expected_address32) &&
                                   memcmp(metadata_address32_att.bytes.pointer, expected_address32, sizeof(expected_address32)) == 0);

        u8 expected_indirect[] = {0xff, 0xd0, 0xff, 0x50, 0x08, 0xff, 0xe0, 0xff, 0x60, 0x08};
        AssemblyEncodeResult metadata_indirect_att = assembly_encode(
            arguments->arena, S8("call *%rax\ncall *8(%rax)\njmp *%rax\njmp *8(%rax)\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_indirect_att.diagnostic_count == 0 && metadata_indirect_att.relocation_count == 0 &&
                                   metadata_indirect_att.bytes.length == sizeof(expected_indirect) &&
                                   memcmp(metadata_indirect_att.bytes.pointer, expected_indirect, sizeof(expected_indirect)) == 0);

        AssemblyEncodeResult metadata_bnd = assembly_encode(arguments->arena, S8("bndcl bnd0, rax\n"),
                                                             (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_bnd.diagnostic_count == 1 && metadata_bnd.bytes.length == 0 &&
                                   metadata_bnd.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   string_equal(metadata_bnd.diagnostics[0].message, S8("instruction requires an enabled target feature")));
        AssemblyEncodeResult metadata_debug = assembly_encode(arguments->arena, S8("mov rax, dr0\n"),
                                                               (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_metadata_debug[] = {0x0f, 0x21, 0xc0};
        BUSTER_TEST(arguments, metadata_debug.diagnostic_count == 0 && metadata_debug.bytes.length == sizeof(expected_metadata_debug) &&
                                   memcmp(metadata_debug.bytes.pointer, expected_metadata_debug, sizeof(expected_metadata_debug)) == 0 &&
                                   metadata_debug.relocation_count == 0 && metadata_debug.symbol_count == 0);

        Target public_invlpgb_target = x86_target;
        public_invlpgb_target.cpu_model = CPU_MODEL_BASELINE;
        public_invlpgb_target.cpu_features_explicit = true;
        public_invlpgb_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_INVLPGB}, 2);
        String8 public_invlpgb_intel_source = S8("invlpgb\naddr32 invlpgb\n");
        AssemblyEncodeResult public_invlpgb_intel = assembly_encode(
            arguments->arena, public_invlpgb_intel_source,
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_invlpgb_att = assembly_encode(
            arguments->arena, public_invlpgb_intel_source,
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_invlpgb[] = {0x0f, 0x01, 0xfe, 0x67, 0x0f, 0x01, 0xfe};
        BUSTER_TEST(arguments, public_invlpgb_intel.diagnostic_count == 0 &&
                                   public_invlpgb_intel.bytes.length == sizeof(expected_public_invlpgb) &&
                                   memcmp(public_invlpgb_intel.bytes.pointer, expected_public_invlpgb,
                                          sizeof(expected_public_invlpgb)) == 0 && public_invlpgb_intel.relocation_count == 0 &&
                                   public_invlpgb_intel.symbol_count == 0);
        BUSTER_TEST(arguments, public_invlpgb_att.diagnostic_count == 0 &&
                                   public_invlpgb_att.bytes.length == sizeof(expected_public_invlpgb) &&
                                   memcmp(public_invlpgb_att.bytes.pointer, expected_public_invlpgb,
                                          sizeof(expected_public_invlpgb)) == 0 && public_invlpgb_att.relocation_count == 0 &&
                                   public_invlpgb_att.symbol_count == 0);
        Target missing_public_invlpgb = public_invlpgb_target;
        missing_public_invlpgb.cpu_features = target_cpu_features_remove(missing_public_invlpgb.cpu_features,
                                                                          TARGET_CPU_FEATURE_X86_INVLPGB);
        AssemblyEncodeResult missing_public_invlpgb_result = assembly_encode(
            arguments->arena, S8("invlpgb\n"),
            (AssemblyEncodeOptions){.target = missing_public_invlpgb, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_invlpgb_result.diagnostic_count == 1);
        BUSTER_TEST(arguments, missing_public_invlpgb_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_public_invlpgb_result.bytes.length == 0 && missing_public_invlpgb_result.relocation_count == 0 &&
                                   missing_public_invlpgb_result.symbol_count == 0);
        AssemblyEncodeResult duplicate_public_address_prefix = assembly_encode(
            arguments->arena, S8("addr32 addr32 invlpgb\n"),
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, duplicate_public_address_prefix.diagnostic_count == 1 &&
                                   duplicate_public_address_prefix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   duplicate_public_address_prefix.bytes.length == 0 && duplicate_public_address_prefix.relocation_count == 0 &&
                                   duplicate_public_address_prefix.symbol_count == 0);

        AssemblyEncodeResult public_addr32_wbinvd = assembly_encode(
            arguments->arena, S8("addr32 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_addr32_wbinvd[] = {0x67, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_addr32_wbinvd.diagnostic_count == 0 &&
                                   public_addr32_wbinvd.bytes.length == sizeof(expected_public_addr32_wbinvd) &&
                                   memcmp(public_addr32_wbinvd.bytes.pointer, expected_public_addr32_wbinvd,
                                          sizeof(expected_public_addr32_wbinvd)) == 0 &&
                                   public_addr32_wbinvd.relocation_count == 0 && public_addr32_wbinvd.symbol_count == 0);
        AssemblyEncodeResult public_addr64_wbinvd = assembly_encode(
            arguments->arena, S8("addr64 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, public_addr64_wbinvd.diagnostic_count == 1 &&
                                   public_addr64_wbinvd.bytes.length == 0 && public_addr64_wbinvd.relocation_count == 0 &&
                                   public_addr64_wbinvd.symbol_count == 0);
        AssemblyEncodeResult duplicate_public_wbinvd_address_prefix = assembly_encode(
            arguments->arena, S8("addr32 addr32 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, duplicate_public_wbinvd_address_prefix.diagnostic_count == 1 &&
                                   duplicate_public_wbinvd_address_prefix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   duplicate_public_wbinvd_address_prefix.bytes.length == 0 &&
                                   duplicate_public_wbinvd_address_prefix.relocation_count == 0 &&
                                   duplicate_public_wbinvd_address_prefix.symbol_count == 0);

        Target public_monitor_target = x86_target;
        public_monitor_target.cpu_model = CPU_MODEL_BASELINE;
        public_monitor_target.cpu_features_explicit = true;
        public_monitor_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_MONITOR}, 2);
        AssemblyEncodeResult public_monitor_intel = assembly_encode(
            arguments->arena, S8("monitor\naddr32 monitor\n"),
            (AssemblyEncodeOptions){.target = public_monitor_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_monitor_att = assembly_encode(
            arguments->arena, S8("monitor\naddr32 monitor\n"),
            (AssemblyEncodeOptions){.target = public_monitor_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_monitor[] = {0x0f, 0x01, 0xc8, 0x67, 0x0f, 0x01, 0xc8};
        BUSTER_TEST(arguments, public_monitor_intel.diagnostic_count == 0 && public_monitor_intel.bytes.length == sizeof(expected_public_monitor) &&
                                   memcmp(public_monitor_intel.bytes.pointer, expected_public_monitor, sizeof(expected_public_monitor)) == 0);
        BUSTER_TEST(arguments, public_monitor_att.diagnostic_count == 0 && public_monitor_att.bytes.length == sizeof(expected_public_monitor) &&
                                   memcmp(public_monitor_att.bytes.pointer, expected_public_monitor, sizeof(expected_public_monitor)) == 0);
        Target missing_public_monitor = public_monitor_target;
        missing_public_monitor.cpu_features = target_cpu_features_remove(missing_public_monitor.cpu_features,
                                                                         TARGET_CPU_FEATURE_X86_MONITOR);
        AssemblyEncodeResult missing_public_monitor_result = assembly_encode(
            arguments->arena, S8("monitor\n"),
            (AssemblyEncodeOptions){.target = missing_public_monitor, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_monitor_result.diagnostic_count == 1 &&
                                   missing_public_monitor_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_monitor_result.bytes.length == 0 && missing_public_monitor_result.relocation_count == 0 &&
                                   missing_public_monitor_result.symbol_count == 0);

        AssemblyEncodeResult public_wbinvd = assembly_encode(
            arguments->arena, S8("wbinvd\nrepne wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_wbinvd_att = assembly_encode(
            arguments->arena, S8("wbinvd\nrepne wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_wbinvd[] = {0x0f, 0x09, 0xf2, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_wbinvd.diagnostic_count == 0 && public_wbinvd.bytes.length == sizeof(expected_public_wbinvd) &&
                                   memcmp(public_wbinvd.bytes.pointer, expected_public_wbinvd, sizeof(expected_public_wbinvd)) == 0);
        BUSTER_TEST(arguments, public_wbinvd_att.diagnostic_count == 0 && public_wbinvd_att.bytes.length == sizeof(expected_public_wbinvd) &&
                                   memcmp(public_wbinvd_att.bytes.pointer, expected_public_wbinvd, sizeof(expected_public_wbinvd)) == 0);
        AssemblyEncodeResult public_rep_wbinvd = assembly_encode(
            arguments->arena, S8("rep wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, public_rep_wbinvd.diagnostic_count == 1 &&
                                   public_rep_wbinvd.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   public_rep_wbinvd.bytes.length == 0 && public_rep_wbinvd.relocation_count == 0 &&
                                   public_rep_wbinvd.symbol_count == 0);
        Target public_wbnoinvd_target = x86_target;
        public_wbnoinvd_target.cpu_model = CPU_MODEL_BASELINE;
        public_wbnoinvd_target.cpu_features_explicit = true;
        public_wbnoinvd_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_WBNOINVD}, 2);
        AssemblyEncodeResult public_wbnoinvd = assembly_encode(
            arguments->arena, S8("wbnoinvd\n"),
            (AssemblyEncodeOptions){.target = public_wbnoinvd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_wbnoinvd[] = {0xf3, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_wbnoinvd.diagnostic_count == 0 && public_wbnoinvd.bytes.length == sizeof(expected_public_wbnoinvd) &&
                                   memcmp(public_wbnoinvd.bytes.pointer, expected_public_wbnoinvd, sizeof(expected_public_wbnoinvd)) == 0);
        Target missing_public_wbnoinvd = public_wbnoinvd_target;
        missing_public_wbnoinvd.cpu_features = target_cpu_features_remove(missing_public_wbnoinvd.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_WBNOINVD);
        AssemblyEncodeResult missing_public_wbnoinvd_result = assembly_encode(
            arguments->arena, S8("wbnoinvd\n"),
            (AssemblyEncodeOptions){.target = missing_public_wbnoinvd, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_wbnoinvd_result.diagnostic_count == 1 &&
                                   missing_public_wbnoinvd_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_wbnoinvd_result.bytes.length == 0 &&
                                   missing_public_wbnoinvd_result.relocation_count == 0 &&
                                   missing_public_wbnoinvd_result.symbol_count == 0);

        u8 expected_public_control_debug[] = {
            0x44, 0x0f, 0x22, 0xf8,
            0x44, 0x0f, 0x20, 0xf8,
            0x44, 0x0f, 0x23, 0xf8,
            0x44, 0x0f, 0x21, 0xf8,
        };
        AssemblyEncodeResult public_control_debug_intel = assembly_encode(
            arguments->arena, S8("mov cr15, rax\nmov rax, cr15\nmov dr15, rax\nmov rax, dr15\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_control_debug_att = assembly_encode(
            arguments->arena, S8("movq %rax, %cr15\nmovq %cr15, %rax\nmovq %rax, %dr15\nmovq %dr15, %rax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_control_debug_intel.diagnostic_count == 0 &&
                                   public_control_debug_intel.bytes.length == sizeof(expected_public_control_debug) &&
                                   memcmp(public_control_debug_intel.bytes.pointer, expected_public_control_debug,
                                          sizeof(expected_public_control_debug)) == 0);
        BUSTER_TEST(arguments, public_control_debug_att.diagnostic_count == 0 &&
                                   public_control_debug_att.bytes.length == sizeof(expected_public_control_debug) &&
                                   memcmp(public_control_debug_att.bytes.pointer, expected_public_control_debug,
                                          sizeof(expected_public_control_debug)) == 0);
        AssemblyEncodeResult invalid_public_control_debug = assembly_encode(
            arguments->arena, S8("mov cr16, rax\nmov rax, cr16\nmov dr16, rax\nmov rax, dr16\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_public_control_debug.diagnostic_count == 4 &&
                                   invalid_public_control_debug.bytes.length == 0 &&
                                   invalid_public_control_debug.relocation_count == 0 &&
                                   invalid_public_control_debug.symbol_count == 0);
        AssemblyEncodeResult invalid_public_control_debug_egpr = assembly_encode(
            arguments->arena, S8("mov cr15, r16\nmov r16, cr15\nmov dr15, r16\nmov r16, dr15\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.diagnostic_count == 4);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.bytes.length == 0);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.relocation_count == 0);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.symbol_count == 0);

        Target public_vmx_target = virtualization_target;
        public_vmx_target.cpu_features = target_cpu_features_remove(public_vmx_target.cpu_features, TARGET_CPU_FEATURE_X86_SVM);
        AssemblyEncodeResult public_vmcall = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmcall[] = {0x0f, 0x01, 0xc1};
        BUSTER_TEST(arguments, public_vmcall.diagnostic_count == 0 && public_vmcall.bytes.length == sizeof(expected_public_vmcall) &&
                                   memcmp(public_vmcall.bytes.pointer, expected_public_vmcall, sizeof(expected_public_vmcall)) == 0);
        AssemblyEncodeResult public_vmcall_att = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vmcall_att.diagnostic_count == 0 && public_vmcall_att.bytes.length == sizeof(expected_public_vmcall) &&
                                   memcmp(public_vmcall_att.bytes.pointer, expected_public_vmcall, sizeof(expected_public_vmcall)) == 0);
        Target missing_public_vmx = public_vmx_target;
        missing_public_vmx.cpu_features = target_cpu_features_remove(missing_public_vmx.cpu_features, TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_vmx_result = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vmx_result.diagnostic_count == 1 &&
                                   missing_public_vmx_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_result.bytes.length == 0 && missing_public_vmx_result.relocation_count == 0 &&
                                   missing_public_vmx_result.symbol_count == 0);

        Target public_svm_target = virtualization_target;
        public_svm_target.cpu_features = target_cpu_features_remove(public_svm_target.cpu_features, TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult public_vmmcall = assembly_encode(
            arguments->arena, S8("vmmcall\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmmcall[] = {0x0f, 0x01, 0xd9};
        BUSTER_TEST(arguments, public_vmmcall.diagnostic_count == 0 && public_vmmcall.bytes.length == sizeof(expected_public_vmmcall) &&
                                   memcmp(public_vmmcall.bytes.pointer, expected_public_vmmcall, sizeof(expected_public_vmmcall)) == 0);
        Target missing_public_svm = public_svm_target;
        missing_public_svm.cpu_features = target_cpu_features_remove(missing_public_svm.cpu_features, TARGET_CPU_FEATURE_X86_SVM);
        AssemblyEncodeResult missing_public_svm_result = assembly_encode(
            arguments->arena, S8("vmmcall\n"),
            (AssemblyEncodeOptions){.target = missing_public_svm, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_svm_result.diagnostic_count == 1 &&
                                   missing_public_svm_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_svm_result.bytes.length == 0 && missing_public_svm_result.relocation_count == 0 &&
                                   missing_public_svm_result.symbol_count == 0);
        AssemblyEncodeResult public_invlpga = assembly_encode(
            arguments->arena, S8("invlpga\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_invlpga[] = {0x0f, 0x01, 0xdf};
        BUSTER_TEST(arguments, public_invlpga.diagnostic_count == 0 && public_invlpga.bytes.length == sizeof(expected_public_invlpga) &&
                                   memcmp(public_invlpga.bytes.pointer, expected_public_invlpga, sizeof(expected_public_invlpga)) == 0);
        AssemblyEncodeResult public_svm_att = assembly_encode(
            arguments->arena, S8("vmmcall\ninvlpga\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_svm_att.diagnostic_count == 0 &&
                                   public_svm_att.bytes.length == sizeof(expected_public_vmmcall) + sizeof(expected_public_invlpga) &&
                                   memcmp(public_svm_att.bytes.pointer, expected_public_vmmcall, sizeof(expected_public_vmmcall)) == 0 &&
                                   memcmp(public_svm_att.bytes.pointer + sizeof(expected_public_vmmcall), expected_public_invlpga,
                                          sizeof(expected_public_invlpga)) == 0);

        AssemblyEncodeResult public_vm_data_intel = assembly_encode(
            arguments->arena,
            S8("vmread qword ptr [rax], rcx\n"
               "vmread rdx, rcx\n"
               "vmwrite rcx, qword ptr [rax]\n"
               "vmwrite rcx, rdx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vm_data[] = {0x0f, 0x78, 0x08, 0x0f, 0x78, 0xca, 0x0f, 0x79, 0x08, 0x0f, 0x79, 0xca};
        BUSTER_TEST(arguments, public_vm_data_intel.diagnostic_count == 0 &&
                                   public_vm_data_intel.bytes.length == sizeof(expected_public_vm_data) &&
                                   memcmp(public_vm_data_intel.bytes.pointer, expected_public_vm_data,
                                          sizeof(expected_public_vm_data)) == 0);
        AssemblyEncodeResult public_vm_data_att = assembly_encode(
            arguments->arena,
            S8("vmreadq %rcx, (%rax)\n"
               "vmreadq %rcx, %rdx\n"
               "vmwriteq (%rax), %rcx\n"
               "vmwriteq %rdx, %rcx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vm_data_att.diagnostic_count == 0 &&
                                   public_vm_data_att.bytes.length == sizeof(expected_public_vm_data) &&
                                   memcmp(public_vm_data_att.bytes.pointer, expected_public_vm_data,
                                          sizeof(expected_public_vm_data)) == 0);
        AssemblyEncodeResult missing_public_vm_data = assembly_encode(
            arguments->arena, S8("vmread qword ptr [rax], rax\nvmwrite rax, qword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vm_data.diagnostic_count == 2 &&
                                   missing_public_vm_data.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vm_data.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vm_data.bytes.length == 0 && missing_public_vm_data.relocation_count == 0 &&
                                   missing_public_vm_data.symbol_count == 0);

        Target public_vmx_advanced_target = advanced_target;
        public_vmx_advanced_target.cpu_features = target_cpu_features_union(
            public_vmx_advanced_target.cpu_features,
            target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF, TARGET_CPU_FEATURE_X86_VMX}, 2));
        Target public_vmx_invpcid_target = public_vmx_target;
        public_vmx_invpcid_target.cpu_features = target_cpu_features_union(
            public_vmx_invpcid_target.cpu_features,
            target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_INVPCID));
        AssemblyEncodeResult public_vmx_memory_intel = assembly_encode(
            arguments->arena,
            S8("invpcid rcx, xmmword ptr [rax]\n"
               "invept rcx, xmmword ptr [rax]\n"
               "invvpid rcx, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_invpcid_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmx_memory[] = {
            0x66, 0x0f, 0x38, 0x82, 0x08,
            0x66, 0x0f, 0x38, 0x80, 0x08,
            0x66, 0x0f, 0x38, 0x81, 0x08,
        };
        BUSTER_TEST(arguments, public_vmx_memory_intel.diagnostic_count == 0 &&
                                   public_vmx_memory_intel.bytes.length == sizeof(expected_public_vmx_memory) &&
                                   memcmp(public_vmx_memory_intel.bytes.pointer, expected_public_vmx_memory,
                                          sizeof(expected_public_vmx_memory)) == 0);
        AssemblyEncodeResult public_vmx_memory_att = assembly_encode(
            arguments->arena,
            S8("invpcid (%rax), %rcx\n"
               "invept (%rax), %rcx\n"
               "invvpid (%rax), %rcx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_invpcid_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vmx_memory_att.diagnostic_count == 0 &&
                                   public_vmx_memory_att.bytes.length == sizeof(expected_public_vmx_memory) &&
                                   memcmp(public_vmx_memory_att.bytes.pointer, expected_public_vmx_memory,
                                          sizeof(expected_public_vmx_memory)) == 0);
        AssemblyEncodeResult missing_public_invpcid = assembly_encode(
            arguments->arena, S8("invpcid rax, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_invpcid.diagnostic_count == 1 &&
                                   missing_public_invpcid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_invpcid.bytes.length == 0 && missing_public_invpcid.relocation_count == 0 &&
                                   missing_public_invpcid.symbol_count == 0);
        Target missing_public_vmx_memory_target = public_vmx_invpcid_target;
        missing_public_vmx_memory_target.cpu_features = target_cpu_features_remove(missing_public_vmx_memory_target.cpu_features,
                                                                                     TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_vmx_memory = assembly_encode(
            arguments->arena, S8("invept rax, xmmword ptr [rax]\ninvvpid rax, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx_memory_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vmx_memory.diagnostic_count == 2 &&
                                   missing_public_vmx_memory.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_memory.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_memory.bytes.length == 0 && missing_public_vmx_memory.relocation_count == 0 &&
                                   missing_public_vmx_memory.symbol_count == 0);

        AssemblyEncodeResult public_apx_vmx = assembly_encode(
            arguments->arena,
            S8("invept r16, xmmword ptr [rax]\ninvvpid r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_apx_vmx[] = {0x62, 0xe4, 0x7e, 0x08, 0xf0, 0x00, 0x62, 0xe4, 0x7e, 0x08, 0xf1, 0x00};
        BUSTER_TEST(arguments, public_apx_vmx.diagnostic_count == 0 && public_apx_vmx.bytes.length == sizeof(expected_public_apx_vmx) &&
                                   memcmp(public_apx_vmx.bytes.pointer, expected_public_apx_vmx,
                                          sizeof(expected_public_apx_vmx)) == 0);
        Target public_apx_system_target = public_vmx_advanced_target;
        public_apx_system_target.cpu_features = target_cpu_features_union(
            public_apx_system_target.cpu_features,
            target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_X86_ENQCMD, TARGET_CPU_FEATURE_X86_INVPCID, TARGET_CPU_FEATURE_X86_MOVDIR64B,
                TARGET_CPU_FEATURE_X86_MSR_IMM}, 4));
        AssemblyEncodeResult public_apx_system = assembly_encode(
            arguments->arena,
            S8("enqcmds r16, zmmword ptr [rax]\n"
               "movdir64b r16, zmmword ptr [rax]\n"
               "invpcid r16, xmmword ptr [rax]\n"
               "rdmsr r16, 0x1234\n"
               "wrmsrns 0x1234, r16\n"),
            (AssemblyEncodeOptions){.target = public_apx_system_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_apx_system[] = {
            0x62, 0xe4, 0x7e, 0x08, 0xf8, 0x00,
            0x62, 0xe4, 0x7d, 0x08, 0xf8, 0x00,
            0x62, 0xe4, 0x7e, 0x08, 0xf2, 0x00,
            0x62, 0xff, 0x7f, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00,
            0x62, 0xff, 0x7e, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00,
        };
        BUSTER_TEST(arguments, public_apx_system.diagnostic_count == 0 &&
                                   public_apx_system.bytes.length == sizeof(expected_public_apx_system) &&
                                   memcmp(public_apx_system.bytes.pointer, expected_public_apx_system,
                                          sizeof(expected_public_apx_system)) == 0);
        Target public_apx_only_target = public_vmx_advanced_target;
        public_apx_only_target.cpu_features = target_cpu_features_remove(public_apx_only_target.cpu_features,
                                                                           TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_apx_vmx_apx_only = assembly_encode(
            arguments->arena, S8("invept r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_apx_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_apx_vmx_apx_only.diagnostic_count == 1 &&
                                   missing_public_apx_vmx_apx_only.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_apx_vmx_apx_only.bytes.length == 0 &&
                                   missing_public_apx_vmx_apx_only.relocation_count == 0 &&
                                   missing_public_apx_vmx_apx_only.symbol_count == 0);
        Target public_vmx_only_target = public_vmx_target;
        AssemblyEncodeResult missing_public_apx_vmx_vmx_only = assembly_encode(
            arguments->arena, S8("invept r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_apx_vmx_vmx_only.diagnostic_count == 1 &&
                                   missing_public_apx_vmx_vmx_only.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_apx_vmx_vmx_only.bytes.length == 0 &&
                                   missing_public_apx_vmx_vmx_only.relocation_count == 0 &&
                                   missing_public_apx_vmx_vmx_only.symbol_count == 0);

        AssemblyEncodeResult public_msr_intel = assembly_encode(
            arguments->arena, S8("wrmsr\nrdmsr\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_msr_att = assembly_encode(
            arguments->arena, S8("wrmsr\nrdmsr\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_msr[] = {0x0f, 0x30, 0x0f, 0x32};
        BUSTER_TEST(arguments, public_msr_intel.diagnostic_count == 0 && public_msr_intel.bytes.length == sizeof(expected_public_msr) &&
                                   memcmp(public_msr_intel.bytes.pointer, expected_public_msr, sizeof(expected_public_msr)) == 0);
        BUSTER_TEST(arguments, public_msr_att.diagnostic_count == 0 && public_msr_att.bytes.length == sizeof(expected_public_msr) &&
                                   memcmp(public_msr_att.bytes.pointer, expected_public_msr, sizeof(expected_public_msr)) == 0);

        AssemblyEncodeResult metadata_rollback = assembly_encode(
            arguments->arena,
            S8("vpperm xmm0, xmm1, xmmword ptr [rip + leaked], xmm2, 7\n"
               "nop\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_rollback.diagnostic_count == 1 &&
                                   metadata_rollback.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_rollback.bytes.length == 1 && metadata_rollback.bytes.pointer[0] == 0x90 &&
                                   metadata_rollback.symbol_count == 0 && metadata_rollback.relocation_count == 0);

        // Public APX ONE syntax omits the source count.  Keep it beside the
        // explicit-immediate spelling so the metadata fallback cannot confuse
        // an implicit ONE() operand with an ordinary immediate.
        u8 expected_apx_rol_one[] = {0xd5, 0x10, 0xd0, 0xc0};
        u8 expected_apx_rol_immediate[] = {0xd5, 0x10, 0xc0, 0xc0, 0x01};
        AssemblyEncodeResult apx_rol_one_intel = assembly_encode(
            arguments->arena, S8("rol r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_rol_one_att = assembly_encode(
            arguments->arena, S8("rolb %r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult apx_rol_immediate_intel = assembly_encode(
            arguments->arena, S8("rol r16b, 1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_rol_immediate_att = assembly_encode(
            arguments->arena, S8("rolb $1, %r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, apx_rol_one_intel.diagnostic_count == 0 &&
                                   apx_rol_one_intel.bytes.length == sizeof(expected_apx_rol_one) &&
                                   memcmp(apx_rol_one_intel.bytes.pointer, expected_apx_rol_one, sizeof(expected_apx_rol_one)) == 0);
        BUSTER_TEST(arguments, apx_rol_one_att.diagnostic_count == 0 &&
                                   apx_rol_one_att.bytes.length == sizeof(expected_apx_rol_one) &&
                                   memcmp(apx_rol_one_att.bytes.pointer, expected_apx_rol_one, sizeof(expected_apx_rol_one)) == 0);
        BUSTER_TEST(arguments, apx_rol_immediate_intel.diagnostic_count == 0 &&
                                   apx_rol_immediate_intel.bytes.length == sizeof(expected_apx_rol_immediate) &&
                                   memcmp(apx_rol_immediate_intel.bytes.pointer, expected_apx_rol_immediate,
                                          sizeof(expected_apx_rol_immediate)) == 0);
        BUSTER_TEST(arguments, apx_rol_immediate_att.diagnostic_count == 0 &&
                                   apx_rol_immediate_att.bytes.length == sizeof(expected_apx_rol_immediate) &&
                                   memcmp(apx_rol_immediate_att.bytes.pointer, expected_apx_rol_immediate,
                                          sizeof(expected_apx_rol_immediate)) == 0);

        Target selector_target = advanced_target;
        selector_target.cpu_features = target_cpu_features_union(selector_target.cpu_features,
                                                                  target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                      TARGET_CPU_FEATURE_X86_IBT, TARGET_CPU_FEATURE_X86_CLDEMOTE,
                                                                      TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_MOVRS,
                                                                      TARGET_CPU_FEATURE_X86_SHSTK}, 5));
        u8 expected_selector[] = {
            0xf3, 0x0f, 0x1e, 0xfb,
            0xf3, 0x0f, 0x1e, 0xfa,
            0x0f, 0x1c, 0x00,
            0x0f, 0x18, 0x20,
        };
        AssemblyEncodeResult selector_intel = assembly_encode(
            arguments->arena,
            S8("endbr32\nendbr64\ncldemote byte ptr [rax]\nprefetchrst2 byte ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult selector_att = assembly_encode(
            arguments->arena,
            S8("endbr32\nendbr64\ncldemote (%rax)\nprefetchrst2 (%rax)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, selector_intel.diagnostic_count == 0 && selector_intel.bytes.length == sizeof(expected_selector) &&
                                   memcmp(selector_intel.bytes.pointer, expected_selector, sizeof(expected_selector)) == 0 &&
                                   selector_intel.relocation_count == 0 && selector_intel.symbol_count == 0);
        BUSTER_TEST(arguments, selector_att.diagnostic_count == 0 && selector_att.bytes.length == sizeof(expected_selector) &&
                                   memcmp(selector_att.bytes.pointer, expected_selector, sizeof(expected_selector)) == 0 &&
                                   selector_att.relocation_count == 0 && selector_att.symbol_count == 0);
        u8 expected_prefetchit[] = {
            0x0f, 0x18, 0x3d, 0x00, 0x00, 0x00, 0x00,
            0x0f, 0x18, 0x35, 0x00, 0x00, 0x00, 0x00,
        };
        AssemblyEncodeResult prefetchit_intel = assembly_encode(
            arguments->arena,
            S8("prefetchit0 byte ptr [rip + prefetchit0_external]\nprefetchit1 byte ptr [rip + prefetchit1_external]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult prefetchit_att = assembly_encode(
            arguments->arena,
            S8("prefetchit0 prefetchit0_external(%rip)\nprefetchit1 prefetchit1_external(%rip)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, prefetchit_intel.diagnostic_count == 0 && prefetchit_intel.bytes.length == sizeof(expected_prefetchit) &&
                                   memcmp(prefetchit_intel.bytes.pointer, expected_prefetchit, sizeof(expected_prefetchit)) == 0 &&
                                   prefetchit_intel.relocation_count == 2 && prefetchit_intel.symbol_count == 2 &&
                                   prefetchit_intel.relocations[0].offset == 3 && prefetchit_intel.relocations[0].addend == -4 &&
                                   prefetchit_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_intel.symbols[prefetchit_intel.relocations[0].symbol].name, S8("prefetchit0_external")) &&
                                   prefetchit_intel.relocations[1].offset == 10 && prefetchit_intel.relocations[1].addend == -4 &&
                                   prefetchit_intel.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_intel.symbols[prefetchit_intel.relocations[1].symbol].name, S8("prefetchit1_external")));
        BUSTER_TEST(arguments, prefetchit_att.diagnostic_count == 0 && prefetchit_att.bytes.length == sizeof(expected_prefetchit) &&
                                   memcmp(prefetchit_att.bytes.pointer, expected_prefetchit, sizeof(expected_prefetchit)) == 0 &&
                                   prefetchit_att.relocation_count == 2 && prefetchit_att.symbol_count == 2 &&
                                   prefetchit_att.relocations[0].offset == 3 && prefetchit_att.relocations[0].addend == -4 &&
                                   prefetchit_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_att.symbols[prefetchit_att.relocations[0].symbol].name, S8("prefetchit0_external")) &&
                                   prefetchit_att.relocations[1].offset == 10 && prefetchit_att.relocations[1].addend == -4 &&
                                   prefetchit_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_att.symbols[prefetchit_att.relocations[1].symbol].name, S8("prefetchit1_external")));
        AssemblyEncodeResult invalid_prefetchit_intel = assembly_encode(
            arguments->arena, S8("prefetchit0 byte ptr [rax]\nprefetchit1 byte ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult invalid_prefetchit_att = assembly_encode(
            arguments->arena, S8("prefetchit0 (%rax)\nprefetchit1 (%rax)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_prefetchit_intel.diagnostic_count == 2 &&
                                   invalid_prefetchit_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_intel.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_intel.bytes.length == 0 && invalid_prefetchit_intel.relocation_count == 0 &&
                                   invalid_prefetchit_intel.symbol_count == 0);
        BUSTER_TEST(arguments, invalid_prefetchit_att.diagnostic_count == 2 &&
                                   invalid_prefetchit_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_att.bytes.length == 0 && invalid_prefetchit_att.relocation_count == 0 &&
                                   invalid_prefetchit_att.symbol_count == 0);
        String8 cet_ordinary_intel_source = S8(
            "clrssbsy [rax]\n"
            "endbr32\n"
            "endbr64\n"
            "incsspd eax\n"
            "incsspq rax\n"
            "rdsspd eax\n"
            "rdsspq rax\n"
            "rstorssp [rax]\n"
            "saveprevssp\n"
            "setssbsy\n"
            "wrssd dword ptr [rax], ebx\n"
            "wrssq qword ptr [rax], rbx\n"
            "wrussd dword ptr [rax], ebx\n"
            "wrussq qword ptr [rax], rbx\n");
        String8 cet_ordinary_att_source = S8(
            "clrssbsy (%rax)\n"
            "endbr32\n"
            "endbr64\n"
            "incsspd %eax\n"
            "incsspq %rax\n"
            "rdsspd %eax\n"
            "rdsspq %rax\n"
            "rstorssp (%rax)\n"
            "saveprevssp\n"
            "setssbsy\n"
            "wrssd %ebx, (%rax)\n"
            "wrssq %rbx, (%rax)\n"
            "wrussd %ebx, (%rax)\n"
            "wrussq %rbx, (%rax)\n");
        u8 expected_cet_ordinary[] = {
            0xf3, 0x0f, 0xae, 0x30,
            0xf3, 0x0f, 0x1e, 0xfb,
            0xf3, 0x0f, 0x1e, 0xfa,
            0xf3, 0x0f, 0xae, 0xe8,
            0xf3, 0x48, 0x0f, 0xae, 0xe8,
            0xf3, 0x0f, 0x1e, 0xc8,
            0xf3, 0x48, 0x0f, 0x1e, 0xc8,
            0xf3, 0x0f, 0x01, 0x28,
            0xf3, 0x0f, 0x01, 0xea,
            0xf3, 0x0f, 0x01, 0xe8,
            0x0f, 0x38, 0xf6, 0x18,
            0x48, 0x0f, 0x38, 0xf6, 0x18,
            0x66, 0x0f, 0x38, 0xf5, 0x18,
            0x66, 0x48, 0x0f, 0x38, 0xf5, 0x18,
        };
        Target ordinary_cet_target = selector_target;
        ordinary_cet_target.cpu_features = target_cpu_features_remove(ordinary_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        ordinary_cet_target.cpu_features = target_cpu_features_remove(ordinary_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
        AssemblyEncodeResult cet_ordinary_intel = assembly_encode(
            arguments->arena, cet_ordinary_intel_source,
            (AssemblyEncodeOptions){.target = ordinary_cet_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult cet_ordinary_att = assembly_encode(
            arguments->arena, cet_ordinary_att_source,
            (AssemblyEncodeOptions){.target = ordinary_cet_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, cet_ordinary_intel.diagnostic_count == 0 && cet_ordinary_intel.bytes.length == sizeof(expected_cet_ordinary) &&
                                   memcmp(cet_ordinary_intel.bytes.pointer, expected_cet_ordinary, sizeof(expected_cet_ordinary)) == 0 &&
                                   cet_ordinary_intel.relocation_count == 0 && cet_ordinary_intel.symbol_count == 0);
        BUSTER_TEST(arguments, cet_ordinary_att.diagnostic_count == 0 && cet_ordinary_att.bytes.length == sizeof(expected_cet_ordinary) &&
                                   memcmp(cet_ordinary_att.bytes.pointer, expected_cet_ordinary, sizeof(expected_cet_ordinary)) == 0 &&
                                   cet_ordinary_att.relocation_count == 0 && cet_ordinary_att.symbol_count == 0);
        u8 expected_shadow_stack[] = {
            0x48, 0x0f, 0x38, 0xf6, 0x18,
            0xf3, 0x48, 0x0f, 0x1e, 0xc9,
        };
        AssemblyEncodeResult shadow_stack_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [rax], rbx\nrdsspq rcx\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult shadow_stack_att = assembly_encode(
            arguments->arena, S8("wrssq %rbx, (%rax)\nrdsspq %rcx\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, shadow_stack_intel.diagnostic_count == 0 &&
                                   shadow_stack_intel.bytes.length == sizeof(expected_shadow_stack) &&
                                   memcmp(shadow_stack_intel.bytes.pointer, expected_shadow_stack, sizeof(expected_shadow_stack)) == 0 &&
                                   shadow_stack_intel.relocation_count == 0 && shadow_stack_intel.symbol_count == 0);
        BUSTER_TEST(arguments, shadow_stack_att.diagnostic_count == 0 && shadow_stack_att.bytes.length == sizeof(expected_shadow_stack) &&
                                   memcmp(shadow_stack_att.bytes.pointer, expected_shadow_stack, sizeof(expected_shadow_stack)) == 0 &&
                                   shadow_stack_att.relocation_count == 0 && shadow_stack_att.symbol_count == 0);

        Target apx_cet_target = selector_target;
        apx_cet_target.cpu_features = target_cpu_features_add(apx_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        u8 expected_apx_cet[] = {0x62, 0xec, 0xfc, 0x08, 0x66, 0x08};
        AssemblyEncodeResult apx_cet_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = apx_cet_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_cet_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = apx_cet_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, apx_cet_intel.diagnostic_count == 0 && apx_cet_intel.bytes.length == sizeof(expected_apx_cet) &&
                                   memcmp(apx_cet_intel.bytes.pointer, expected_apx_cet, sizeof(expected_apx_cet)) == 0 &&
                                   apx_cet_intel.relocation_count == 0 && apx_cet_intel.symbol_count == 0);
        BUSTER_TEST(arguments, apx_cet_att.diagnostic_count == 0 && apx_cet_att.bytes.length == sizeof(expected_apx_cet) &&
                                   memcmp(apx_cet_att.bytes.pointer, expected_apx_cet, sizeof(expected_apx_cet)) == 0 &&
                                   apx_cet_att.relocation_count == 0 && apx_cet_att.symbol_count == 0);

        Target missing_apx_cet_apx = apx_cet_target;
        missing_apx_cet_apx.cpu_features = target_cpu_features_remove(missing_apx_cet_apx.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        Target missing_apx_cet_shstk = apx_cet_target;
        missing_apx_cet_shstk.cpu_features = target_cpu_features_remove(missing_apx_cet_shstk.cpu_features, TARGET_CPU_FEATURE_X86_SHSTK);
        AssemblyEncodeResult missing_apx_cet_apx_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_apx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_apx_cet_shstk_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_shstk, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_apx_cet_apx_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_apx, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult missing_apx_cet_shstk_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_shstk, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_apx_cet_apx_intel.diagnostic_count == 1 &&
                                   missing_apx_cet_apx_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_apx_intel.bytes.length == 0 && missing_apx_cet_apx_intel.relocation_count == 0 &&
                                   missing_apx_cet_apx_intel.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_shstk_intel.diagnostic_count == 1 &&
                                   missing_apx_cet_shstk_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_shstk_intel.bytes.length == 0 && missing_apx_cet_shstk_intel.relocation_count == 0 &&
                                   missing_apx_cet_shstk_intel.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_apx_att.diagnostic_count == 1 &&
                                   missing_apx_cet_apx_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_apx_att.bytes.length == 0 && missing_apx_cet_apx_att.relocation_count == 0 &&
                                   missing_apx_cet_apx_att.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_shstk_att.diagnostic_count == 1 &&
                                   missing_apx_cet_shstk_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_shstk_att.bytes.length == 0 && missing_apx_cet_shstk_att.relocation_count == 0 &&
                                   missing_apx_cet_shstk_att.symbol_count == 0);

        Target missing_shstk_target = selector_target;
        missing_shstk_target.cpu_features = target_cpu_features_remove(missing_shstk_target.cpu_features, TARGET_CPU_FEATURE_X86_SHSTK);
        AssemblyEncodeResult missing_shstk = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [rax], rbx\nrdsspq rcx\n"),
            (AssemblyEncodeOptions){.target = missing_shstk_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_shstk_att = assembly_encode(
            arguments->arena, S8("wrssq %rbx, (%rax)\nrdsspq %rcx\n"),
            (AssemblyEncodeOptions){.target = missing_shstk_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_shstk.diagnostic_count == 2 && missing_shstk.bytes.length == 0 &&
                                   missing_shstk.relocation_count == 0 && missing_shstk.symbol_count == 0 &&
                                   missing_shstk.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_shstk.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_shstk_att.diagnostic_count == 2 && missing_shstk_att.bytes.length == 0 &&
                                   missing_shstk_att.relocation_count == 0 && missing_shstk_att.symbol_count == 0 &&
                                   missing_shstk_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_shstk_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        Target missing_ibt_target = selector_target;
        missing_ibt_target.cpu_features = target_cpu_features_remove(missing_ibt_target.cpu_features, TARGET_CPU_FEATURE_X86_IBT);
        AssemblyEncodeResult missing_ibt = assembly_encode(
            arguments->arena, S8("endbr32\nendbr64\n"),
            (AssemblyEncodeOptions){.target = missing_ibt_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_ibt_att = assembly_encode(
            arguments->arena, S8("endbr32\nendbr64\n"),
            (AssemblyEncodeOptions){.target = missing_ibt_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_ibt.diagnostic_count == 2 && missing_ibt.bytes.length == 0 &&
                                   missing_ibt.relocation_count == 0 && missing_ibt.symbol_count == 0 &&
                                   missing_ibt.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_ibt.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_ibt_att.diagnostic_count == 2 && missing_ibt_att.bytes.length == 0 &&
                                   missing_ibt_att.relocation_count == 0 && missing_ibt_att.symbol_count == 0 &&
                                   missing_ibt_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_ibt_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        String8 selector_intel_sources[] = {
            S8("endbr64\n"),
            S8("cldemote byte ptr [rax]\n"),
            S8("prefetchit0 byte ptr [rip + missing_prefetchit0]\nprefetchit1 byte ptr [rip + missing_prefetchit1]\n"),
            S8("prefetchrst2 byte ptr [rax]\n"),
        };
        String8 selector_att_sources[] = {
            S8("endbr64\n"),
            S8("cldemote (%rax)\n"),
            S8("prefetchit0 missing_prefetchit0(%rip)\nprefetchit1 missing_prefetchit1(%rip)\n"),
            S8("prefetchrst2 (%rax)\n"),
        };
        TargetCpuFeature selector_features[] = {
            TARGET_CPU_FEATURE_X86_IBT,
            TARGET_CPU_FEATURE_X86_CLDEMOTE,
            TARGET_CPU_FEATURE_X86_PREFETCHI,
            TARGET_CPU_FEATURE_X86_MOVRS,
        };
        for (u32 selector_index = 0; selector_index < BUSTER_ARRAY_LENGTH(selector_features); selector_index += 1)
        {
            Target missing_selector_target = selector_target;
            missing_selector_target.cpu_features = target_cpu_features_remove(missing_selector_target.cpu_features, selector_features[selector_index]);
            AssemblyEncodeResult missing_selector_intel = assembly_encode(
                arguments->arena, selector_intel_sources[selector_index],
                (AssemblyEncodeOptions){.target = missing_selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult missing_selector_att = assembly_encode(
                arguments->arena, selector_att_sources[selector_index],
                (AssemblyEncodeOptions){.target = missing_selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            u32 expected_selector_diagnostics = selector_index == 2 ? 2 : 1;
            BUSTER_TEST(arguments, missing_selector_intel.diagnostic_count == expected_selector_diagnostics &&
                                       missing_selector_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                       (expected_selector_diagnostics == 1 ||
                                        missing_selector_intel.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE) &&
                                       missing_selector_intel.bytes.length == 0 && missing_selector_intel.relocation_count == 0 &&
                                       missing_selector_intel.symbol_count == 0);
            BUSTER_TEST(arguments, missing_selector_att.diagnostic_count == expected_selector_diagnostics &&
                                       missing_selector_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                       (expected_selector_diagnostics == 1 ||
                                        missing_selector_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE) &&
                                       missing_selector_att.bytes.length == 0 && missing_selector_att.relocation_count == 0 &&
                                       missing_selector_att.symbol_count == 0);
        }
    }

    {
        u8 const expected_classic_intel[] = {
            0xa4,
            0x66, 0xa5,
            0xa5,
            0x48, 0xa5,
            0xa6,
            0x66, 0xa7,
            0xa7,
            0x48, 0xa7,
            0xaa,
            0x66, 0xab,
            0xab,
            0x48, 0xab,
            0xac,
            0x66, 0xad,
            0xad,
            0x48, 0xad,
            0xae,
            0x66, 0xaf,
            0xaf,
            0x48, 0xaf,
            0x6c,
            0x66, 0x6d,
            0x6d,
            0x6e,
            0x66, 0x6f,
            0x6f,
        };
        AssemblyEncodeResult classic_intel = assembly_encode(
            arguments->arena,
            S8("movsb\nmovsw\nmovsd\nmovsq\n"
               "cmpsb\ncmpsw\ncmpsd\ncmpsq\n"
               "stosb\nstosw\nstosd\nstosq\n"
               "lodsb\nlodsw\nlodsd\nlodsq\n"
               "scasb\nscasw\nscasd\nscasq\n"
               "insb\ninsw\ninsd\noutsb\noutsw\noutsd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, classic_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_intel.bytes, expected_classic_intel,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_intel)) &&
                                   classic_intel.relocation_count == 0 && classic_intel.symbol_count == 0);

        AssemblyEncodeResult classic_att = assembly_encode(
            arguments->arena,
            S8("movsb\nmovsw\nmovsl\nmovsq\n"
               "cmpsb\ncmpsw\ncmpsl\ncmpsq\n"
               "stosb\nstosw\nstosl\nstosq\n"
               "lodsb\nlodsw\nlodsl\nlodsq\n"
               "scasb\nscasw\nscasl\nscasq\n"
               "insb\ninsw\ninsl\noutsb\noutsw\noutsl\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, classic_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_att.bytes, expected_classic_intel,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_intel)) &&
                                   classic_att.relocation_count == 0 && classic_att.symbol_count == 0);

        typedef struct ClassicRepeatEncodingCase ClassicRepeatEncodingCase;
        struct ClassicRepeatEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[3];
            u8 byte_count;
        };
        static ClassicRepeatEncodingCase const classic_repeat_matrix[] = {
            {S8_INITIALIZER("rep movsb"), S8_INITIALIZER("rep movsb"), {0xf3, 0xa4}, 2},
            {S8_INITIALIZER("rep movsw"), S8_INITIALIZER("rep movsw"), {0xf3, 0x66, 0xa5}, 3},
            {S8_INITIALIZER("rep movsd"), S8_INITIALIZER("rep movsl"), {0xf3, 0xa5}, 2},
            {S8_INITIALIZER("rep movsq"), S8_INITIALIZER("rep movsq"), {0xf3, 0x48, 0xa5}, 3},
            {S8_INITIALIZER("rep cmpsb"), S8_INITIALIZER("rep cmpsb"), {0xf3, 0xa6}, 2},
            {S8_INITIALIZER("repz cmpsw"), S8_INITIALIZER("repz cmpsw"), {0xf3, 0x66, 0xa7}, 3},
            {S8_INITIALIZER("repe cmpsd"), S8_INITIALIZER("repe cmpsl"), {0xf3, 0xa7}, 2},
            {S8_INITIALIZER("repz cmpsq"), S8_INITIALIZER("repz cmpsq"), {0xf3, 0x48, 0xa7}, 3},
            {S8_INITIALIZER("repne cmpsb"), S8_INITIALIZER("repne cmpsb"), {0xf2, 0xa6}, 2},
            {S8_INITIALIZER("repnz cmpsw"), S8_INITIALIZER("repnz cmpsw"), {0xf2, 0x66, 0xa7}, 3},
            {S8_INITIALIZER("repne cmpsd"), S8_INITIALIZER("repne cmpsl"), {0xf2, 0xa7}, 2},
            {S8_INITIALIZER("repnz cmpsq"), S8_INITIALIZER("repnz cmpsq"), {0xf2, 0x48, 0xa7}, 3},
            {S8_INITIALIZER("rep stosb"), S8_INITIALIZER("rep stosb"), {0xf3, 0xaa}, 2},
            {S8_INITIALIZER("rep stosw"), S8_INITIALIZER("rep stosw"), {0xf3, 0x66, 0xab}, 3},
            {S8_INITIALIZER("rep stosd"), S8_INITIALIZER("rep stosl"), {0xf3, 0xab}, 2},
            {S8_INITIALIZER("rep stosq"), S8_INITIALIZER("rep stosq"), {0xf3, 0x48, 0xab}, 3},
            {S8_INITIALIZER("rep lodsb"), S8_INITIALIZER("rep lodsb"), {0xf3, 0xac}, 2},
            {S8_INITIALIZER("rep lodsw"), S8_INITIALIZER("rep lodsw"), {0xf3, 0x66, 0xad}, 3},
            {S8_INITIALIZER("rep lodsd"), S8_INITIALIZER("rep lodsl"), {0xf3, 0xad}, 2},
            {S8_INITIALIZER("rep lodsq"), S8_INITIALIZER("rep lodsq"), {0xf3, 0x48, 0xad}, 3},
            {S8_INITIALIZER("rep insb"), S8_INITIALIZER("rep insb"), {0xf3, 0x6c}, 2},
            {S8_INITIALIZER("rep insw"), S8_INITIALIZER("rep insw"), {0xf3, 0x66, 0x6d}, 3},
            {S8_INITIALIZER("rep insd"), S8_INITIALIZER("rep insl"), {0xf3, 0x6d}, 2},
            {S8_INITIALIZER("rep outsb"), S8_INITIALIZER("rep outsb"), {0xf3, 0x6e}, 2},
            {S8_INITIALIZER("rep outsw"), S8_INITIALIZER("rep outsw"), {0xf3, 0x66, 0x6f}, 3},
            {S8_INITIALIZER("rep outsd"), S8_INITIALIZER("rep outsl"), {0xf3, 0x6f}, 2},
            {S8_INITIALIZER("rep scasb"), S8_INITIALIZER("rep scasb"), {0xf3, 0xae}, 2},
            {S8_INITIALIZER("repz scasw"), S8_INITIALIZER("repz scasw"), {0xf3, 0x66, 0xaf}, 3},
            {S8_INITIALIZER("repe scasd"), S8_INITIALIZER("repe scasl"), {0xf3, 0xaf}, 2},
            {S8_INITIALIZER("repz scasq"), S8_INITIALIZER("repz scasq"), {0xf3, 0x48, 0xaf}, 3},
            {S8_INITIALIZER("repne scasb"), S8_INITIALIZER("repne scasb"), {0xf2, 0xae}, 2},
            {S8_INITIALIZER("repnz scasw"), S8_INITIALIZER("repnz scasw"), {0xf2, 0x66, 0xaf}, 3},
            {S8_INITIALIZER("repne scasd"), S8_INITIALIZER("repne scasl"), {0xf2, 0xaf}, 2},
            {S8_INITIALIZER("repnz scasq"), S8_INITIALIZER("repnz scasq"), {0xf2, 0x48, 0xaf}, 3},
            {S8_INITIALIZER("repne movsb"), S8_INITIALIZER("repne movsb"), {0xf2, 0xa4}, 2},
            {S8_INITIALIZER("repnz movsw"), S8_INITIALIZER("repnz movsw"), {0xf2, 0x66, 0xa5}, 3},
            {S8_INITIALIZER("repne movsd"), S8_INITIALIZER("repne movsl"), {0xf2, 0xa5}, 2},
            {S8_INITIALIZER("repnz movsq"), S8_INITIALIZER("repnz movsq"), {0xf2, 0x48, 0xa5}, 3},
            {S8_INITIALIZER("repnz stosb"), S8_INITIALIZER("repnz stosb"), {0xf2, 0xaa}, 2},
            {S8_INITIALIZER("repne stosw"), S8_INITIALIZER("repne stosw"), {0xf2, 0x66, 0xab}, 3},
            {S8_INITIALIZER("repnz stosd"), S8_INITIALIZER("repnz stosl"), {0xf2, 0xab}, 2},
            {S8_INITIALIZER("repne stosq"), S8_INITIALIZER("repne stosq"), {0xf2, 0x48, 0xab}, 3},
            {S8_INITIALIZER("repne lodsb"), S8_INITIALIZER("repne lodsb"), {0xf2, 0xac}, 2},
            {S8_INITIALIZER("repnz lodsw"), S8_INITIALIZER("repnz lodsw"), {0xf2, 0x66, 0xad}, 3},
            {S8_INITIALIZER("repne lodsd"), S8_INITIALIZER("repne lodsl"), {0xf2, 0xad}, 2},
            {S8_INITIALIZER("repnz lodsq"), S8_INITIALIZER("repnz lodsq"), {0xf2, 0x48, 0xad}, 3},
            {S8_INITIALIZER("repnz insb"), S8_INITIALIZER("repnz insb"), {0xf2, 0x6c}, 2},
            {S8_INITIALIZER("repne insw"), S8_INITIALIZER("repne insw"), {0xf2, 0x66, 0x6d}, 3},
            {S8_INITIALIZER("repnz insd"), S8_INITIALIZER("repnz insl"), {0xf2, 0x6d}, 2},
            {S8_INITIALIZER("repne outsb"), S8_INITIALIZER("repne outsb"), {0xf2, 0x6e}, 2},
            {S8_INITIALIZER("repnz outsw"), S8_INITIALIZER("repnz outsw"), {0xf2, 0x66, 0x6f}, 3},
            {S8_INITIALIZER("repne outsd"), S8_INITIALIZER("repne outsl"), {0xf2, 0x6f}, 2},
        };
        for (u32 repeat_index = 0; repeat_index < BUSTER_ARRAY_LENGTH(classic_repeat_matrix); repeat_index += 1)
        {
            ClassicRepeatEncodingCase repeat_case = classic_repeat_matrix[repeat_index];
            AssemblyEncodeResult repeat_intel = assembly_encode(
                arguments->arena, repeat_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult repeat_att = assembly_encode(
                arguments->arena, repeat_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, repeat_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(repeat_intel.bytes, repeat_case.bytes, repeat_case.byte_count) &&
                                       repeat_intel.relocation_count == 0 && repeat_intel.symbol_count == 0 &&
                                       repeat_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(repeat_att.bytes, repeat_case.bytes, repeat_case.byte_count) &&
                                       repeat_att.relocation_count == 0 && repeat_att.symbol_count == 0);
        }

        typedef struct ClassicAddressSizeEncodingCase ClassicAddressSizeEncodingCase;
        struct ClassicAddressSizeEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[3];
            u8 byte_count;
        };
        static ClassicAddressSizeEncodingCase const classic_address_size_matrix[] = {
            {S8_INITIALIZER("addr32 movsb"), S8_INITIALIZER("addr32 movsb"), {0x67, 0xa4}, 2},
            {S8_INITIALIZER("addr32 cmpsb"), S8_INITIALIZER("addr32 cmpsb"), {0x67, 0xa6}, 2},
            {S8_INITIALIZER("addr32 stosb"), S8_INITIALIZER("addr32 stosb"), {0x67, 0xaa}, 2},
            {S8_INITIALIZER("addr32 lodsb"), S8_INITIALIZER("addr32 lodsb"), {0x67, 0xac}, 2},
            {S8_INITIALIZER("addr32 scasb"), S8_INITIALIZER("addr32 scasb"), {0x67, 0xae}, 2},
            {S8_INITIALIZER("addr32 insb"), S8_INITIALIZER("addr32 insb"), {0x67, 0x6c}, 2},
            {S8_INITIALIZER("addr32 outsb"), S8_INITIALIZER("addr32 outsb"), {0x67, 0x6e}, 2},
            {S8_INITIALIZER("addr32 rep movsb"), S8_INITIALIZER("addr32 rep movsb"), {0x67, 0xf3, 0xa4}, 3},
            {S8_INITIALIZER("addr32 repe cmpsb"), S8_INITIALIZER("addr32 repz cmpsb"), {0x67, 0xf3, 0xa6}, 3},
            {S8_INITIALIZER("addr32 rep stosb"), S8_INITIALIZER("addr32 rep stosb"), {0x67, 0xf3, 0xaa}, 3},
            {S8_INITIALIZER("addr32 rep lodsb"), S8_INITIALIZER("addr32 rep lodsb"), {0x67, 0xf3, 0xac}, 3},
            {S8_INITIALIZER("addr32 rep insb"), S8_INITIALIZER("addr32 rep insb"), {0x67, 0xf3, 0x6c}, 3},
            {S8_INITIALIZER("addr32 rep outsb"), S8_INITIALIZER("addr32 rep outsb"), {0x67, 0xf3, 0x6e}, 3},
            {S8_INITIALIZER("addr32 repne scasb"), S8_INITIALIZER("addr32 repnz scasb"), {0x67, 0xf2, 0xae}, 3},
        };
        for (u32 address_size_index = 0; address_size_index < BUSTER_ARRAY_LENGTH(classic_address_size_matrix);
             address_size_index += 1)
        {
            ClassicAddressSizeEncodingCase address_size_case = classic_address_size_matrix[address_size_index];
            AssemblyEncodeResult address_size_intel = assembly_encode(
                arguments->arena, address_size_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult address_size_att = assembly_encode(
                arguments->arena, address_size_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, address_size_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(address_size_intel.bytes, address_size_case.bytes,
                                                                 address_size_case.byte_count) &&
                                       address_size_intel.relocation_count == 0 && address_size_intel.symbol_count == 0 &&
                                       address_size_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(address_size_att.bytes, address_size_case.bytes,
                                                                 address_size_case.byte_count) &&
                                       address_size_att.relocation_count == 0 && address_size_att.symbol_count == 0);
        }

        typedef struct ClassicSegmentPrefixEncodingCase ClassicSegmentPrefixEncodingCase;
        struct ClassicSegmentPrefixEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[4];
            u8 byte_count;
        };
        static ClassicSegmentPrefixEncodingCase const classic_segment_prefix_matrix[] = {
            {S8_INITIALIZER("es movsb"), S8_INITIALIZER("es movsb"), {0x26, 0xa4}, 2},
            {S8_INITIALIZER("cs movsb"), S8_INITIALIZER("cs movsb"), {0x2e, 0xa4}, 2},
            {S8_INITIALIZER("ss movsb"), S8_INITIALIZER("ss movsb"), {0x36, 0xa4}, 2},
            {S8_INITIALIZER("ds movsb"), S8_INITIALIZER("ds movsb"), {0x3e, 0xa4}, 2},
            {S8_INITIALIZER("fs movsb"), S8_INITIALIZER("fs movsb"), {0x64, 0xa4}, 2},
            {S8_INITIALIZER("gs movsb"), S8_INITIALIZER("gs movsb"), {0x65, 0xa4}, 2},
            {S8_INITIALIZER("fs rep movsw"), S8_INITIALIZER("fs rep movsw"), {0x64, 0xf3, 0x66, 0xa5}, 4},
            {S8_INITIALIZER("addr32 fs rep movsb"), S8_INITIALIZER("addr32 fs rep movsb"), {0x64, 0x67, 0xf3, 0xa4}, 4},
            {S8_INITIALIZER("fs addr32 rep movsb"), S8_INITIALIZER("fs addr32 rep movsb"), {0x64, 0x67, 0xf3, 0xa4}, 4},
            {S8_INITIALIZER("gs outsb"), S8_INITIALIZER("gs outsb"), {0x65, 0x6e}, 2},
            {S8_INITIALIZER("cs cmpsb"), S8_INITIALIZER("cs cmpsb"), {0x2e, 0xa6}, 2},
            {S8_INITIALIZER("ss lodsb"), S8_INITIALIZER("ss lodsb"), {0x36, 0xac}, 2},
            {S8_INITIALIZER("ds xlat"), S8_INITIALIZER("ds xlat"), {0x3e, 0xd7}, 2},
            {S8_INITIALIZER("fs xlatb"), S8_INITIALIZER("fs xlatb"), {0x64, 0xd7}, 2},
        };
        for (u32 segment_index = 0; segment_index < BUSTER_ARRAY_LENGTH(classic_segment_prefix_matrix); segment_index += 1)
        {
            ClassicSegmentPrefixEncodingCase segment_case = classic_segment_prefix_matrix[segment_index];
            AssemblyEncodeResult segment_intel = assembly_encode(
                arguments->arena, segment_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult segment_att = assembly_encode(
                arguments->arena, segment_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, segment_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(segment_intel.bytes, segment_case.bytes, segment_case.byte_count) &&
                                       segment_intel.relocation_count == 0 && segment_intel.symbol_count == 0 &&
                                       segment_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(segment_att.bytes, segment_case.bytes, segment_case.byte_count) &&
                                       segment_att.relocation_count == 0 && segment_att.symbol_count == 0);
        }

        String8 const invalid_segment_prefixes[] = {
            S8("fs insb"),
            S8("fs stosb"),
            S8("fs scasb"),
            S8("%fs movsb"),
            S8("fs: movsb"),
            S8("fs fs movsb"),
            S8("fs gs movsb"),
            S8("fs lock movsb"),
            S8("fs jz 0"),
        };
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_segment_prefixes); invalid_index += 1)
        {
            AssemblyEncodeResult invalid_intel = assembly_encode(
                arguments->arena, invalid_segment_prefixes[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult invalid_att = assembly_encode(
                arguments->arena, invalid_segment_prefixes[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, invalid_intel.diagnostic_count > 0 && invalid_intel.bytes.length == 0 &&
                                       invalid_intel.relocation_count == 0 && invalid_intel.symbol_count == 0 &&
                                       invalid_att.diagnostic_count > 0 && invalid_att.bytes.length == 0 &&
                                       invalid_att.relocation_count == 0 && invalid_att.symbol_count == 0);
        }
        AssemblyEncodeResult invalid_visible_segment = assembly_encode(
            arguments->arena, S8("fs mov rax, fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_visible_segment.diagnostic_count > 0 && invalid_visible_segment.bytes.length == 0 &&
                                   invalid_visible_segment.relocation_count == 0 && invalid_visible_segment.symbol_count == 0);
        AssemblyEncodeResult invalid_moffs_segment = assembly_encode(
            arguments->arena, S8("fs mov al, byte ptr fs:[0x1234]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_moffs_segment.diagnostic_count > 0 && invalid_moffs_segment.bytes.length == 0 &&
                                   invalid_moffs_segment.relocation_count == 0 && invalid_moffs_segment.symbol_count == 0);

        AssemblyEncodeResult segment_label_intel = assembly_encode(
            arguments->arena, S8("fs:\nmovsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult segment_label_att = assembly_encode(
            arguments->arena, S8("fs:\nmovsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, segment_label_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_label_intel.bytes, (u8 const[]){0xa4}, 1) &&
                                   segment_label_intel.relocation_count == 0 && segment_label_intel.symbol_count == 1 &&
                                   segment_label_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_label_att.bytes, (u8 const[]){0xa4}, 1) &&
                                   segment_label_att.relocation_count == 0 && segment_label_att.symbol_count == 1);

        AssemblyEncodeResult segment_memory_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult segment_memory_att = assembly_encode(
            arguments->arena, S8("movl %fs:(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_segment_memory[] = {0x64, 0x8b, 0x03};
        BUSTER_TEST(arguments, segment_memory_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_memory_intel.bytes, expected_segment_memory,
                                                             BUSTER_ARRAY_LENGTH(expected_segment_memory)) &&
                                   segment_memory_intel.relocation_count == 0 && segment_memory_intel.symbol_count == 0 &&
                                   segment_memory_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_memory_att.bytes, expected_segment_memory,
                                                             BUSTER_ARRAY_LENGTH(expected_segment_memory)) &&
                                   segment_memory_att.relocation_count == 0 && segment_memory_att.symbol_count == 0);

        u8 const expected_classic_repeat[] = {
            0xf3, 0xa4,
            0xf3, 0xa6,
            0xf3, 0x66, 0xa7,
            0xf2, 0xae,
            0xf2, 0xaf,
            0xf3, 0x66, 0xab,
            0xf3, 0xad,
            0xf3, 0x6d,
            0xf3, 0x6f,
        };
        AssemblyEncodeResult classic_repeat = assembly_encode(
            arguments->arena,
            S8("rep movsb\nrepe cmpsb\nrepz cmpsw\nrepne scasb\nrepnz scasd\n"
               "rep stosw\nrep lodsd\nrep insd\nrep outsd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, classic_repeat.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_repeat.bytes, expected_classic_repeat,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_repeat)) &&
                                   classic_repeat.relocation_count == 0 && classic_repeat.symbol_count == 0);

        u8 const expected_loop[] = {0xe2, 0x00, 0xe1, 0x00, 0xe0, 0x00};
        AssemblyEncodeResult loop_forward = assembly_encode(
            arguments->arena, S8("loop forward\nforward:\nloope backward\nbackward:\nloopne done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_forward.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_forward.bytes, expected_loop, BUSTER_ARRAY_LENGTH(expected_loop)) &&
                                   loop_forward.relocation_count == 0 && loop_forward.symbol_count == 3);

        u8 const expected_loop_aliases[] = {0xe1, 0x00, 0xe0, 0x00};
        AssemblyEncodeResult loop_aliases = assembly_encode(
            arguments->arena, S8("loopz target\ntarget:\nloopnz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_aliases.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_aliases.bytes, expected_loop_aliases,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_aliases)) &&
                                   loop_aliases.relocation_count == 0 && loop_aliases.symbol_count == 2);

        AssemblyEncodeResult loop_att = assembly_encode(
            arguments->arena, S8("loop forward\nforward:\nloopz backward\nbackward:\nloopnz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, loop_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_att.bytes, expected_loop, BUSTER_ARRAY_LENGTH(expected_loop)) &&
                                   loop_att.relocation_count == 0 && loop_att.symbol_count == 3);

        u8 const expected_backward_loop[] = {0x90, 0xe2, 0xfd};
        AssemblyEncodeResult loop_backward = assembly_encode(
            arguments->arena, S8("target:\nnop\nloop target\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_backward.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_backward.bytes, expected_backward_loop,
                                                             BUSTER_ARRAY_LENGTH(expected_backward_loop)) &&
                                   loop_backward.relocation_count == 0 && loop_backward.symbol_count == 1);

        u8 const expected_jcxz[] = {0x67, 0xe3, 0x00, 0xe3, 0x00};
        AssemblyEncodeResult jcxz_long_mode = assembly_encode(
            arguments->arena, S8("jecxz target\ntarget:\njrcxz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, jcxz_long_mode.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(jcxz_long_mode.bytes, expected_jcxz,
                                                             BUSTER_ARRAY_LENGTH(expected_jcxz)) &&
                                   jcxz_long_mode.relocation_count == 0 && jcxz_long_mode.symbol_count == 2);

        AssemblyEncodeResult jcxz_att = assembly_encode(
            arguments->arena, S8("jecxz target\ntarget:\njrcxz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, jcxz_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(jcxz_att.bytes, expected_jcxz, BUSTER_ARRAY_LENGTH(expected_jcxz)) &&
                                   jcxz_att.relocation_count == 0 && jcxz_att.symbol_count == 2);

        AssemblyEncodeResult explicit_addr32_jecxz = assembly_encode(
            arguments->arena, S8("addr32 jecxz external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_explicit_addr32_jecxz[] = {0x67, 0xe3, 0x00};
        BUSTER_TEST(arguments, explicit_addr32_jecxz.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(explicit_addr32_jecxz.bytes, expected_explicit_addr32_jecxz,
                                                             BUSTER_ARRAY_LENGTH(expected_explicit_addr32_jecxz)) &&
                                   explicit_addr32_jecxz.relocation_count == 1 && explicit_addr32_jecxz.symbol_count == 1 &&
                                   explicit_addr32_jecxz.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   explicit_addr32_jecxz.relocations[0].offset == 2);

        AssemblyEncodeResult explicit_addr32_loop = assembly_encode(
            arguments->arena, S8("addr32 loop external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_explicit_addr32_loop[] = {0x67, 0xe2, 0x00};
        BUSTER_TEST(arguments, explicit_addr32_loop.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(explicit_addr32_loop.bytes, expected_explicit_addr32_loop,
                                                             BUSTER_ARRAY_LENGTH(expected_explicit_addr32_loop)) &&
                                   explicit_addr32_loop.relocation_count == 1 && explicit_addr32_loop.symbol_count == 1 &&
                                   explicit_addr32_loop.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   explicit_addr32_loop.relocations[0].offset == 2);

        AssemblyEncodeResult loop_repeat_external = assembly_encode(
            arguments->arena,
            S8("repne loopne external_loopne\n"
               "rep loope external_loope\n"
               "addr32 repne loopne external_addr32_loopne\n"
               "addr32 rep loope external_addr32_loope\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_loop_repeat_external[] = {
            0xf2, 0xe0, 0x00,
            0xf3, 0xe1, 0x00,
            0x67, 0xf2, 0xe0, 0x00,
            0x67, 0xf3, 0xe1, 0x00,
        };
        BUSTER_TEST(arguments, loop_repeat_external.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_repeat_external.bytes, expected_loop_repeat_external,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_repeat_external)) &&
                                   loop_repeat_external.relocation_count == 4 && loop_repeat_external.symbol_count == 4 &&
                                   loop_repeat_external.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[2].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[3].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[0].offset == 2 &&
                                   loop_repeat_external.relocations[1].offset == 5 &&
                                   loop_repeat_external.relocations[2].offset == 9 &&
                                   loop_repeat_external.relocations[3].offset == 13);

        AssemblyEncodeResult loop_repeat_aliases = assembly_encode(
            arguments->arena, S8("repe loope loop_target\nloop_target:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_loop_repeat_aliases[] = {0xf3, 0xe1, 0x00};
        BUSTER_TEST(arguments, loop_repeat_aliases.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_repeat_aliases.bytes, expected_loop_repeat_aliases,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_repeat_aliases)) &&
                                   loop_repeat_aliases.relocation_count == 0 && loop_repeat_aliases.symbol_count == 1);

        AssemblyEncodeResult invalid_loop_controls = assembly_encode(
            arguments->arena, S8("addr16 loop external_addr16\nrep repne loopne external_duplicate\nrepne loop external_loop\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_loop_controls.diagnostic_count == 3 && invalid_loop_controls.bytes.length == 0 &&
                                   invalid_loop_controls.relocation_count == 0 && invalid_loop_controls.symbol_count == 0);

        AssemblyEncodeResult invalid_addr32_jrcxz = assembly_encode(
            arguments->arena, S8("addr32 jrcxz external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_addr32_jrcxz.diagnostic_count > 0 && invalid_addr32_jrcxz.bytes.length == 0 &&
                                   invalid_addr32_jrcxz.relocation_count == 0 && invalid_addr32_jrcxz.symbol_count == 0);

        u8 const expected_enter[] = {0xc8, 0x34, 0x12, 0x56};
        AssemblyEncodeResult enter_intel = assembly_encode(
            arguments->arena, S8("enter 0x1234, 0x56\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, enter_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(enter_intel.bytes, expected_enter, BUSTER_ARRAY_LENGTH(expected_enter)));
        AssemblyEncodeResult enter_att = assembly_encode(
            arguments->arena, S8("enter $0x1234, $0x56\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, enter_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(enter_att.bytes, expected_enter, BUSTER_ARRAY_LENGTH(expected_enter)));

        AssemblyEncodeResult xlat = assembly_encode(
            arguments->arena, S8("xlat\nxlatb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_xlat[] = {0xd7, 0xd7};
        BUSTER_TEST(arguments, xlat.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(xlat.bytes, expected_xlat, BUSTER_ARRAY_LENGTH(expected_xlat)) &&
                                   xlat.relocation_count == 0 && xlat.symbol_count == 0);

        AssemblyEncodeResult xlat_att = assembly_encode(
            arguments->arena, S8("xlat\nxlatb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, xlat_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(xlat_att.bytes, expected_xlat, BUSTER_ARRAY_LENGTH(expected_xlat)) &&
                                   xlat_att.relocation_count == 0 && xlat_att.symbol_count == 0);

        String8 const invalid_classic_sources[] = {
            S8("enter 65536, 0\n"),
            S8("enter 0, 256\n"),
            S8("movsb rax\n"),
            S8("lock movsb\n"),
            S8("lock lock add dword ptr [rax], ecx\n"),
            S8("jcxz 0\n"),
            S8("rep rep movsb\n"),
            S8("repe repne cmpsb\n"),
            S8("repne repne scasb\n"),
            S8("repne add rax, rbx\n"),
        };
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_classic_sources); invalid_index += 1)
        {
            AssemblyEncodeResult invalid_classic = assembly_encode(
                arguments->arena, invalid_classic_sources[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, invalid_classic.diagnostic_count > 0 && invalid_classic.bytes.length == 0 &&
                                       invalid_classic.relocation_count == 0 && invalid_classic.symbol_count == 0);
        }

        AssemblyEncodeResult invalid_duplicate_lock_att = assembly_encode(
            arguments->arena, S8("lock lock addl %ecx, (%rax)\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_duplicate_lock_att.diagnostic_count > 0 && invalid_duplicate_lock_att.bytes.length == 0 &&
                                   invalid_duplicate_lock_att.relocation_count == 0 && invalid_duplicate_lock_att.symbol_count == 0);

        AssemblyEncodeResult invalid_duplicate_rep_att = assembly_encode(
            arguments->arena, S8("rep rep movsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_duplicate_rep_att.diagnostic_count > 0 && invalid_duplicate_rep_att.bytes.length == 0 &&
                                   invalid_duplicate_rep_att.relocation_count == 0 && invalid_duplicate_rep_att.symbol_count == 0);

        typedef struct ClassicPc8BoundaryCase ClassicPc8BoundaryCase;
        struct ClassicPc8BoundaryCase
        {
            String8 source;
            bool succeeds;
            u8 displacement;
        };
        static ClassicPc8BoundaryCase const classic_pc8_boundary_cases[] = {
            {S8_INITIALIZER("loop 129\n"), true, 0x7f},
            {S8_INITIALIZER("loop 130\n"), false, 0},
            {S8_INITIALIZER("loop -126\n"), true, 0x80},
            {S8_INITIALIZER("loop -127\n"), false, 0},
        };
        for (u32 boundary_index = 0; boundary_index < BUSTER_ARRAY_LENGTH(classic_pc8_boundary_cases); boundary_index += 1)
        {
            ClassicPc8BoundaryCase boundary = classic_pc8_boundary_cases[boundary_index];
            AssemblyEncodeResult boundary_result = assembly_encode(
                arguments->arena, boundary.source,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            if (boundary.succeeds)
            {
                u8 expected[] = {0xe2, boundary.displacement};
                BUSTER_TEST(arguments, boundary_result.diagnostic_count == 0 &&
                                           assembly_test_bytes_equal(boundary_result.bytes, expected, 2) &&
                                           boundary_result.relocation_count == 0 && boundary_result.symbol_count == 0);
            }
            else
            {
                BUSTER_TEST(arguments, boundary_result.diagnostic_count == 1 &&
                                           boundary_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE &&
                                           boundary_result.bytes.length == 0 && boundary_result.relocation_count == 0 &&
                                           boundary_result.symbol_count == 0);
            }
        }

        AssemblyEncodeResult valid_then_invalid_classic = assembly_encode(
            arguments->arena, S8("nop\njcxz 0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_valid_then_invalid_classic[] = {0x90};
        BUSTER_TEST(arguments, valid_then_invalid_classic.diagnostic_count == 1 &&
                                   assembly_test_bytes_equal(valid_then_invalid_classic.bytes, expected_valid_then_invalid_classic, 1) &&
                                   valid_then_invalid_classic.relocation_count == 0 && valid_then_invalid_classic.symbol_count == 0);

        AssemblyEncodeResult unresolved_pc8 = assembly_encode(
            arguments->arena, S8("loop external_loop\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_unresolved_pc8[] = {0xe2, 0x00};
        BUSTER_TEST(arguments, unresolved_pc8.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(unresolved_pc8.bytes, expected_unresolved_pc8,
                                                             BUSTER_ARRAY_LENGTH(expected_unresolved_pc8)) &&
                                   unresolved_pc8.symbol_count == 1 && unresolved_pc8.relocation_count == 1 &&
                                   unresolved_pc8.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   unresolved_pc8.relocations[0].offset == 1 && unresolved_pc8.relocations[0].addend == -1 &&
                                   unresolved_pc8.relocations[0].symbol == 0);

        typedef struct ClassicUnresolvedPc8Case ClassicUnresolvedPc8Case;
        struct ClassicUnresolvedPc8Case
        {
            String8 mnemonic;
            u8 expected_prefix;
            u8 expected_opcode;
            u32 expected_offset;
        };
        static ClassicUnresolvedPc8Case const classic_unresolved_pc8_cases[] = {
            {S8_INITIALIZER("jecxz"), 0x67, 0xe3, 2},
            {S8_INITIALIZER("jrcxz"), 0x00, 0xe3, 1},
        };
        for (u32 unresolved_index = 0; unresolved_index < BUSTER_ARRAY_LENGTH(classic_unresolved_pc8_cases); unresolved_index += 1)
        {
            ClassicUnresolvedPc8Case unresolved_case = classic_unresolved_pc8_cases[unresolved_index];
            AssemblyEncodeResult unresolved_intel = assembly_encode(
                arguments->arena, string_format(arguments->arena, S8("{S8} external\n"), unresolved_case.mnemonic),
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult unresolved_att = assembly_encode(
                arguments->arena, string_format(arguments->arena, S8("{S8} external\n"), unresolved_case.mnemonic),
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            u8 expected_unresolved[3] = {0};
            if (unresolved_case.expected_prefix)
            {
                expected_unresolved[0] = unresolved_case.expected_prefix;
                expected_unresolved[1] = unresolved_case.expected_opcode;
            }
            else
            {
                expected_unresolved[0] = unresolved_case.expected_opcode;
            }
            u32 expected_byte_count = unresolved_case.expected_prefix ? 3 : 2;
            BUSTER_TEST(arguments, unresolved_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(unresolved_intel.bytes, expected_unresolved, expected_byte_count) &&
                                       unresolved_intel.symbol_count == 1 && unresolved_intel.relocation_count == 1 &&
                                       unresolved_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                       unresolved_intel.relocations[0].offset == unresolved_case.expected_offset &&
                                       unresolved_intel.relocations[0].addend == -1 && unresolved_intel.relocations[0].symbol == 0 &&
                                       unresolved_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(unresolved_att.bytes, expected_unresolved, expected_byte_count) &&
                                       unresolved_att.symbol_count == 1 && unresolved_att.relocation_count == 1 &&
                                       unresolved_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                       unresolved_att.relocations[0].offset == unresolved_case.expected_offset &&
                                       unresolved_att.relocations[0].addend == -1 && unresolved_att.relocations[0].symbol == 0);
        }
    }

    return result;
}
#endif

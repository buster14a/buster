#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/string.h>

// Named AArch64 system-register mechanisms imported from the official Arm
// A-profile SysReg XML snapshot.  The generated rows are intentionally
// pointer-free; this public view materializes String8 values only on demand.
typedef enum Aarch64SystemRegisterMechanism
{
    AARCH64_SYSTEM_REGISTER_MRS = 1,
    AARCH64_SYSTEM_REGISTER_MSR_REGISTER = 2,
    AARCH64_SYSTEM_REGISTER_MRRS = 3,
    AARCH64_SYSTEM_REGISTER_MSRR_REGISTER = 4,
} Aarch64SystemRegisterMechanism;

typedef enum Aarch64SystemRegisterMode
{
    AARCH64_SYSTEM_REGISTER_MODE_NONE = 0,
    AARCH64_SYSTEM_REGISTER_MODE_READ = 1,
    AARCH64_SYSTEM_REGISTER_MODE_WRITE = 2,
    AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE = 3,
} Aarch64SystemRegisterMode;

typedef struct Aarch64SystemRegister Aarch64SystemRegister;
struct Aarch64SystemRegister
{
    String8 name;
    String8 accessor_target;
    String8 source_file;
    String8 accessor;
    u32 feature_profile_id;
    u16 packed_encoding;
    u8 mechanism;
    u8 mode;
    u16 parameter_start;
    u16 parameter_end;
    u64 feature_digest;
    u64 source_digest;
    u64 access_digest;
    bool accessor_alias;
    bool parameterized;
    bool raw_s3;
    u8 reserved;
};
BUSTER_CT_CHECK(sizeof(Aarch64SystemRegister) == 112);

typedef struct Aarch64SystemRegisterLookup Aarch64SystemRegisterLookup;
struct Aarch64SystemRegisterLookup
{
    String8 canonical_name;
    u16 packed_encoding;
    u8 mode;
    u8 alias_count;
    u8 mechanism_count;
    u8 parameterized;
    u8 raw_s3;
    u8 reserved[3];
};
BUSTER_CT_CHECK(sizeof(Aarch64SystemRegisterLookup) == 32);

typedef struct Aarch64SystemRegisterCensus Aarch64SystemRegisterCensus;
struct Aarch64SystemRegisterCensus
{
    u32 relevant_mechanism_count;
    u32 accepted_mechanism_count;
    u32 fixed_count;
    u32 parameterized_count;
    u32 fixed_target_name_count;
    u32 fixed_encoding_count;
    u32 readable_fixed_name_count;
    u32 writable_fixed_name_count;
    u32 both_fixed_name_count;
    // Source-row counts retain the exact XML mechanism accounting.  The
    // expected research census above uses accessor-alias expansion.
    u32 source_fixed_row_count;
    u32 source_parameterized_row_count;
    u32 source_raw_s3_row_count;
};

BUSTER_F_DECL u32 aarch64_system_register_count(void);
BUSTER_F_DECL bool aarch64_system_register_at(u32 index, Aarch64SystemRegister* result);
BUSTER_F_DECL Aarch64SystemRegisterCensus aarch64_system_register_census(void);

// Name lookup is bounded and deterministic.  On failure result is untouched.
// When a spelling has both MRS and MSR rows, mode is READ_WRITE.
BUSTER_F_DECL bool aarch64_system_register_lookup_name(String8 name, Aarch64SystemRegisterLookup* result);
// Reverse lookup aggregates same-encoding aliases and picks the canonical
// spelling lexicographically, with an MRS spelling preferred when available.
BUSTER_F_DECL bool aarch64_system_register_lookup_encoding(u16 packed_encoding, Aarch64SystemRegisterLookup* result);
BUSTER_F_DECL bool aarch64_system_register_name_is_eligible(String8 name, Aarch64SystemRegisterMode mode);
BUSTER_F_DECL bool aarch64_system_register_lookup_expanded_name(String8 name, Aarch64SystemRegisterLookup* result);

// Parse and format the generic S3 spelling.  op0 is fixed at 3; op1/op2 are
// three-bit fields and CRn/CRm are four-bit fields.  Formatting requires an
// arena and writes only on successful validation.
BUSTER_F_DECL bool aarch64_system_register_parse_raw_s3(String8 text, u16* packed_encoding);
BUSTER_F_DECL bool aarch64_system_register_format_raw_s3(Arena* arena, u16 packed_encoding, String8* result);

// Expand one parameterized family (for example DBGBCR<n>_EL1) at an index in
// its XML-declared range.  The output is arena-owned and remains immutable on
// failure.
BUSTER_F_DECL bool aarch64_system_register_expand_name(Arena* arena, String8 family, u32 index, String8* result);
BUSTER_F_DECL bool aarch64_system_register_expand_name_encoding(Arena* arena, String8 family, u32 index, String8* result, u16* packed_encoding);

// Checked MRS/MSR (register) word helpers.  Rt 31 is accepted with its normal
// architectural X-register encoding; no SP/ZR runtime privilege policy is
// imposed here.
BUSTER_F_DECL bool aarch64_system_register_encode_mrs(u16 packed_encoding, u32 rt, u32* word);
BUSTER_F_DECL bool aarch64_system_register_encode_msr(u16 packed_encoding, u32 rt, u32* word);
BUSTER_F_DECL bool aarch64_system_register_decode_word(u32 word, bool* is_read, u16* packed_encoding, u32* rt);
// Pair helpers are architectural layout utilities.  The pinned Apple profile
// excludes MRRS/MSRR rows because their FEAT_D128 gate is false, but callers
// may still use these helpers for an explicitly selected D128 profile.
BUSTER_F_DECL bool aarch64_system_register_encode_mrrs(u16 packed_encoding, u32 rt, u32* word);
BUSTER_F_DECL bool aarch64_system_register_encode_msrr(u16 packed_encoding, u32 rt, u32* word);
BUSTER_F_DECL bool aarch64_system_register_decode_pair_word(u32 word, bool* is_read, u16* packed_encoding, u32* rt, u32* rt2);

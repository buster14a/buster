#pragma once

#include <buster/lib/compiler/assembly/aarch64_encoding.h>

// Durable identity for one Arm canonical row.  `form_index` is dense only
// within the checked-in Arm snapshot; `row_digest` makes the identity survive
// row reordering and fails closed when the canonical source changes.
typedef struct A64ExactFormKey A64ExactFormKey;
struct A64ExactFormKey
{
    u32 form_index;
    u64 row_digest;
};

typedef struct A64ExactCrosswalkEntry A64ExactCrosswalkEntry;
struct A64ExactCrosswalkEntry
{
    String8 llvm_name;
    u32 llvm_form_id;
    u64 llvm_source_hash;
    A64ExactFormKey canonical;
    u8 llvm_field_count;
    u8 canonical_field_count;
    u8 reserved[2];
};

BUSTER_F_DECL u32 a64_exact_crosswalk_count(void);
BUSTER_F_DECL bool a64_exact_crosswalk(u32 index, A64ExactCrosswalkEntry* result);
BUSTER_F_DECL bool a64_exact_lookup(String8 llvm_name, A64ExactFormKey* result);
BUSTER_F_DECL bool a64_exact_key(u32 index, A64ExactFormKey* result);
BUSTER_F_DECL bool a64_exact_key_valid(A64ExactFormKey key);

// Normalize LLVM production-plan fields into the Arm canonical field order.
// The output count and values are committed only on success.  Identity rows
// may append the Arm `shift` field (default zero); ADDXri/SUBSXri split LLVM's
// packed imm12|sh source field; UBFMWri is kept as its canonical imms/immr
// pair.  These operations are allocation-free and bounded to four/five fields.
BUSTER_F_DECL bool a64_exact_normalize_ubfm_wri(u32 const* llvm_fields, u32 llvm_field_count, u32* normalized_fields,
                                                u32 normalized_capacity, u32* normalized_count);
BUSTER_F_DECL bool a64_exact_normalize_subs_xri(u32 const* llvm_fields, u32 llvm_field_count, u32* normalized_fields,
                                                u32 normalized_capacity, u32* normalized_count);
BUSTER_F_DECL bool a64_exact_normalize_add_xri(u32 const* llvm_fields, u32 llvm_field_count, u32* normalized_fields,
                                               u32 normalized_capacity, u32* normalized_count);
BUSTER_F_DECL bool a64_exact_normalize(u32 crosswalk_index, u32 const* llvm_fields, u32 llvm_field_count, u32* normalized_fields,
                                        u32 normalized_capacity, u32* normalized_count);

// Emit one exact Arm row from already-normalized canonical fields.  No LLVM
// mnemonic lookup or alias selection occurs here; the key's digest and the
// generated Arm row are checked before raw architectural encoding.
BUSTER_F_DECL bool a64_exact_emit(A64ExactFormKey key, u32 const* normalized_fields, u32 field_count, u32* word);

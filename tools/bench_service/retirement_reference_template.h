/* #881/#1020 installed reference-template and inventory codec.
 * Owns the byte formats of two operator-installed, profile-pinned policy
 * files that feed the private reference producer:
 *   recipes/native-retirement-performance-v1.reference-template
 *   recipes/native-retirement-performance-v1.reference-inventory
 * Entry points: bq_retirement_reference_template_write emits exactly the
 * stream that bq_retirement_oracle_template_hash hashes, so the file SHA-256
 * is the template hash; bq_retirement_reference_template_decode parses it;
 * bq_retirement_reference_inventory_decode parses the v1 stream written by
 * bq_retirement_reference_inventory_encode against a decoded template.
 * Decoders are bounded, strictly ordered and reject trailing bytes, then
 * re-derive the canonical digest through the existing hash/encoder and require
 * it to equal the SHA-256 of the input bytes. They read no files and check no
 * pins; the service importer joins them to profile pins, A and the toolchain.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_REFERENCE_TEMPLATE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_REFERENCE_TEMPLATE_H

#include "retirement_reference_producer.h"

#define BQ_RETIREMENT_REFERENCE_TEMPLATE_CAP (64u * 1024u * 1024u)
/* Smallest encoded template row: three u32 fields, four hex digests, a u32
 * name length and a one-byte name. Callers may bound row slots from length. */
#define BQ_RETIREMENT_REFERENCE_TEMPLATE_ROW_MIN (3u * 4u + 4u * 64u + 4u + 1u)

/* Writes the canonical stream into output and checks that its SHA-256 equals
 * bq_retirement_oracle_template_hash. For installers and fixtures only; the
 * written digest is never authority until a compiled profile pins it. */
BUSTER_F_DECL bool bq_retirement_reference_template_write(
    BqRetirementOracleTemplate const* template, uint8_t* output,
    uint64_t capacity, uint64_t* length);

/* rows receives reference_count entries; template->references points at it. */
BUSTER_F_DECL bool bq_retirement_reference_template_decode(
    uint8_t const* bytes, uint64_t length,
    BqRetirementOracleTemplateRow* rows, uint32_t row_slots,
    BqRetirementOracleTemplate* template, char digest[65]);

/* rows receives template->reference_count entries. Flag, environment and
 * runtime argument strings are NUL-terminated copies in text; a text capacity
 * of at least length always suffices. plan->template points at template. */
BUSTER_F_DECL bool bq_retirement_reference_inventory_decode(
    uint8_t const* bytes, uint64_t length,
    BqRetirementOracleTemplate const* template,
    BqRetirementReferencePlanRow* rows, uint32_t row_slots,
    char* text, uint64_t text_capacity, BqRetirementReferencePlan* plan,
    BqRetirementReferenceSourceIdentity source[2],
    char toolchain_identity_sha256[65], char digest[65]);

#endif

/* Identical declarations for independently compiled C17 translation units.
 * No packing, extended bit-field types, or representation-level observations.
 */
#ifndef ORACLE_EXCHANGE_CONTRACT_H
#define ORACLE_EXCHANGE_CONTRACT_H
struct plain { unsigned char a; unsigned char b; };
struct zero { unsigned char a; unsigned int : 0; unsigned char b; };
struct aligned_zero { _Alignas(4) unsigned char a; unsigned int : 0; unsigned char b; };
struct plain_nested { unsigned char lead; struct plain inner; unsigned char tail; };
struct zero_nested { unsigned char lead; struct zero inner; unsigned char tail; };
struct aligned_nested { unsigned char lead; struct aligned_zero inner; unsigned char tail; };
struct zero_pair { struct zero inner[2]; };
extern unsigned observed_payload;
extern unsigned observed_tag;
unsigned consume_plain(struct plain_nested value, unsigned tag);
unsigned consume_single(struct zero value, unsigned tag);
unsigned consume_nested(struct zero_nested value, unsigned tag);
unsigned consume_aligned(struct aligned_nested value, unsigned tag);
unsigned consume_pair(struct zero_pair value, unsigned tag);
#endif

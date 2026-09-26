#!/usr/bin/env python3
"""Prepare an exact-source diagnostic, not a production build or acceptance gate."""
import hashlib
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: prepare.py PINNED_SOURCE_ROOT OUTPUT_DIRECTORY")
root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
path = root / "src/buster/lib/compiler/frontend/c/c_gen.c"
raw = path.read_bytes()
expected = "864e146cae683832846b00d26a44bd918fa3350b114e879175c34a54ce4e8fae"
if hashlib.sha256(raw).hexdigest() != expected:
    raise SystemExit("pinned c_gen.c identity mismatch")
source = raw.decode("utf-8")
start = source.index("BUSTER_C_INTERNAL u32 c_ir_float_suffix(String8 spelling, char8* code_out)\n{")
end = source.index("// Static x87 long doubles", start)
parser = source[start:end]
start = source.index("enum\n{\n    C_IR_FLOAT16_SIGNIFICAND_BITS = 10,")
end = source.index("BUSTER_C_INTERNAL f64 c_ir_float16_to_f64(u64 bits)\n{", start)
encoder = source[start:end]

prefix = r'''/* Exact-source extracted mechanism. Not a full Buster executable. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <fenv.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef char char8;
typedef double f64;
typedef struct String8 { char8 const *pointer; u64 length; } String8;
#define BUSTER_C_INTERNAL static
#define BUSTER_C_SHARED static
'''
main = r'''
/* All 1024 positive binary16 midpoints in [1,2).  Each is
 * (2049 + 2*k)/2048 = ((2049 + 2*k)*5^11)/10^11 exactly.
 * Appending decimal zeros changes neither its value nor the nearest-even
 * answer.  No floating arithmetic or production conversion derives expected.
 * 2049+2*k is at most 4095, so the decimal integer fits uint64_t.
 */
int main(void)
{
    if (fegetround() != FE_TONEAREST) return 2;
    puts("index,padding,input,binary64_bits,observed_half,expected_half");
    for (u32 index = 0; index < 1024; index += 1)
    {
        u64 scaled = ((u64)2049 + (u64)2 * index) * UINT64_C(48828125);
        for (u32 form = 0; form < 2; form += 1)
        {
            char spelling[96];
            int length = snprintf(spelling, sizeof(spelling), "%" PRIu64 ".%011" PRIu64,
                                  scaled / UINT64_C(100000000000), scaled % UINT64_C(100000000000));
            if (length < 0 || (size_t)length + 20 >= sizeof(spelling)) return 2;
            u32 padding = form ? 16 : 0;
            for (u32 digit = 0; digit < padding; digit += 1) spelling[length++] = '0';
            memcpy(spelling + length, "f16", 4);
            f64 value = 0.0;
            char8 suffix = 0;
            String8 token = {.pointer = spelling, .length = strlen(spelling)};
            if (!c_ir_float_parse(token, &value, &suffix) || suffix != 'h') return 2;
            u64 bits = 0;
            memcpy(&bits, &value, sizeof(bits));
            u64 observed = c_ir_float16_bits_from_f64(value);
            u64 expected_half = UINT64_C(0x3c00) + index + (index & 1u);
            printf("%u,%u,%s,%016" PRIx64 ",%04" PRIx64 ",%04" PRIx64 "\n",
                   index, padding, spelling, bits, observed, expected_half);
        }
    }
    return ferror(stdout) ? 2 : 0;
}
'''
probe = prefix + parser + encoder + main
(out / "probe.c").write_text(probe, encoding="utf-8")
(out / "extraction.txt").write_text(
    "source_commit=ade6ac4b6ecb21f30b61b656439bac476c145e2f\n"
    "source_tree=4c5306221fdb22fccc929b55e333163742de17d0\n"
    f"c_gen_sha256={expected}\n"
    f"parser_sha256={hashlib.sha256(parser.encode()).hexdigest()}\n"
    f"encoder_sha256={hashlib.sha256(encoder.encode()).hexdigest()}\n"
    f"probe_sha256={hashlib.sha256(probe.encode()).hexdigest()}\n"
    "rows=2048; exact midpoints=1024; forms=unpadded,padded-by-16-zeros\n"
    "hypothesis=legal host contraction changes binary64 accumulation and may change half rounding\n"
    "stop_rule=do not expand full-compiler matrix without a differing mechanism-level row\n"
    "scope=exact helper extraction only; no full-compiler verdict\n",
    encoding="utf-8")

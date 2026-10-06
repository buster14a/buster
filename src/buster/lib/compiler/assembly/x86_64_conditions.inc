// Ordinary Jcc/SETcc/CMOVcc condition identity and spelling projection.
// Each row owns one architectural nibble, the XED canonical suffix, and
// at most two alternate source suffixes. Consumers define BUSTER_X86_CONDITION.
// Encoding fields, operands, features and effects remain in XED metadata;
// IR comparisons and floating unordered policy remain in machine_x86_64.c.
// Consolidated from assembly.c/machine.h at 6fc08ec475694aeb3b1062c1d390483a69668508.
// Canonical spellings checked against the pinned Apache-2.0 XED snapshot;
// see docs/x86-64-condition-projection.md for provenance and exceptions.
BUSTER_X86_CONDITION(OVERFLOW,      0x0, "o",   "",    "",    "JO",   "SETO",   "CMOVO")
BUSTER_X86_CONDITION(NOT_OVERFLOW,  0x1, "no",  "",    "",    "JNO",  "SETNO",  "CMOVNO")
BUSTER_X86_CONDITION(BELOW,         0x2, "b",   "c",   "nae", "JB",   "SETB",   "CMOVB")
BUSTER_X86_CONDITION(ABOVE_EQUAL,   0x3, "nb",  "ae",  "nc",  "JNB",  "SETNB",  "CMOVNB")
BUSTER_X86_CONDITION(EQUAL,         0x4, "z",   "e",   "",    "JZ",   "SETZ",   "CMOVZ")
BUSTER_X86_CONDITION(NOT_EQUAL,     0x5, "nz",  "ne",  "",    "JNZ",  "SETNZ",  "CMOVNZ")
BUSTER_X86_CONDITION(BELOW_EQUAL,   0x6, "be",  "na",  "",    "JBE",  "SETBE",  "CMOVBE")
BUSTER_X86_CONDITION(ABOVE,         0x7, "nbe", "a",   "",    "JNBE", "SETNBE", "CMOVNBE")
BUSTER_X86_CONDITION(SIGN,          0x8, "s",   "",    "",    "JS",   "SETS",   "CMOVS")
BUSTER_X86_CONDITION(NOT_SIGN,      0x9, "ns",  "",    "",    "JNS",  "SETNS",  "CMOVNS")
BUSTER_X86_CONDITION(PARITY,        0xa, "p",   "pe",  "",    "JP",   "SETP",   "CMOVP")
BUSTER_X86_CONDITION(NOT_PARITY,    0xb, "np",  "po",  "",    "JNP",  "SETNP",  "CMOVNP")
BUSTER_X86_CONDITION(LESS,          0xc, "l",   "nge", "",    "JL",   "SETL",   "CMOVL")
BUSTER_X86_CONDITION(GREATER_EQUAL, 0xd, "nl",  "ge",  "",    "JNL",  "SETNL",  "CMOVNL")
BUSTER_X86_CONDITION(LESS_EQUAL,    0xe, "le",  "ng",  "",    "JLE",  "SETLE",  "CMOVLE")
BUSTER_X86_CONDITION(GREATER,       0xf, "nle", "g",   "",    "JNLE", "SETNLE", "CMOVNLE")

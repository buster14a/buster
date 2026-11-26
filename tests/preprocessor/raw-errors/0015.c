#pragma clang diagnostic error "-Wunicode"
#pragma clang diagnostic error "-Wdelimited-escape-sequence-extension"
#if 0
\u0041x \u0001y \uD800z \U00110000w
\N{LATIN CAPITAL LETTER A}a \N{LINE FEED}b \N{FOO}c
\N{latin capital letter a}d \N{greek small letter alpha}e
\N{GREEK SMALL LETTER ALPHA}f
\u \U \N \u{} \u{1 \N{} \N{A
#endif
int kept;

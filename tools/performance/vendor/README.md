# Publication validator dependencies

These components are separate from the compiler and retain their upstream terms.
No local implementation changes were made to the vendored C sources.

| Component | Exact source | Terms and retained records |
| --- | --- | --- |
| Apache nanoarrow 0.7.0 | `apache/arrow-nanoarrow`, `2cfba631b40886f1418a463f3b7c4552c8ae0dc7` | Apache-2.0, `nanoarrow/LICENSE.txt`, `nanoarrow/NOTICE.txt`, embedded notices |
| flatcc runtime | `thirdparty/flatcc` in that nanoarrow revision | Apache-2.0, `nanoarrow/FLATCC-LICENSE`, embedded author notices |
| yyjson 0.12.0 | `ibireme/yyjson`, `8b4a38dc994a110abaec8a400615567bd996105f` | MIT, `yyjson/LICENSE`, embedded YaoYuan copyright |

The nanoarrow files were produced with **that revision's unchanged upstream**
`ci/scripts/bundle.py --with-ipc --with-flatcc --output-dir DIST`. Retain its
`include/` and `src/` directories and remove unused `.hpp` headers. This is a
one-time source import; Python and CMake are not build/runtime dependencies.
The yyjson files are unchanged `src/yyjson.c`, `src/yyjson.h`, and `LICENSE`.
`SHA256SUMS` records the imported distribution bytes.

The native executable dynamically uses installed OpenSSL (SHA-256/base64),
PCRE2-8 (the canonical schema's regular expressions), and libcurl (opt-in HTTPS).
The tested versions belong in execution receipts; required API baselines are
OpenSSL 3, PCRE2 10, and libcurl 7.85. No source from those libraries is imported.
The ordinary Arrow IPC stream implementation is upstream nanoarrow; Buster
adds no binary codec or replacement physical type schema.

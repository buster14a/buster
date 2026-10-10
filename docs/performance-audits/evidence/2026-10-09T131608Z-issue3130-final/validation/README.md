# #3130 canonical-source validation appendix

Captured at 2026-10-09T13:22:26Z. This small appendix supplements the published core evidence; see [`../PUBLICATION.md`](../PUBLICATION.md). It does not contain either original receipt ZIP or alter the core archive/manifest.

Both self-host attempts use the canonical Git commit `42fc9eb51891da028883058d7618a42abe540d9e`, tree `4310f86cf35b41acdca079f52ad8133505fd4800`, and command `./build.sh test_self_host --config Release`. Attempt 1 reached the 39,703,952-byte self-host fixed point and completed stage 1 and stage 2, then exited 1 because the sparse test checkout lacked `tests/basic_c_operations.c` for the C frontend benchmark. The required tracked tests tree was materialized without tracked source changes before attempt 2. The second run completed the frontend and machine benchmarks and exited 0 in 82 seconds, with the same fixed point. Both raw logs are preserved as separate captures.

The canonical source-size capture also runs on literal commit `42fc9eb` against `dfedd934e50a5465975c28a2870b0d6382632a60`: production growth +4,992 bytes and build growth 0, both within 32,768-byte limits; baseline `66c8a99c005e083853fe0e9f97531bef80280cab`. The focused analyzer native self-test reports 93 checks, 36 expected rejections, and zero failures on local commit `e32c9c40cb7e0bf1ce061183724946d1434bd1c9`, whose tree is byte-identical to the canonical tree. It is labeled tree-equivalent, not as a literal run at `42fc9eb`.

The included provenance records the canonical source object and tree mapping, pinned TinyCC 0.9.28rc, fresh Clang 21.1.8 recovery after a local shared-library truncation, and CMake 3.28.3 / Ninja 1.11.1 / mold 2.30.0 package and startup verification. Package and tool binaries are not included. Root reports the independent source review approved tree `4310f86…`.

This appendix records local build/self-host validation. It contains no new physical performance run and does not establish an A/B result. The final hosted CI state remained separately pending when the core archive was frozen.

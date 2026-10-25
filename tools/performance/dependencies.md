# External performance-tool dependencies

Upstream implementations are not tracked in this repository. Run
`bash tools/performance/prepare-deps.sh` to download and verify exact source
archives in ignored `build/performance/deps/`. `BUSTER_PERFORMANCE_DEPS` may
select an existing external cache. Cached archives are verified on each run;
network access is needed only for missing archives.

| Dependency | Exact input | Archive SHA-256 |
| --- | --- | --- |
| nanoarrow 0.7.0 and its flatcc runtime | `apache/arrow-nanoarrow@2cfba631b40886f1418a463f3b7c4552c8ae0dc7` | `2e27a776772fba80018ea4adaefef0a3e3fb90697685f1c5c4f5c42fcce34567` |
| yyjson 0.12.0 | `ibireme/yyjson@8b4a38dc994a110abaec8a400615567bd996105f` | `94e9c90f8f12d8329f2c0756782d21e5c278729023cd2c5fe47e323a35947ed6` |
| Arrow JavaScript 21.1.0 | npm `apache-arrow-21.1.0.tgz` | `fa3fd5781698838eafc0f74f5ae35ab0bf13bb8cb227b3f57d40bf3b34055d72` |

The scripts compile the upstream C translation units directly. A generated
configuration header contains the pinned nanoarrow version; no amalgamation,
Python generator or additional build system is needed.

The Arrow browser file and its license/NOTICE are copied from this cache only
when assembling a disposable test or publishing artifact. Its file SHA-256 is
`d3f0ded2a2bdd1208232b942f8e4810f7a402564fac3c78b4574158cd542acb9`.
Consumers use the locally published asset and need no runtime package registry.

nanoarrow/flatcc and Arrow JS retain Apache-2.0 without the LLVM exception;
yyjson retains YaoYuan's MIT terms. Distributing a built artifact requires the
upstream license, NOTICE and embedded attribution from the exact downloaded
input. OpenSSL 3, PCRE2-8 10 and libcurl 7.85 or newer remain external installed
libraries. Actual execution versions belong in the measurement receipts.

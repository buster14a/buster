/* Fixed first-slice native execution contract (#2648), shared by the registry,
 * materializer, broker and entry gate. No caller command, path or environment. */
#ifndef BUSTER_BENCH_NATIVE_PROFILE_H
#define BUSTER_BENCH_NATIVE_PROFILE_H
#define BQ_NATIVE_RECIPE "native-execute-v1"
#define BQ_NATIVE_STAGE 29u
#define BQ_NATIVE_STAGE_NAME "native-execute"
#define BQ_NATIVE_OUTER_VERB "start-native-outer"
#define BQ_NATIVE_PROGRAM_CAP (4u * 1024u * 1024u)
#define BQ_NATIVE_STORE_CAP 128u
#define BQ_NATIVE_MANIFEST_CAP 256u
#define BQ_NATIVE_LOG_CAP (1024u * 1024u)
#define BQ_NATIVE_PROFILE "schema=1\nrecipe=native-execute-v1\nsource-manifest=BQ-NATIVE-V1\noperation=execute-once\nplatform=linux-x86-64-static-elf\narguments=none\ncompilation=unavailable\nbenchmark-metrics=unavailable\n"
#endif

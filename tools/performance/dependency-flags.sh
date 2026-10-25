#!/usr/bin/env bash
# Source after prepare-deps.sh. These are original build-path lists, not copied
# upstream source or a second format implementation.
dependency_root=${BUSTER_PERFORMANCE_DEPS:-"$PWD/build/performance/deps"}
performance_dependency_includes=(
    -I"$dependency_root/include"
    -I"$dependency_root/nanoarrow/src"
    -I"$dependency_root/nanoarrow/thirdparty/flatcc/include"
    -I"$dependency_root/yyjson/src"
)
performance_dependency_sources=(
    "$dependency_root/yyjson/src/yyjson.c"
    "$dependency_root/nanoarrow/src/nanoarrow/common/array.c"
    "$dependency_root/nanoarrow/src/nanoarrow/common/array_stream.c"
    "$dependency_root/nanoarrow/src/nanoarrow/common/schema.c"
    "$dependency_root/nanoarrow/src/nanoarrow/common/utils.c"
    "$dependency_root/nanoarrow/src/nanoarrow/ipc/codecs.c"
    "$dependency_root/nanoarrow/src/nanoarrow/ipc/decoder.c"
    "$dependency_root/nanoarrow/src/nanoarrow/ipc/encoder.c"
    "$dependency_root/nanoarrow/src/nanoarrow/ipc/reader.c"
    "$dependency_root/nanoarrow/src/nanoarrow/ipc/writer.c"
    "$dependency_root/nanoarrow/thirdparty/flatcc/src/runtime/builder.c"
    "$dependency_root/nanoarrow/thirdparty/flatcc/src/runtime/emitter.c"
    "$dependency_root/nanoarrow/thirdparty/flatcc/src/runtime/refmap.c"
    "$dependency_root/nanoarrow/thirdparty/flatcc/src/runtime/verifier.c"
)

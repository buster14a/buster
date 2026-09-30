#!/bin/bash
# Callgrind Ir census: baseline vs prototype observation binaries (x86-64-v3 builds of the
# same Release compile command). Observation only; not timing evidence.
cd /home/user/buster
BASE=/tmp/claude-0/obsbuild/ide-obs
PROTO=/tmp/claude-0/protobuild/ide-proto-obs
OUT=/tmp/claude-0/ledger/fam
NU="-Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=0 -DBUSTER_INCLUDE_TESTS=0"
run() { # name tag binary args...
  local name=$1 tag=$2 bin=$3; shift 3
  valgrind --tool=callgrind --dump-instr=no --collect-jumps=no --callgrind-out-file=$OUT/$name.$tag.cg "$bin" "$@" > $OUT/$name.$tag.log 2>&1
  echo "$name $tag rc=$?" >> $OUT/status.txt
}
job() { local name=$1; shift; run $name base $BASE "$@" -o $OUT/$name.base.out; run $name proto $PROTO "$@" -o $OUT/$name.proto.out; }
printf '#include <stdio.h>\nint main(void){ printf("hi\\n"); return 0; }\n' > $OUT/hello.c
job F1_hello_c cc -c $OUT/hello.c
job F1_hello_link cc $OUT/hello.c
job F2_ops cc -c tests/basic_c_operations.c
job F3_string cc $NU -c src/buster/lib/string.c
cp /tmp/claude-0/scoutA/tiny.c $OUT/tiny.c 2>/dev/null || printf 'int add(int a,int b){return a+b;}\n' > $OUT/tiny.c
job F1_tiny_aarch64 cc --target=aarch64-linux-gnu -c $OUT/tiny.c
for f in $(cat /tmp/claude-0/ledger/heldout_files.txt); do n=H1_$(basename $f .c); job $n cc $NU -c $f; done
echo DONE >> $OUT/status.txt

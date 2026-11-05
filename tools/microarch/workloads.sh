# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The campaign's workloads, sourced by run.sh from the run root
# (docs/commands/microarch.md). Every path is relative to the run root, so that the
# objects' FILE symbols and the compiler's diagnostics name the same paths on every
# host:
#   corpus -> the frozen corpus (preprocessor-391e729af658, tools/ci/fetch-corpus.sh)
#   bin/compiler-VARIANT -> the measured executables
#   self-main.i -> the compiler's own main.c, preprocessed once by the measured
#                  source's compiler (copy the same file to every host)
#   predefines-znver5.h -> clang -dM -E -march=znver5 -fwrapv -funsigned-char
# SCRATCH is where outputs go (a per-host directory of the run root).

corpus=corpus
COMMON=(--predefines "$corpus/provenance/gnu17-pinned-predefines.txt" --clang-features --target=x86_64-linux-gnu -march=znver5 -std=gnu17
    -nostdinc -isystem "$corpus/headers/usr/lib/clang/23/include" -isystem "$corpus/headers/usr/local/include"
    -isystem "$corpus/headers/usr/include")
SQLITE_DEFINES=(-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE)
SQLITE=("${SQLITE_DEFINES[@]}" "-I$corpus/sqlite" "$corpus/sqlite/sqlite3.c")
musl="$corpus/libc/musl"
MUSL_OPTIONS=(-std=c99 -nostdinc -ffreestanding -D_XOPEN_SOURCE=700 -I "$musl/arch/x86_64" -I "$musl/arch/generic"
    -I "$corpus/musl-generated/obj/src/internal" -I "$musl/src/include" -I "$musl/src/internal"
    -I "$corpus/musl-generated/obj/include" -I "$musl/include")
SELF_OPTIONS=(--predefines predefines-znver5.h -march=znver5 -fwrapv -funsigned-char -fno-strict-aliasing)

# The bytes each workload reads, for throughput: sqlite3.c's original source
# (the denominator the earlier records use) and the .i's size.
SQLITE_BYTES=9507037

# workload VARIANT NAME: sets the array WL to the command line.
#   sq-c      sqlite3.c to an object on one lane (-c --lanes 1)
#   sq-syn    sqlite3.c through the checker (-fsyntax-only)
#   sq-E      sqlite3.c preprocessed (-E)
#   self-syn  the compiler's main.c (.i) through the checker
#   musl-syn  musl's 1,355 sources as one multi-file command on N lanes (VARIANT:N)
#   tcc-c     tcc's -c of sqlite3.c with the frozen headers (the reference compiler)
workload()
{
    local variant=$1 name=$2 lanes=1
    if [[ $variant == *:* ]]; then lanes=${variant#*:}; variant=${variant%%:*}; fi
    local bin=bin/compiler-$variant
    case $name in
        sq-c) WL=("$bin" "${COMMON[@]}" "${SQLITE[@]}" -c -o "$SCRATCH/$variant-sq.o" --lanes "$lanes") ;;
        sq-c-default) WL=("$bin" "${COMMON[@]}" "${SQLITE[@]}" -c -o "$SCRATCH/$variant-sq-default.o") ;;
        sq-syn) WL=("$bin" "${COMMON[@]}" "${SQLITE[@]}" -fsyntax-only) ;;
        sq-E) WL=("$bin" "${COMMON[@]}" "${SQLITE[@]}" -E -o "$SCRATCH/$variant-sq.i") ;;
        self-syn) WL=("$bin" "${SELF_OPTIONS[@]}" -fsyntax-only self-main.i) ;;
        musl-syn) WL=("$bin" "${MUSL_OPTIONS[@]}" --lanes "$lanes" -fsyntax-only "$musl"/src/*/*.c "$musl"/src/*/x86_64/*.c) ;;
        tcc-c) WL=(tcc -c -o "$SCRATCH/tcc-sq.o" -nostdinc -I/usr/lib/tcc/include "-I$corpus/headers/usr/include" "${SQLITE[@]}") ;;
        *) echo "unknown workload $name" >&2; return 1 ;;
    esac
}

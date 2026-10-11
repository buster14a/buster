#!/usr/bin/env bash
# #1454: link Buster Mach-O objects that reference dylib data with ld64.lld, a
# linker sharing no code with Buster, and require the same meaning Clang's
# object gets: per symbol, the same relocation types, and in the linked
# image the same dyld bindings (data reached through a bound __got slot, not
# through a call stub or a null page).
set -euo pipefail

ide=${1:-build/Release/ide}
output=${2:-build/mach-o-dylib-data-lld}
mkdir -p "$output"

for tool in clang ld64.lld llvm-objdump; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done
clang --version | tee "$output/clang-version.txt"
ld64.lld --version | tee "$output/lld-version.txt"

# The (symbol, relocation type) pairs of an object's __text section.
text_relocations()
{
    llvm-objdump --macho -r "$1" |
        awk '/^Relocation information/ { text = ($0 ~ /\(__TEXT,__text\)/); next }
             text && $1 ~ /^[0-9a-f]+$/ { print $NF, $5 }' |
        sort -u
}

# The (segment, section, symbol) triples of every dyld binding in an image.
bindings()
{
    {
        llvm-objdump --macho --bind "$1" | awk 'NF >= 6 && $1 ~ /^__/ { print $1, $2, $NF }'
        llvm-objdump --macho --lazy-bind "$1" | awk 'NF >= 5 && $1 ~ /^__/ { print $1, $2, $NF }'
    } | sort -u
}

status=0
while IFS='|' read -r name buster_target clang_target lld_arch; do
    directory="$output/$name"
    mkdir -p "$directory"
    link=(ld64.lld -arch "$lld_arch" -platform_version macos 13.0.0 13.0.0)
    clang --target="$clang_target" -g0 -c tests/basic_c_mach_dylib_data_library.c -o "$directory/library.o"
    clang --target="$clang_target" -g0 -c tests/basic_c_mach_dylib_data_system.c -o "$directory/system.o"
    "${link[@]}" -dylib -install_name @rpath/libmachdata.dylib -o "$directory/libmachdata.dylib" "$directory/library.o"
    "${link[@]}" -dylib -install_name /usr/lib/libSystem.B.dylib -o "$directory/libSystem.dylib" "$directory/system.o"
    for optimization in O0 O2; do
        "$ide" cc -target "$buster_target" -g0 "-$optimization" -c tests/basic_c_mach_dylib_data_subject.c \
            -o "$directory/buster-$optimization.o"
        clang --target="$clang_target" -g0 "-$optimization" -c tests/basic_c_mach_dylib_data_subject.c \
            -o "$directory/clang-$optimization.o"
        for compiler in buster clang; do
            stem="$directory/$compiler-$optimization"
            text_relocations "$stem.o" > "$stem.relocations.txt"
            "${link[@]}" -e _main -o "$stem.linked" "$stem.o" "$directory/libmachdata.dylib" "$directory/libSystem.dylib"
            bindings "$stem.linked" > "$stem.bindings.txt"
        done
        # Clang's -O2 may fold or inline what -O0 keeps; only the data
        # reference's own meaning is compared there.
        if [[ $optimization == O0 ]]; then
            if ! diff -u "$directory/clang-$optimization.relocations.txt" "$directory/buster-$optimization.relocations.txt"; then
                printf '%s %s: relocation types differ from Clang\n' "$name" "$optimization" >&2
                status=1
            fi
        fi
        if ! grep -q '^__DATA[_A-Z]* __got _mach_dylib_data$' "$directory/buster-$optimization.bindings.txt"; then
            printf '%s %s: _mach_dylib_data is not bound through __got\n' "$name" "$optimization" >&2
            cat "$directory/buster-$optimization.bindings.txt" >&2
            status=1
        fi
        if ! diff -u "$directory/clang-$optimization.bindings.txt" "$directory/buster-$optimization.bindings.txt"; then
            printf '%s %s: dyld bindings differ from Clang\n' "$name" "$optimization" >&2
            status=1
        fi
    done
done <<'TARGETS'
x86_64|x86_64-apple-macos|x86_64-apple-macos13|x86_64
aarch64|aarch64-apple-macos|arm64-apple-macos13|arm64
TARGETS
exit "$status"

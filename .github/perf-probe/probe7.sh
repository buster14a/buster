#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its seventh round: real projects' files against tcc, for the files whose
# ratio stands out. Everything goes to standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep 'Model name'
cd "$work/real"
curl -sSL --retry 3 -o zlib.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz && tar -xzf zlib.tar.gz
for name in stb_image stb_truetype stb_image_write stb_image_resize2 stb_vorbis; do
    extension=h
    [ "$name" = stb_vorbis ] && extension=c
    curl -sSL --retry 3 -o "$name.$extension" "https://raw.githubusercontent.com/nothings/stb/master/$name.$extension"
    upper="$(tr '[:lower:]' '[:upper:]' <<< "${name#stb_}")"
    if [ "$extension" = h ]; then
        printf '#define STB_%s_IMPLEMENTATION\n#include "%s.h"\n' "$upper" "$name" > "unit_$name.c"
    else
        cp "$name.c" "unit_$name.c"
    fi
done
curl -sSL --retry 3 -o cJSON.c https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
curl -sSL --retry 3 -o cJSON.h https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
curl -sSL --retry 3 -o sqlite.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' sqlite.zip .

files=(zlib-1.3.1/*.c unit_stb_*.c cJSON.c sqlite-amalgamation-3530200/shell.c)
for file in "${files[@]}"; do
    directory="$(dirname "$file")"
    section "real $file"
    if ! "$tcc" -c "-I$directory" "$file" -o tcc.o > tcc.log 2>&1; then
        echo "tcc failed: $(head -c 200 tcc.log | tr '\n' ' ')"
        continue
    fi
    if ! "$compiler" -c "-I$directory" "$file" -o buster.o > buster.log 2>&1; then
        echo "buster failed: $(grep -m1 'error' buster.log | head -c 200)"
        continue
    fi
    "$compiler" measure-compile --runs 5 --warmups 1 --reference "$tcc" -- -c "-I$directory" "$file" | grep -E 'against reference|^  [a-z]' | sort -t' ' -k3 -n -r | head -n 7
done
section "done"

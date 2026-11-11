#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# A hosted correctness row's compiler, built once for the row's shards (#2032; docs/ci.md, "Shared row products").
# Run from the checkout's root, after tools/ci/setup-hosted.sh, with the row's BUSTER_* settings in the environment:
#
#   hosted-product.sh build                  Builds the external Clang pragma oracle, the clang server (status 77: none
#                                            for this toolchain, which fails on the hosted one) and the compiler, as each
#                                            shard did before, and exports their paths (GITHUB_ENV). A native row's
#                                            shards still run this themselves.
#   hosted-product.sh ownership              Runs the inventory's unity fixture ownership check on the build's tree,
#                                            the one check that reads the tree rather than the executable.
#   hosted-product.sh pack DIRECTORY         Packs what `build` built, with its identity, as DIRECTORY/product.tar.zst,
#                                            and writes its SHA-256 and size to GITHUB_OUTPUT (digest, bytes).
#   hosted-product.sh install DIRECTORY DIGEST
#                                            A shard's side: DIRECTORY holds the downloaded artifact, product.tar.zst
#                                            alone. Checks its SHA-256 against DIGEST (the producer's job output, or for
#                                            a vector configuration the one its artifact's name holds), every file's
#                                            against the product's list, and the product's identity against this
#                                            checkout's and runner's, runs each executable once, and only then installs
#                                            them where `build` puts them and exports the same paths.
#
# Any difference fails the shard: nothing is rebuilt and nothing is fetched again, so a bad product stops the row
# rather than starting four builds. Only an explicit target is shared: BUSTER_HOST_ARCH=x86-64, the baseline that every
# x86-64 runner executes, and BUSTER_HOST_ARCH=vector (#2033), host_isa.h's predicate, which only a runner whose probe
# qualifies executes: there, both sides run the vector row's probe (BUSTER_VECTOR_PROBE, built with the target's flags
# by hosted-correctness.yml) again, which must pass, and its answers are part of the identity. A native build is its
# runner's CPU's and is never packed or installed.
set -euo pipefail
shopt -s inherit_errexit

format='buster-hosted-product 1'
ownership_heading='== Check unity fixture ownership without rebuilding'

# On standard error, which the runner reads workflow commands from too, so that a failure inside a command
# substitution still shows.
die()
{
    printf '::error title=Shared compiler product::%s\n' "$*" >&2
    exit 1
}

summary()
{
    printf 'hosted-product: %s\n' "$*"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf -- '- %s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
    fi
}

# Whether this runner runs the row's explicit target: always for x86-64; for vector, when the row's probe, run again
# here, finds every feature the target uses (status 0 and every answer `yes`), which it prints.
portable()
{
    local answers status=0
    case "${BUSTER_HOST_ARCH:-native}" in
        x86-64) ;;
        vector)
            [ -n "${BUSTER_VECTOR_PROBE:-}" ] && [ -x "$BUSTER_VECTOR_PROBE" ] ||
                die "a vector product needs this runner's probe (BUSTER_VECTOR_PROBE), which the vector row's draw builds: '${BUSTER_VECTOR_PROBE:-}'"
            answers=$("$BUSTER_VECTOR_PROBE" 2>&1) || status=$?
            if [ "$status" -ne 0 ] || [ -z "$answers" ] || grep -qv $'\tyes$' <<< "$answers"; then
                printf '%s\n' "$answers" >&2
                die "this runner's CPU doesn't run the vector target (its probe: status $status), so it can't run or pack a vector product"
            fi
            ;;
        *) die "only the explicit x86-64 and vector targets are shared between runners; BUSTER_HOST_ARCH=${BUSTER_HOST_ARCH:-native} is built on each shard's own runner (#2032)" ;;
    esac
}

# The SHA-256 of a tree's files and symbolic links' targets, in a stable order: `file PATH` and `link PATH TARGET` lines
# and every file's hash, hashed in turn. Other kinds of entry fail.
tree_digest()
{
    (
        cd "$1"
        if [ -n "$(find . ! -type f ! -type l ! -type d -print -quit)" ]; then
            echo "hosted-product: $1 has entries that are neither files, links nor directories" >&2
            exit 1
        fi
        find . -type l -printf 'link\t%p\t%l\n' | LC_ALL=C sort
        find . -type f -print0 | xargs -0 -r -n 64 -P "$(nproc)" sha256sum | LC_ALL=C sort -k 2
    ) | sha256sum | cut -c 1-64
}

# Runs "$@", and reports its time on standard error.
timed()
{
    local start end
    start=$(date +%s%N)
    "$@"
    end=$(date +%s%N)
    printf 'hosted-product: %s %s took %s ms\n' "$1" "${2##*/}" "$(((end - start) / 1000000))" >&2
}

# The hosted toolchain's installation (tools/ci/install-hosted-tools.sh): the directory above the clang on PATH that
# holds the copy of hosted-tools.lock it was installed from. hosted_root prints it, or nothing for another toolchain;
# toolchain_root requires it.
hosted_root()
{
    local root
    root=$(cd "$(dirname "$(realpath "$(command -v clang)")")/../.." && pwd)
    if [ -f "$root/hosted-tools.lock" ]; then
        printf '%s\n' "$root"
    fi
}

toolchain_root()
{
    local root
    root=$(hosted_root)
    [ -n "$root" ] || die "the clang on PATH ($(realpath "$(command -v clang)")) is not the hosted toolchain's installation"
    printf '%s\n' "$root"
}

# The identity a product must share with the shard that runs it, which both sides compute with this function and
# compare byte for byte: what it was built from (the repository, run, commit and its tree, unmodified); how (build.sh's
# own record of the toolchain, tools and settings it configured with, and its descriptions of the flags and sources);
# for which target (its flags, build.sh's print-host-flags, and the host ISA's predefined macros with them,
# BUSTER_HOST_VECTOR's among them, which must be 1 for vector and 0 for x86-64; for vector, the probe's answers on this
# runner, every feature `yes`); with which toolchain (every file of the hosted installation: clang, its resource
# directory and sanitizer runtimes, the linkers, libclang-cpp, CMake and Ninja) and baseline runtime
# (BUSTER_HOST_RUNTIME's tree); and what the product's executables load on this runner (ldd's resolution of each).
identity()
{
    local product=$1 out=$2 stamp file revision tree root toolchain runtime=none vector
    local -a host_flags
    mkdir -p "$out"
    git diff --quiet HEAD -- || die "the checkout's tracked files differ from its commit"
    # Each value is assigned first, so that a failure stops the script rather than leaving a field empty on both sides.
    revision=$(git rev-parse HEAD)
    tree=$(git rev-parse 'HEAD^{tree}')
    mapfile -d '' -t host_flags < <(./build.sh print-host-flags)
    [ "${#host_flags[@]}" -gt 0 ] || die "build.sh gave no host target's flags"
    root=$(toolchain_root)
    toolchain=$(timed tree_digest "$root")
    if [ -n "${BUSTER_HOST_RUNTIME:-}" ]; then
        runtime=$(tree_digest "$BUSTER_HOST_RUNTIME")
    fi
    {
        printf 'format\t%s\n' "$format"
        printf 'repository\t%s\n' "${GITHUB_REPOSITORY_ID:?}"
        printf 'run\t%s\n' "${GITHUB_RUN_ID:?}"
        printf 'revision\t%s\n' "$revision"
        printf 'tree\t%s\n' "$tree"
        printf 'row\t%s %s O%s\n' "$BUSTER_HOST_ARCH" "${BUSTER_SANITIZER:-none}" "${BUSTER_SANITIZER_OPTIMIZATION:-3}"
        printf 'host-flags\t%s\n' "${host_flags[*]}"
        printf 'toolchain\t%s\n' "$toolchain"
        printf 'runtime\t%s\n' "$runtime"
    } > "$out/summary"
    # build.sh's configuration record for these settings: it writes it when it configures the tree, the same text
    # whether the tree was built or only configured.
    ./build.sh configure 2> "$out/.configure.log" || { cat "$out/.configure.log" >&2; die "build.sh couldn't configure this row's tree"; }
    rm -f "$out/.configure.log"
    stamp="$(dirname "$(dirname "$(./build.sh print-output --config Release)")")/build.sh.configuration"
    cp -- "$stamp" "$out/configuration"
    ./build.sh print-compile-flags --config Release | tr '\0' '\n' > "$out/compile-flags"
    ./build.sh print-sanitizer-flags | tr '\0' '\n' > "$out/sanitizer-flags"
    ./build.sh print-sources | tr '\0' '\n' > "$out/sources"
    clang "${host_flags[@]}" -Isrc -dM -E -include src/buster/compiler/host_isa.h -x c /dev/null | LC_ALL=C sort > "$out/target"
    vector=0
    [ "$BUSTER_HOST_ARCH" != vector ] || vector=1
    grep -qx "#define BUSTER_HOST_VECTOR $vector" "$out/target" || die "the $BUSTER_HOST_ARCH target (${host_flags[*]}) doesn't select BUSTER_HOST_VECTOR $vector"
    if [ "$vector" -eq 1 ]; then
        "$BUSTER_VECTOR_PROBE" > "$out/probe"
    fi
    : > "$out/libraries"
    for file in "$product"/bin/*; do
        printf '%s:\n' "${file##*/}" >> "$out/libraries"
        # ldd exits 1 for a static executable, which it says.
        { ldd "$file" 2>&1 || true; } | sed -E 's/ \(0x[0-9a-f]+\)$//' >> "$out/libraries"
    done
    if grep -q 'not found' "$out/libraries"; then
        cat "$out/libraries" >&2
        die "a product's executable needs a library this runner doesn't have"
    fi
}

# Where the product was built: reported, not compared. The runner (`host`) is a shard's own anyway; its image, glibc
# and the Ubuntu archive's packages (`environment`, `packages`) can differ between a row's runners, as they could
# between its shards when each built its own compiler, and a shard reports where they do.
provenance()
{
    local out=$1
    mkdir -p "$out"
    {
        printf 'runner\t%s\n' "${RUNNER_NAME:-unknown}"
        printf 'cpu\t%s\n' "$(sed -n 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -n 1)"
        printf 'cpus\t%s\n' "$(nproc)"
        printf 'kernel\t%s\n' "$(uname -r)"
    } > "$out/host"
    {
        printf 'image\t%s %s\n' "${ImageOS:-unknown}" "${ImageVersion:-unknown}"
        printf 'libc\t%s\n' "$(getconf GNU_LIBC_VERSION)"
    } > "$out/environment"
    { dpkg-query -W -f '${Package} ${Version}\n' libc6 libc6-dev linux-libc-dev binutils gcc g++ libstdc++6 \
        libzstd-dev libxml2-dev zlib1g-dev 2> /dev/null || true; } | LC_ALL=C sort > "$out/packages"
}

build_products()
{
    : "${RUNNER_TEMP:?}" "${GITHUB_ENV:?}"
    local oracle="$RUNNER_TEMP/preprocessor-pragma-oracle" server="$RUNNER_TEMP/clang-server" served=0 compiler hosted
    printf 'Building on: %s, %s CPUs\n' "$(sed -n 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -n 1)" "$(nproc)"
    bash tools/build-preprocessor-pragma-oracle.sh "$oracle"
    # The clang server that the semantic driver's checks send the oracle clang's runs to (#2071), linked with the
    # release's static clang and LLVM archives, one copy of LLVM; its build checks one request against a spawned clang.
    # Status 77 is a toolchain without what one copy needs: no server, and the oracle clang runs as children. The
    # hosted installation has what it needs (install-hosted-tools.sh extracts the archives), so there 77 is a regression,
    # a change to the extracted files or the release's layout, which would leave the row green but much slower: it fails.
    hosted=$(hosted_root)
    bash tools/clang-server/build.sh "$server" || served=$?
    if [ "$served" -eq 77 ] && [ -n "$hosted" ]; then
        die "the clang server didn't build with the hosted toolchain ($hosted): status 77, no server, is a regression there (#2071), not a toolchain without one"
    elif [ "$served" -eq 77 ]; then
        echo "::notice::No clang server for this toolchain: the semantic driver's checks run the oracle clang as children."
    elif [ "$served" -ne 0 ]; then
        exit "$served"
    fi
    # A compile per CPU: build.sh's default, one per physical core, is two on these runners' two cores of two threads,
    # which left the address row's build waiting on CPU (#2030).
    ./build.sh --config Release --jobs "$(nproc)"
    # The steps after this run the Release configuration's own executable (BUSTER_COMPILER); build/compiler, the
    # compatibility path, is that same file, and the only executable build.sh leaves in build/ itself.
    compiler=$(./build.sh print-output --config Release)
    test -x "$compiler"
    test build/compiler -ef "$compiler"
    test "$(find build -maxdepth 1 -type f -executable | wc -l)" -eq 1
    {
        printf 'BUSTER_COMPILER=%s\n' "$compiler"
        printf 'BUSTER_PPF_PRAGMA_ORACLE=%s\n' "$oracle"
        if [ "$served" -eq 0 ]; then
            printf 'BUSTER_CLANG_SERVER=%s\n' "$server"
        fi
    } >> "$GITHUB_ENV"
    clang --version
    llvm-config --version
}

# The inventory's own heading, run by the built executable's runner on the tree it was built in. A shard of a shared
# product has no such tree, so its inventory skips the heading on this record (tools/ci/correctness.tests).
ownership()
{
    : "${RUNNER_TEMP:?}" "${BUSTER_COMPILER:?}"
    local list="$RUNNER_TEMP/product-ownership.tests" report="$RUNNER_TEMP/product-ownership.report"
    awk -v heading="$ownership_heading" '/^== / { selected = $0 == heading } selected' tools/ci/correctness.tests > "$list"
    [ "$(grep -c '^== ' "$list")" -eq 1 ] || die "the inventory has no heading '$ownership_heading'"
    env -u BUSTER_PRODUCT "$BUSTER_COMPILER" parallel --github --timeout 600 < "$list" 2>&1 | tee "$report"
    # Passed, not skipped: the record's first line is what the shards' inventory reads.
    grep -q ': 1 passed, 0 failed, 0 skipped, 0 not started' "$report" || die "the ownership check didn't pass on the build's tree"
    {
        printf 'passed\n'
        printf '%s\n' "${ownership_heading#== }"
        grep -m 1 ': 1 passed, 0 failed, 0 skipped, 0 not started' "$report"
    } > "$RUNNER_TEMP/product-ownership"
}

pack()
{
    local output=$1 staging product start=$SECONDS digest bytes unpacked
    : "${RUNNER_TEMP:?}" "${GITHUB_OUTPUT:?}"
    portable
    test -f "$RUNNER_TEMP/product-ownership" || die "the ownership check hasn't passed on this build's tree"
    mkdir -p -- "$output"
    output=$(cd -- "$output" && pwd)
    test ! -e "$output/product.tar.zst" || die "$output already holds a product"
    staging=$(mktemp -d "$output/staging.XXXXXXXX")
    product="$staging/product"
    mkdir -p "$product/bin"
    cp -- "${BUSTER_COMPILER:?}" "$product/bin/compiler"
    cp -- "${BUSTER_PPF_PRAGMA_ORACLE:?}" "$product/bin/preprocessor-pragma-oracle"
    [ -n "${BUSTER_CLANG_SERVER:-}" ] || die "no clang server to pack: the hosted toolchain's build gives one (#2071)"
    cp -- "$BUSTER_CLANG_SERVER" "$product/bin/clang-server"
    chmod 0755 "$product"/bin/*
    cp -- build/compile_commands.json "$product/compile_commands.json"
    identity "$product" "$product/identity"
    provenance "$product/provenance"
    cp -- "$RUNNER_TEMP/product-ownership" "$product/provenance/ownership"
    (cd "$product" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$staging/files.sha256"
    mv -- "$staging/files.sha256" "$product/files.sha256"
    unpacked=$(du -sb "$product" | cut -f 1)
    for file in "$product"/bin/*; do
        summary "${file##*/}: $(stat -c %s "$file") bytes"
    done
    # Fixed names, owners, times and order: the same build packs to the same bytes.
    tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 --format=gnu -C "$staging" -cf - product |
        zstd -q -T0 -3 -o "$output/product.tar.zst"
    rm -rf -- "$staging"
    digest=$(sha256sum < "$output/product.tar.zst" | cut -c 1-64)
    bytes=$(stat -c %s "$output/product.tar.zst")
    printf 'digest=%s\nbytes=%s\n' "$digest" "$bytes" >> "$GITHUB_OUTPUT"
    summary "packed product.tar.zst: $bytes bytes ($unpacked unpacked), SHA-256 $digest, in $((SECONDS - start)) s"
}

install_product()
{
    local download=$1 digest=$2 archive staging product actual start=$SECONDS file listing names
    : "${RUNNER_TEMP:?}" "${GITHUB_ENV:?}"
    portable
    [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || die "the producer gave no SHA-256 for this row's product: '$digest'"
    # The artifact is the archive alone.
    [ "$(find "$download" -mindepth 1 | wc -l)" -eq 1 ] && [ -f "$download/product.tar.zst" ] ||
        die "the downloaded artifact isn't product.tar.zst alone: $(find "$download" -mindepth 1 | head -n 5 | tr '\n' ' ')"
    archive="$download/product.tar.zst"
    actual=$(sha256sum < "$archive" | cut -c 1-64)
    [ "$actual" = "$digest" ] || die "the downloaded product's SHA-256 is $actual, its producer's $digest"
    # Only directories and regular files, all under product/, before anything is extracted.
    listing=$(tar --zstd -tvf "$archive") || die "the product isn't a readable archive"
    if grep -Ev '^[-d]' <<< "$listing" | grep -q .; then
        die "the product holds entries that are neither files nor directories"
    fi
    names=$(tar --zstd -tf "$archive") || die "the product isn't a readable archive"
    if grep -Ev '^product(/|$)' <<< "$names" | grep -q . || grep -Eq '(^|/)\.\.(/|$)' <<< "$names"; then
        die "the product holds paths outside product/"
    fi
    staging=$(mktemp -d "$RUNNER_TEMP/compiler-product.XXXXXXXX")
    tar --zstd -xf "$archive" -C "$staging" --no-same-owner
    product="$staging/product"
    test -f "$product/files.sha256" || die "the product has no list of its files"
    # Exactly the listed files, each with its hash: a partial or altered extraction fails here.
    if ! (cd "$product" && find . -type f ! -path ./files.sha256 -print0 | LC_ALL=C sort -z | tr '\0' '\n') |
        cmp -s - <(sed -E 's/^[0-9a-f]{64}  //' "$product/files.sha256"); then
        die "the product's files aren't the ones it lists"
    fi
    (cd "$product" && sha256sum --check --strict --quiet files.sha256) || die "a product file's SHA-256 differs from its list"
    # The server too: a product is built with the hosted toolchain, where `build` fails without one.
    test -x "$product/bin/compiler" && test -x "$product/bin/preprocessor-pragma-oracle" && test -x "$product/bin/clang-server" ||
        die "the product lacks its executables"
    # The same identity, computed here from this checkout, these settings, this toolchain and this runner.
    identity "$product" "$staging/expected"
    if ! diff -r "$product/identity" "$staging/expected" > "$staging/difference"; then
        cat "$staging/difference"
        die "this row's product was built for another configuration than this shard's (the difference is above)"
    fi
    provenance "$staging/here"
    for file in environment packages; do
        if ! diff "$product/provenance/$file" "$staging/here/$file" > "$staging/provenance-$file"; then
            printf '::notice title=Shared compiler product::the producer'"'"'s runner differs from this one (%s; reported, not compared):\n' "$file"
            cat "$staging/provenance-$file"
        fi
    done
    # Each executable runs here before the suite does.
    "$product/bin/compiler" --version > /dev/null || die "the product's compiler doesn't run on this runner"
    "$product/bin/preprocessor-pragma-oracle" --version > /dev/null || die "the product's pragma oracle doesn't run on this runner"
    [ "$("$product/bin/clang-server" --version)" = "$(clang -dumpversion)" ] || die "the product's clang server doesn't run as this clang"
    # Where `build` puts them. The compiler finds its headers from its own path (build/ in the checkout), so it goes in
    # build/ itself; build.sh's tree is left to the commands that build in it.
    mkdir -p build
    test ! -e build/compiler || die "build/compiler already exists"
    mv -- "$product/bin/compiler" build/compiler
    mv -- "$product/compile_commands.json" build/compile_commands.json
    mv -- "$product/bin/preprocessor-pragma-oracle" "$RUNNER_TEMP/preprocessor-pragma-oracle"
    test "$(find build -maxdepth 1 -type f -executable | wc -l)" -eq 1
    {
        printf 'BUSTER_COMPILER=%s\n' "$PWD/build/compiler"
        printf 'BUSTER_PPF_PRAGMA_ORACLE=%s\n' "$RUNNER_TEMP/preprocessor-pragma-oracle"
        mv -- "$product/bin/clang-server" "$RUNNER_TEMP/clang-server"
        printf 'BUSTER_CLANG_SERVER=%s\n' "$RUNNER_TEMP/clang-server"
        # The product's identity and provenance, which the inventory's ownership heading reads.
        printf 'BUSTER_PRODUCT=%s\n' "$product"
    } >> "$GITHUB_ENV"
    sed -n 's/^cpu\t/producer cpu: /p' "$product/provenance/host"
    summary "verified and installed product.tar.zst ($(stat -c %s "$archive") bytes, SHA-256 $digest) in $((SECONDS - start)) s"
}

case "${1:-}" in
    build)
        [ $# -eq 1 ] || { echo 'usage: hosted-product.sh build' >&2; exit 2; }
        build_products
        ;;
    ownership)
        [ $# -eq 1 ] || { echo 'usage: hosted-product.sh ownership' >&2; exit 2; }
        ownership
        ;;
    pack)
        [ $# -eq 2 ] || { echo 'usage: hosted-product.sh pack DIRECTORY' >&2; exit 2; }
        pack "$2"
        ;;
    install)
        [ $# -eq 3 ] || { echo 'usage: hosted-product.sh install DIRECTORY DIGEST' >&2; exit 2; }
        install_product "$2" "$3"
        ;;
    *)
        echo 'usage: hosted-product.sh build | ownership | pack DIRECTORY | install DIRECTORY DIGEST' >&2
        exit 2
        ;;
esac

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# tools/ci/hosted-product.sh's producer and shard sides on a fake checkout, toolchain and build: a product that
# installs, every way one must be refused (digest, files, entries, identity, libraries, native, no server), a vector
# configuration's product (#2033), which installs only where the runner's probe qualifies, and only as the same target,
# the clang server's status 77 (a failure on the hosted toolchain, none elsewhere), and the inventory's ownership heading
# on a shard. No network; tiny scripts stand in for the executables.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
checks=0
fail() { printf 'hosted-product fixture: %s\n' "$*" >&2; exit 1; }
pass() { checks=$((checks + 1)); }

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C

# The hosted toolchain: a clang (and llvm-config) under a root that holds the lock's copy, as install-hosted-tools.sh
# leaves it.
toolchain="$work/toolchain"
mkdir -p "$toolchain/llvm/bin"
printf 'llvm 23.1.4 fixture\n' > "$toolchain/hosted-tools.lock"
cat > "$toolchain/llvm/bin/clang" <<'CLANG'
#!/usr/bin/env bash
case " $* " in
    *' -dumpversion '*) echo 23.1.4 ;;
    *' --version '*) echo 'clang version 23.1.4 (fixture)' ;;
    *' -dM -E '*)
        march= vector=0
        for argument in "$@"; do case "$argument" in -march=*) march=${argument#-march=} ;; esac; done
        # The explicit vector target's flags select the vector path, unless the fixture says they don't.
        if [ "$march" = x86-64-v4 ] && [ -z "${FIXTURE_SCALAR_TARGET:-}" ]; then vector=1; fi
        printf '#define __x86_64__ 1\n#define FIXTURE_MARCH_%s 1\n#define BUSTER_HOST_VECTOR %s\n' "${march//-/_}" "$vector" ;;
    *) echo "fixture clang: $*" >&2; exit 1 ;;
esac
CLANG
printf '#!/usr/bin/env bash\necho 23.1.4\n' > "$toolchain/llvm/bin/llvm-config"
chmod +x "$toolchain/llvm/bin/clang" "$toolchain/llvm/bin/llvm-config"
export PATH="$toolchain/llvm/bin:$PATH"

# The baseline runtime that the x86-64 plain row links against.
mkdir -p "$work/runtime/usr/lib"
printf 'libc fixture\n' > "$work/runtime/usr/lib/libc.so.6"

# The checkout: a build.sh whose configuration record names the toolchain, the settings and the checkout's path (as
# build.sh's does), an inventory with the ownership heading, and the oracle's and the server's builds.
origin="$work/origin"
mkdir -p "$origin/tools/ci" "$origin/tools/clang-server" "$origin/src/buster/compiler"
printf 'build/\n' > "$origin/.gitignore"
: > "$origin/src/buster/compiler/host_isa.h"
cp "$root/tools/ci/hosted-product.sh" "$origin/tools/ci/hosted-product.sh"
cat > "$origin/build.sh" <<'BUILD'
#!/usr/bin/env bash
set -euo pipefail
tree="$PWD/build/cmake/fixture-${BUSTER_HOST_ARCH:-native}-${BUSTER_SANITIZER:-none}"
configure()
{
    mkdir -p "$tree"
    printf 'clang %s\nsettings %s %s %s %s\nroot %s\n' "$(command -v clang)" "${BUSTER_HOST_ARCH:-native}" \
        "${BUSTER_SANITIZER:-}" "${BUSTER_SANITIZER_OPTIMIZATION:-3}" "${BUSTER_HOST_RUNTIME:-}" "$PWD" > "$tree/build.sh.configuration"
}
case "$1" in
    configure) configure ;;
    print-output) printf '%s/Release/compiler\n' "$tree" ;;
    print-compile-flags) printf -- '-march=%s\0-O3\0' "${BUSTER_HOST_ARCH:-native}" ;;
    print-sanitizer-flags) [ -z "${BUSTER_SANITIZER:-}" ] || [ "$BUSTER_SANITIZER" = none ] || printf -- '-fsanitize=%s\0' "$BUSTER_SANITIZER" ;;
    print-sources) printf 'src/main.c\0' ;;
    print-host-flags)
        if [ "${BUSTER_HOST_ARCH:-native}" = vector ]; then printf -- '-march=x86-64-v4\0-mavx512vbmi\0'; else printf -- '-march=%s\0' "${BUSTER_HOST_ARCH:-native}"; fi ;;
    --config)
        configure
        mkdir -p "$tree/Release"
        cat > "$tree/Release/compiler" <<'COMPILER'
#!/usr/bin/env bash
# The fixture's compiler: --version, and a parallel runner of one heading (its summary line as the runner's).
case "$1" in
    --version) echo 'buster fixture' ;;
    parallel)
        script=$(sed -n '/^== /,$p' | sed '1d')
        status=0
        BUSTER_COMPILER=$0 bash -e -c "$script" || status=$?
        passed=0 failed=0 skipped=0
        case "$status" in 0) passed=1 ;; 77) skipped=1 ;; *) failed=1 ;; esac
        printf 'parallel: 1 commands in 0.0 s, 0.0 s of CPU, at most 1 CPUs at once: %s passed, %s failed, %s skipped, 0 not started\n' "$passed" "$failed" "$skipped"
        [ "$failed" -eq 0 ] ;;
    *) exit 2 ;;
esac
COMPILER
        chmod +x "$tree/Release/compiler"
        ln -f "$tree/Release/compiler" build/compiler
        printf '[]\n' > build/compile_commands.json ;;
    *) echo "fixture build.sh: $*" >&2; exit 2 ;;
esac
BUILD
cat > "$origin/tools/build-preprocessor-pragma-oracle.sh" <<'ORACLE'
#!/usr/bin/env bash
set -euo pipefail
printf '#!/usr/bin/env bash\necho oracle 23.1.4\n' > "$1"
chmod +x "$1"
ORACLE
cat > "$origin/tools/clang-server/build.sh" <<'SERVER'
#!/usr/bin/env bash
set -euo pipefail
[ "${FIXTURE_SERVER:-0}" -eq 0 ] || exit "$FIXTURE_SERVER"
printf '#!/usr/bin/env bash\necho 23.1.4\n' > "$1"
chmod +x "$1"
SERVER
cat > "$origin/tools/ci/correctness.tests" <<'TESTS'
== Another heading
false
== Check unity fixture ownership without rebuilding
test -x "$BUSTER_COMPILER"
if [ -n "${FIXTURE_OWNERSHIP_SKIP:-}" ]; then exit 77; fi
echo ownership checked
== A later heading
false
TESTS
chmod +x "$origin/build.sh"
git -c init.templateDir= init -q -b main "$origin"
git -C "$origin" add -A
git -C "$origin" -c user.name=Fixture -c user.email=fixture@example.invalid commit -q -m fixture
git -C "$origin" -c user.name=Fixture -c user.email=fixture@example.invalid commit -q --allow-empty -m later

# One runner: a fresh checkout at the commit and a fresh RUNNER_TEMP, at the same paths every time, as hosted runners'.
checkout="$work/runner/work/checkout"
export RUNNER_TEMP="$work/runner/temp"
fresh_runner()
{
    rm -rf -- "$work/runner"
    mkdir -p "$RUNNER_TEMP"
    git clone -q "$origin" "$checkout"
    git -C "$checkout" checkout -q "${1:-HEAD}"
    : > "$RUNNER_TEMP/env"
    : > "$RUNNER_TEMP/output"
    export GITHUB_ENV="$RUNNER_TEMP/env" GITHUB_OUTPUT="$RUNNER_TEMP/output" GITHUB_STEP_SUMMARY="$RUNNER_TEMP/summary"
}
# The steps' environment: the row's settings and what earlier steps exported.
run()
{
    (
        cd "$checkout"
        set -a
        # shellcheck disable=SC1090
        . "$GITHUB_ENV"
        set +a
        bash tools/ci/hosted-product.sh "$@"
    )
}
export GITHUB_REPOSITORY_ID=1421805693 GITHUB_RUN_ID=42 BUSTER_HOST_ARCH=x86-64 BUSTER_SANITIZER=none
export BUSTER_SANITIZER_OPTIMIZATION=3 BUSTER_HOST_RUNTIME="$work/runtime"
unset BUSTER_PRODUCT BUSTER_COMPILER BUSTER_CLANG_SERVER BUSTER_PPF_PRAGMA_ORACLE FIXTURE_SERVER FIXTURE_OWNERSHIP_SKIP

# A producer: build (with or without a server), the ownership check, and the pack, into $work/products/NAME.
produce()
{
    local name=$1
    fresh_runner
    run build > "$work/log" 2>&1 || { cat "$work/log"; fail "$name: build failed"; }
    run ownership > "$work/log" 2>&1 || { cat "$work/log"; fail "$name: ownership failed"; }
    run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1 || { cat "$work/log"; fail "$name: pack failed"; }
    mkdir -p "$work/products/$name"
    cp "$RUNNER_TEMP/product/product.tar.zst" "$work/products/$name/"
    sed -n 's/^digest=//p' "$RUNNER_TEMP/output" > "$work/products/$name.digest"
    [ "$(sha256sum < "$work/products/$name/product.tar.zst" | cut -c 1-64)" = "$(cat "$work/products/$name.digest")" ] || fail "$name: the output digest isn't the archive's"
}
# A shard's install of DIRECTORY with DIGEST on a fresh runner: expect success (0) or refusal (1), and then nothing
# installed.
consume()
{
    local expected=$1 what=$2 directory=$3 digest=$4 status=0
    fresh_runner "${FIXTURE_COMMIT:-HEAD}"
    run install "$directory" "$digest" > "$work/log" 2>&1 || status=$?
    if [ "$expected" -eq 0 ]; then
        [ "$status" -eq 0 ] || { cat "$work/log"; fail "$what: refused"; }
    else
        [ "$status" -ne 0 ] || { cat "$work/log"; fail "$what: accepted"; }
        [ ! -e "$checkout/build/compiler" ] || fail "$what: installed a compiler it refused"
        ! grep -q '^BUSTER_' "$GITHUB_ENV" || fail "$what: exported paths it refused"
        grep -q '::error' "$work/log" || { cat "$work/log"; fail "$what: refused without an error"; }
    fi
    pass
}

# The product holds the compiler, the pragma oracle, the clang server and the build's commands, and installs.
export FIXTURE_SERVER=0
produce plain
# The packing runner's own suite (a vector configuration's item 1) reads the ownership check's pass from the record the
# pack keeps, as a shard's does from its install: the pack's configure leaves the tree's build out of date.
packed=$(sed -n 's/^BUSTER_PRODUCT=//p' "$GITHUB_ENV")
[ "$packed" = "$RUNNER_TEMP/product/record" ] && [ "$(head -n 1 "$packed/provenance/ownership")" = passed ] && [ -f "$packed/identity/summary" ] || fail "the pack's record of its product: '$packed'"
pass
consume 0 'a verified product' "$work/products/plain" "$(cat "$work/products/plain.digest")"
grep -qx "BUSTER_COMPILER=$checkout/build/compiler" "$GITHUB_ENV" || fail 'BUSTER_COMPILER is not build/compiler'
grep -q '^BUSTER_PPF_PRAGMA_ORACLE=' "$GITHUB_ENV" || fail 'no pragma oracle exported'
grep -qx "BUSTER_CLANG_SERVER=$RUNNER_TEMP/clang-server" "$GITHUB_ENV" && [ -x "$RUNNER_TEMP/clang-server" ] || fail 'the server is not installed'
product=$(sed -n 's/^BUSTER_PRODUCT=//p' "$GITHUB_ENV")
[ "$(head -n 1 "$product/provenance/ownership")" = passed ] || fail 'the ownership record'
[ -x "$checkout/build/compiler" ] && [ -f "$checkout/build/compile_commands.json" ] || fail 'not installed in build/'
[ "$(find "$checkout/build" -maxdepth 1 -type f -executable | wc -l)" -eq 1 ] || fail 'build/ holds other executables'
pass
# The inventory's real ownership heading on that shard: it skips on the product's record, and fails without one.
heading=$(awk '/^== / { selected = $0 == "== Check unity fixture ownership without rebuilding" } selected' "$root/tools/ci/correctness.tests" | sed 1d)
status=0
(cd "$checkout" && BUSTER_PRODUCT=$product BUSTER_COMPILER=/bin/false bash -e -c "$heading") > /dev/null || status=$?
[ "$status" -eq 77 ] || fail "the inventory's ownership heading on a shared product: status $status, not 77"
pass
printf 'skipped\n' > "$product/provenance/ownership"
status=0
(cd "$checkout" && BUSTER_PRODUCT=$product BUSTER_COMPILER=/bin/false bash -e -c "$heading") > /dev/null 2>&1 || status=$?
[ "$status" -ne 0 ] && [ "$status" -ne 77 ] || fail 'the inventory skipped ownership without a passing record'
pass

# The same build packs to the same bytes.
produce again
cmp -s "$work/products/plain/product.tar.zst" "$work/products/again/product.tar.zst" || fail 'packing is not reproducible'
pass

# The clang server's build, which exits 77 for a toolchain without what the server needs. On the hosted toolchain that
# is a regression (#2071): the build fails, loudly, and exports nothing.
fresh_runner
if FIXTURE_SERVER=77 run build > "$work/log" 2>&1; then cat "$work/log"; fail 'status 77 on the hosted toolchain passed'; fi
grep -q '::error.*status 77' "$work/log" || { cat "$work/log"; fail 'status 77 on the hosted toolchain failed without its error'; }
! grep -q '^BUSTER_' "$GITHUB_ENV" || fail 'status 77 on the hosted toolchain exported paths'
[ ! -e "$checkout/build/compiler" ] || fail 'status 77 on the hosted toolchain built the compiler'
pass
# With another toolchain's clang (no lock above it: a distribution's, the 9700X's), 77 is no server, a notice, and the
# rest builds without one.
mv "$toolchain/hosted-tools.lock" "$toolchain/moved.lock"
fresh_runner
FIXTURE_SERVER=77 run build > "$work/log" 2>&1 || { cat "$work/log"; mv "$toolchain/moved.lock" "$toolchain/hosted-tools.lock"; fail 'status 77 on another toolchain failed'; }
mv "$toolchain/moved.lock" "$toolchain/hosted-tools.lock"
grep -q '::notice::No clang server' "$work/log" || fail 'status 77 on another toolchain gave no notice'
grep -q '^BUSTER_COMPILER=' "$GITHUB_ENV" && ! grep -q '^BUSTER_CLANG_SERVER=' "$GITHUB_ENV" || fail 'status 77 on another toolchain exported a server, or no compiler'
pass
# A pack without a server, which only another toolchain's build leaves, is refused even before the identity.
run ownership > /dev/null 2>&1
if run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1; then fail 'packed a product without a server'; fi
grep -q '::error.*no clang server' "$work/log" || { cat "$work/log"; fail 'refused a product without a server without its error'; }
pass
# A server build that fails otherwise fails the build, on any toolchain.
fresh_runner
if FIXTURE_SERVER=1 run build > "$work/log" 2>&1; then fail 'a failing server build passed'; fi
pass

digest=$(cat "$work/products/plain.digest")
plain="$work/products/plain/product.tar.zst"
# The archive's digest: another digest, no digest, a truncated (partial) download, a changed byte.
consume 1 'another digest' "$work/products/plain" "$(printf other | sha256sum | cut -c 1-64)"
consume 1 'no digest' "$work/products/plain" ''
mkdir -p "$work/bad/truncated"
head -c "$(($(stat -c %s "$plain") / 2))" "$plain" > "$work/bad/truncated/product.tar.zst"
consume 1 'a truncated archive' "$work/bad/truncated" "$digest"
mkdir -p "$work/bad/flipped"
cp "$plain" "$work/bad/flipped/product.tar.zst"
printf '\xff' | dd of="$work/bad/flipped/product.tar.zst" bs=1 seek=100 conv=notrunc status=none
consume 1 'a changed byte' "$work/bad/flipped" "$digest"
# The artifact's contents: an extra file beside the archive, or no archive.
mkdir -p "$work/bad/extra"
cp "$plain" "$work/bad/extra/"
: > "$work/bad/extra/other"
consume 1 'an artifact with another file' "$work/bad/extra" "$digest"
mkdir -p "$work/bad/empty"
consume 1 'an empty artifact' "$work/bad/empty" "$digest"

# Products altered and repacked with their own digest, as a producer bug or a crafted archive would be: each must be
# refused by the shard's checks, not by the digest.
repack()
{
    local name=$1 edit=$2
    rm -rf "$work/edit"
    mkdir -p "$work/edit" "$work/bad/$name"
    tar --zstd -xf "$plain" -C "$work/edit"
    (cd "$work/edit" && eval "$edit")
    (cd "$work/edit" && tar --zstd -cf "$work/bad/$name/product.tar.zst" -- *)
    sha256sum < "$work/bad/$name/product.tar.zst" | cut -c 1-64
}
consume 1 'a file unlike its listed hash' "$work/bad/changed" "$(repack changed 'printf x >> product/bin/compiler')"
consume 1 'an unlisted file' "$work/bad/unlisted" "$(repack unlisted ': > product/bin/extra')"
consume 1 'a missing file' "$work/bad/missing" "$(repack missing 'rm product/compile_commands.json')"
consume 1 'a symbolic link' "$work/bad/link" "$(repack link 'ln -s /bin/sh product/link')"
consume 1 'a path outside product/' "$work/bad/outside" "$(repack outside 'printf x > outside')"
consume 1 'no list of files' "$work/bad/unlisted-all" "$(repack unlisted-all 'rm product/files.sha256')"
# A product without its server, its identity and list made to match: refused for the missing executable.
consume 1 'a product without a server' "$work/bad/serverless" "$(repack serverless "rm product/bin/clang-server && sed -i '/^clang-server:\$/,/^preprocessor-pragma-oracle:\$/{/^preprocessor-pragma-oracle:\$/!d}' product/identity/libraries && (cd product && find . -type f ! -path ./files.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > files.sha256)")"
grep -q 'lacks its executables' "$work/log" || { cat "$work/log"; fail 'a product without a server was refused for another reason'; }
consume 1 'another identity, listed' "$work/bad/identity" "$(repack identity "sed -i 's/^run\t42/run\t41/' product/identity/summary && (cd product && find . -type f ! -path ./files.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > files.sha256)")"

# A shard whose own configuration differs from the producer's: another run, revision, row, toolchain or runtime.
GITHUB_RUN_ID=43 consume 1 'another run' "$work/products/plain" "$digest"
FIXTURE_COMMIT=HEAD~1 consume 1 'another revision' "$work/products/plain" "$digest"
BUSTER_SANITIZER=address consume 1 'another sanitizer' "$work/products/plain" "$digest"
BUSTER_SANITIZER_OPTIMIZATION=1 consume 1 'another optimization' "$work/products/plain" "$digest"
BUSTER_HOST_RUNTIME= consume 1 'no baseline runtime' "$work/products/plain" "$digest"
printf 'changed\n' > "$work/runtime/usr/lib/libc.so.6"
consume 1 'another baseline runtime' "$work/products/plain" "$digest"
printf 'libc fixture\n' > "$work/runtime/usr/lib/libc.so.6"
printf 'llvm 23.1.5 fixture\n' > "$toolchain/hosted-tools.lock"
consume 1 'another toolchain' "$work/products/plain" "$digest"
printf 'llvm 23.1.4 fixture\n' > "$toolchain/hosted-tools.lock"
consume 0 'the same product again' "$work/products/plain" "$digest"
# A clang that isn't the hosted toolchain's installation (no lock beside it), on either side.
mv "$toolchain/hosted-tools.lock" "$toolchain/moved.lock"
consume 1 'a clang outside the hosted toolchain' "$work/products/plain" "$digest"
fresh_runner
run build > /dev/null 2>&1
run ownership > /dev/null 2>&1
if run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1; then fail 'packed with a clang outside the hosted toolchain'; fi
grep -q '::error' "$work/log" || fail 'refused a clang outside the hosted toolchain without an error'
pass
mv "$toolchain/moved.lock" "$toolchain/hosted-tools.lock"
# A library the shard can't load.
mkdir -p "$work/ldd"
printf '#!/usr/bin/env bash\nprintf "\\tlibfixture.so.1 => not found\\n"\n' > "$work/ldd/ldd"
chmod +x "$work/ldd/ldd"
PATH="$work/ldd:$PATH" consume 1 'a missing library' "$work/products/plain" "$digest"

# A vector configuration's product (#2033): packed and installed only where the row's probe, run again, qualifies, and
# only as the same target.
probe="$work/probe"
cat > "$probe" <<'PROBE'
#!/usr/bin/env bash
# The vector row's probe on a fixture CPU: FIXTURE_PROBE `no` lacks a feature, `stops` doesn't run.
case "${FIXTURE_PROBE:-yes}" in
    yes) printf 'x86-64-v4\tyes\navx512vbmi\tyes\n' ;;
    no) printf 'x86-64-v4\tyes\navx512vbmi\tno\n'; exit 1 ;;
    lies) printf 'x86-64-v4\tyes\navx512vbmi\tno\n' ;;
    stops) exit 132 ;;
esac
PROBE
chmod +x "$probe"
export BUSTER_VECTOR_PROBE="$probe"
BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= produce vector
vector_digest=$(cat "$work/products/vector.digest")
tar --zstd -xOf "$work/products/vector/product.tar.zst" product/identity/summary | grep -q $'^host-flags\t-march=x86-64-v4 -mavx512vbmi$' || fail "the vector product's identity lacks its flags"
tar --zstd -xOf "$work/products/vector/product.tar.zst" product/identity/target | grep -qx '#define BUSTER_HOST_VECTOR 1' || fail "the vector product's identity lacks BUSTER_HOST_VECTOR 1"
tar --zstd -xOf "$work/products/vector/product.tar.zst" product/identity/probe | grep -q $'^avx512vbmi\tyes$' || fail "the vector product's identity lacks the probe's answers"
pass
BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 0 'a vector product on a qualifying runner' "$work/products/vector" "$vector_digest"
FIXTURE_PROBE=no BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 'a vector product on a runner that lacks a feature' "$work/products/vector" "$vector_digest"
grep -q "doesn't run the vector target" "$work/log" || { cat "$work/log"; fail 'an unqualified runner was refused for another reason'; }
FIXTURE_PROBE=lies BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 "a vector product on a runner whose probe answers no but passes" "$work/products/vector" "$vector_digest"
FIXTURE_PROBE=stops BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 'a vector product on a runner whose probe stops' "$work/products/vector" "$vector_digest"
BUSTER_VECTOR_PROBE= BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 'a vector product without the probe' "$work/products/vector" "$vector_digest"
BUSTER_HOST_ARCH=vector BUSTER_SANITIZER=address BUSTER_HOST_RUNTIME= consume 1 "another configuration's vector product" "$work/products/vector" "$vector_digest"
BUSTER_HOST_RUNTIME= consume 1 'a vector product on an x86-64 shard' "$work/products/vector" "$vector_digest"
BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 'an x86-64 product on a vector item' "$work/products/plain" "$digest"
FIXTURE_SCALAR_TARGET=1 BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= consume 1 'a vector target that selects the scalar path' "$work/products/vector" "$vector_digest"
grep -q 'BUSTER_HOST_VECTOR 1' "$work/log" || { cat "$work/log"; fail 'a scalar vector target was refused for another reason'; }
# No pack where the probe doesn't qualify.
fresh_runner
BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= run build > /dev/null 2>&1
BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= run ownership > /dev/null 2>&1
if FIXTURE_PROBE=no BUSTER_HOST_ARCH=vector BUSTER_HOST_RUNTIME= run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1; then fail 'a vector product packed where the probe fails'; fi
pass
unset BUSTER_VECTOR_PROBE

# Native rows are never shared: no pack, no install.
fresh_runner
run build > /dev/null 2>&1
run ownership > /dev/null 2>&1
if BUSTER_HOST_ARCH=native run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1; then fail 'a native product was packed'; fi
pass
BUSTER_HOST_ARCH=native consume 1 'a native shard' "$work/products/plain" "$digest"
# A pack needs the ownership check's pass on its tree: none, or a skipped one.
fresh_runner
run build > /dev/null 2>&1
if run pack "$RUNNER_TEMP/product" > "$work/log" 2>&1; then fail 'packed without the ownership check'; fi
pass
if FIXTURE_OWNERSHIP_SKIP=1 run ownership > "$work/log" 2>&1; then fail 'a skipped ownership check passed'; fi
[ ! -e "$RUNNER_TEMP/product-ownership" ] || fail 'a skipped ownership check left a record'
pass
printf 'hosted-product fixture: %u checks passed\n' "$checks"

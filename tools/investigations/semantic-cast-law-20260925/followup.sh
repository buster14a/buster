#!/usr/bin/env bash
set -euo pipefail
root=$(pwd)
probe="$root/tools/investigations/semantic-cast-law-20260925"
evidence="$root/evidence"
base=ade6ac4b6ecb21f30b61b656439bac476c145e2f
mkdir -p "$evidence"
git rev-parse HEAD HEAD^{tree} > "$evidence/probe-identity.txt"
for variant in baseline prototype; do
 work="$root/../cast-law-$variant"
 git worktree add --detach "$work" "$base"
 test "$(git -C "$work" rev-parse HEAD^{tree})" = 4c5306221fdb22fccc929b55e333163742de17d0
 mkdir -p "$evidence/$variant"
 if [[ $variant == prototype ]]; then
  git -C "$work" apply --check "$probe/sign-extension.patch"
  git -C "$work" apply "$probe/sign-extension.patch"
  git -C "$work" diff > "$evidence/$variant/source.patch"
  git -C "$work" add src/buster/lib/compiler/frontend/c/c_gen.c
 fi
 git -C "$work" write-tree > "$evidence/$variant/source-tree.txt"
 git -C "$work" diff --cached --stat > "$evidence/$variant/source-stat.txt"
 (
  cd "$work"
  mkdir -p .cache/cast-law
  clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o .cache/cast-law/build > "$evidence/$variant/bootstrap.stdout" 2> "$evidence/$variant/bootstrap.stderr"
  .cache/cast-law/build generate --cc clang --ci --linker DEFAULT --build-directory build-cast-law -- -DBUSTER_INCLUDE_TESTS=OFF -DBUSTER_UNITY_BUILD=OFF > "$evidence/$variant/generate.stdout" 2> "$evidence/$variant/generate.stderr"
  .cache/cast-law/build build --config Release --build-directory build-cast-law -t ide > "$evidence/$variant/build.stdout" 2> "$evidence/$variant/build.stderr"
  cp build-cast-law/CMakeCache.txt "$evidence/$variant/"
  git diff --exit-code
  sha256sum build-cast-law/Release/ide > "$evidence/$variant/compiler.sha256"
 )
 # Each artifact is compiled and executed on the same hosted machine.
 for profile in fast-ssa-O0 fast-memory-O0 fast-ssa-O2 mir-stack-ssa-O0 quality-ssa-O0 none-ssa-O0; do
  export PROBE_COMPILERS=buster PROBE_OPTIMIZATION=-O0 PROBE_ALLOCATOR=fast PROBE_FRONTEND=-ffrontend-ssa
  case "$profile" in
   fast-memory-O0) PROBE_FRONTEND=-fno-frontend-ssa;;
   fast-ssa-O2) PROBE_OPTIMIZATION=-O2;;
   mir-stack-ssa-O0) PROBE_ALLOCATOR=mir-stack;;
   quality-ssa-O0) PROBE_ALLOCATOR=quality;;
   none-ssa-O0) PROBE_ALLOCATOR=none;;
  esac
  if [[ $variant == baseline && $profile == fast-ssa-O0 ]]; then PROBE_COMPILERS='clang gcc buster'; fi
  bash "$probe/run.sh" "$work/build-cast-law/Release/ide" "$evidence/$variant/$profile"
 done
 git -C "$work" diff --exit-code
 test "$(git -C "$work" write-tree)" = "$(cat "$evidence/$variant/source-tree.txt")"
done
# A green collection workflow does not mean the compiler passed. Keep each
# failed cell and both full variants; classify only after artifact review.
find "$evidence" -type f ! -name COMPLETE-SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > "$evidence/COMPLETE-SHA256SUMS"

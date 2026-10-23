ssh david@benchpress 'date -u
EXCLUSIVE_OWNER=codex-linker-debug88 EXCLUSIVE_TITLE='\''String consumer 12-baseline-100ms-r3'\'' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86 "$HOME/buster-fast-bench/exclusive.sh" "$HOME/buster-fast-bench/codex-linker-tls85/tls85-measure" taskset -c 2 "$HOME/buster-fast-bench/linker-strings86/build3/build/compiler" benchmark-foreign-strings --counts 64,4096,131072 --runs 11 --keep --directory /tmp --order nomerge-exact,merge-exact,nomerge-vector,merge-vector --after-scalar 100
strings_result=$?
printf '\''remote_exit %s\n'\'' "$strings_result"
date -u
exit "$strings_result"' > work/strings86-evidence/12-baseline-100ms-r3.log 2>&1
strings_result=$?
printf '%s\n' "$strings_result" > work/strings86-evidence/12-baseline-100ms-r3.status
exit "$strings_result"

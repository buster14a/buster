ssh david@benchpress 'EXCLUSIVE_OWNER=codex-linker-debug88 EXCLUSIVE_TITLE='\''Frozen string writer relocation inspection'\'' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86 "$HOME/buster-fast-bench/exclusive.sh" sh -c '\''set -eu
date -u
for variant in build3 apply1; do
  binary="$HOME/buster-fast-bench/linker-strings86/$variant/build/compiler"
  printf '\''\'\'''\''DISASSEMBLY_BEGIN %s\n'\''\'\'''\'' "$variant"
  objdump -d -w -Mintel --start-address=0xba6b70 --stop-address=0xbc04ed "$binary"
  printf '\''\'\'''\''DISASSEMBLY_END %s\n'\''\'\'''\'' "$variant"
done
printf '\''\'\'''\''remote_exit 0\n'\''\'\'''\''
date -u'\''' > work/strings86-evidence/writer-disassembly.txt 2> work/strings86-evidence/writer-disassembly-lock.log
strings_inspect_status=$?
printf '%s\n' "$strings_inspect_status" > work/strings86-evidence/writer-disassembly.status
exit "$strings_inspect_status"

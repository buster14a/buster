ssh david@benchpress 'EXCLUSIVE_OWNER=codex-linker-debug88 EXCLUSIVE_TITLE='\''Frozen string campaign writer machine-code provenance'\'' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86 "$HOME/buster-fast-bench/exclusive.sh" sh -c '\''set -eu
date -u
for variant in build3 apply1; do
  binary="$HOME/buster-fast-bench/linker-strings86/$variant/build/compiler"
  printf '\''\'\'''\''WRITER_BEGIN %s\n'\''\'\'''\'' "$variant"
  readelf -SW "$binary" | grep '\''\'\'''\'' \.text '\''\'\'''\''
  nm -S --defined-only "$binary" | grep '\''\'\'''\''^0000000000ba6b70 '\''\'\'''\''
  dd if="$binary" bs=64K iflag=skip_bytes,count_bytes skip=12213104 count=104829 status=none | sha256sum
  printf '\''\'\'''\''WRITER_END %s\n'\''\'\'''\'' "$variant"
done
date -u'\''' > work/strings86-evidence/writer-byte-provenance.txt 2> work/strings86-evidence/writer-byte-lock.log
status=$?
printf '%s\n' "$status" > work/strings86-evidence/writer-byte.status
exit "$status"

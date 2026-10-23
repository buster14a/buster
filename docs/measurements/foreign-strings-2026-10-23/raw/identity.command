ssh david@benchpress 'EXCLUSIVE_OWNER=codex-linker-debug88 EXCLUSIVE_TITLE="Validate frozen string campaign file identities" EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86 "$HOME/buster-fast-bench/exclusive.sh" sh -s -- /tmp/buster-foreign-strings-4kBoLm /tmp/buster-foreign-strings-R4Durn /tmp/buster-foreign-strings-8VkUSB /tmp/buster-foreign-strings-WLbqgv /tmp/buster-foreign-strings-JxoyqX /tmp/buster-foreign-strings-69tq8P /tmp/buster-foreign-strings-FQRJy2 /tmp/buster-foreign-strings-19fWMV /tmp/buster-foreign-strings-yBd1fp /tmp/buster-foreign-strings-kYbXo7 /tmp/buster-foreign-strings-UZmfrK /tmp/buster-foreign-strings-ek7Ueb' > work/strings86-evidence/retained-manifests.txt 2> work/strings86-evidence/identity-lock.log <<'STRINGS_IDENTITY'
set -eu
reference=$1
ordinal=0
for directory do
    ordinal=$((ordinal + 1))
    cd "$directory"
    input_count=0
    for input in strings_duplicate_*.o strings_unique_*.o; do
        test -f "$input"
        input_count=$((input_count + 1))
    done
    test "$input_count" -eq 1064
    printf 'MANIFEST_BEGIN %02d %s\n' "$ordinal" "$directory"
    printf 'INPUTS_BEGIN %02d\n' "$ordinal"
    sha256sum -- strings_duplicate_*.o strings_unique_*.o
    printf 'INPUTS_END %02d\n' "$ordinal"
    output_count=0
    for output in strings_output_*; do
        test -f "$output"
        cmp -- "$reference/$output" "$output"
        output_count=$((output_count + 1))
    done
    test "$output_count" -eq 24
    printf 'OUTPUTS_BEGIN %02d\n' "$ordinal"
    sha256sum -- strings_output_*
    printf 'OUTPUTS_END %02d\n' "$ordinal"
    for count in 64 4096 131072; do
        for variant in 1 2 3; do
            cmp -- "strings_output_${count}_0_0" "strings_output_${count}_0_${variant}"
        done
        cmp -- "strings_output_${count}_1_0" "strings_output_${count}_1_2"
        cmp -- "strings_output_${count}_1_1" "strings_output_${count}_1_3"
    done
    printf 'IDENTITY_OK %02d inputs %s outputs %s\n' "$ordinal" "$input_count" "$output_count"
    stat -f -c '%T %n' "$directory"
    printf 'MANIFEST_END %02d\n' "$ordinal"
done
printf 'VALIDATED_PROCESSES %s\n' "$ordinal"
test "$ordinal" -eq 12
date -u
sha256sum "$HOME/buster-fast-bench/linker-strings86/build3/source.tar.gz" "$HOME/buster-fast-bench/linker-strings86/apply1/source.tar.gz" "$HOME/buster-fast-bench/linker-strings86/build3/build/compiler" "$HOME/buster-fast-bench/linker-strings86/apply1/build/compiler" "$HOME/buster-fast-bench/codex-linker-tls85/tls85-measure"
cat /sys/devices/system/cpu/online
cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor
cat /sys/devices/system/cpu/cpufreq/boost
readelf -SW "$HOME/buster-fast-bench/linker-strings86/build3/build/compiler" | grep ' \.text '
readelf -SW "$HOME/buster-fast-bench/linker-strings86/apply1/build/compiler" | grep ' \.text '
STRINGS_IDENTITY
strings_result=$?
printf '%s\n' "$strings_result" > work/strings86-evidence/identity.status
exit "$strings_result"

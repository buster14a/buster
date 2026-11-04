#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Original #1239 validation harness. Downloaded sources are not committed.
set -euo pipefail
repo="$PWD"
work="$RUNNER_TEMP/issue-1239-corpus"
mkdir -p "$work/minimp3" "$work/dr" "$work/generated"
curl -fsSL --retry 3 https://codeload.github.com/lieff/minimp3/tar.gz/ea99364f61c14656440e8d77e9c233ccf3124633 -o "$work/minimp3.tar.gz"
curl -fsSL --retry 3 https://codeload.github.com/mackron/dr_libs/tar.gz/35b5c392cc15456736c440a68dbe073178bbffe0 -o "$work/dr.tar.gz"
sha256sum "$work/"*.tar.gz > "$work/revisions.txt"
tar -xzf "$work/minimp3.tar.gz" --strip-components=1 -C "$work/minimp3"
tar -xzf "$work/dr.tar.gz" --strip-components=1 -C "$work/dr"
clang --version >> "$work/revisions.txt"
lscpu > "$work/cpu.txt"
for configuration in '44100 2 128k' '22050 1 48k' '48000 2 192k'; do
  read -r rate channels bitrate <<< "$configuration"
  ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=997:sample_rate=$rate:duration=1" -ac "$channels" -c:a libmp3lame -b:a "$bitrate" "$work/generated/tone-$rate-$channels-$bitrate.mp3"
done
cat > "$work/dr-implementation.c" <<'EOF'
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
EOF
cat > "$work/mini-implementation.c" <<'EOF'
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
EOF
cat > "$work/dr-driver.c" <<'EOF'
#include <stdio.h>
#include "dr_mp3.h"
int main(int argc, char** argv)
{
    int result = argc == 2 ? 0 : 2;
    if (result == 0)
    {
        drmp3_config config = {0};
        drmp3_uint64 frames = 0;
        float* floats = drmp3_open_file_and_read_pcm_frames_f32(argv[1], &config, &frames, NULL);
        printf("f32 %u %u %llu %u\n", config.channels, config.sampleRate, (unsigned long long)frames, floats != NULL);
        if (floats != NULL)
        {
            const size_t samples = (size_t)frames * config.channels;
            result |= fwrite(floats, sizeof(*floats), samples, stdout) != samples;
            drmp3_free(floats, NULL);
        }
        config = (drmp3_config){0};
        frames = 0;
        drmp3_int16* integers = drmp3_open_file_and_read_pcm_frames_s16(argv[1], &config, &frames, NULL);
        printf("s16 %u %u %llu %u\n", config.channels, config.sampleRate, (unsigned long long)frames, integers != NULL);
        if (integers != NULL)
        {
            const size_t samples = (size_t)frames * config.channels;
            result |= fwrite(integers, sizeof(*integers), samples, stdout) != samples;
            drmp3_free(integers, NULL);
        }
    }
    return result;
}
EOF
cat > "$work/mini-driver.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "minimp3_ex.h"
int main(int argc, char** argv)
{
    int result = argc == 2 ? 0 : 2;
    if (result == 0)
    {
        mp3dec_t decoder = {0};
        mp3dec_file_info_t info = {0};
        const int status = mp3dec_load(&decoder, argv[1], &info, NULL, NULL);
        printf("%d %zu %d %d %d\n", status, info.samples, info.channels, info.hz, info.layer);
        if (status == 0 && info.buffer != NULL)
        {
            result = fwrite(info.buffer, sizeof(*info.buffer), info.samples, stdout) != info.samples;
            free(info.buffer);
        }
    }
    return result;
}
EOF
mapfile -d '' inputs < <(find "$work/minimp3/vectors" "$work/generated" -type f \( -name '*.bit' -o -name '*.mp3' \) -print0 | sort -z)
printf '%s\n' "INPUTS=${#inputs[@]}" | tee "$work/results.txt"
failures=0
for target in x86-64 x86-64-v2 x86-64-v3 x86-64-v4 znver5; do
  if [[ "$target" = x86-64-v3 ]] && ! grep -qw fma /proc/cpuinfo; then echo "SKIP native $target" | tee -a "$work/results.txt"; continue; fi
  if [[ "$target" = x86-64-v4 || "$target" = znver5 ]] && ! grep -qw avx512f /proc/cpuinfo; then echo "SKIP native $target" | tee -a "$work/results.txt"; continue; fi
  for mode in on off; do
    for variant in dr mini-s16 mini-f32; do
      flags=("-march=$target" "-ffp-contract=$mode" -O0)
      library=mini; include="$work/minimp3"
      if [[ "$variant" = dr ]]; then library=dr; include="$work/dr"; fi
      if [[ "$variant" = mini-f32 ]]; then flags+=(-DMINIMP3_FLOAT_OUTPUT); fi
      prefix="$work/$target-$mode-$variant"
      clang "${flags[@]}" -I"$include" -c "$work/$library-driver.c" -o "$work/driver.o"
      clang "${flags[@]}" -I"$include" -c "$work/$library-implementation.c" -o "$work/clang.o"
      "$repo/build/compiler" --verify "${flags[@]}" -I"$include" -c "$work/$library-implementation.c" -o "$work/buster.o"
      clang "$work/driver.o" "$work/clang.o" -lm -o "$work/oracle"
      clang "$work/driver.o" "$work/buster.o" -lm -o "$work/actual"
      mismatches=0
      for input in "${inputs[@]}"; do
        expected_status=0; actual_status=0
        timeout 15s "$work/oracle" "$input" > "$work/expected.bin" || expected_status=$?
        timeout 15s "$work/actual" "$input" > "$work/actual.bin" || actual_status=$?
        if [[ "$expected_status" != "$actual_status" ]] || ! cmp -s "$work/expected.bin" "$work/actual.bin"; then
          echo "MISMATCH $target $mode $variant $(basename "$input") status=$expected_status/$actual_status" | tee -a "$work/results.txt"
          mismatches=$((mismatches+1))
        fi
        printf '%s %s %s %s %s %s\n' "$target" "$mode" "$variant" "$(basename "$input")" "$expected_status" "$actual_status" >> "$work/statuses.txt"
      done
      echo "CHECKED $target $mode $variant inputs=${#inputs[@]} mismatches=$mismatches" | tee -a "$work/results.txt"
      failures=$((failures+mismatches))
    done
  done
done
echo "TOTAL_MISMATCHES=$failures" | tee -a "$work/results.txt"
test "$failures" = 0

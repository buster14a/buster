#!/bin/sh
# usage: ./run.sh <path-to-ide>   -- compiles every test under default/fast/quality (and -g fast) and diffs against expected.txt
IDE=${1:-../base/ide}
cd "$(dirname "$0")"
gcc -c my_setjmp.s -o my_setjmp.o || exit 1
fail=0
while read -r file tag rest; do
  exp="$tag $(echo "$rest" | sed 's/ *(.*//')"
  exp=$(echo "$exp" | sed 's/ *$//')
  extra=""; [ "$file" = t6_returns_twice_alias.c ] && extra=my_setjmp.o
  for mode in "" "-fregister-allocator=fast" "-fregister-allocator=quality" "-g -fregister-allocator=fast"; do
    out=/tmp/rv.$$; $IDE cc $mode "$file" $extra -o $out 2>&1 | head -3
    got=$($out 2>&1); rm -f $out
    if [ "$got" = "$exp" ]; then echo "PASS $file [$mode]"; else echo "FAIL $file [$mode] got: $got"; fail=1; fi
  done
done < expected.txt
exit $fail

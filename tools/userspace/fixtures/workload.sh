#!/bin/ash
set -eu
cd /tmp
mkdir -p work/sub
cd work
printf 'beta\nalpha\nbeta\n' > input
sort input | head -n 1
grep -c beta input
sed 's/beta/B/' input | tail -n 1
awk '{n++} END {print n}' input
cp input copied
cmp input copied
mv copied sub/moved
ln -s sub/moved linked
cat linked | wc -l
chmod 600 sub/moved
tar -cf bundle.tar input sub/moved
mkdir extracted
cd extracted
tar -xf ../bundle.tar
cmp input ../input
cmp sub/moved ../sub/moved
cd ..
if false; then exit 20; else status=$?; test "$status" -eq 1; printf 'false-status=%s\n' "$status"; fi
/bin/busybox ash -c 'exit 7' && exit 21 || test "$?" -eq 7
printf 'child-status=7\n'
printf 'abc\n' | sha256sum
/bin/busybox sleep 0 &
wait "$!"
rm linked sub/moved input bundle.tar
rmdir sub
rm extracted/input extracted/sub/moved
rmdir extracted/sub extracted
printf 'userspace-ok\n'

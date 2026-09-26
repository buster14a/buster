#!/bin/bash
# binary search the largest recursion depth that completes under an 8 MiB stack
bin=$1; lo=1; hi=${2:-2000000}
while [ $((hi - lo)) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  if ( ulimit -s 8192; ./$bin $mid > /dev/null 2>&1 ); then lo=$mid; else hi=$mid; fi
done
echo "$bin max_depth=$lo"

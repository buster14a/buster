set debuginfod enabled off
set pagination off
set $hits = 0
break c_ir_ssa_finish_loop_exits
commands
silent
set $hits = $hits + 1
continue
end
run cc -march=native -I../4029-frozen/src -I../4029-frozen/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -c ../4029-frozen/src/buster/apps/ide/ide.c -o ../4029-evidence/count-exits.o
printf "Loop-exit preparation calls: %d\n", $hits

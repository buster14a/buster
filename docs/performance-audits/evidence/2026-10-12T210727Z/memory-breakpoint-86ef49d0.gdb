set pagination off
set follow-fork-mode child
set breakpoint pending on
break c_parse.c:12476
commands
silent
printf "KEEP %u %u\n", replay->element_count, machine->initializer_replays->bound_count
continue
end
run
quit

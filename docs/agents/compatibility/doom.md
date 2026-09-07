# DoomGeneric compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in DoomGeneric compatibility harness takes an external, pristine
DoomGeneric checkout and a WAD the caller supplies separately; upstream sources
are never copied into or patched in this repository, and no Doom data is stored
in it at all:

```sh
./build.sh build --config Release -t ide
./build/build test_doom --config Release /path/to/doomgeneric /path/to/DOOM1.WAD
```

The checkout must be commit
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` with no tracked or untracked
changes. The WAD is game data: the harness only checks that the file is one,
records its size and hash so a transcript can be traced back to it, and hands
the same file to both compilers. The shareware `DOOM1.WAD` (4.196.020 bytes) is
what the fixture's input script was written against, and the run starts with
`-warp 1 1 -skill 3`, which is the Doom I spelling of that argument. Another
IWAD runs, but its maps differ, so its transcript is its own — and a Doom II
IWAD would need the other `-warp` form.

The harness owns an explicit 80-unit manifest — upstream's makefile is a link
line for one window-system backend, and driving it is a later driver milestone
— and compiles in named stages, failing at the first one that breaks: the game
simulation and renderer (`engine`, 63 units), then the file, timing, sound and
video layer beneath the platform (`platform-io`, 17 units), then the platform
itself (`headless-platform`). `doomgeneric_xlib.c` is the one upstream unit
left out, because `tests/basic_doom_headless.c` replaces it. The 81 objects are
linked into an executable by `ide cc`, so the driver is covered across a
program of that size rather than only the compiler.

`tests/basic_doom_headless.c` is this repository's headless platform layer, and
it is what makes a whole game a compiler test. DoomGeneric leaves six functions
to the platform, and every upstream backend fills them with a window system and
a wall clock — the two things a deterministic test cannot use. This one
replaces both with counters: drawing a frame advances Doom's clock to the start
of the next game tic, a sleep advances it by the milliseconds asked for (Doom's
inner loops wait for the clock to move and would otherwise spin), input is a
table of scripted key events rather than a device, and the frame buffer is
cleared before Doom writes into it, because upstream mallocs it and only writes
the letterboxed region. The run prints one `DOOM_TICK` line per frame carrying
a hash of the frame buffer and a hash of the simulation state — player, level
counters and every thinker's position, angle, momentum, state index and health
— so a mismatch names the first divergent tic and says whether the pixels
moved, the simulation moved, or both.

The scripted run covers what the acceptance criteria ask for in one pass over
480 frames: WAD file I/O at startup, the renderer and the play simulation with
monsters awake, scripted movement, turning, firing and the use key, the menu,
the automap, a save and a load, the intermission, and the load of the next
level. The save/load leg is exact rather than indicative. `G_SaveGame` does not
save — it raises `sendsave`, which travels through the next ticcmd and only
then becomes the gameaction that the following tic carries out — so the world
that reaches the file is hashed while that gameaction is still pending. The
load is then observed with the simulation frozen (`P_Ticker` returns early
while the menu is up, which is what the platform raises around it), because the
tic that carries out a load also runs the world forward and would otherwise
hide what `p_saveg.c` restored behind one tic of simulation. The two hashes
must be equal, and the `DOOM_SAVELOAD` line reports them either way. The
savegame's *bytes* are deliberately not compared: a vanilla savegame stores
mobj pointers as the addresses they had, which differ between two runs of one
binary, so only its size is checked against the reference.

Every stage runs under FAST, NONE, MIR_STACK and QUALITY, and each build's
transcript must equal the Clang reference's exactly. Compiler cost and
generated-code quality are reported separately and must not be conflated:
`DOOM_METRIC` carries per-unit compiler wall time with the `-fsource-metrics=`
source metrics, `DOOM_FALLBACK` carries the number of functions that fell back
to the canonical emitter (a fallback is correct code, so the number is a
quality signal, not a failure), and `DOOM_RUN` carries the run's wall time and
instructions retired. The instruction counts come from the same Linux hardware
counter as `STEP_INSTRUCTIONS`, read either side of a child that runs alone,
so they follow this process tree only and are the number to trend; where no
counter is available the field is simply zero, which is never an error. One
recorded run put the Clang reference at 4,43 G instructions for the 480-frame
workload against 24,9 G for QUALITY, 28,3 G for FAST, 48,6 G for MIR_STACK and
54,7 G for NONE — the allocator ordering the names promise, with NONE 2,2x
QUALITY. The whole matrix takes about half a minute and leaves about 19 MB
under `build/doomgeneric-<pid>/`, which is not cleaned up on the way out.

A graphical backend is explicitly not part of this gate. The bugs this harness
found are reduced to `tests/basic_c_doom_shapes.c`, which the ordinary test
suite runs; the harness itself stays opt-in because it consumes an external
checkout and data that cannot be committed here.

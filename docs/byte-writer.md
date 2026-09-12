# Bounded CodeView and PDB writing

`src/buster/lib/byte_writer.h` defines a non-owning writer over a pre-reserved
span. CodeView and PDB retain their existing arena allocations and capacity
estimates; the shared layer neither grows nor frees storage. No callback or
function-pointer dispatch is involved. Object and machine-code writers remain
outside this experiment.

`byte_writer_make` starts at count zero. Each append, zero fill, patch or
four-byte alignment operation either writes its entire requested range or
leaves bytes and count unchanged. Failure sets the sticky `overflow` flag;
subsequent operations cannot change the buffer. Format-specific record-length
checks may set that same flag. Alignment uses a single bounded zero fill, so
insufficient capacity cannot strand PDB emission in an alignment loop.

The primitive checks use subtraction after validating the offset. Patches can
only address the written prefix, including an empty patch at its end; spare
capacity is not initialized output. Zero-length writes accept null data and do
not perform pointer arithmetic or call `memcpy`/`memset`. Nonempty copy spans
must not overlap. Integer writes and patches explicitly encode little-endian
bytes and require no alignment.

`byte_writer_commit` publishes a borrowed `ByteSlice` only when the writer is
valid; on failure it leaves the supplied output unchanged. It does not seal the
writer or transfer ownership, so the view keeps the backing storage's lifetime
and may observe subsequent successful writes. CodeView publishes its symbol
and type slices together after both streams succeed. PDB checks every stream's
failure state before constructing the MSF container and only publishes a valid
container after directory emission succeeds. Failed format results remain
invalid; no partial artifact becomes a successful result.

The registered `byte_writer_tests` exercise exact endian bytes, guard regions,
empty operations, all small alignment tails, patch ranges, huge lengths,
sticky errors and publication. Existing CodeView/PDB tests continue to decode
records, field-list continuations, source tables, scopes, contributions and MSF
page maps independently. For a full run:

```sh
./build.sh build --config Release -t test_all
./build.sh test_self_host --config Release
```

Performance admission for other consumers requires separate evidence. Measure
compiler-build cost and emitted writer code size alongside complete debug
compilation/linking workloads; a primitive append microbenchmark alone does not
establish a compiler-throughput gain.

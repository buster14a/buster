# Upstream license documents

These files accompany the [third-party notice inventory](../THIRD_PARTY_NOTICES.md).
They preserve upstream terms; they do **not** select a license for Buster's
first-party code. See [#620](https://github.com/buster14a/buster/issues/620)
for notice restoration and [#621](https://github.com/buster14a/buster/issues/621)
for the separate first-party decision and unresolved permission review.

## Provenance

Documents were retrieved on 2026-09-14. The first four are byte-for-byte
copies of the identified upstream files, including their original whitespace.
A license-document version is not a substitute for a missing import version.
The XCB entry is an explicitly identified source-header extraction, not a
copy of an entire XML file.

| Local file | Upstream source of the text | Local Git blob SHA-1 |
|---|---|---|
| [intel-xed-LICENSE.txt](intel-xed-LICENSE.txt) | [intelxed/xed LICENSE](https://github.com/intelxed/xed/blob/519c843c86547e2003f5a404a53358a7dcfb82f3/LICENSE), commit `519c843c86547e2003f5a404a53358a7dcfb82f3` | `7b1fcae7b322e9270a48a68ddc374870069f3533` |
| [llvm-LICENSE.txt](llvm-LICENSE.txt) | [llvm/llvm-project llvm/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/ca7933e47d3a3451d81e72ac174dcb5aa28b59d1/llvm/LICENSE.TXT), commit `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1` | `fa6ac540007032cbd0ec772a1c72e6cb5527a4fe` |
| [zig-LICENSE.txt](zig-LICENSE.txt) | [ziglang/zig LICENSE](https://github.com/ziglang/zig/blob/3c46da14db/LICENSE), the `3c46da14db` revision recorded by Buster's ABI prelude | `9ce01373c89082fd81e8ea0793aab0978140526b` |
| [xxhash-LICENSE.txt](xxhash-LICENSE.txt) | [Cyan4973/xxHash LICENSE](https://github.com/Cyan4973/xxHash/blob/v0.8.3/LICENSE), `v0.8.3`; license-source snapshot, not an asserted Buster import pin | `e4c5da7234eca7baad9a46f0b144e49ccad2e500` |
| [xcb-xproto-LICENSE.txt](xcb-xproto-LICENSE.txt) | [stapelberg/xcb-proto src/xproto.xml](https://github.com/stapelberg/xcb-proto/blob/master/src/xproto.xml), source XML blob `1ec25e6598d7cc104c81f098b72b6358019a4e9b`; lines 3-26, the complete copyright/license comment content without XML delimiters | `495004962e125aa5be350044adf259ef00184324` |

For the first four entries, the local blob IDs equal the upstream license
blob IDs. For XCB, `1ec25e...` identifies the whole source XML and `495004...`
identifies only the extracted license text; they are intentionally different.
The upstream source can also be retrieved by its immutable
[Git blob ID](https://api.github.com/repos/stapelberg/xcb-proto/git/blobs/1ec25e6598d7cc104c81f098b72b6358019a4e9b).
The original XCB manual-page revision remains unknown; no such revision is
inferred from the retrieval snapshot.

The XED copyright notice is retained in the root inventory from
[`datafiles/xed-isa.txt`](https://github.com/intelxed/xed/blob/519c843c86547e2003f5a404a53358a7dcfb82f3/datafiles/xed-isa.txt).
The LLVM source attribution is retained from
[`llvm/lib/Target/AArch64/AArch64InstrInfo.td`](https://github.com/llvm/llvm-project/blob/ca7933e47d3a3451d81e72ac174dcb5aa28b59d1/llvm/lib/Target/AArch64/AArch64InstrInfo.td).
The inspected paths `intelxed/xed:NOTICE` and
`llvm/llvm-project:llvm/NOTICE` were absent at the corresponding pinned
revisions. This records those specific checks, not a claim that no
per-file or component-specific notices can exist elsewhere upstream.

## Checking and updating copies

From the repository root, use Git's unfiltered blob hashing and compare the
results with the table above:

```sh
git hash-object --no-filters \
    LICENSES/intel-xed-LICENSE.txt \
    LICENSES/llvm-LICENSE.txt \
    LICENSES/zig-LICENSE.txt \
    LICENSES/xxhash-LICENSE.txt \
    LICENSES/xcb-xproto-LICENSE.txt
```

These are Git object identities, not release signing or verification that
an upstream source tree was the actual importer input. Upstream retrieval
and the importer manifests supply that separate provenance evidence.
When updating a source pin, re-read the source's actual per-file terms and
any applicable `NOTICE`, retain complete notices/exceptions, and update
both this table and the root inventory. Do not replace project-specific
texts with shorter SPDX-name-only files or generic templates.

Keep these upstream copies byte-for-byte: the XED file ends with a
whitespace-only line, and the LLVM file ends with an additional blank line.
Do not strip those merely to make a general whitespace checker silent.
The local `.gitattributes` preserves the `.txt` bytes across checkouts and
scopes the trailing/EOF-whitespace exceptions to those license copies.
Validate the authored documentation and verify the copied files by identity
rather than changing upstream text.

The Arm release terms are not among these open-source license copies.
The root inventory explicitly records the restricted inputs, derived
projections and unresolved permission review. Adding a placeholder license
or guessing an SPDX identifier would not resolve that review.

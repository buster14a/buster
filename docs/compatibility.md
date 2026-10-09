# Driver spelling compatibility

`ide cc` accepts the GCC/Clang spellings it can honor and refuses the rest with
`unsupported option: <spelling as typed>` or a named diagnostic. A refusal is
deliberate where listed here: no spelling is ignored silently when ignoring it
would change what the user asked for. Open requests are tracked on GitHub #1418.

## Deliberately rejected spellings

| Spelling | Result | Reason |
|---|---|---|
| `-nostdlib`, `-nostartfiles`, `-nodefaultlibs` when linking | `unsupported option: -nostdlib (...)` (first one named) | The link semantics (no C runtime start-up files or default libraries) are not implemented, so a link would still contain the runtime. With `-c`, `-S`, `-E` or `-fsyntax-only` nothing is linked and they are accepted and ignored, as GCC and Clang do. |
| `-nostdlib++`, `-nostdlibs`, `-nolibc` | `unsupported option` | Not GCC/Clang spellings of the above. |
| `-mfoo`, `-mno-foo` (a name no architecture has as a target feature) | `unsupported option: -mfoo` | `-m<feature>` and `-mno-<feature>` are aliases of `-mattr=+feature` and `-mattr=-feature` and take only the names `-mattr` takes. |
| `-mavx2` for a non-x86 target, or for a GPU target | `unsupported option: -mavx2` | The feature belongs to another architecture, or the external GPU pipeline has no feature overrides. |
| `-m32`, `-mred-zone`, `-mno-red-zone` | `unsupported option` | They name no target feature and have no implementation. |
| `-mAVX2`, `-mavx2=1`, `-m`, `-mno-` | `unsupported option` | Feature names are exact and lower case; `=` forms belong to `-march=`, `-mcpu=`, `-mtune=`, `-mattr=` and `-masm=`. |
| `-mno-avx2` on a set with AVX-512 (and similar) | `invalid target feature combination: ...` | GCC's implied-feature closure (`-mno-avx2` also dropping AVX-512) is not part of the alias; it fails exactly as `-mattr=-avx2` does. |
| `-xc++` and any other unknown joined `-x<lang>` | `unsupported language: c++` | Same language names as `-x <lang>`; C is the only source frontend. |
| bare `-x` with no operand | `missing argument after -x` | Same as GCC and Clang. |

See [the driver guide](agents/driver.md) for the accepted forms.

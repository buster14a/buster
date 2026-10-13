# Git local-operation qualification

[Harness index](../compatibility.md) · [#3180](https://github.com/buster14a/buster/issues/3180)

## Pin and configuration

The native opt-in `tools/git_qualification.c` uses pristine Git **v2.51.0**,
commit `c44beea485f0f2feaf460e2ac87fdd5608d63cf0`. It requires Linux
x86-64, a trusted Clang-built Buster `ide`, an independent Clang, GNU Make,
host binutils, glibc development headers/runtime and zlib development files.
Unmodified upstream shell/Perl generators and test infrastructure remain
external prerequisites. They are not Buster language frontends.

The declared first configuration uses upstream `make all`, `-O0 -g -Wall -fPIE` and `LDFLAGS=-pie`,
two workers, computed header dependencies, and the platform defaults from
`config.mak.uname`. It explicitly selects:

```text
NO_CURL=YesPlease NO_EXPAT=YesPlease NO_OPENSSL=YesPlease
NO_TCLTK=YesPlease NO_GETTEXT=YesPlease NO_PERL=YesPlease
NO_PYTHON=YesPlease COMPUTE_HEADER_DEPENDENCIES=yes
```

Local file transport, the normal Linux threading/platform paths, reftable,
SHA-1 collision detection, builtin SHA-256 and host zlib remain enabled.
HTTP(S), XML HTTP push, OpenSSL integration, Tcl/Tk GUIs, translations and
installed Perl/Python helper functionality are outside this configuration.
The PIE mode and those exclusions are declared before either independent build.
The explicit PIE flags align both compilers with the host executable link mode;
Buster non-PIE objects cannot be fed to a platform-default PIE link when they
take imported function addresses. Earlier non-PIE build-census receipts retain
their original configuration and do not become PIE qualification evidence. Test scripts can still need host Perl.

This is a first build/blocker census. It is not whole-project qualification.
The [retained first census](../../compatibility-evidence/git-v2.51.0/README.md)
records actual outcomes and explicit unrun gates. A separate depfile-candidate follow-up gets past the option gate and records
333 successful C actions with 199 source refusals. Latest revisions, runs and
blocker ownership belong on #3180; neither result qualifies the whole project.

## Licenses and external input boundary

The inspected primary sources below are all at the selected Git commit.

| Input | Verified license/source |
|---|---|
| Git | GPL-2.0-only, with file-specific exceptions; [COPYING](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/COPYING) |
| SHA-1 collision detection | MIT; [sha1dc/LICENSE.txt](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/sha1dc/LICENSE.txt) |
| Reftable | BSD-3-Clause; [reftable/LICENSE](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/reftable/LICENSE) |
| Obstack compatibility code | LGPL-2.1-or-later; [compat/obstack.c](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/compat/obstack.c) |
| EWAH bitmap code | GPL-2.0-or-later; [ewah/ewah_bitmap.c](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/ewah/ewah_bitmap.c) |
| LibXDiff | LGPL-2.1-or-later; [xdiff/xdiff.h](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/xdiff/xdiff.h) |
| Clar unit-test framework | ISC; [t/unit-tests/clar/COPYING](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/t/unit-tests/clar/COPYING) |
| Bundled regex, if selected by upstream platform policy | LGPL-2.1-or-later; [compat/regex/regex.c](https://github.com/git/git/blob/c44beea485f0f2feaf460e2ac87fdd5608d63cf0/compat/regex/regex.c) |
| Other Git source/test fixtures | Covered by the upstream repository's main grant unless a file supplies different terms; this is not a binary redistribution clearance or a complete per-file exception audit. |
| Host zlib and glibc | Package copyright files are retained before execution; respectively Zlib and LGPL terms with component exceptions. Exact package versions are recorded by `dpkg-query`. |
| Clang | Apache-2.0 WITH LLVM-exception; Buster's [LLVM license document](../../../LICENSES/llvm-LICENSE.txt) records that grant. |
| Buster | First-party license unselected; [LICENSES/README.md](../../../LICENSES/README.md) and #621 retain that decision. |

Nothing is vendored or installed as a new Buster runtime dependency. Evidence
uploads retain logs, provenance and dependency records; they do not ship Git
objects, executables or test fixtures. Further configuration/redistribution
requires its own component and dependency intake.

## Running the bounded census

Build the trusted compiler with the existing native build driver first. On an
authorized Linux cloud host, from the Buster repository root:

```sh
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  tools/git_qualification.c -o /absolute/path/git-qualification-cc
/absolute/path/git-qualification-cc --self-test
mkdir /absolute/path/fresh-git-evidence
/absolute/path/git-qualification-cc --run \
  /absolute/path/pristine-git \
  /absolute/path/buster/build/Release/ide \
  /absolute/path/clang \
  /absolute/path/fresh-git-evidence
```

The input checkout must be at the exact pin, with no tracked modifications,
untracked/ignored inputs or index skip/assume-unchanged flags. The harness
checks it before and after execution and builds independent disposable
clones. Its input controls require tracked-dirty, untracked, ignored, wrong-pin,
assume-unchanged and skip-worktree cases to fail, then verify restoration.
Separate process controls require exit 7 and timeout 124 to retain those outcomes. The CC wrapper receives the normal Makefile arguments unchanged.
Buster C/query actions use `ide cc -target x86_64-linux -march=baseline`;
object/archive-only links explicitly use host Clang. The Clang cell compiles
and links independently. No failed Buster C action falls back to Clang.

`make -k` retains independent compilation failures rather than stopping the
census at the first one. `*-actions/*.action` records compiler, working
directory, exact shell-quoted argument vector, stage and exit status.
Compilation actions and distinct input sources are separate denominators.
A missing final status means incomplete execution, not success.

`summary.tsv` and `commands.tsv` record stage outcomes and logs. Each build
has a 900-second process-group deadline; unit/integration stages have
300-second deadlines. Exit 124 is a timeout. Nonzero/signalled/incomplete
builds cannot enter runtime testing. The result stays nonzero on either
reference or candidate build/test failure. Original input dirtiness also
fails the campaign. Hosted elapsed time is explicitly diagnostic.

For a successful full build, the harness runs all upstream C unit programs
through `make -C t unit-tests`, plus this **bounded six-script** integration
inventory through the normal upstream test Makefile:

```text
t0000-basic.sh t0001-init.sh t1500-rev-parse.sh
t2000-conflict-when-checking-files-out.sh t5601-clone.sh t5300-pack-object.sh
```

Retain TAP logs and upstream `t/test-results/*.counts` for attempted,
passed, failed and prerequisite skips. Unattempted scripts remain outside
this denominator. A reference failure or skipped prerequisite must remain
visible. An upstream unit-test assertion failure cannot be turned into a
successful compiler or runtime result.

The native operation witness uses new private repositories with fixed author,
committer, dates and configuration. It checks a three-stage unmerged index
after an expected conflict (exit 1), resolves that conflict, creates a successful
non-fast-forward merge, pushes to a private bare remote, clones it locally,
pushes another commit and fetches/fast-forwards the original. It verifies the
literal final file contents, runs repack and full fsck, and requires a missing
reference to fail with exit 128. Both cells' final commit and tree identities
must match; arbitrary pack bytes are never compared. These checks are attempted
only after a full build succeeds. Their implementation is not execution
evidence.

The hosted workflow records machine specifications and actual checkout
identity and retains diagnostics even when the census fails. The main-only external-project workflow runs this bounded configuration after
main pushes. Ordinary PR and merge-group checks do not run whole Git
qualification; the standalone Git workflow remains an opt-in diagnostic route.

## Remaining milestone gates

Build-census success alone does not satisfy #3180. Qualification still needs:

- The complete applicable upstream integration inventory and exact skip reasons.
- A successful execution of the implemented deterministic operation witness
  with both builds, including its paired commit/tree comparison.
- Complete selected header/generated-input/dependency and link closure.
- Clean/source-edit/header-edit rebuilds, checked operation runtime, code/debug
  size and defined memory accounting; debug/debug first, optimized gap separate.
- Applicable exact-candidate/workload/configuration Ryzen 7 9700X evidence
  through the approved existing comparison route. Hosted timings cannot satisfy
  performance acceptance.
- The useful developer workflow, affected checks and self-host preservation.

The normal dependency-enabled build needs the driver contract tracked in
[#2302](https://github.com/buster14a/buster/issues/2302). Retain `-MF/-MQ/-MMD/-MP`
when retesting; removing them cannot qualify this incremental-build configuration.

Report reduced compiler failures to existing driver/frontend/ABI/backend
owners; do not expand this harness branch into a competing #3102/#56
implementation or modify native-retirement authority.

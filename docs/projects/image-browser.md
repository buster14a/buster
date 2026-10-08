# Native image browser and inspector

[Catalogue](../../PROJECTS.md) · [Media contract](media.md) ·
[Graphics contract](graphics-ui.md) · Area: `image-browser`

## Workflow and first platform

`image_browser <directory-or-image>` opens one flat directory snapshot, or an
explicit image and its neighboring supported-extension files. Inspect decoded
image metadata in the window title and terminal; navigate, pan and zoom the
image in the native window. The first slice is **Linux x86-64, XCB TrueColor,
CPU presentation**. Xvfb is a real X server for validating this backend.
Other platforms, Wayland, Vulkan and GPU acceleration are not product support
claims. The opt-in graph rejects unsupported platform selections and the single-threaded
runtime configuration (`BUSTER_SINGLE_THREADED` must remain `OFF`).

The target reuses `image`, `window`, `rendering_raster`, arenas, entry points
and the existing OS/lane services. Its bounded catalog and persistent loader
are specific to this workflow. No UI/font framework, editor or unused media
module is attached to the headless compiler. No dependency or vendored code
is added.

## Launch

Use CMake/Ninja, Clang, the repository's normal TCC bootstrap prerequisite,
a running X server, and the existing Linux window libraries/development headers:
`xcb`, `xcb-imdkit`, `xcb-util`, `xcb-keysyms`, `xcb-xkb`, `xkbcommon-x11`,
and `xkbcommon`. The current module graph already owns these dependencies;
Xvfb/xauth are hosted validation tools, not application libraries. See the
[platform guide](../agents/platform.md). From a checkout containing the target:

```sh
./build.sh generate --cc clang --config Release --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_BUILD_IMAGE_BROWSER=ON -DBUSTER_INCLUDE_TESTS=OFF
./build.sh build --config Release -t image_browser
build/Release/image_browser /absolute/path/to/images
```

`generate` recreates the selected build directory; finish other work using it
first. A file path is accepted in place of the directory. No file-picker dialog,
recursive scan, live directory watching or drag-and-drop is implemented.
Restart to refresh directory membership; `R` reloads the selected snapshot path.
The initial bounded catalog scan runs before the window opens; it has no
wall-clock deadline. Subsequent encoded-file reads and decoding use the worker.

| Input | Behavior |
|---|---|
| Left/Right or Page Up/Page Down | Previous/next file |
| Mouse wheel, `+`/`-` | Zoom around pointer/viewport centre |
| Left-button drag | Pan |
| `F` | Fit oriented image |
| `1` | One image pixel per viewport pixel |
| `R` | Reload selected file |
| Escape or native close | Stop loader, release storage and close |

## Formats and honest errors

The actual decoder accepts PNG, sequential 8-bit JPEG (SOF0/SOF1), GIF first
image, BMP, TGA, QOI, and PNM/PAM. Extension filtering is only navigation
admission; the decoder verifies the contents. An explicit unsupported file can
be attempted and receives a diagnostic. Progressive JPEG, CMYK JPEG, animation
playback and recognized-only formats remain unsupported. Metadata reports
format, encoded dimensions, channel/precision/alpha facts, orientation and
frame/color flags where the decoder supplies them. It does not promise every
Exif field, a full metadata tree or arbitrary embedded profiles.

All eight reported orientations are sampled during display without a second
oriented pixel allocation. Alpha is composited over a checkerboard with nearest
sampling. ICC/gamma color conversion is absent. Loading hides the old image
while retaining its owned allocation until replacement: a stale image is never
shown under a newer selected file's metadata. Errors replace current content
with an explicit status, including read errors, unsupported features, decoder
capacity/work limits and failed resource admission.

## Ownership, limits and responsiveness

Only the main thread mutates application state. One persistent OS worker loads
one active request; the state keeps only the latest pending generation. Source
bytes are an immutable, worker-owned copy. Output/scratch arenas are distinct,
bounded and committed at creation so allocation failure can be reported before
decoding. Decoding uses the existing one-lane path. Source and scratch are
released before synchronized result handoff; a successful result owns only its
output arena. The presenter borrows pixels and canvas within each call.

Navigation supersedes older work. Reads check cancellation between chunks;
synchronous decoder work has no cancellation hook, so it finishes within its
admitted byte/work limits. The main loop remains active meanwhile. Stale results
are released rather than published. Shutdown forbids new publication, signals
stop, joins the worker, consumes/releases its final result, then destroys image,
presentation, window and arena resources. Ordinary filesystem I/O has no
wall-clock cancellation guarantee; a stalled regular-file read can delay join.

The application owns one retained, bounded canvas for its lifetime. New image
generations, viewport changes and canvas-size changes rerasterize it. XCB expose
events request presentation from those retained pixels, so uncovering a window
does not need to resample the image. With no content change or redraw event, the
loop does not schedule a periodic raster or presentation.

| Application-owned resource | Admission |
|---|---|
| Encoded copy | 32 MiB; regular files only; bounded read chunks |
| Decoded output | 32 MiB, at most 8,388,608 pixels; each axis at most 8192 |
| Decode scratch | 96 MiB plus arena overhead |
| Live decoded images | One published image and one active/completed replacement |
| Canvas | 64 MiB; each viewport axis at most 4096 |
| Event arena | 32 MiB committed; at most 32 native events per poll |
| Catalog | 4096 entries, 1 MiB path bytes, paths up to 4095 bytes, 32,768 scanned records; overflow is an error |
| Zoom | 1/64× to 64× |

The listed buffers sum to about 289 MiB at their simultaneous maxima, plus
arena overhead. They are owned-buffer limits, not an exact process RSS promise: thread stacks,
OS/XCB/XKB/XIM allocations and X-server storage are separate. Each arena has
small explicit overhead. The existing OS worker reserves an 8 MiB stack and
creates two default scratch arenas (256 MiB virtual reservation each, initially
256 KiB committed each); the explicit image scratch arena handles decoding.
One-lane dispatch creates no additional lane gang. Runtime startup and native
library allocation keep their existing failure contracts; thread-context arena
creation is fail-stop, so graceful recovery from every process-wide OOM is not
promised. A failed join retains ownership and is retried once; a second failure
stops the process before releasing storage a live worker may still access.
The browser opts out of native file drops; bounded
polling alone would not cap the retained XDND negotiation path. It retains no
event/path pointers after resetting the event arena. Leaf symlinks and special files
are refused by the loader. The loader rejects changes observed by its before/after
file-stat checks and decodes an immutable byte copy; these checks do not guarantee
an atomic filesystem snapshot.

## Validation

The hosted native workflow independently separates `test_rendering_raster`
(headless pixel/orientation/admission goldens), `test_image_browser_state`
(headless transitions, real filesystem/worker handoff, join-failure retry and
owned-buffer lifetimes), `test_window` (display-free window backend seams such
as URI decoding, file-drop budgets and XIM style advancement), and actual XCB
execution.
`test_rendering_raster_native` reads server pixels and exercises resize, events,
bounded polling and repeated shutdown. `test_rendering_raster_no_display`
checks recoverable unavailable-display failure.

`test_image_browser_native` runs the actual application with the hand-authored
`src/buster/tests/image_browser/fixtures` directory and `--smoke`: it loads and presents both files,
checks decoded pixels/metadata and server readback, sends native XCB key,
wheel, drag and close events, clears the native window to require an XCB expose
repaint from the retained canvas, verifies that repaint did not rerasterize,
resizes the native window and checks the new canvas readback, then shuts down
the persistent loader. The headless state suite separately counts requested
raster and presentation work across idle, content-change and repaint-only
transitions. Smoke is available only in test builds (`BUSTER_INCLUDE_TESTS=ON`).
The hosted Release/unity lane separately compiles the production graph with
that option `OFF` and checks its CLI.
The smoke's native protocol events exercise the actual event loop. Physical
desktop interaction remains an unexecuted graphical gate. A headless success is not native rendering evidence. Run the native gates under
an X server; for this CPU backend, `xvfb-run -a ./build.sh build --config Release
-t test_image_browser_native` is sufficient. Exact revisions, commands and
results belong on #2179 and its component/application PRs. Physical desktop
interaction and other platform/backend graphical gates remain unexecuted unless
separately recorded.

The first complete hosted slice passed all four native configurations in
[run 36936514171](https://github.com/buster14a/buster/actions/runs/36936514171),
head `0dc876f37ff25a037bfa63eff247ce7f23c9680f`, actual merge source
`47cac4b4120b1812fc3d5aab6ec97b688f78ba25`: Clang Debug/split, Clang
Release/unity, sanitized Clang Debug/split and GCC Release/split. Each passed
70/70 real-worker ownership assertions, state tests with zero failures,
the native browser input/readback/joined-shutdown gate, and the separate raster
CPU/native/unavailable-display gates (99/99, 180/180 and 1/1). Release/unity
also compiled the tests-OFF production target and checked its CLI. This is
software XCB evidence under Xvfb; subsequent exact-head and merge-group
validation is recorded on [the application PR](https://github.com/buster14a/buster/pull/2213).
Hosted binaries use the build system's host CPU flags and are not a portable
ISA promise; build the source target for the selected Linux/XCB host.

## Provenance and licenses

All new implementation and pattern assets are project-authored. The encoded
fixture bytes and Git identities are documented in
[fixture provenance](../image-test-fixtures.md). No external implementation,
image corpus, package or codec is imported. Buster's first-party license remains
unselected: see [license status](../../LICENSES/README.md) and #621. Existing
`LICENSES/xcb-xproto-LICENSE.txt` is a verified permissive xproto source-header
extraction; it is not asserted to license the complete installed XCB stack.

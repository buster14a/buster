# Native image browser and inspector

Status: **removed**. The image-browser application, Linux loader and state
sources, headless tests, PPM smoke fixtures, and `cmake/ImageBrowser.cmake` are
no longer in the tree. There is no `image_browser` target or current launch
command. Shared image, raster and window libraries remain unmaintained,
outside the compiler build and automatic CI.

[Catalogue](../../PROJECTS.md) · [Media contract](media.md) ·
[Graphics contract](graphics-ui.md) · Area: `image-browser`

## Historical workflow and validation

The former Linux x86-64/XCB application browsed one flat directory snapshot,
loaded bounded immutable image copies on a persistent worker, and presented
CPU-rasterized pixels with pan, zoom and orientation handling. It did not
provide GPU acceleration, Wayland support, animation playback or color
management. Headless tests and XCB server-pixel readback were distinct checks;
physical desktop interaction was a separate unexecuted gate.

The [last pre-removal guide](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/docs/projects/image-browser.md)
retains the detailed controls, resource/lifecycle limits and revision-specific
hosted results. The [application sources](https://github.com/buster14a/buster/tree/02a6414e020f5d549122197c3a7c8407df5bed76/src/buster/apps/image_browser)
and [fixture provenance](../image-test-fixtures.md#native-image-browser-fixtures)
identify the deleted implementation and test inputs. Historical build and smoke
commands require that checkout and are not current-tree instructions.

The `image-browser` Area ID remains for historical issue routing. A future
consumer needs an explicit owner decision, restored application/build/test
integration and fresh native validation before it is supported.

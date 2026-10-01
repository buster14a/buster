# Media components

[Catalogue](../../PROJECTS.md) · Area: `media` · [Tracking](../project-tracking.md)

## Purpose and entry points

Provide dependency-free raster-image recognition, metadata probing and decoding
from untrusted, bounded memory. The public entry point is
[image.h](../../src/buster/lib/image.h); codec implementations live under
[image/](../../src/buster/lib/image/). The library accepts a `ByteSlice`, not a
path or stream. Callers that start from a file compose it with the platform file
API and retain responsibility for file-size policy.

| Stable feature ID | Entry point | Observable boundary |
|---|---|---|
| `media.images.detect` | `image_detect_format` | Signature-first recognition only. A recognized signature is not structural validation, and TGA is a conservative last-resort header match because it has no reliable magic value. |
| `media.images.probe` | `image_probe` | Returns bounded dimensions, source layout, image-count/orientation and color-metadata summaries without allocating a pixel buffer. Probe applies the codec's metadata validation but is not a promise that every decode-only entropy path is valid. JPEG probes require the EOI marker and traverse byte-stuffed entropy envelopes to reach later marker metadata without Huffman-decoding coefficients or validating entropy padding and restart cadence. |
| `media.images.decode` | `image_decode` | Validates and decodes one canonical, arena-owned image. Status, encoded-byte error offset and limit/feature detail distinguish malformed, truncated, unsupported, checksum, resource-limit and arena-capacity failures. |
| `media.images.orientation` | `image_apply_orientation` | Explicitly allocates a copy in any of the eight Exif display orientations. Decode itself never applies Exif orientation. |

`ImageDecodeOptions.format_hint` selects a codec explicitly. This is most useful
for TGA, but it does not turn an unsupported format into a decoder. With no hint,
the signature detector selects the codec; unknown data returns
`IMAGE_DECODE_UNRECOGNIZED_FORMAT`, while a recognized format without a decoder
returns `IMAGE_DECODE_UNSUPPORTED_FORMAT`.

A minimal decode uses the zero-valued defaults and checks the structured status:

```c
ImageDecodeResult decoded = image_decode(arena, encoded, (ImageDecodeOptions){0});
if (decoded.status == IMAGE_DECODE_SUCCESS)
{
    // decoded.image.pixels is arena-owned, tightly packed RGBA8.
}
```

## Canonical image and metadata contract

A successful decode returns tightly packed RGBA8: four bytes per pixel in red,
green, blue, alpha order, `stride == width * 4`, top row first, and no row padding.
Alpha is straight, never premultiplied. Sources without alpha produce 255 in the
alpha byte. BMP and TGA storage origins are normalized to top-left as part of
decoding.

Exif orientation, when present in a supported source, is reported as metadata
and leaves the decoded sample grid unchanged. Callers that need display order use
`image_apply_orientation`; orientations 5 through 8 exchange output width and
height. The helper produces a new image and does not mutate its source.

Source channel count, maximum stored channel precision, color model, alpha
presence, frame/image summary, orientation and the presence of recognized color
metadata are descriptive facts about the encoded input. Indexed sources report
their index width instead. QOI's channel header is purely informative, so its
source channel count and color model preserve that header while `has_alpha` also
reports nonopaque samples found in the validated opcode stream. ICC, sRGB, gamma,
chromaticity, CICP, Exif and Adobe transform flags do not expose or apply profile
payloads. QOI's colorspace byte is reported separately. PNG streams containing
multiple color-space chunks report every recognized presence flag; the PNG-defined
precedence between those chunks is left to a color-management layer. The decoder
performs format-required sample unpacking and JPEG component conversion, but no ICC
conversion, gamma correction, gamut mapping or other color management. Output
samples therefore remain in the source's encoded color interpretation, normalized
to eight-bit RGBA by the selected codec.

`frame_count` follows the source format rather than pretending every multi-image
grammar has the same timeline model. Static images and the first image returned
from a concatenated PNM stream report one; GIF reports its validated image
descriptor count; APNG reports the `acTL` animation-frame count, which can
exclude a separate default image. `has_more_images` separately says that the
canonical pixels are only the first selectable/default image returned by this
API.

## Decoded-format matrix

| Format | Decoded scope | Boundary that remains explicit |
|---|---|---|
| PNG | All legal grayscale, truecolor, indexed, grayscale-alpha and RGBA color-type/bit-depth combinations; PLTE/tRNS; all five filters; split IDAT; stored, fixed and dynamic DEFLATE blocks; zlib/Adler-32 and chunk CRC validation; non-interlaced and Adam7 images. Out-of-range palette indexes recover as opaque black, and unused bytes after the zlib stream are ignored as required by PNG. | Unknown critical chunks and invalid chunk ordering are rejected. Ancillary color/profile information is metadata only. APNG control chunks do not provide animation-frame selection; only the ordinary IDAT/default image can be returned. |
| JPEG | Eight-bit Huffman sequential DCT in SOF0 and SOF1, with one or three components, 8- or 16-bit quantization tables, interleaved or component scans, restart markers, staged fixed-point IDCT, grayscale/RGB/YCbCr conversion, JFIF/Exif and supported integral sampling ratios. Common 2:1 horizontal and 2:1-by-2:1 chroma reduction uses reference-compatible centered reconstruction; other accepted integral ratios use sample replication. | Progressive, arithmetic-coded, lossless, differential, non-eight-bit and four-component CMYK/YCCK decoding are unsupported features. Exif orientation is report-only. |
| GIF | GIF87a/GIF89a, global/local color tables, transparency, interlace and LZW. The returned image is the first image descriptor composited on a transparent-black logical-screen canvas; a transparent index leaves the canvas unchanged, and the declared background color is not painted. Later image streams are validated and counted. | There is no frame/timeline, delay, loop or disposal playback API. Animation metadata does not imply that later frames can be requested. As a tolerant interoperability extension, a descriptor extending past the logical screen is validated but clipped during composition; GIF89a otherwise requires it to fit. |
| BMP | `BITMAPCOREHEADER` and Windows DIB sizes 40, 52, 56, 108 and 124; 1/4/8-bit indexed pixels; 16/24/32-bit direct pixels; `BI_RGB`, RLE4, RLE8, `BI_BITFIELDS` and `BI_ALPHABITFIELDS`; bottom-up and supported top-down rows. | Embedded JPEG/PNG, CMYK encodings, OS/2 variants beyond the supported core header and other compression modes are unsupported. RLE gaps are transparent in the canonical canvas. As a tolerant interoperability extension, an RLE bitmap with zero `biSizeImage` is bounded by `bfSize` or the supplied byte slice; a nonzero value is enforced as the compressed-raster boundary. |
| TGA | Color-mapped, truecolor and grayscale images; raw and RLE image types; 8/16-bit indexes, 15/16/24/32-bit color-map or truecolor samples and 8/16-bit grayscale samples; horizontal and vertical origin normalization. TGA 2.0 Attributes Types distinguish ignored, retained and useful straight-alpha data. | TGA has no dependable signature, so explicit format hints are preferred. Unsupported interleave modes and declared premultiplied-alpha semantics are rejected rather than silently returned as straight alpha. Extension/developer data is not a public metadata API. |
| QOI | RGB and RGBA headers, both colorspace values, every QOI opcode and the exact terminal marker. Probe and decode both validate the complete opcode stream. | QOI channel and colorspace tags are descriptive only; an RGB header may legally encode alpha changes, which are preserved and reported. Extra encoded data is rejected. |
| PNM/PAM | Netpbm P1 through P6 in ASCII or binary form, comments, PBM/PGM/PPM, maximum values through 65535, and P7 PAM tuples for black-and-white, grayscale, RGB and their alpha variants. | The API returns the first image and reports when another concatenated image is present; it has no image-selection API. Unknown PAM tuple types are unsupported rather than guessed. PFM is recognized separately but not decoded. |

Sample values, table references, compressed-stream termination, declared lengths
and checksums are validated within each supported codec, subject to the format's
required decoder-recovery rules. A
syntactically recognized variant outside the table's decoded scope returns
`IMAGE_DECODE_UNSUPPORTED_FEATURE` rather than being misclassified as an unknown
file.

## Recognized but unsupported formats

The detector assigns these families a stable `ImageFormat`, but `image_probe`
and `image_decode` return `IMAGE_DECODE_UNSUPPORTED_FORMAT` until a bounded codec
is added:

| Family | Recognized forms |
|---|---|
| WebP | RIFF/WEBP |
| TIFF | Classic little- and big-endian TIFF, plus BigTIFF |
| ICO/CUR | Structurally plausible icon and cursor directories |
| ISO base media images | AVIF and HEIF/HEIC brands, with AVIF taking precedence when a bounded compatible-brand scan contains both |
| JPEG XL | Codestream and container signatures |
| JPEG 2000 | Raw codestream and JP2 container signatures |
| Adobe/film images | PSD, OpenEXR and Radiance HDR/RGBE |
| Texture containers | DDS, KTX 1 and KTX 2 |
| Netpbm floating point | PFM (`PF` and `Pf`) |

Recognition is deliberately bounded. It does not imply that a complete file was
parsed, that a filename extension agrees, or that a future decoder will accept
every feature in the family.

## Resource and arena policy

All zero-valued public numeric options select these defaults:

| Option | Default | Meaning |
|---|---:|---|
| `max_width` | 16,384 | Maximum decoded width in pixels. |
| `max_height` | 16,384 | Maximum decoded height in pixels. |
| `max_pixels` | 67,108,864 | Maximum `width * height`. |
| `max_decoded_bytes` | 268,435,456 | Maximum canonical RGBA8 output size. |
| `max_work` | 1,073,741,824 | Maximum codec-charged work units; this is deterministic accounting, not elapsed time. |
| `max_frames` | 4,096 | Maximum declared or encountered image/frame records. |
| `max_chunks` | 16,384 | Maximum PNG chunks. |
| `max_segments` | 16,384 | Maximum JPEG marker segments. |
| `max_blocks` | 16,777,216 | Maximum codec structural units: DEFLATE blocks, JPEG 8x8 blocks, GIF data/extension blocks, RLE packets or QOI opcodes. |
| `max_scans` | 4,096 | Maximum JPEG scan records. |
| `max_depth` | 64 | Maximum explicit parser nesting. Current codec grammars are iterative and do not consume this budget. |

Callers may set a lower or higher nonzero value, subject to checked arithmetic,
arena reservation and codec structural bounds. Structural counters are charged
at the record offset before the record is processed; a probe charges only paths
it actually validates, while decode additionally charges entropy/pixel records.
QOI probe validates and charges its complete opcode stream so that actual alpha
presence and structural limits are identical between probe and decode.
A limit failure can identify width, height, pixel, decoded-byte or work accounting,
and codec guards can name frames, chunks, segments, blocks, scans or nesting depth
when applicable. The
reported observed and allowed values are diagnostics; callers should branch on
the status and limit identity rather than an error-message string.

`image_decode` is transactional with respect to its output arena: on every
failure it restores the entry position and returns an empty `Image`. A capacity
failure means the selected arena could not admit an allocation; it is distinct
from a configured input/resource limit. Output storage remains owned by the
caller's arena on success.

Codecs that need working memory use `ImageDecodeOptions.scratch_arena` when one
is supplied. Otherwise they use a conflict-aware scratch arena from the selected
Buster thread, falling back to temporary space in the output arena when no thread
context is available. Decoder-created scratch allocations are released before
return on both success and failure; the fallback preserves successful output.
Callers must keep the encoded bytes readable for the duration of the call and
must not concurrently mutate either arena.

## Validation and follow-ups

The registered `image_tests` module covers format recognition, unsupported-format
classification, exact RGBA fixtures, probe/decode metadata agreement, all eight
orientation transforms, every strict fixture prefix, configured limits, checksum
and malformed-stream failures, output-capacity failures and transactional arena
rollback. The shared fuzz entry sends bounded inputs through `image_decode` when
fuzzing is enabled. Run the focused suite after building `ide`:

```sh
./build.sh build --config Release -t ide
build/Release/ide test --ci=1 --module=image_tests
```

New codec coverage should retain fixture provenance and hashes, test probe and
decode independently, exercise every supported mode plus nearby rejected modes,
and compare decoded pixels with an independently named reference implementation.
Security validation includes truncation at structural boundaries, deterministic
work/capacity limits, malformed entropy/table indexes, integer-overflow edges and
sanitizer/fuzz runs. Record the exact revision and the configurations actually run
on the owning PR rather than turning this page into a live result log.

[Issue #1834](https://github.com/buster14a/buster/issues/1834) owns the reader
capability. Shared bounded-parser primitives are tracked in
[#1840](https://github.com/buster14a/buster/issues/1840). Progressive JPEG,
animation-frame/timeline APIs, multi-image selection, color transforms and
decoders for the recognized-only families remain separate capability additions;
signature recognition is not a commitment to ship those features together.

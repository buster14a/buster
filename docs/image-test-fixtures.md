# Image test fixture provenance

[Media component](projects/media.md) ·
[Test source](../src/buster/tests/image_test.c)

## Scope and provenance

The tables below identify every encoded byte array referenced by
`image_test_fixtures` and `image_test_advanced_fixtures`. The SHA-256 values
cover the encoded bytes in the named C array, in order. They do not cover the
human-readable `image_test_expected_*` RGBA arrays, short format-detection
signatures, or byte-level derivatives assembled inside `image_tests`.

These are project-created fixtures, not files copied from a third-party corpus:

- PNG, BMP, TGA, QOI, PNM and PAM bytes were assembled with a small ad hoc
  Python 3.12 encoder. The PNG compatibility cases also used Python's `binascii`
  and `zlib` modules.
- JPEG and GIF bytes were produced with Pillow 12.3.0 or controlled byte-level
  edits of that output. `image_test_gif_no_initial_clear` is the exception: its
  clear-free LZW payload was packed by hand and is explained beside the array.
- The original ad hoc generation code is not retained. The arrays and hashes
  are therefore the durable byte identity; exact structural offsets called out
  in the test source are part of the fixture contract.

Decoded RGBA was compared with implementations independent of Buster: Pillow
12.3.0 for PNG, JPEG, GIF, BMP, TGA and PNM; ffmpeg for QOI; and ImageMagick for
16-bit PAM. The nonuniform JPEG precision oracle additionally records Pillow's
libjpeg-turbo 6.2 backend beside `image_test_expected_jpeg_precision` because
IDCT rounding and chroma reconstruction are observable there.

## Encoded byte identities

### Primary fixtures

| Encoded array | Bytes | SHA-256 |
|---|---:|---|
| `image_test_png` | 77 | `ad6d7f36424f03e0895e2c51cfcf38ac4b8e9aa0023d0c253a736e57ad487cb8` |
| `image_test_png_reserved_ancillary` | 80 | `59c33b1f0226f3051b99393fcc66f542447320eefe41074a3e67587ead4870de` |
| `image_test_png_trns_high_bits` | 82 | `f3ce86eaf7ba5f4e7085c2aecb4a2b4eb384f3eb3fda5d62bcf512e410c5552a` |
| `image_test_jpeg` | 333 | `0564ca7355c5c82a56913054971a4caba567e6ebfb7c1661331187926ae18f88` |
| `image_test_gif` | 46 | `772e54b448def4b4322aaa0c8b80cf5cf19573c12ee6625577c9c324fa79275d` |
| `image_test_bmp` | 70 | `e3e4fbdc25d4d44234ea6fe3fc856cd0587c84def9c33981430550454936931f` |
| `image_test_tga` | 34 | `37deba99c9509c36be79a139cb5905c666e2bd80013b3e1d5c1b6a38b76fae82` |
| `image_test_qoi` | 42 | `0cd4c66bebd133b86f364f330b257ef62488079f6cedfd6dc57a7635336d4dbe` |
| `image_test_pnm` | 45 | `b835561b83193431190345c53d8c91132fc9d0291248e0291fcef3593aa0494b` |

### Advanced fixtures

| Encoded array | Bytes | SHA-256 |
|---|---:|---|
| `image_test_png_indexed_adam7` | 117 | `a0fa009b9d1bc78018602dd350d483ff6fe091b7b507f56b81e6eda98c8fcf54` |
| `image_test_jpeg_color_420` | 288 | `1b81549e775e3d8b225fee0a931a49c2a843a49cff6135418760fe5e641dcdd6` |
| `image_test_jpeg_precision` | 681 | `0b8aa02f9e305f68a36c9692bd1a1430ad9cae8339e3ebaa282d2a56787ac080` |
| `image_test_gif_animation` | 113 | `8372a3c176a091d764140fadfead48b51458d06d4a201821e6fa59a71cb7c012` |
| `image_test_gif_clipped` | 46 | `8a91694a2328a922ac715dd6ffcfa74d9a50d98b7020cb237dc1124286d6b361` |
| `image_test_gif_no_initial_clear` | 45 | `415b86cc5bc9c57fb588f0f258b54e81fdca665248b4db3d54484b0cc66b106a` |
| `image_test_bmp_rle8` | 88 | `c689334478fc795333762a73a3c6e27b81822c48d5f12f76884f4c6bcfddac82` |
| `image_test_tga_rle_top_right` | 38 | `84e1d74efd8da9b2ab8b8caea0d47d280d8c248eb32d39620a21f7b4e9271fd9` |
| `image_test_qoi_mixed_ops` | 32 | `20803f25b0686b6b16932b21d8062f93f06929ca91fc3840694fbdc2ca341a66` |
| `image_test_pam_rgba16` | 99 | `2a20f54d38eb1b54996d61357f2d8fda349d67d22abc74832386a9d0ce5c2993` |

## Verification and updates

Run this from the repository root to derive the byte counts and hashes directly
from the two fixture tables and their numeric C initializers:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib
import re

source = Path("src/buster/tests/image_test.c").read_text()
fixtures = []
for table in ("image_test_fixtures", "image_test_advanced_fixtures"):
    match = re.search(rf"{table}\[\] = \{{(.*?)\n\}};", source, re.S)
    if not match:
        raise SystemExit(f"missing fixture table: {table}")
    fixtures.extend(re.findall(
        r"\{\s*(image_test_\w+),\s*sizeof\(\1\)", match.group(1)))
for fixture in fixtures:
    match = re.search(
        rf"u8 const {fixture}\[\] = \{{(.*?)\n\}};", source, re.S)
    if not match:
        raise SystemExit(f"missing encoded array: {fixture}")
    values = [value.strip() for value in match.group(1).split(",")
              if value.strip()]
    if not all(re.fullmatch(r"(?:0x[0-9a-fA-F]+|[0-9]+)", value)
               for value in values):
        raise SystemExit(f"non-numeric initializer in {fixture}")
    encoded = bytes(int(value, 0) for value in values)
    print(f"{fixture}\t{len(encoded)}\t{hashlib.sha256(encoded).hexdigest()}")
PY
```

When an encoded fixture must change, preserve or replace its documented
producer, compare its decoded RGBA with the named external implementation, run
the focused `image_tests` module, and update the corresponding byte count and
hash in the same commit. Pixel equivalence alone is insufficient when a test
also relies on exact encoded offsets or packet structure.

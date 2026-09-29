# Third-party notices

Fonts are vendored under `fonts/`. cJSON and stb are vendored under `third_party/`. LVGL is fetched at build time and is not committed.

## cJSON

- Path: `third_party/cJSON/`
- License: MIT
- Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
- Text: `third_party/cJSON/LICENSE`

## stb

- Path: `third_party/stb/stb_image_write.h` (v1.16)
- License: dual, public domain (Unlicense) or MIT, either at your choice
- Copyright (c) 2017 Sean Barrett (MIT text in the header)
- The header also embeds a public-domain JPEG writer
- Text: the license block at the end of the header

## LVGL

- Not vendored. `make fetch-lvgl` clones the pin in `tools/lvgl.pin` (`v9.2.2`, commit `7f07a129e8d77f4984fff8e623fd5be18ff42e74`) into `.cache/lvgl/`
- License: MIT
- Copyright (c) 2021 LVGL Kft
- Text: `LICENCE.txt` in that checkout

## Fonts

- Path: `fonts/`
- License: SIL Open Font License 1.1
- Barlow Semi Condensed (SemiBold, Bold, ExtraBold) and Alfa Slab One (Regular)
- Texts: `fonts/OFL-BarlowSemiCondensed.txt`, `fonts/OFL-AlfaSlabOne.txt`, and `fonts/README.md`
- `generated/` holds converted subsets. Those subsets remain OFL font software

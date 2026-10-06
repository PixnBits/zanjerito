# Fonts

TrueType sources used by the kiosk UI, converted to LVGL C arrays in `generated/`.

| File | Family / weight | C symbol family |
|------|-----------------|-----------------|
| `BarlowSemiCondensed-SemiBold.ttf` | Barlow Semi Condensed SemiBold | `zk_font_sb_*` |
| `BarlowSemiCondensed-Bold.ttf` | Barlow Semi Condensed Bold | `zk_font_b_*` |
| `BarlowSemiCondensed-ExtraBold.ttf` | Barlow Semi Condensed ExtraBold | `zk_font_xb_*` |
| `AlfaSlabOne-Regular.ttf` | Alfa Slab One Regular | `zk_font_alfa_*` |

Upstream (SIL Open Font License 1.1):

- Barlow Semi Condensed: https://github.com/google/fonts/tree/main/ofl/barlowsemicondensed
- Alfa Slab One: https://github.com/google/fonts/tree/main/ofl/alfaslabone

License texts for these files are `OFL-BarlowSemiCondensed.txt` and `OFL-AlfaSlabOne.txt` in this directory.

The C arrays produced by `tools/gen-fonts.sh` are converted subsets (selected pixel sizes and glyph ranges) of these fonts, not the complete TTF files. Subsets remain Font Software under SIL OFL 1.1.

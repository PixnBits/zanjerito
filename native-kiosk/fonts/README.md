# Fonts

TrueType sources used by the kiosk UI, converted to LVGL C arrays in `generated/`.

| File | Family / weight | C symbol family |
|------|-----------------|-----------------|
| `BarlowSemiCondensed-Regular.ttf` | Barlow Semi Condensed Regular (400) | `zk_font_r_*` |
| `BarlowSemiCondensed-Medium.ttf` | Barlow Semi Condensed Medium (500) | `zk_font_m_*` |
| `BarlowSemiCondensed-SemiBold.ttf` | Barlow Semi Condensed SemiBold (600) | `zk_font_sb_*` |
| `BarlowSemiCondensed-Bold.ttf` | Barlow Semi Condensed Bold (700) | `zk_font_b_*` |
| `Literata-SemiBold-Text.ttf` | Literata SemiBold, text optical size | `zk_font_lit_18`, `lit_20`, `lit_26`, `lit_32` |
| `Literata-SemiBold.ttf` | Literata SemiBold, display optical size | `zk_font_lit_40`, `lit_48` |

Upstream (SIL Open Font License 1.1):

- Barlow Semi Condensed: https://github.com/google/fonts/tree/main/ofl/barlowsemicondensed
- Literata: https://github.com/google/fonts/tree/main/ofl/literata

License texts are `OFL-BarlowSemiCondensed.txt` and `OFL-Literata.txt` in this directory.

Literata upstream is a variable font. `lv_font_conv` 1.5.3 cannot select weight 600 from it (`Literata.ttf:600` is not a file it can open). These two files are static SemiBold instances made with fonttools: text optical size (`opsz` 12, `wght` 600) for the tile and sheet sizes, and display optical size (`opsz` 48, `wght` 600) for the 40 and 48 px headlines. The variable font is not committed.

`tools/gen-fonts.sh` subsets printable ASCII, Latin-1 (U+00A0–U+00FF, including NBSP, middot, and the accented letters), en dash (U+2013), em dash (U+2014), left single quote (U+2018), curly apostrophe (U+2019), double quotes (U+201C, U+201D), bullet (U+2022), ellipsis (U+2026), and the single right-pointing angle quotation mark (U+203A). U+26A0 WARNING SIGN is not in the subset. The minute stepper draws an ASCII hyphen. The overlap warning is a drawn triangle, not a glyph. Ingest folds other dashes, quotes, and bullets onto this set before anything is drawn. Line height and baseline in the generated C are pinned back to the pre-Latin-1 metrics, so the extra accents do not restack rows that were measured against the old line boxes.

The C arrays are converted subsets (selected pixel sizes and that glyph range), not the complete TTF files. Subsets remain Font Software under SIL OFL 1.1.

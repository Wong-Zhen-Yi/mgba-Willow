# Library title artwork

`library-titles.json` is a filename index of `Named_Titles` PNGs from the public
[Libretro thumbnail collection](https://github.com/libretro-thumbnails), retrieved
2026-10-31. The GB, GBC, and GBA arrays correspond to these repositories:

- `Nintendo_-_Game_Boy`
- `Nintendo_-_Game_Boy_Color`
- `Nintendo_-_Game_Boy_Advance`

Images are downloaded on demand from the repositories' `master/Named_Titles/`
directories and cached in Qt's per-application cache directory under
`library-artwork`. No artwork images are bundled with the emulator. The image
content remains the property of its respective copyright holders.

Matching uses the ROM filename (the first compatible member name for archives),
platform, and release metadata. Punctuation, accents, the word "Version", and
common region/revision tags are normalized. Other qualifiers are retained to
avoid treating a hack or unrelated edition as the original game. If there is no
match or a download fails, the cartridge remains a placeholder. Refresh retries
failed downloads. Cached images work offline. ROMs are never executed to obtain
artwork, and browsing does not change saves or extract archives to disk.

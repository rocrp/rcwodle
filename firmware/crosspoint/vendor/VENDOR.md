# Vendored CrossPoint Reader snapshot

- Origin: ~/w/_hw/crosspoint-reader (github CrossPoint Reader)
- Commit: b12839d1d49e22ccbbd5b4dffeb7a0bd4d49f6c3 (2026-06-01)
- Excluded at copy time: lib/KOReaderSync, lib/OpdsParser, lib/EpdFont/builtinFonts
  (subset re-added at font slice), src/network/, Wifi/Opds stores.
- Local modifications are marked with `// WODLE-PORT:` comments.
- I18n: lib/I18n/translations/chinese.yaml is a wodle addition (not upstream);
  I18nKeys.h/I18nStrings.{h,cpp} regenerated locally via upstream
  scripts/gen_i18n.py (25 langs, ZH=24), with `--strip-unused` (138 dead keys
  = ~82KB flash; a stripped key newly used by synced code = compile error →
  just re-run). Re-run after any yaml change:
  `uv run ~/w/_hw/crosspoint-reader/scripts/gen_i18n.py vendor/lib/I18n/translations vendor/lib/I18n --strip-unused --src-dirs vendor/src vendor/lib src port`
- Builtin UI fonts (ubuntu_10/12 reg+bold, notosans_8_regular .h) regenerated
  locally with a CJK fallback + per-translation charset intervals via
  `uv run tools/build_ui_cjk_fonts.py` (also fixes upstream Hebrew U+05F4
  tofu). On upstream sync, re-run BOTH scripts instead of taking upstream's
  generated files.

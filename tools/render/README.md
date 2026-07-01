# Host renderer — local UI preview (no hardware, no Wokwi)

Renders every CYD-Claudemon page to a 320×240 PNG by compiling the real
`src/ui_*.h` drawing code against a small mock of `TFT_eSPI` that draws into an
in-memory framebuffer. Instant, offline, no cloud token — the fastest way to
eyeball landscape-layout changes while iterating.

## Use

```bash
./build.sh        # builds + runs; PNGs land in out/
```

Requires a C++17 compiler and zlib (both preinstalled on macOS). If ImageMagick
is installed (`brew install imagemagick`), `build.sh` also stitches every page
into one labelled contact sheet at `out/contact.png` — the fastest way to eyeball
all pages at once.

**This is the recommended way to verify landscape layout.** The Wokwi *VS Code
extension* currently mis-renders the ILI9341 in landscape (a large horizontal
offset) due to a bug in its bundled simulation engine — see the repo's "Known
limitations". The host renderer here is pixel-accurate and matches hardware, so
prefer it for layout work; use the Wokwi **cloud/web** sim (its engine is
current and faithful) or real hardware for live behavior.

## What it is / isn't

- Text uses the library's **actual** GLCD (font 1) and Font16 (font 2) data, so
  glyph widths / datums / positions match the device pixel-for-pixel.
- Colors are shown **as drawn** (true colors). The device calls
  `invertDisplay(true)` because the CYD panel is pre-inverted in hardware; the
  renderer (and a correctly-configured emulator) show the intended look.
- It verifies **layout**, not runtime behavior (touch, polling, OTA, the live
  Anthropic API). Use the Wokwi emulator or real hardware for those.
- Pages covered: USAGE, COST, MODELS, TREND, CLAUDEMON, CLOCK, SYSTEM, SETTINGS,
  plus alert-state variants (`alt_*`). Sample data comes from `src/fake_data.h`.

## Keeping it in sync

The mock implements only the `TFT_eSPI` methods/fonts the UI currently uses. If a
page starts using a new call or font, add it to `TFT_eSPI.h` — the build fails
loudly until you do.

## Fonts (attribution)

`fonts/glcdfont.c` and `fonts/Font16.c` are copied verbatim from
[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer (FreeBSD license);
`glcdfont` originates from Adafruit-GFX. Included here only to render faithful
text metrics.

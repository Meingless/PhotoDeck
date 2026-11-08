# PhotoDeck application icon

This is the selected first concept for the next release: two overlapping photo
frames, turquoise behind mint, on a charcoal rounded-square tile. The mark is
redrawn as vectors; no pixels from the concept presentation are embedded.

## Source and assets

- `photodeck.svg`: editable, self-contained 1024-unit vector master. Transparent
  canvas corners, three paths, three gradients, no font or image dependencies.
- `photodeck.png`: 512 px application/AppImage icon.
- `icons/photodeck-{size}.png`: exact RGBA representations at 16, 24, 32, 48,
  64, 128, 256, 512 and 1024 px.
- `photodeck.ico`: 16–256 px Windows executable/Explorer representations.
- `photodeck.icns`: 16–1024 px macOS representations, including Retina sizes.

Qt embeds the 16–512 px PNGs, so runtime icon loading does not need an SVG or
ICO image-format plugin. CMake and qmake include the ICO in Windows executables
and the ICNS in macOS application bundles. Linux desktop installation uses the
SVG; AppImage packaging uses the 512 px PNG.

## Regeneration and checks

Normal builds use the committed assets. To edit or regenerate them, install
Inkscape and Python Pillow, edit only the SVG master, then run:

```sh
python3 scripts/generate-icons.py
python3 -m unittest discover -s tests -p 'test_icon_assets.py' -v
```

The renderer supersamples the master at 4096 px and derives every PNG directly
from that image. ICO/ICNS containers reuse the exact PNG representations. For
reproducible byte-for-byte regeneration use Inkscape 1.4 and Pillow 12.3.0.

The asset tests use only Python's standard library and run in the existing CI
test discovery. The existing Qt functional suite also loads all eight embedded
sizes and checks dimensions and transparency. Platform-native executable and
bundle verification still belongs to the Windows/macOS build jobs.

This preparation does not change the application version or publish a release.

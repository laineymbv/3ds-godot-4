# Documentation site

Static HTML built from the platform guide (`platform/3ds/README.md`) and the
class reference XML in `platform/3ds/doc_classes/`.

The published site lives in `docs/` at the repo root, which GitHub Pages serves.

Rebuild after changing either source:

```bash
python3 docs-site/build.py --out docs \
    --title "Godot for the Nintendo 3DS" --home-title "Documentation" \
    --guide guide "3DS platform guide" platform/3ds/README.md \
    --class-dir platform/3ds/doc_classes
```

Needs Python 3 and the `markdown` package (`pip install markdown`). Open
`docs/index.html` in a browser, or serve the `docs/` folder with any static
host. Pages have no scripts and no external fonts.

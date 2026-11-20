#!/usr/bin/env python3
"""Regenerate (or verify) EMBEDDED_PANEL_HTML in plugin.py from ui/panel.html.

    python embed_panel.py          # rewrite the blob in plugin.py
    python embed_panel.py --check  # exit 1 if the blob does not decode to ui/panel.html

The blob is compared by its decompressed content, not by bytes, because compressed output can differ
between zlib versions. The panel is read with universal newlines, exactly like plugin.py does.
"""

import ast
import base64
import re
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLUGIN = HERE / "plugin.py"
PANEL = HERE / "ui" / "panel.html"
BLOB_RE = re.compile(r"(EMBEDDED_PANEL_HTML = \()(.*?)(\n\)\r?\n)", re.S)
LINE_WIDTH = 100


def read_text_keep_newline_style(path: Path) -> tuple[str, str]:
    raw = path.read_bytes().decode("utf-8")
    return raw, "\r\n" if "\r\n" in raw else "\n"


def build_blob(html: str) -> str:
    packed = base64.b85encode(zlib.compress(html.encode("utf-8"), 9)).decode("ascii")
    chunks = [packed[i : i + LINE_WIDTH] for i in range(0, len(packed), LINE_WIDTH)]
    return "\n" + "\n".join(f'    "{chunk}"' for chunk in chunks)


def decode_blob(source: str) -> str:
    match = BLOB_RE.search(source)
    if not match:
        raise SystemExit("EMBEDDED_PANEL_HTML block not found in plugin.py")
    value = ast.literal_eval("(" + match.group(2).replace("\r", "") + "\n)")
    return zlib.decompress(base64.b85decode(value.encode("ascii"))).decode("utf-8")


def main() -> int:
    html = PANEL.read_text(encoding="utf-8")  # universal newlines, same as plugin.py
    source, newline = read_text_keep_newline_style(PLUGIN)
    if "--check" in sys.argv:
        if decode_blob(source) == html:
            print("EMBEDDED_PANEL_HTML is up to date.")
            return 0
        print("EMBEDDED_PANEL_HTML is stale: run embed_panel.py")
        return 1
    blob = build_blob(html).replace("\n", newline)
    match = BLOB_RE.search(source)
    if not match:
        raise SystemExit("EMBEDDED_PANEL_HTML block not found in plugin.py")
    updated = source[: match.start(2)] + blob + source[match.end(2) :]
    PLUGIN.write_bytes(updated.encode("utf-8"))
    assert decode_blob(updated) == html
    print(f"Updated EMBEDDED_PANEL_HTML ({len(html)} chars).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

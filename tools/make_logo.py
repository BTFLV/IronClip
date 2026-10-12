#!/usr/bin/env python3
"""Generate the IronClip logo SVGs in assets/.

The mark mirrors the logo drawn in the plug-in header: a sine wave whose peaks
are cut flat by the ceiling. The wordmark is converted to outlines from
Barlow Semi Condensed Bold so the SVGs render identically everywhere.

Usage:  python3 tools/make_logo.py
Requires fontTools (development only).
"""
import math
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "resources/fonts/BarlowSemiCondensed-Bold.ttf"
OUT = ROOT / "assets"

ACCENT = "#FF621F"
STEEL = "#C8D1DB"
TILE = "#1E2125"
TILE_STROKE = "#40464E"


def mark(x: float, y: float, size: float) -> str:
    """Logo mark as SVG elements inside a size x size box at (x, y)."""
    r = size * 0.22
    cx, cy = x + size / 2, y + size / 2
    w, amp, clip = size * 0.66, size * 0.33, size * 0.18
    pts = []
    steps = 96
    for i in range(steps + 1):
        t = i / steps
        px = cx - w / 2 + t * w
        py = cy - max(-clip, min(clip, amp * math.sin(t * 2 * math.pi)))
        pts.append(f"{px:.2f},{py:.2f}")
    t_clip = math.asin(clip / amp) / (2 * math.pi)
    flats = []
    for start in (t_clip, 0.5 + t_clip):
        x1 = cx - w / 2 + start * w
        x2 = cx - w / 2 + (start + 0.5 - 2 * t_clip) * w
        fy = cy - clip if start < 0.5 else cy + clip
        flats.append(f'<line x1="{x1:.2f}" y1="{fy:.2f}" x2="{x2:.2f}" y2="{fy:.2f}"/>')
    sw = size * 0.06
    return (
        f'<rect x="{x + 0.5:.2f}" y="{y + 0.5:.2f}" width="{size - 1:.2f}" height="{size - 1:.2f}" '
        f'rx="{r:.2f}" fill="{TILE}" stroke="{TILE_STROKE}" stroke-width="1"/>'
        f'<polyline points="{" ".join(pts)}" fill="none" stroke="{STEEL}" stroke-width="{sw:.2f}" '
        f'stroke-linecap="round" stroke-linejoin="round"/>'
        f'<g stroke="{ACCENT}" stroke-width="{sw * 1.35:.2f}" stroke-linecap="round">{"".join(flats)}</g>'
    )


def wordmark(font: TTFont, text_parts, x: float, baseline: float, size: float, tracking_em: float):
    """Returns (svg elements, end x) for text drawn as outlines."""
    glyph_set = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font["head"].unitsPerEm
    out = []
    pen_x = x
    for text, color in text_parts:
        paths = []
        for ch in text:
            name = cmap[ord(ch)]
            glyph = glyph_set[name]
            svg_pen = SVGPathPen(glyph_set)
            glyph.draw(TransformPen(svg_pen, (scale, 0, 0, -scale, pen_x, baseline)))
            paths.append(svg_pen.getCommands())
            pen_x += glyph.width * scale + tracking_em * size
        out.append(f'<path fill="{color}" d="{" ".join(paths)}"/>')
    return "".join(out), pen_x - tracking_em * size


def svg(width: float, height: float, body: str) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" '
        f'viewBox="0 0 {width:.0f} {height:.0f}" role="img" aria-label="IronClip">'
        f"<title>IronClip</title>{body}</svg>\n"
    )


def main() -> None:
    OUT.mkdir(exist_ok=True)
    font = TTFont(FONT)

    (OUT / "icon.svg").write_text(svg(128, 128, mark(0, 0, 128)))

    height, mark_size, text_size = 96, 96, 64
    for variant, iron_color in (("dark", "#E8EAED"), ("light", "#16181B")):
        words, end = wordmark(font, [("IRON", iron_color), ("CLIP", ACCENT)],
                              mark_size + 26, height / 2 + text_size * 0.355, text_size, 0.11)
        (OUT / f"logo-{variant}.svg").write_text(svg(end + 4, height, mark(0, 0, mark_size) + words))
    print("wrote", ", ".join(p.name for p in sorted(OUT.glob("*.svg"))))


if __name__ == "__main__":
    main()

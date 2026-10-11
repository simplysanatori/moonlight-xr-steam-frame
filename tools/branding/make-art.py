#!/usr/bin/env python3
"""Moonlight XR for Frame: makes the artwork in assets/branding/ from
assets/branding/source/moonlight-xr-logo.png (Moonlight XR's logo, GPLv3).
Needs python3 with pycairo and PyGObject (Pango); both ship with SteamOS.

    python3 -I tools/branding/make-art.py          writes the files next to it

Text is set in Noto Sans (SIL OFL 1.1, already part of the app).
"""
import math, os, sys
import cairo, gi
gi.require_version("Pango", "1.0"); gi.require_version("PangoCairo", "1.0")
from gi.repository import Pango, PangoCairo

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "branding")
SRC = cairo.ImageSurface.create_from_png(os.path.join(OUT, "source", "moonlight-xr-logo.png"))

# the disc inside the 960x1024 source (found by scanning its alpha)
DX0, DY0, DSZ = 83, 85, 819
TOP, BOTTOM = (0.075, 0.098, 0.140), (0.125, 0.165, 0.235)   # night-blue gradient
WHITE, SOFT = (1, 1, 1), (0.66, 0.71, 0.79)


def surface(w, h):
    s = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
    return s, cairo.Context(s)


def background(cr, w, h):
    g = cairo.LinearGradient(0, 0, 0, h)
    g.add_color_stop_rgb(0, *TOP); g.add_color_stop_rgb(1, *BOTTOM)
    cr.set_source(g); cr.paint()


def disc(cr, x, y, size, alpha=1.0):
    """The logo's disc, its top-left corner at x, y, `size` px wide."""
    k = size / DSZ
    cr.save()
    cr.translate(x, y); cr.scale(k, k); cr.translate(-DX0, -DY0)
    cr.set_source_surface(SRC, 0, 0)
    cr.get_source().set_filter(cairo.FILTER_BEST)
    cr.rectangle(DX0, DY0, DSZ, DSZ); cr.clip()
    cr.paint_with_alpha(alpha)
    cr.restore()


def text(cr, s, family, px, x, y, color, anchor="l"):
    """Draw s with its baseline at y; anchor l / c / r on x. Returns the width."""
    fo = cairo.FontOptions()
    fo.set_antialias(cairo.ANTIALIAS_GRAY)       # no coloured fringes on transparent art
    PangoCairo.context_set_font_options(PangoCairo.create_context(cr), fo)
    lay = PangoCairo.create_layout(cr)
    PangoCairo.context_set_font_options(lay.get_context(), fo)
    lay.set_font_description(Pango.FontDescription(f"{family} {px}px"))
    lay.set_text(s, -1)
    w = lay.get_pixel_size()[0]
    base = lay.get_baseline() / Pango.SCALE
    cr.set_source_rgb(*color)
    cr.move_to(x - (w / 2 if anchor == "c" else w if anchor == "r" else 0), y - base)
    PangoCairo.show_layout(cr, lay)
    return w


def save(s, name, flatten=False):
    p = os.path.join(OUT, name)
    s.write_to_png(p)
    print(f"{name}: {s.get_width()}x{s.get_height()}")


def wordmark(cr, x, y_base, px, align="l"):
    text(cr, "Moonlight XR", "Noto Sans Medium", px, x, y_base, WHITE, align)
    text(cr, "for Frame", "Noto Sans", px * 0.62, x, y_base + px * 0.80, SOFT, align)


def main():
    # icon: the disc on a transparent square: 512, 256 (launcher window icon), 128 and 64 (dock icon theme sizes)
    for n in (512, 256, 128, 64):
        s, cr = surface(n, n)
        m = n * 0.02
        disc(cr, m, m, n - 2 * m)
        save(s, f"icon-{n}.png")

    # portrait capsule 600x900
    s, cr = surface(600, 900)
    background(cr, 600, 900)
    disc(cr, 600 / 2 - 190, 150, 380)
    wordmark(cr, 300, 700, 76, "c")
    save(s, "portrait-600x900.png")

    # wide capsule 920x430
    s, cr = surface(920, 430)
    background(cr, 920, 430)
    disc(cr, 60, 55, 320)
    wordmark(cr, 410, 225, 70)
    save(s, "wide-920x430.png")

    # hero 3840x1240: night gradient and the wheel, large, cropped at the right edge, faint
    s, cr = surface(3840, 1240)
    background(cr, 3840, 1240)
    disc(cr, 2480, -300, 1840, 0.16)
    save(s, "hero-3840x1240.png")

    # logo with transparency, 1280x540
    s, cr = surface(1280, 540)
    disc(cr, 40, 70, 400)
    wordmark(cr, 500, 290, 104)
    save(s, "logo-1280x540.png")


if __name__ == "__main__":
    main()

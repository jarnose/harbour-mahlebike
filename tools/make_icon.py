#!/usr/bin/env python3
"""Renders icons/src/harbour-mahlebike.svg to the four sizes Harbour allows (86, 108, 128, 172 px).

The SVG is drawn after the Sailfish OS app icon template (https://sailfishos.org/design/icons/).

    python3 -m venv /tmp/qv && /tmp/qv/bin/pip install cairosvg
    /tmp/qv/bin/python tools/make_icon.py
"""
import os

import cairosvg

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
svg = os.path.join(root, "icons", "src", "harbour-mahlebike.svg")
for n in (86, 108, 128, 172):
    out = os.path.join(root, "icons", "%dx%d" % (n, n), "harbour-mahlebike.png")
    cairosvg.svg2png(url=svg, write_to=out, output_width=n, output_height=n)
print("icons written")

#!/usr/bin/env python3
"""SWEEP-007 image evidence: dialog PNGs + v1-vs-pump battery map.

1. putty/pageant messagebox_1.bmp (guest's own MessageBoxW dialogs, rendered
   by the SWEEP-004 GDI-lite raster) -> 2x nearest-upscale PNGs.
2. battery_map.png — honest SW-562 stage map: every program's SWEEP-003 v1
   stage vs its MW_GUI_EVENTS=1 pump-v2 stage (15/15 3x-deterministic).
"""
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EV = os.path.join(ROOT, "evidence", "sweep007_battery")


def bmp_to_png2x(bmp_path, png_path):
    img = Image.open(bmp_path)
    w, h = img.size
    img = img.resize((w * 2, h * 2), Image.NEAREST)
    img.save(png_path)
    print(f"[PNG] {os.path.relpath(png_path, ROOT)} ({w}x{h} -> {w*2}x{h*2})")


def battery_map():
    # program, v1 SWEEP-003 stage, pump-v2 SW-562 stage (this battery)
    rows = [
        ("produkey",      "EXIT0 v1 lifecycle",  "CRASH 0x22f24 in WM_CREATE (pump)"),
        ("cports",        "CRASH 0x1b338",       "CRASH 0x2119 in WM_CREATE (pump)"),
        ("procmon64",     "EXIT0 (pre-window)",  "EXIT0 (pre-window, armed=0)"),
        ("diskmon64",     "EXIT0 (pre-window)",  "EXIT0 (pre-window, armed=0)"),
        ("coreinfoex64",  "EXIT0",               "EXIT1 (version drift, SW-561)"),
        ("putty",         "EXIT1 + DIALOG-IMG",  "EXIT1 + DIALOG-IMG (2nd confirm)"),
        ("pageant",       "EXIT1",               "EXIT1 + DIALOG-IMG (NEW)"),
        ("procexp64",     "EXIT2",               "EXIT2 (named-stub gate)"),
        ("autoruns64",    "EXIT1",               "EXIT1 (named-stub gate)"),
        ("rammap64",      "EXIT1",               "EXIT1 (named-stub gate)"),
        ("puttygen",      "CRASH RIP=0",         "CRASH RIP=0 (SW-552)"),
        ("searchmyfiles", "CRASH 139",           "CRASH 0x28001"),
        ("zoomit64",      "TRAP133",             "TRAP133 (C++ unhandled)"),
        ("bginfo64",      "TERM9 C++ unhandled", "TERM9 C++ unhandled"),
        ("winmergeu",     "TERM9 C++ unhandled", "TERM9 C++ unhandled"),
        ("adexplorer64",  "TRAP133",             "UNOBTAINABLE (retired 404)"),
    ]

    n = len(rows)
    LH = 34                      # row height
    W, H = 1240, 150 + n * LH + 60
    img = Image.new("RGB", (W, H), (18, 22, 30))
    px = img.load()

    # palette per pump-v2 class
    def cls_color(stage):
        if stage.startswith("CRASH"):
            return (231, 76, 60)
        if "DIALOG-IMG" in stage or stage.startswith("EXIT0"):
            return (46, 204, 113)
        if stage.startswith("EXIT") or stage.startswith("TERM") or "TRAP" in stage:
            return (241, 196, 15)
        return (127, 140, 141)  # unobtainable

    try:
        from PIL import ImageFont, ImageDraw
        d = ImageDraw.Draw(img)
        try:
            f_big = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
            f_med = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
            f_sm = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)
        except OSError:
            f_big = f_med = f_sm = None
    except ImportError:
        d = None

    title = "SW-562 battery: 15 GUI programs x3 under MW_GUI_EVENTS=1 (pump v2)"
    sub = ("MiniWin 2026-10-18 - 15/15 3x-deterministic - green = exit0 / dialog image, "
           "yellow = clean gate, red = guest crash, grey = retired binary")
    if d:
        d.text((30, 22), title, fill=(240, 240, 240), font=f_big)
        d.text((30, 54), sub, fill=(160, 170, 185), font=f_med)

        y = 116
        d.text((36, y), "PROGRAM", fill=(200, 205, 215), font=f_sm)
        d.text((280, y), "SWEEP-003 v1 (no pump)", fill=(200, 205, 215), font=f_sm)
        d.text((640, y), "SWEEP-007 pump v2 (MW_GUI_EVENTS=1)", fill=(200, 205, 215), font=f_sm)
        y += 14
        for i, (name, v1, v2) in enumerate(rows):
            ry = y + i * LH
            if i % 2 == 0:
                for xx in range(24, W - 24):
                    for yy in range(ry, ry + LH - 6):
                        px[xx, yy] = (24, 29, 39)
            d.text((36, ry + 5), name, fill=(235, 235, 240), font=f_sm)
            d.text((280, ry + 5), v1, fill=(185, 190, 200), font=f_med)
            d.text((640, ry + 5), v2, fill=cls_color(v2), font=f_med)
        # legend strip
        ly = y + n * LH + 8
        d.text((30, ly), "2 programs arm the pump (produkey, cports - both crash in "
                         "synchronous WM_CREATE); putty/pageant render "
                         "dialogs to BMP.",
               fill=(140, 200, 160), font=f_med)

    out = os.path.join(EV, "battery_map.png")
    img.save(out)
    print(f"[PNG] {os.path.relpath(out, ROOT)} ({W}x{H})")


def main():
    for name in ("putty", "pageant"):
        bmp = os.path.join(EV, f"{name}_frames", "messagebox_1.bmp")
        if os.path.exists(bmp):
            bmp_to_png2x(bmp, os.path.join(EV, f"{name}_dialog_minwin.png"))
        else:
            print(f"[SKIP] {bmp} missing")
    battery_map()


if __name__ == "__main__":
    sys.exit(main())

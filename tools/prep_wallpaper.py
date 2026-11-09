#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
prep_wallpaper.py -- 把任意图片转成 LVGL 直接能用的裸 RGB565

为什么不在运行时解码 PNG：MIPS/ARM 上解一张 160x128 的 PNG 要几十毫秒，
跳秒时必掉帧。预转成裸数据后，运行期只是 memset/memcpy 级别的拷贝。

用法：
    python tools/prep_wallpaper.py in.jpg 160 128 -o build/wallpapers/bg.raw
    python tools/prep_wallpaper.py img/ 160 128          # 整个目录批量转

输出文件名：bg.raw 或 bg_<W>x<H>.raw（layer_bg.c 按这个顺序找）
依赖：Pillow（没有就 pip install pillow）
"""
import argparse
import pathlib
import sys

try:
    from PIL import Image
except ImportError:
    raise SystemExit("需要 Pillow: pip install pillow")

EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def convert(src: pathlib.Path, w: int, h: int, dst: pathlib.Path) -> None:
    im = Image.open(src).convert("RGB")

    # 等比放大后居中裁剪，保证铺满且不变形
    sw, sh = im.size
    scale = max(w / sw, h / sh)
    nw, nh = max(w, int(round(sw * scale))), max(h, int(round(sh * scale)))
    im = im.resize((nw, nh), Image.LANCZOS)
    left = (nw - w) // 2
    top = (nh - h) // 2
    im = im.crop((left, top, left + w, top + h))

    # RGB565 little-endian：LV_COLOR_FORMAT_RGB565
    out = bytearray(w * h * 2)
    px = im.load()
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[i] = v & 0xFF
            out[i + 1] = v >> 8
            i += 2

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(out)
    print(f"{src} -> {dst}  ({w}x{h}, {len(out)} bytes)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="图片文件或目录")
    ap.add_argument("w", type=int)
    ap.add_argument("h", type=int)
    ap.add_argument("-o", "--out", help="输出文件（目录模式时可忽略）")
    a = ap.parse_args()

    src = pathlib.Path(a.src)
    if src.is_dir():
        files = [p for p in sorted(src.iterdir()) if p.suffix.lower() in EXTS]
        if not files:
            raise SystemExit(f"{src} 里没有图片")
        for p in files:
            convert(p, a.w, a.h, src / f"{p.stem}_{a.w}x{a.h}.raw")
        return

    out = pathlib.Path(a.out) if a.out else src.with_suffix(".raw")
    convert(src, a.w, a.h, out)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""把 /dev/fb0 抓下来的裸数据转成 PNG，并生成四种逻辑旋转候选。

板子：128x160 一页、24bpp、line_length=384（每行 128 像素 x 3 字节），
内存顺序 B,G,R（little-endian 0x00RRGGBB）。

用法:
    python scripts/fbdump2png.py build/dev.raw [--out build]

输出 build/phys.png（物理正向，128x160）与 build/rot_{a,b,c,d}.png（逻辑 160x120）。
四个候选的映射关系：
    a: col = y          row = (H-1) - x     （当前固件用的）
    b: col = y          row = x
    c: col = (W-1) - y  row = (H-1) - x
    d: col = (W-1) - y  row = x
"""
import argparse
import os
import struct
import zlib

PW, PH = 128, 160          # 物理一页
LW, LH = 160, 120          # 逻辑分辨率


def read_phys(path):
    data = open(path, "rb").read()
    line = len(data) // PH
    assert line >= PW * 3, f"unexpected line_length {line}"
    img = [[(0, 0, 0)] * PW for _ in range(PH)]
    for r in range(PH):
        base = r * line
        for c in range(PW):
            b, g, rr = data[base + c * 3: base + c * 3 + 3]
            img[r][c] = (rr, g, b)
    return img


def write_png(path, img):
    h, w = len(img), len(img[0])
    raw = b"".join(b"\x00" + b"".join(bytes(px) for px in row) for row in img)

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    open(path, "wb").write(png)


def rot(phys, mode, lw, lh):
    out = [[(0, 0, 0)] * lw for _ in range(lh)]
    for y in range(lh):
        for x in range(lw):
            col = (PW - 1 - y) if mode in ("c", "d") else y
            row = x if mode in ("b", "d") else (PH - 1 - x)
            if 0 <= col < PW and 0 <= row < PH:
                out[y][x] = phys[row][col]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw")
    ap.add_argument("--out", default="build")
    ap.add_argument("--lw", type=int, default=LW, help="逻辑宽（默认 160）")
    ap.add_argument("--lh", type=int, default=LH, help="逻辑高（默认 120）")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    phys = read_phys(a.raw)
    write_png(os.path.join(a.out, "phys.png"), phys)
    for m in "abcd":
        write_png(os.path.join(a.out, f"rot_{m}.png"), rot(phys, m, a.lw, a.lh))
    print("wrote phys.png + rot_a..d.png to", a.out)


if __name__ == "__main__":
    main()

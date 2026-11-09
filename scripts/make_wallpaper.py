#!/usr/bin/env python3
"""把任意图片转成设备能直接吃的壁纸文件（裸 RGB565 little-endian）。

为什么不做成运行时解码：目标板是 arm Linux + fbdev，没有 png/jpg 解码器，
内存也放不下一张全屏位图的解码结果。所以壁纸在电脑上转好再拷过去，
设备侧只做一次 read()，零解码、零额外内存。

依赖 ffmpeg（PATH 里能找到即可），不需要任何 python 图像库。

用法：

    # 小屏 160x128
    python scripts/make_wallpaper.py photo.jpg --screen 160x128

    # 大屏 1024x600，输出到指定目录
    python scripts/make_wallpaper.py photo.jpg --screen 1024x600 -o out/wallpapers/

输出文件名固定为 bg_<W>x<H>.raw —— 设备端会从文件名解析尺寸，
所以换了屏幕尺寸也不用改代码，拷进去就会被自动缩放。

图片按 cover 方式处理：等比放大到铺满，多出来的边缘居中裁掉，
不变形、不留黑边。
"""
import argparse
import os
import shutil
import subprocess
import sys


def parse_size(s):
    try:
        w, h = s.lower().split("x")
        w, h = int(w), int(h)
    except ValueError:
        raise SystemExit(f"尺寸格式应为 WxH，比如 160x128，收到的是 {s!r}")
    if w <= 0 or h <= 0:
        raise SystemExit("尺寸必须为正")
    return w, h


def main():
    ap = argparse.ArgumentParser(description="图片 -> 设备壁纸（裸 RGB565 LE）")
    ap.add_argument("image", help="源图片（png/jpg/webp/bmp 都行）")
    ap.add_argument("--screen", required=True, help="屏幕分辨率，比如 160x128 / 1024x600")
    ap.add_argument("-o", "--out", default="wallpapers", help="输出目录（默认 wallpapers/）")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        sys.exit("找不到 ffmpeg。装一个（winget install ffmpeg / apt install ffmpeg），"
                 "或者手跑：\n"
                 f'  ffmpeg -i "{args.image}" -vf '
                 f'"scale=W:H:force_original_aspect_ratio=increase,crop=W:H" '
                 "-pix_fmt rgb565le -f rawvideo bg.raw")

    if not os.path.isfile(args.image):
        sys.exit(f"找不到输入文件：{args.image}")

    w, h = parse_size(args.screen)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, f"bg_{w}x{h}.raw")

    vf = f"scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h}"
    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-i", args.image,
        "-vf", vf,
        "-pix_fmt", "rgb565le",
        "-f", "rawvideo",
        path,
    ]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit("ffmpeg 转换失败")

    need = w * h * 2
    got = os.path.getsize(path)
    if got != need:
        sys.exit(f"产出大小不对：{got} 字节，应为 {need}")

    print(f"{args.image} -> {path}  {w}x{h}  {got} 字节")


if __name__ == "__main__":
    main()

# 壁纸（背景图）

**没有壁纸时就是纯黑**，这是默认状态，不需要任何配置。

壁纸文件不是 png/jpg，而是**预转好的裸 RGB565（little-endian）**。原因：目标板是
arm Linux + fbdev，没有图片解码器，内存也放不下一张全屏位图的解码结果。所以在电脑上
转好再拷过去，设备侧只做一次 `read()`，零解码、零额外内存。

## 三步

**1. 转**（需要 ffmpeg 在 PATH 里）

```bash
# 小屏 160×128
python scripts/make_wallpaper.py photo.jpg --screen 160x128

# 大屏 1024×600
python scripts/make_wallpaper.py photo.jpg --screen 1024x600
```

脚本会按 cover 方式处理：等比放大到铺满，多出来的边缘居中裁掉，不变形、不留黑边。
产出 `wallpapers/bg_<W>x<H>.raw`（例如 `bg_1024x600.raw`）。

不用 python 也行，一条 ffmpeg：

```bash
ffmpeg -i photo.jpg \
  -vf "scale=1024:600:force_original_aspect_ratio=increase,crop=1024:600" \
  -pix_fmt rgb565le -f rawvideo wallpapers/bg_1024x600.raw
```

**2. 拷** 到设备上，和可执行文件放在一起（或运行目录里）：

```
workdayAlarmClock          # 可执行文件
wallpapers/
  bg_1024x600.raw
```

**3. 重启** 程序。日志里应该能看到：

```
[wac] wallpaper loaded: /root/wallpapers/bg_1024x600.raw
```

没有壁纸时是：

```
[wac] wallpaper: none -> pure black
```

## 查找顺序

第一个能读通的就用：

| 顺序 | 路径 | 说明 |
|---|---|---|
| 1 | `$WAC_WALLPAPER` | 调试用，直接指定任意路径，不看别的 |
| 2 | `<dir>/wallpapers/bg_<屏宽>x<屏高>.raw` | 与屏幕同尺寸，零缩放，最快 |
| 3 | `<dir>/wallpapers/bg.raw` | 裸尺寸，同样必须与屏幕完全一致 |
| 4 | `<dir>/wallpapers/bg_*.raw` | 任意尺寸，从**文件名**解析宽高后 cover 缩放 |

`<dir>` 先试当前工作目录，再试可执行文件所在目录（`/proc/self/exe`）—— 开机自启时
cwd 常常是 `/`，只认 cwd 会找不到图。

第 4 条意味着：换了屏幕尺寸不用改代码、不用改名，把 `bg_320x240.raw` 丢进去就会被
自动裁切缩放。

## 尺寸规则

- 文件大小必须等于 `宽 × 高 × 2` 字节，否则拒绝加载并打日志。
- 名字里带 `<w>x<h>` 的会按这个尺寸解释，再缩放到屏幕；
- 名字里没有尺寸（比如 `bg.raw`）就要求它**正好**是屏幕尺寸。

## 遮罩

有壁纸时会盖一层固定 50% 的黑色遮罩，保证白字按钮在任何照片上都看得清（见
`WAC_MASK_OPA`）。没有壁纸时不建这层——纯黑上盖黑没有任何效果，只是白占一次合成。

## 调试：运行时换图

```bash
WAC_WALLPAPER=/tmp/test.raw ./workdayAlarmClock lvgl
```

路径写错会直接回退纯黑，日志里是 `cannot open`。

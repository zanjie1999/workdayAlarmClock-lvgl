# 工作咩闹钟

工作咩闹钟的 LVGL 壳。跑在 TinaLinux / sun5i 这类小内存Linux板子上（fbdev + 静态 musl），
让Linux也能享受Android App的部分功能

## 目录

| 路径 | 说明 |
|---|---|
| `src/` | 外壳源码（`ui/` 页面、`state/` 状态、`proto/` 与 Go 的通信、`hal/` 屏幕与背光） |
| `third_party/` | lvgl 与 freetype 的 **submodule**（裁剪只靠 `lv_conf.h` 和 CMake 变量，源码没改） |
| `assets/fonts/` | 运行时读的 TTF（中文用 `wac_cjk.ttf`，时钟数字用 `zyymeclock.ttf`） |
| `cmake/toolchain-*.cmake` | ARM / MIPS 的 musl 静态工具链（走 Zig，见下） |
| `scripts/` | 上板用的启动 / 部署脚本 |
| `.github/workflows/build.yml` | CI：出 ARM（aku）和 MIPS（rns）两个静态可执行文件 |

## 拉代码

`third_party/lvgl` 和 `third_party/freetype` 是 submodule，必须递归拉，否则是空目录、编译直接报
`add_subdirectory` 找不到：

```sh
git clone --recurse-submodules <仓库地址>

# 已经 clone 过、发现 third_party 是空的，补一句：
git submodule update --init --recursive
```

## 本地编译（电脑上看界面）

需要 CMake ≥ 3.20 和 Ninja；Windows 上 `sim` 后端直接开一个 GDI 窗口。
在仓库根目录开 cmd 执行：

```bat
cmake -S . -B build -G Ninja -DWAC_BACKEND=sim
cmake --build build
build\wac.exe
```

调试用环境变量（不影响正常启动）：`WAC_SHOT=x.bmp` 截一帧到文件，
可配 `WAC_SHOT_PAGE`（0 时钟 / 1 播放 / 2 设置）、`WAC_SHOT_FOCUS`、
`WAC_SHOT_CMD`（先给 Go 发一条命令）、`WAC_SHOT_EVENT_FILE`（先喂一条事件行）。
`WAC_AUTOTEST=1` 跑一遍内置自检。

cmd 里这样用（PowerShell 把 `set X=Y` 换成 `$env:X="Y"`）：

```bat
set WAC_SHOT=shot.bmp
set WAC_SHOT_PAGE=0
build\wac.exe
```

交叉编译要本地出包也可以，工具链脚本会自动找 `zig`（或 `WAC_ZIG` 指定路径）：

```bat
cmake -S . -B build-arm -G Ninja -DCMAKE_BUILD_TYPE=Release -DWAC_BACKEND=fbdev -DWAC_PANEL=aku -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-musl.cmake
cmake --build build-arm
```

## 从 Releases 下载

| Artifact | 目标设备 |
|---|---|
| `wac-aku-armv7` | aku（sun5i / ARMv7，fbdev 128×160 转 90°） |
| `wac-rns-mips` | rns 音箱（mipsel / MIPS，fbdev **480×320 横屏不旋转**） |

两个都是**静态链接**的单个可执行文件（CI 里有 `readelf` 断言，出现 `INTERP` 段就判失败），
下载下来解压直接就是 `wac`，不用装任何运行库。Release 包链接时带 `-s`，
调试段和符号表都丢掉了（mips 那份 8.6MB → 1.8MB）——设备的 /usr/data 只有 17MB。

## 部署到设备

两块板的**应用目录不一样**，脚本内容一样（`start.sh` 自己按所在位置推目录）：

| 设备 | 应用目录 | 备注 |
|---|---|---|
| aku | `/root/workdayAlarmClock` | 根分区够大，字体跟着程序走 |
| rns | `/usr/data/workdayAlarmClock` | 数据分区只有 17MB，**不推字体**，用系统自带的楷体 |

解压对应 artifact，得到 `wac`，然后（设备已连 adb）：

```sh
# 1. 推可执行文件（rns 换/usr/data 前缀）
adb push wac /usr/data/workdayAlarmClock/lvgl/wac
adb shell "chmod 755 /usr/data/workdayAlarmClock/lvgl/wac"

# 2. 第一次部署还要推启动脚本
adb push scripts/start-device.sh /usr/data/workdayAlarmClock/start.sh
adb push scripts/S99workdayAlarmClock /etc/init.d/S99workdayAlarmClock
adb shell "chmod 755 /usr/data/workdayAlarmClock/start.sh /etc/init.d/S99workdayAlarmClock"

# 3. 字体：aku 推 assets/fonts/ 下两个 TTF；rns 不推，走系统 /usr/share/fonts/ 里的楷体
adb push assets/fonts/wac_cjk.ttf    /root/workdayAlarmClock/lvgl/assets/fonts/
adb push assets/fonts/zyymeclock.ttf /root/workdayAlarmClock/lvgl/assets/fonts/

# 4. 启动（脚本会先停掉老的 playKey.sh / timeDisplay.sh 和 Go）
adb shell sh /usr/data/workdayAlarmClock/start.sh
```

日志在设备上 `lvgl/wac.log`：`adb shell tail -f /usr/data/workdayAlarmClock/lvgl/wac.log`。

**开机自启**：`/etc/init.d/S99workdayAlarmClock` 的 `start` 只做两件事——设背光、
调 `$BASE/start.sh`。手动跑同一份脚本就能验证开机会不会起来。

**字体**：`ui_font.c` 里的候选顺序是 雅黑类 → 其他无衬线中文 → 楷体。
rns 上没推雅黑，会自动落到 `/usr/share/fonts/Reeji-CloudKaiTi-GB.ttf`，
日志里能看到 `font: cjk = ...` 选的到底是哪个。

LVGL壳需要手动更新，工作咩闹钟本身会自动更新

## 关掉 rns 音箱自带的小度（保留联网 + 蓝牙音箱）

这块板子出厂跑着 dueros：`duer_linux` + `ims_service` + 四麦前端服务。它常驻两个独占资源，
和 LVGL 外壳直接冲突：

- `/dev/fb0` —— 一直在画自己的界面，是「背景残影」的来源；
- `/dev/snd/pcmC0D0p` —— 以多个 fd **直接打开 hw 设备**，绕过 `/etc/asound.conf` 里的
  `dmix`/`aeq`，导致 `bluealsa-aplay`（蓝牙放音）和提示音都拿不到声卡。

停掉它不影响另外两件事，它们都是独立的进程/脚本：

| 功能 | 依赖 | 受小度影响 |
|---|---|---|
| 联网 | `S40network`(wpa_supplicant) + `udhcpc` + `S80dnsmasq` | 否 |
| 蓝牙音箱 | `S32rtk_bluetooth` + `bluetoothd` + `S43a2dp-server`(bluealsa / bluealsa-aplay) + `S43auto_agent`(配对) | 否 |

配对记录在 `/var/lib/bluetooth`，蓝牙名取自 `/etc/bluetooth/main.conf` 的 `Name`，
两者都与小度无关，关掉后手机照样能连、名字也不变。

开关是一块标记文件，改动可回滚：

```sh
# 关掉小度（立即生效）
adb shell "touch /usr/data/duer_off && /etc/init.d/S99dueros_device stop"

# 恢复小度
adb shell "rm /usr/data/duer_off && /etc/init.d/S99dueros_device start"
```

开机时 `S99dueros_device` / `S95mic-server` 会先看这个标记：存在就只挂 zram swap、直接跳过小度。
两个脚本的原始版本在设备上留了 `.orig` 备份（`S99dueros_device.orig`、`S95mic-server.orig`），
仓库里对应的改造版是 `scripts/S99dueros_device` 和 `scripts/S95mic-server`。

实测在这块 120MB 内存的板子上，关掉后空闲内存从 13MB 涨到 41MB。

## 设备上的几个配置文件

都在应用目录的 `lvgl/` 下，不随代码走（已在 `.gitignore` 里）：

- `wac_ui.conf` —— 界面选项（显示秒 / 显示电量 / 反转音量 / 24 小时制 / 亮度 / 上次音量），
  首次运行自动生成，也可以直接手写。
- `wac_keys.conf` —— 按键映射。aku 是 `confirm=116 / prev=115 / next=114`（电源 + 音量加减）；
  rns 只有 `gpio-keys` 一个输入设备，可用键码是 `103`（播放）/`169`（一键听歌）/`115`（音量+）/`114`（音量-）。



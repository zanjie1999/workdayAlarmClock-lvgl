# workdayAlarmClock-lvgl

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
| `wac-rns-mips` | rns 音箱（mipsel / MIPS，面板由触发时填的参数决定） |

两个都是**静态链接**的单个可执行文件（CI 里有 `readelf` 断言，出现 `INTERP` 段就判失败），
下载下来解压直接就是 `wac`，不用装任何运行库。

## 部署到设备

解压对应 artifact，得到 `wac`，然后（设备已连 adb）：

```sh
# 1. 推可执行文件
adb push wac /root/workdayAlarmClock/lvgl/wac
adb shell "chmod 755 /root/workdayAlarmClock/lvgl/wac"

# 2. 第一次部署还要推启动脚本和字体
adb push scripts/start-device.sh /root/workdayAlarmClock/start.sh
adb push assets/fonts/wac_cjk.ttf     /root/workdayAlarmClock/lvgl/assets/fonts/
adb push assets/fonts/zyymeclock.ttf  /root/workdayAlarmClock/lvgl/assets/fonts/
adb shell "chmod 755 /root/workdayAlarmClock/start.sh"

# 3. 启动（脚本会先停掉老的 timeDisplay.sh / pwrKey.sh / volKey.sh 和 Go）
adb shell sh /root/workdayAlarmClock/start.sh
```

日志在设备上 `/root/workdayAlarmClock/lvgl/wac.log`：`adb shell tail -f` 直接看。

**开机自启**：在设备的 `/etc/rc.local` 里加一行（`exit 0` 之前），

```sh
/root/workdayAlarmClock/start.sh &
```

LVGL壳需要手动更新，工作咩闹钟本身会自动更新

## 设备上的几个配置文件

都在 `/root/workdayAlarmClock/lvgl/`，不随代码走（已在 `.gitignore` 里）：

- `wac_ui.conf` —— 界面选项（显示秒 / 显示电量 / 反转音量 / 24 小时制 / 亮度 / 上次音量），
  首次运行自动生成，也可以直接手写。
- `wac_keys.conf` —— 按键映射。这块板是 `confirm=116 / prev=115 / next=114`（3 键：电源 + 音量加减）。


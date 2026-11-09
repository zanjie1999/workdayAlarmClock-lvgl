#!/bin/sh
# 交叉编译并部署到设备（armv7 / musl / 静态）
#
# 目标：/root/workdayAlarmClock/lvgl/
#   wac                     LVGL 外壳（静态 ELF）
#   run.sh                  启动器（先停旧脚本和 Go，再 exec wac）
#   wac_keys.conf           3 键绑定：电源=确定 / 音量+=上 / 音量-=下
#   wac_ui.conf             界面选项
#   assets/fonts/*.ttf      TTF（FreeType 运行时读）
#
# 用法：sh scripts/deploy-device.sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-arm"
DEV_DIR=/root/workdayAlarmClock/lvgl

echo "== 编译（armv7 musl static）=="
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-arm-musl.cmake" \
    -DWAC_BACKEND=fbdev \
    -DWAC_PANEL=120x160
cmake --build "$BUILD"

echo "== 准备目录 =="
adb shell "mkdir -p $DEV_DIR/assets/fonts"

echo "== 推送 =="
adb push "$BUILD/wac" "$DEV_DIR/wac"
adb push "$ROOT/scripts/device-run.sh" "$DEV_DIR/run.sh"
adb push "$ROOT/assets/fonts/wac_cjk.ttf" "$DEV_DIR/assets/fonts/wac_cjk.ttf"
adb push "$ROOT/assets/fonts/zyymeclock.ttf" "$DEV_DIR/assets/fonts/zyymeclock.ttf"

echo "== 配置 =="
adb shell "cat > $DEV_DIR/wac_keys.conf <<'EOF'
confirm=116
prev=115
next=114
EOF
cat > $DEV_DIR/wac_ui.conf <<'EOF'
show_seconds=1
show_battery=0
EOF
chmod +x $DEV_DIR/wac $DEV_DIR/run.sh"

echo "== 完成 =="
adb shell "ls -l $DEV_DIR"

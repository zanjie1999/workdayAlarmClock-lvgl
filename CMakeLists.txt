cmake_minimum_required(VERSION 3.20)
project(wac C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- 配置项
# 后端：sim = Windows/GDI 窗口（开发用），fbdev = 真机 /dev/fb0，drm = 真机 DRM/KMS
set(WAC_BACKEND "sim" CACHE STRING "sim | fbdev | drm")
# 面板：aku(128x160) | 120x160 | 480x320 | 320x240 | 480x1280 | 800x1280 | 1024x600 ...
set(WAC_PANEL "aku" CACHE STRING "aku | 120x160 | 480x320 | 320x240 | 1280x480 | 800x1280 | 1024x600")
# 旋转角度（对物理面板）：0 | 90 | 180 | 270
set(WAC_ROT "" CACHE STRING "0|90|180|270，留空则用面板预设")
# 用 FreeType 直接读 TTF（默认开）。关掉会回落到 LVGL 内置 Montserrat，中文不可见。
option(WAC_FREETYPE "Render text from TTF via FreeType" ON)
# 设备的数据分区只有 17MB：调试段和符号表运行时完全用不到，直接不产出。
# 编译期 -g0 从源头上不生成 .debug_*（mips 那份光调试段就 5.1MB），
# 链接期 -s 再把 .symtab / .strtab 也去掉：8.6MB -> 1.8MB。
option(WAC_STRIP "Release 下不产调试信息（-g0 + 链接 -s）" ON)
if(WAC_STRIP AND NOT MSVC AND NOT WAC_BACKEND STREQUAL "sim")
    add_compile_options(-g0)
endif()

# ---------------------------------------------------------------- FreeType
# 只开 TrueType 必需的一部分：关掉 zlib/png/bzip2/brotli/harfbuzz，静态库约 500KB。
# 真机上如果系统已有 libfreetype，也可以改成 find_package(Freetype) 链接系统库。
if(WAC_FREETYPE)
    set(FT_DISABLE_ZLIB      ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2     ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG       ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ  ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI    ON  CACHE BOOL "" FORCE)
    set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------- 面板预设
if(WAC_PANEL STREQUAL "aku")
    set(_pw 128)
    set(_ph 160)
    set(_rot 90) # README 用 transpose=2，实测可能需 270
elseif(WAC_PANEL STREQUAL "120x160")
    # 目标板：128x160 的面板（可见区 120 列）× 两页 128x320，转过 90° 用 → 逻辑 160x120
    set(_pw 120)
    set(_ph 160)
    set(_rot 90)
elseif(WAC_PANEL STREQUAL "480x320")
    # 目标板：RNS 音箱（MIPS），fb0 实测 480x320p / 32bpp / line 1920。
    # 横屏直接用，不旋转（yres_virtual 是 640，含第二页，别拿来算行号）
    set(_pw 480)
    set(_ph 320)
    set(_rot 0)
elseif(WAC_PANEL STREQUAL "320x240")
    set(_pw 320)
    set(_ph 240)
    set(_rot 0)
elseif(WAC_PANEL STREQUAL "1280x480")
    # 作业帮辅导机 / sprd SL8541E（DSI icnl9707）：物理是 1280x480 横屏，
    # 但 DSI 扫描是转 90° 的 600x1280、可见区只有左边 480 列。
    # DRM 后端会再把画面顺时针转 90° 铺进扫描缓冲（见 hal/disp_drm.c）。
    set(_pw 1280)
    set(_ph 480)
    set(_rot 0)
elseif(WAC_PANEL STREQUAL "800x1280")
    set(_pw 800)
    set(_ph 1280)
    set(_rot 0)
elseif(WAC_PANEL STREQUAL "1024x600")
    set(_pw 1024)
    set(_ph 600)
    set(_rot 0)
else()
    message(FATAL_ERROR "Unknown WAC_PANEL=${WAC_PANEL}")
endif()
if(NOT WAC_ROT STREQUAL "")
    set(_rot ${WAC_ROT})
endif()

# 逻辑分辨率 = 旋转后的方向
math(EXPR _swap "${_rot} % 180")
if(_swap)
    set(_lw ${_ph})
    set(_lh ${_pw})
else()
    set(_lw ${_pw})
    set(_lh ${_ph})
endif()

message(STATUS "wac: backend=${WAC_BACKEND} panel=${_pw}x${_ph} rot=${_rot} => logical ${_lw}x${_lh}")

# ---------------------------------------------------------------- LVGL
set(LV_BUILD_CONF_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl)

if(WAC_FREETYPE)
    target_compile_definitions(lvgl PUBLIC WAC_FREETYPE=1)
    # LVGL 的 freetype 模块要 include <ft2build.h>，链接信息一并传给 lvgl
    target_link_libraries(lvgl PUBLIC freetype)
endif()

# LVGL 的 src 里有 neon/helium 的 .S，os_desktop.cmake 会无条件 GLOB 进来。
# 它们靠 LV_USE_DRAW_SW_ASM 在源码里自我屏蔽，但预处理阶段仍会把 C 头塞进汇编器
# （MinGW 直接报 "no such instruction: typedef unsigned short uint_least16_t"）。
# 我们不用这两个优化路径（ARM 板子也走纯 C 软件混合），全部剔除。
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# LVGL 的半透明填充（lv_draw_sw_blend_to_rgb565.c 里"一次混两个像素"的快路径）
# 用了 uint32_t 指针去读写 uint16_t 缓冲区，属于依赖严格别名的写法。zig cc 在
# armv7 上会把它编成"每列只写一个像素"：带 50% 遮罩时整屏出现隔列条纹。
# 关掉严格别名优化即可，别去改 LVGL 源码。
target_compile_options(lvgl PRIVATE -fno-strict-aliasing)

if(WAC_BACKEND STREQUAL "sim")
    target_compile_definitions(lvgl PUBLIC WAC_SIM=1)
endif()

# ---------------------------------------------------------------- 源文件
# 开发期用 GLOB + CONFIGURE_DEPENDS：新增 .c 不用改 CMake（发布前可换成显式列表）
file(GLOB_RECURSE WAC_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

if(WAC_BACKEND STREQUAL "sim")
    list(FILTER WAC_SRC EXCLUDE REGEX "hal/disp_fbdev\\.c|hal/disp_drm\\.c|hal/indev_touch\\.c|proto/go_proc\\.c|proto/state_http\\.c")
elseif(WAC_BACKEND STREQUAL "drm")
    list(FILTER WAC_SRC EXCLUDE REGEX "hal/disp_sim\\.c|hal/disp_fbdev\\.c|proto/mock_go\\.c")
else()
    list(FILTER WAC_SRC EXCLUDE REGEX "hal/disp_sim\\.c|hal/disp_drm\\.c|proto/mock_go\\.c")
endif()

add_executable(wac ${WAC_SRC})

target_include_directories(wac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/src/state
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nav
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)
target_compile_definitions(wac PRIVATE
    WAC_PANEL_W=${_pw}
    WAC_PANEL_H=${_ph}
    WAC_ROT=${_rot}
    WAC_LCD_W=${_lw}
    WAC_LCD_H=${_lh}
)
if(WAC_BACKEND STREQUAL "sim")
    target_compile_definitions(wac PRIVATE WAC_SIM=1)
endif()
if(WAC_BACKEND STREQUAL "drm")
    target_compile_definitions(wac PRIVATE WAC_DRM=1)
endif()
if(WAC_FREETYPE)
    target_compile_definitions(wac PRIVATE WAC_FREETYPE=1)
endif()

target_link_libraries(wac PRIVATE lvgl)

if(WAC_BACKEND STREQUAL "sim")
    # LVGL 的 windows 驱动只用 GDI/User32，不需要 SDL
    target_link_libraries(wac PRIVATE gdi32 user32)
else()
    target_link_libraries(wac PRIVATE m pthread)
    # 静态 musl + -s：只留 loader 需要的段，8.6MB -> 1.8MB
    if(WAC_STRIP)
        target_link_options(wac PRIVATE -s)
    endif()
endif()

# 警告：自己代码严格，LVGL 头文件不报
target_compile_options(wac PRIVATE -Wall -Wextra -Wno-unused-parameter)

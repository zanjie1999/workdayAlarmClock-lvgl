# 目标板交叉编译：armv7 + musl，静态链接。
#
# 用 Zig 当交叉编译器（Windows 原生，自带 musl libc 和 arm 目标支持）：
#   zig cc -target arm-linux-musleabihf
# 板子是 uClibc（TinaLinux / sun5i），系统 loader 是 ld-uClibc.so.0，
# 所以二进制一律 -static，运行时零依赖。
#
# 环境变量 WAC_ZIG 可覆盖 zig.exe 位置。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{WAC_ZIG})
    set(WAC_ZIG "$ENV{WAC_ZIG}")
else()
    set(WAC_ZIG "D:/C/toolchain/zig/zig-windows-x86_64-0.13.0/zig.exe")
endif()

# zig 用 "cc" 子命令当编译器；-target 必须放在 flags 里，
# 这样编译和链接（CMake 链接时也带 CFLAGS）都会走对目标。
set(CMAKE_C_COMPILER "${WAC_ZIG}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER "${WAC_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")

# zig 的 CPU 名字用 LLVM 的写法（cortex_a8，没有连字符），
# 写成 -march=armv7-a 会被当成 -mcpu=armv7 然后报 unknown CPU。
set(CMAKE_C_FLAGS_INIT "-target arm-linux-musleabihf -mcpu=cortex_a8")
set(CMAKE_CXX_FLAGS_INIT "-target arm-linux-musleabihf -mcpu=cortex_a8")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

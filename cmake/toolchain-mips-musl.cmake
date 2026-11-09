# 目标机交叉编译：mipsel + musl，静态链接。
#
# 目标设备：MIPS 的 RNS 音箱（假定 Ingenic 一类，mipsel 小端、MIPS32r2）。
# 与 arm 那份一样用 Zig 当交叉编译器：
#   zig cc -target mipsel-linux-musl -mcpu=mips32r2
#
# 注意 Zig 0.13 不提供 `mipsel-linux-musleabi` 这套 ABI 名下的 libc
# （会报 "unable to find or provide libc"），必须用 `mipsel-linux-musl`。
#
# 环境变量 WAC_ZIG 可覆盖 zig 位置（CI 里传 `$(command -v zig)`）。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)

if(DEFINED ENV{WAC_ZIG})
    set(WAC_ZIG "$ENV{WAC_ZIG}")
else()
    find_program(WAC_ZIG zig)
endif()

# CPU 可覆盖：mips32r2 / mips32 / mips64r2 ...
if(NOT WAC_MCPU)
    set(WAC_MCPU "mips32r2")
endif()

# 目标三元组可覆盖：默认小端，大端设备改成 mips-linux-musl
if(NOT WAC_TARGET)
    set(WAC_TARGET "mipsel-linux-musl")
endif()

set(CMAKE_C_COMPILER "${WAC_ZIG}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER "${WAC_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")

set(CMAKE_C_FLAGS_INIT "-target ${WAC_TARGET} -mcpu=${WAC_MCPU}")
set(CMAKE_CXX_FLAGS_INIT "-target ${WAC_TARGET} -mcpu=${WAC_MCPU}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

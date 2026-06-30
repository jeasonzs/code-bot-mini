cmake_minimum_required(VERSION 3.16)
project(codebot_firmware
    VERSION 0.17
    DESCRIPTION "Code Bot USB info display firmware for CH32X033F8P6"
    LANGUAGES C ASM
)

# =============================================================================
# Toolchain Configuration
# =============================================================================
# 使用 WCH 官方 GCC 工具链, 通过环境变量 WCH_SDK_ROOT 配置 WCH SDK 路径
# 默认值匹配本机路径, 可被 -DWCH_SDK_ROOT=... 覆盖

set(WCH_SDK_ROOT
    "$ENV{WCH_SDK_ROOT}"
    CACHE PATH "WCH SDK 根目录 (含 EVT/ Toolchain/ OpenOCD/ 子目录)"
)

if(NOT WCH_SDK_ROOT)
    message(FATAL_ERROR
        "WCH_SDK_ROOT 未设置. 请运行:\n"
        "  export WCH_SDK_ROOT=/path/to/wch-sdk\n"
        "或使用 -DWCH_SDK_ROOT=/path/to/wch-sdk 传递给 cmake"
    )
endif()

# 校验 SDK 路径
set(WCH_EVT_SRC  "${WCH_SDK_ROOT}/EVT/EXAM/SRC")
set(WCH_TOOLCHAIN_BIN "${WCH_SDK_ROOT}/Toolchain/RISC-V Embedded GCC15/bin")
set(WCH_OPENOCD_BIN   "${WCH_SDK_ROOT}/OpenOCD/OpenOCD/bin/openocd")
set(WCH_OPENOCD_CFG   "${WCH_SDK_ROOT}/OpenOCD/OpenOCD/bin/wch-riscv.cfg")
set(WCH_FLASH_TOOL    "wchisp" CACHE STRING "Flash tool: wchisp or openocd")

if(NOT EXISTS "${WCH_EVT_SRC}/Ld/Link.ld")
    message(FATAL_ERROR "WCH Link.ld not found at ${WCH_EVT_SRC}/Ld/. 请检查 WCH_SDK_ROOT.")
endif()

# 设置交叉编译器
set(CMAKE_C_COMPILER   "${WCH_TOOLCHAIN_BIN}/riscv32-wch-elf-gcc")
set(CMAKE_OBJCOPY      "${WCH_TOOLCHAIN_BIN}/riscv32-wch-elf-objcopy")
set(CMAKE_SIZE         "${WCH_TOOLCHAIN_BIN}/riscv32-wch-elf-size")
set(CMAKE_ASM_COMPILER "${WCH_TOOLCHAIN_BIN}/riscv32-wch-elf-gcc")

# 验证工具链
if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR
        "WCH GCC 工具链未找到: ${CMAKE_C_COMPILER}\n"
        "请检查 WCH_SDK_ROOT 或设置 -DWCH_TOOLCHAIN_BIN=..."
    )
endif()

# 不检测 C++ 等
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =============================================================================
# Compile Flags (从 EVT .cproject 提取)
# =============================================================================
set(CMAKE_C_FLAGS "-march=rv32imac_zicsr -mabi=ilp32 -msave-restore -Os -ffunction-sections -fdata-sections -fno-common -fsigned-char -Wall -Wunused -Wuninitialized -ffreestanding -nostdlib -nostartfiles -fno-builtin")

# Debug build 优化降级
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

# =============================================================================
# Linker Configuration (使用 WCH 官方 Link.ld)
# =============================================================================
# -nostartfiles: 不链接 crt0 (我们有 startup_ch32x035.S)
# -nostdlib:    不链接 newlib (libc 我们自己用 libc_stubs.c 提供)
# libgcc.a 通过 target_link_libraries 在末尾追加, 这样静态库成员扫描能找到引用
set(CMAKE_EXE_LINKER_FLAGS "-T${WCH_EVT_SRC}/Ld/Link.ld -Wl,--gc-sections -Wl,--print-memory-usage -nostartfiles -nostdlib")

# libgcc.a 绝对路径 (在 -nostdlib 下不被默认查找, 显式指定)
set(WCH_LIBGCC "${WCH_TOOLCHAIN_BIN}/../lib/gcc/riscv32-wch-elf/15.2.0/libgcc.a")

# =============================================================================
# Include Directories
# =============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${WCH_EVT_SRC}/Peripheral/inc
    ${WCH_EVT_SRC}/Core
    ${WCH_EVT_SRC}/Debug
)

# =============================================================================
# Source Files
# =============================================================================

# --- 我们自己的源文件 ---
file(GLOB OUR_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/util/*.c"
    "${CMAKE_SOURCE_DIR}/src/display/*.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/*.c"
    "${CMAKE_SOURCE_DIR}/src/app/*.c"
)

# --- 我们自己的 USB 描述符 + 端点 (vendor 副本 + 端点处理) ---
# 库副本在 src/usb/ch32x035_usbfs_device.{c,h}, 我们 patch 过加了 EP3 + DataUp
# usbd_compatibility_hid.{c,h} 是从 WCH 剥过来的薄壳, 只留 HID 符号
list(APPEND OUR_SOURCES
    "${CMAKE_SOURCE_DIR}/src/usb/usb_desc.c"
    "${CMAKE_SOURCE_DIR}/src/usb/usb_endp.c"
    "${CMAKE_SOURCE_DIR}/src/usb/hid_kbd.c"
    "${CMAKE_SOURCE_DIR}/src/usb/ch32x035_usbfs_device.c"
    "${CMAKE_SOURCE_DIR}/src/usb/usbd_compatibility_hid.c"
)

# --- ch32x035_it.c (WCH 模板的弱中断处理, 我们自己覆盖) ---
list(APPEND OUR_SOURCES
    "${CMAKE_SOURCE_DIR}/src/ch32x035_it.c"
)

# --- WCH SDK 源文件 (不复制, 直接引用绝对路径) ---
set(WCH_PERIPH_SOURCES
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_gpio.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_rcc.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_spi.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_i2c.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_dma.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_exti.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_misc.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_flash.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_pwr.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_usart.c
    ${WCH_EVT_SRC}/Peripheral/src/ch32x035_tim.c
    ${WCH_EVT_SRC}/Core/core_riscv.c
    ${WCH_EVT_SRC}/Debug/debug.c
    # ch32x035_usbfs_device.c 已 vendor 到 src/usb/ (上面 OUR_SOURCES 里), 不再 link WCH_EVT_USB 副本
)

# --- WCH SDK 启动文件 ---
set(WCH_ASM_SOURCES
    ${WCH_EVT_SRC}/Startup/startup_ch32x035.S
)

# =============================================================================
# Build Target
# =============================================================================
add_executable(firmware.elf
    ${OUR_SOURCES}
    ${WCH_PERIPH_SOURCES}
    ${WCH_ASM_SOURCES}
)

# libgcc 提供 __riscv_save/restore 等编译器运行时 helper (在末尾追加, 让 archive 扫描能找到引用)
target_link_libraries(firmware.elf "${WCH_LIBGCC}")

# =============================================================================
# Post-Build: 生成 .hex / .bin
# =============================================================================
add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:firmware.elf> firmware.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware.elf> firmware.bin
    COMMENT "Generated firmware.hex / firmware.bin"
)

# =============================================================================
# size target - 显示 Flash/RAM 占用
# =============================================================================
add_custom_target(size
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware.elf>
    DEPENDS firmware.elf
    USES_TERMINAL
    COMMENT "Show firmware size"
)

# =============================================================================
# flash target - 通过 wchisp 烧录 (USB ISP)
# =============================================================================
find_program(WCHISP_EXE ${WCH_FLASH_TOOL})

if(NOT WCHISP_EXE)
    message(WARNING
        "wchisp 工具未找到. 请运行: pip install wchisp\n"
        "或设置 -DWCH_FLASH_TOOL=openocd 切换到 openocd (需 WCH-LinkE)"
    )
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo "ERROR: wchisp not found. Install: pip install wchisp"
    )
else()
    add_custom_target(flash
        COMMAND ${WCHISP_EXE} flash $<TARGET_FILE:firmware.elf>
        DEPENDS firmware.elf
        USES_TERMINAL
        COMMENT "Flashing via ${WCH_FLASH_TOOL}"
    )
endif()

# =============================================================================
# clean target - 删 build/
# =============================================================================
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Remove build directory"
)

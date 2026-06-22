cmake_minimum_required(VERSION 3.12.4)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/arm-toolchain.cmake"
    CACHE STRING "Toolchain file")

project(rgb_lcd C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Board's NFS-mounted rootfs; build artifacts are auto-deployed here.
set(NFS_ROOTFS_DIR "$ENV{HOME}/imx6u-workbench/nfs/root"
    CACHE PATH "Board's NFS-mounted root filesystem")

# ---- LVGL (v9.3) static library --------------------------------------------
# Keep the build lean: no demos, examples, or bundled ThorVG. Point LVGL at our
# lv_conf.h via LV_BUILD_CONF_PATH (LVGL adds its directory to the include path).
set(CONFIG_LV_BUILD_DEMOS         OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h"
    CACHE FILEPATH "" FORCE)

add_subdirectory(lvgl)

# LVGL globs in hand-written assembly (NEON/Helium) SW-blend kernels. We build
# with LV_USE_DRAW_SW_ASM=NONE so they are empty, but their unconditional header
# include trips up the cross-assembler (glibc headers ignore __ASSEMBLY__).
# Drop the .S sources from the target instead of patching the vendored tree.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# ---- Sensor kernel modules --------------------------------------------------
add_subdirectory(driver)

# ---- Application ------------------------------------------------------------
add_subdirectory(app)

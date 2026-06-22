# Two out-of-tree kernel modules for the sensors this app displays:
#   ap3216c.ko  — AP3216C ALS/PS/IR over I2C (IIO)
#   icm20608.ko — ICM20608 accel/gyro/temp over SPI (IIO)
# Built via the kernel's Kbuild; the resulting .ko files auto-deploy to NFS.

set(KERNEL_DIR   "$ENV{HOME}/imx6u-workbench/linux" CACHE PATH "Kernel source directory")
set(ARCH         "arm" CACHE STRING "Target architecture")
# Absolute cross-compiler prefix: the toolchain is not assumed to be on PATH.
set(CROSS_COMPILE
    "/opt/arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-"
    CACHE STRING "Cross-compiler prefix")

set(MODULE_SOURCES ap3216c icm20608)

# Kbuild builds whatever obj-m lists; symlink each source next to the Kbuild.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Kbuild "obj-m := ap3216c.o icm20608.o\n")
foreach(mod ${MODULE_SOURCES})
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/${mod}.c
            ${CMAKE_CURRENT_BINARY_DIR}/${mod}.c
    )
endforeach()

# Deploy commands, one per module.
set(_deploy_cmds
    COMMAND ${CMAKE_COMMAND} -E make_directory ${NFS_ROOTFS_DIR})
foreach(mod ${MODULE_SOURCES})
    list(APPEND _deploy_cmds
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${mod}.ko ${NFS_ROOTFS_DIR}/)
endforeach()

add_custom_target(sensor_drivers ALL
    COMMAND $(MAKE) -C ${KERNEL_DIR}
            M=${CMAKE_CURRENT_BINARY_DIR}
            ARCH=${ARCH}
            CROSS_COMPILE=${CROSS_COMPILE}
            modules
    ${_deploy_cmds}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building ap3216c.ko + icm20608.ko and deploying to NFS rootfs"
)

add_custom_target(clean_module
    COMMAND $(MAKE) -C ${KERNEL_DIR}
            M=${CMAKE_CURRENT_BINARY_DIR}
            ARCH=${ARCH}
            CROSS_COMPILE=${CROSS_COMPILE}
            clean
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning kernel module build artifacts"
)

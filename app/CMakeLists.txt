add_executable(rgb_lcd_app
    main.c
    ui.c
    sensors.c
)

# `lvgl` exports its root dir as a public include path, so the sources can use
# #include "lvgl.h"; -lm is needed for the floating-point scaling math.
target_link_libraries(rgb_lcd_app PRIVATE lvgl m)

# Auto-deploy the cross-built binary and the on-target helper script into the
# board's NFS rootfs.
add_custom_command(TARGET rgb_lcd_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${NFS_ROOTFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rgb_lcd_app> ${NFS_ROOTFS_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/../misc/load_and_run.sh ${NFS_ROOTFS_DIR}/
    COMMENT "Deploying rgb_lcd_app + load_and_run.sh to NFS rootfs (${NFS_ROOTFS_DIR})"
)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_DIR /opt/arm-none-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_DIR}/bin/arm-none-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/bin/arm-none-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_DIR}/arm-none-linux-gnueabihf/libc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

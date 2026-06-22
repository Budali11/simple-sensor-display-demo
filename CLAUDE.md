# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

This directory is a **complete sensor-display project**: an LVGL v9.3 userspace application
(`rgb_lcd_app`) that shows live sensor data on the ATK 4.3" RGB LCD via the Linux
framebuffer (`/dev/fb0`), plus the two IIO sensor kernel modules it consumes. Data is read
from `/sys/bus/iio/devices/iio:deviceN/...`.

Layout:
- `driver/` — the two sensor kernel modules, copied/renamed from the sibling `i2cd`/`spid`
  directories: `ap3216c.c` (AP3216C ALS/PS/IR over I2C) and `icm20608.c` (ICM20608
  accel/gyro/temp over SPI). Both build via Kbuild into `ap3216c.ko`/`icm20608.ko`.
  Note: `ap3216c_read_raw()` returns the values cached by the background poll
  (`read_work_func`) instead of doing I2C transfers, so a sysfs read never blocks the bus.
  These are byte-identical in behaviour to `i2cd`/`spid` and bind the same `compatible`
  strings (`dunnan,ap3216c`, `tdk,icm20608`) — load only one set on the target.
- `app/` — application sources: `main.c` (LVGL init, fbdev display, update loop),
  `ui.c`/`ui.h` (two `lv_win` windows, one per sensor), `sensors.c`/`sensors.h`
  (IIO sysfs reader; devices located by `name`, not a fixed index).
- `lvgl/` — vendored LVGL v9.3 (shallow clone of `release/v9.3`), built as a static lib.
- `lv_conf.h` — LVGL config (color depth, fbdev backend, fonts). `LV_COLOR_DEPTH` **must**
  match the framebuffer bpp; the app prints the real bpp at startup and aborts on mismatch.
- `CMakeLists.txt` + `app/CMakeLists.txt` + `arm-toolchain.cmake` — cross build; the app
  binary auto-deploys to `~/imx6u-workbench/nfs/root/`.
- `docs/` — reference PDFs: `ATK-4.3RGBLCD(...).pdf` (panel datasheet, 800x480 / 480x272),
  `IMX6ULL_ALPHA_V2.2(底板原理图).pdf` (baseboard schematic),
  `IMX6ULL_CORE_V1.6(核心板原理图).pdf` (core board schematic).
- `misc/` — scratch space for test scripts / notes.

### Build
```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=arm-toolchain.cmake
cmake --build build            # liblvgl.a, ap3216c.ko, icm20608.ko, rgb_lcd_app -> nfs/root
cmake --build build --target clean_module   # clean kernel module artifacts
```
On-target: `insmod ap3216c.ko && insmod icm20608.ko`, then run `./rgb_lcd_app`.

## Repo context

This directory is one lesson/module inside `~/imx6u-workbench`, a workspace for ATK (正点原子) i.MX6ULL embedded Linux driver development. Siblings under `drivers/` (`blkd`, `i2cd`, `interrupt`, `mykey`, `myled`, `spid`, `timer_led`) are each a self-contained out-of-tree kernel module + matching userspace test app, all built the same way. `rgb-lcd` should follow that same shape once populated.

Key paths in the wider workbench:
- `~/imx6u-workbench/linux` — kernel source (`KERNEL_DIR` for module builds). Board DTS lives at `arch/arm/boot/dts/nxp/imx/imx6ull-alientek-emmc.dts`.
- `~/imx6u-workbench/buildroot-2026.02.2` — root filesystem build.
- `~/imx6u-workbench/nfs` — NFS-exported rootfs the target board mounts during development; built `.ko`/app binaries get copied here to test on-target.
- `~/imx6u-workbench/tftpboot` — TFTP directory for serving the kernel image to the board.
- `~/imx6u-workbench/.tmuxinator.yml` — dev workspace layout (`tmuxinator start .` from `~/imx6u-workbench`); sets `ARCH=arm` and `CROSS_COMPILE=arm-none-linux-gnueabihf-` in every pane.

## Established per-driver pattern (from sibling directories)

Each `drivers/<name>/` directory contains:
- `<name>.c` (or a module-specific name, e.g. `mykey/key.c`) — a kernel module built out-of-tree against `KERNEL_DIR`, implemented as a `platform_driver` matched via an `of_device_id` table against a custom `compatible` string (e.g. `"myled"`, `"mykey"`), exposing a char device (`cdev` + `class_create`/`device_create`) under `/dev/<name>`.
- `<name>_app.c` or `<name>_app.cpp` — a small native userspace program that `open()`s the `/dev` node and exercises it via `read`/`write` (see `myled/led_app.c` for the simplest example: `led_app <dev> <0|1>`).
- `CMakeLists.txt` — identical template across all siblings:
  - `project(<name> C)` or `C CXX`
  - `KERNEL_DIR` cached path to `linux-7.1-rc5`
  - `CROSS_COMPILE` = `arm-none-linux-gnueabihf-`, `ARCH` = `arm`
  - Writes a generated `Kbuild` (`obj-m := <name>.o`) into the build dir and symlinks the source next to it, so out-of-tree `make ... modules` builds the `.ko`
  - A `<name>` custom target driving the kernel module build (`make -C $KERNEL_DIR M=<build-dir> ARCH=arm CROSS_COMPILE=... modules`) plus `gen_compile_commands.py` for clangd support
  - `add_executable(<name>_app <name>_app.c[pp])` for the userspace test binary (cross-compiled via the toolchain file)
  - A `clean_module` custom target for `make ... clean`
- `arm-toolchain.cmake` — CMake toolchain file pointing `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` at the `arm-none-linux-gnueabihf-` cross compiler, used as `CMAKE_TOOLCHAIN_FILE` so the `add_executable` app cross-builds.

Adding a new driver also requires a device tree node + pinctrl group in `imx6ull-alientek-emmc.dts` with a matching `compatible` string (see the `myled`/`mykey` nodes there for the convention) — the kernel/DTS must be rebuilt and redeployed (via `tftpboot`/`nfs`) for the module to bind.

## Build commands (once source exists, matching siblings)

From within this directory:
```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-toolchain.cmake
cmake --build build              # builds the .ko (via kernel Kbuild) and the _app binary
cmake --build build --target clean_module   # kernel module clean
```
Build artifacts (`*.ko`, `*_app`, `compile_commands.json`) land in `build/`. Deploy by copying the `.ko` and `_app` binary into `~/imx6u-workbench/nfs` (the board's NFS rootfs) and loading the module with `insmod`/`depmod` on-target.

# rgb-lcd — i.MX6ULL 传感器数据 LVGL 显示项目

在正点原子 i.MX6ULL 开发板 + ATK 4.3" RGB LCD 上，用 [LVGL v9.3](https://lvgl.io)
把两个传感器的数据实时显示到屏幕上，每个传感器占一个窗口。这是一个**完整项目**：
既包含两个传感器的内核驱动，也包含显示用的用户态应用。

- **AP3216C**（I2C）：环境光 (ALS) / 接近 (PS) / 红外 (IR)
- **ICM20608**（SPI）：三轴加速度 + 三轴陀螺仪 + 温度

应用通过 Linux **IIO 子系统**的 sysfs 接口
(`/sys/bus/iio/devices/iio:deviceN/...`) 读取数据，再通过 **framebuffer**
(`/dev/fb0`) 渲染界面，**纯显示、无触摸输入**。

---

## 目录结构

```
rgb-lcd/
├── CMakeLists.txt          # 顶层构建：交叉编译 + 集成 LVGL，串起 driver/ 和 app/
├── arm-toolchain.cmake     # 交叉工具链 (/opt/arm-none-linux-gnueabihf/)
├── lv_conf.h               # LVGL 配置（颜色深度、fbdev 后端、字体）
├── driver/                 # 两个传感器内核模块
│   ├── ap3216c.c           #   AP3216C ALS/PS/IR (I2C, IIO)
│   ├── icm20608.c          #   ICM20608 accel/gyro/temp (SPI, IIO)
│   └── CMakeLists.txt
├── app/                    # LVGL 用户态应用
│   ├── main.c              #   初始化、fbdev 显示、刷新循环
│   ├── ui.c / ui.h         #   两个 lv_win 窗口（每传感器一个）
│   ├── sensors.c / .h      #   IIO sysfs 读取（按 name 定位设备，不写死 deviceN）
│   └── CMakeLists.txt
├── lvgl/                   # LVGL v9.3 源码（浅克隆，编译为静态库）
├── misc/
│   └── load_and_run.sh     # 板上脚本：装驱动 + 跑 app
└── docs/                   # 参考资料（LCD 数据手册、底板/核心板原理图 PDF）
```

---

## 依赖与前提

构建机（主机）：

- 交叉工具链：`/opt/arm-none-linux-gnueabihf/`
- 内核源码树：`~/imx6u-workbench/linux/`（须已配置并能编译模块）
- CMake ≥ 3.12、`make`
- NFS 根文件系统：`~/imx6u-workbench/nfs/root/`（板子开发时挂载为 `/`）

内核 / 设备树（板端）：

- `CONFIG_DRM_MXSFB=y` + `CONFIG_DRM_FBDEV_EMULATION=y` → 提供 `/dev/fb0`
- IIO 子系统已开启
- 设备树中存在两个传感器节点，`compatible` 分别为
  `dunnan,ap3216c` 和 `tdk,icm20608`

---

## 获取源码

LVGL 以 git submodule 的形式引入（固定在 v9.3.0 对应提交），克隆时需带上子模块：

```sh
git clone --recursive git@github.com:Budali11/simple-sensor-display-demo.git
# 已经克隆但忘了 --recursive：
git submodule update --init --depth 1
```

## 构建

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=arm-toolchain.cmake
cmake --build build
```

一次构建会产出并**自动部署到 `~/imx6u-workbench/nfs/root/`**：

| 产物 | 说明 |
|------|------|
| `ap3216c.ko`    | AP3216C 内核模块 |
| `icm20608.ko`   | ICM20608 内核模块 |
| `rgb_lcd_app`   | LVGL 显示应用（ARM 可执行文件） |
| `load_and_run.sh` | 板上一键加载 + 运行脚本 |

清理内核模块构建产物：

```sh
cmake --build build --target clean_module
```

---

## 在板子上运行

板子启动并挂载好 NFS 根文件系统后，在 NFS 根目录（即上面那些产物所在目录）执行：

```sh
./load_and_run.sh
```

脚本会：

1. 卸载可能已加载的同类模块（含原始的 `i2cd` / `spid`，它们绑定同样的设备）；
2. `insmod ap3216c.ko` 和 `icm20608.ko`；
3. 打印注册成功的 IIO 设备；
4. 启动 `rgb_lcd_app`（前台运行，`Ctrl-C` 退出）。

只想加载驱动、不启动应用：

```sh
./load_and_run.sh --no-app
```

手动等价操作：

```sh
insmod ap3216c.ko
insmod icm20608.ko
./rgb_lcd_app
```

---

## 数据来源（IIO sysfs 属性）

应用按 `name` 自动定位设备目录（不依赖 `iio:device0/1` 的固定编号），读取：

**AP3216C**（`name = ap3216c`）

| 显示项 | sysfs 属性 |
|--------|-----------|
| 环境光 ALS | `in_illuminance_clear_raw` |
| 接近 PS    | `in_proximity_raw` |
| 红外 IR    | `in_illuminance_ir_raw` |

**ICM20608**（`name = icm20608`）

| 显示项 | sysfs 属性 |
|--------|-----------|
| 加速度 X/Y/Z | `in_accel_{x,y,z}_raw` × `in_accel_scale`（m/s²） |
| 角速度 X/Y/Z | `in_anglvel_{x,y,z}_raw` × `in_anglvel_scale`（rad/s） |
| 温度 | `in_temp_raw`（按数据手册换算成 ℃：`raw/326.8 + 25`） |

---

## 重要提示

- **颜色深度必须匹配 framebuffer**：`lv_conf.h` 中 `LV_COLOR_DEPTH = 32`
  （DRM fbdev 模拟通常是 XRGB8888）。应用启动时会打印实际的 `分辨率` 与 `bpp`；
  若与编译值不符会**直接报错并提示**对应数值——届时改 `lv_conf.h` 后重新编译即可
  （例如面板是 RGB565 就改成 16）。
- **不要同时加载新旧两套驱动**：`ap3216c.ko`/`icm20608.ko` 与原来的
  `i2cd.ko`/`spid.ko` 匹配同样的设备树节点，只能加载其中一套。`load_and_run.sh`
  已会先卸载旧的。
- 若界面显示 “no data”，说明对应驱动没加载成功或设备没探测到，检查
  `dmesg` 与 `/sys/bus/iio/devices/`。

---

## 实现要点

- LVGL 作为静态库 (`liblvgl.a`) 集成；构建时关掉了 demos / examples / ThorVG，
  并剔除了无法被交叉汇编器处理的 NEON/Helium 汇编源（`LV_USE_DRAW_SW_ASM=NONE`）。
- LVGL 的毫秒 tick 由 `clock_gettime(CLOCK_MONOTONIC)` 提供
  （`main.c` 中 `lv_tick_set_cb`）。
- 一个 200ms 周期的 `lv_timer` 读取两个传感器并刷新窗口。
- `ap3216c.c` 的 `read_raw` 回调直接返回后台轮询（每 500ms）缓存在 `prv_data`
  中的值，sysfs 读取不再阻塞 I2C 总线。

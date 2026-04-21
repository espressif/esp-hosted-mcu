# ESP-Hosted-MCU Kconfig 配置系统详解

本文档详细阐述 ESP-Hosted-MCU 项目中 Kconfig 配置语言的用法、VSCode 可视化配置、`sdkconfig.defaults.xxx` 与 `Kconfig.projbuild` 的关系，以及整套配置流程与原理。

---

## 1. Kconfig 语言基础

Kconfig 是 Linux 内核和 ESP-IDF 采用的配置描述语言，用于声明编译时可配置的选项、菜单、依赖关系和默认值。ESP-IDF 使用 `konfiglib`（Python 实现）解析 Kconfig 文件，生成交互式配置界面（menuconfig）以及最终的 `sdkconfig` 配置文件。

### 1.1 核心语法元素

| 关键字 | 作用 | 示例 |
|--------|------|------|
| `config` | 声明一个配置项 | `config ESP_HOSTED_ENABLED` |
| `bool` / `int` / `string` / `hex` | 指定配置项的数据类型 | `bool "Enable ESP-Hosted"` |
| `menu` | 创建一个可折叠的菜单组 | `menu "ESP-Hosted config"` |
| `choice` | 创建一组互斥的单选项 | `choice ESP_HOSTED_HOST_INTERFACE` |
| `depends on` | 定义可见性/可编辑性的前置条件 | `depends on ESP_HOSTED_ENABLED` |
| `select` | 自动选中（强制开启）另一个配置 | `select PM_ENABLE` |
| `default` | 指定默认值 | `default y if ESP_WIFI_REMOTE_ENABLED` |
| `range` | 限制整数配置项的取值范围 | `range 10 240` |
| `help` | 配置项的帮助说明文本 | `help ...` |
| `if` / `endif` | 条件块，控制内部配置的可见性 | `if ESP_HOSTED_SDIO_4_BIT_BUS` |
| `comment` | 在菜单中显示只读注释 | `comment "Warning: ..."` |
| `source` / `rsource` | 引入外部 Kconfig 文件 | `rsource "Kconfig.light_sleep"` |

### 1.2 配置项命名规则

ESP-IDF 生成的所有配置项宏均以 `CONFIG_` 为前缀。例如：

- Kconfig 中声明：`config ESP_HOSTED_SPI_HOST_INTERFACE`
- C 代码中使用：`#ifdef CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE`
- CMake 中使用：`if(CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)`

### 1.3 `select` 与 `depends on` 的区别

| 特性 | `depends on` | `select` |
|------|-------------|----------|
| 方向 | 子项依赖父项 | 父项强制开启子项 |
| 用户可见性 | 不满足时隐藏 | 被选中后用户仍可看到 |
| 典型用途 | 条件显示菜单 | 自动开启必须的依赖功能 |

在 `slave/main/Kconfig.light_sleep` 中，`select PM_ENABLE` 用于在开启轻睡眠模式时自动启用电源管理，避免用户遗漏关键依赖。

---

## 2. 本项目 Kconfig 文件体系

ESP-Hosted-MCU 项目包含 host（主机）和 slave（协处理器）两套固件，因此配置体系也分为两个层面：

### 2.1 根目录 `Kconfig`（Host 侧配置）

**路径**: `Kconfig`（2238 行）

该文件定义了 **host 侧**（运行 `esp_wifi_remote` 的 MCU）的配置选项。主要内容：

- **组件总开关**：`ESP_HOSTED_ENABLED`
- **协处理器目标选择**：`ESP_HOSTED_CP_TARGET_ESP32/C3/C6...`，与 `esp_wifi_remote` 组件联动
- **开发板 GPIO 预设**：`ESP_HOSTED_P4_DEV_BOARD_*`，为 ESP32-P4 系列开发板自动映射引脚
- **传输层选择**：`ESP_HOSTED_HOST_INTERFACE`（SPI Full-duplex / SDIO / SPI Half-duplex / UART）
- **详细传输参数**：
  - SPI 模式、控制器选择、GPIO 映射
  - SDIO 位宽、Slot 选择、GPIO 映射
  - UART 波特率、数据位、校验位、停止位
- **蓝牙模式**：NimBLE VHCI、BlueDroid VHCI、UART HCI、禁用
- **功能开关**：Network Split、Host Power Save、GPIO Expander、Peer Data Transfer

**关键设计**：此文件中的 GPIO 默认值大量使用 `if IDF_TARGET_ESP32P4`、`if ESP_HOSTED_P4_C6_CORE_BOARD` 等条件，实现**开发板级别的自动引脚映射**。用户选择开发板后，相关 GPIO 自动填入正确值，无需手动配置。

### 2.2 Slave 侧 `Kconfig.projbuild`

**路径**: `slave/main/Kconfig.projbuild`（1661 行）

该文件定义了 **slave（协处理器 ESP）** 固件的配置选项。`Kconfig.projbuild` 是 ESP-IDF 的**特殊命名约定**：放在 `main/` 目录下的 `Kconfig.projbuild` 会被构建系统自动加载，用于定义项目级别（project-level）的配置。

主要内容：

- **示例配置菜单**（`menu "Example Configuration"`）
  - `ESP_HOSTED_COPROCESSOR`：标记当前为协处理器模式
  - `ESP_HOSTED_CP_BT`：是否通过 Hosted 共享蓝牙给 Host
  - `ESP_HOSTED_CP_WIFI`：是否在协处理器上启用 Wi-Fi
  - `ESP_HOSTED_COPROCESSOR_APP_MAIN`：是否使用内置的 `app_main()`
- **开发板选择**：`ESP_HOST_DEV_BOARD_*`
- **传输层配置**：`ESP_HOST_INTERFACE`（SPI/SDIO/SPI-HD/UART）及详细参数
- **Network Split**：网络分流功能的端口范围配置
- **Host Power Save**：Host 休眠时的总线卸载、唤醒配置
- **Light Sleep**：`rsource "Kconfig.light_sleep"` 引入轻睡眠电源管理
- **Mempool**：共享内存池配置
- **统计与调试**：`ESP_HOSTED_FUNCTION_PROFILING`、`ESP_PKT_STATS`

**与根目录 Kconfig 的关系**：

| 维度 | 根目录 `Kconfig` | `slave/main/Kconfig.projbuild` |
|------|-----------------|-------------------------------|
| 所属固件 | Host MCU | Slave ESP |
| 命名空间 | `ESP_HOSTED_*`（Host 视角） | `ESP_*` / `ESP_HOSTED_*`（Slave 视角） |
| 默认加载 | ESP-IDF 组件自动加载 | `main/` 目录自动加载 |
| GPIO 语义 | Host 侧 GPIO 号 | Slave 侧 GPIO 号 |

### 2.3 `slave/main/Kconfig.light_sleep`

**路径**: `slave/main/Kconfig.light_sleep`（224 行）

该文件通过 `rsource "Kconfig.light_sleep"` 被 `Kconfig.projbuild` 引入（第 1326 行）。`rsource` 是**相对路径引用**，表示从当前文件所在目录查找被引用文件。

设计亮点：

- **四级电源模式**：Disabled / Minimum（开发模式）/ Maximum（生产模式）/ Manual（手动）
- **一键自动依赖**：`Minimum` 和 `Maximum` 模式通过 `select` 自动开启 `PM_ENABLE`、`FREERTOS_USE_TICKLESS_IDLE`、`ESP_PHY_MAC_BB_PD` 等必需配置
- **手动模式诊断**：当选择 `Manual` 时，菜单会动态显示缺失的依赖项（带红色警告注释），并提示用户去哪个菜单开启
- **条件注释**：大量 `comment ... depends on !XXX` 实现动态状态提示

```kconfig
config ESP_HOSTED_LIGHT_SLEEP_POWER_MIN
    bool "Minimum Power Saving (development mode)"
    select PM_ENABLE
    select FREERTOS_USE_TICKLESS_IDLE
    select ESP_PHY_MAC_BB_PD if BT_ENABLED
```

---

## 3. `sdkconfig.defaults` 与 `sdkconfig.defaults.xxx`

### 3.1 文件作用

`sdkconfig.defaults` 是 **ESP-IDF 的默认配置文件机制**。当项目中不存在 `sdkconfig`（或执行 `idf.py fullclean` 后），构建系统会按以下优先级加载默认值：

1. `sdkconfig.defaults.{目标芯片}`（如 `sdkconfig.defaults.esp32c6`）
2. `sdkconfig.defaults`

这些文件的语法是**键值对**，直接写 `CONFIG_XXX=y` 或 `CONFIG_XXX=值`，等价于用户在 menuconfig 中的操作。

### 3.2 Slave 目录中的文件清单

```
slave/
├── sdkconfig.defaults              # 通用默认配置（所有芯片）
├── sdkconfig.defaults.esp32        # ESP32 专用
├── sdkconfig.defaults.esp32c2      # ESP32-C2 专用
├── sdkconfig.defaults.esp32c3      # ESP32-C3 专用
├── sdkconfig.defaults.esp32c5      # ESP32-C5 专用
├── sdkconfig.defaults.esp32c6      # ESP32-C6 专用
├── sdkconfig.defaults.esp32c61     # ESP32-C61 专用
├── sdkconfig.defaults.esp32h2      # ESP32-H2 专用
├── sdkconfig.defaults.esp32h4      # ESP32-H4 专用
├── sdkconfig.defaults.esp32s2      # ESP32-S2 专用
└── sdkconfig.defaults.esp32s3      # ESP32-S3 专用
```

### 3.3 配置分层策略

**`sdkconfig.defaults`（通用层）**：

```
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_ONLY=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_TWO_OTA=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_WIFI_NVS_ENABLED=y
CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_PERF=y
```

通用层设置**所有芯片共用的基础配置**：开启蓝牙控制器模式、4MB Flash、双 OTA 分区、1000Hz FreeRTOS 节拍、NVS 存储 Wi-Fi 配置、编译器性能优化。

**`sdkconfig.defaults.esp32c6`（芯片特定层）**：

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y
CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.esp32c6.csv"
CONFIG_WIFI_CMD_BASIC_ONLY=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
# ... 大量 Wi-Fi/LWIP 缓冲区调优
```

芯片特定层覆盖或追加：
- **CPU 频率**：C6 默认 160MHz
- **分区表**：C6 使用自定义分区表
- **HCI 接口**：BLE over SPI/SDIO 使用 RAM 传输
- **Wi-Fi 缓冲区**：静态/动态 RX/TX 缓冲区数量、AMPDU 窗口大小
- **LWIP 参数**：TCP/UDP 发送/接收窗口、邮箱大小、SACK、核心锁

### 3.4 与 Kconfig 的关系

```
Kconfig 文件体系
    ↓ 定义选项、依赖、默认值、菜单结构
sdkconfig.defaults / sdkconfig.defaults.esp32c6
    ↓ 提供项目/芯片级别的默认覆盖值
menuconfig（用户交互界面）
    ↓ 用户修改、验证依赖、解决冲突
sdkconfig
    ↓ 最终生成的扁平化配置文件（CMake/C 宏源）
构建系统
    ↓ 生成 sdkconfig.h（C 头文件）+ CMake 变量
源代码 / CMakeLists.txt
```

**重要原则**：`sdkconfig.defaults` 中的值**必须**是 Kconfig 中已经声明的合法值。如果写入 Kconfig 中不存在的配置项，或给 `bool` 类型的配置赋了非 `y`/`n` 的值，menuconfig 会报错或在生成 `sdkconfig` 时忽略。

---

## 4. VSCode 中的 Menuconfig 可视化配置

### 4.1 ESP-IDF VSCode 插件

Espressif 官方提供了 **ESP-IDF VSCode Extension**，内置对 menuconfig 的完整支持。

#### 启动方式

1. **命令面板**（`Ctrl+Shift+P` / `Cmd+Shift+P`）：
   - 输入 `ESP-IDF: SDK Configuration editor (menuconfig)`
   - 或 `ESP-IDF: Open sdkconfig file`

2. **底部状态栏**：
   - 点击 ESP-IDF 插件状态栏中的 **"SDK Configuration Editor"** 图标

3. **侧边栏**：
   - ESP-IDF 活动栏 → "Configure" → "SDK Configuration Editor"

### 4.2 Menuconfig 界面结构

启动后，界面以树形结构展示所有配置项，与 Kconfig 文件中的 `menu` 层级一一对应：

```
Top
├── Component config
│   ├── Bluetooth
│   ├── Driver configurations
│   ├── ESP-Hosted              ← 根目录 Kconfig 生成的菜单
│   │   ├── Choose the Co-processor to use
│   │   ├── Transport layer
│   │   ├── SPI Configuration
│   │   └── ...
│   ├── Power Management
│   └── Wi-Fi
├── Example Configuration       ← Kconfig.projbuild 生成的菜单
│   ├── Enable BT sharing via hosted
│   ├── Enable Wi-Fi on Co-processor
│   ├── Bus Config in between Host and Co-processor
│   └── ...
└── ...
```

### 4.3 操作说明

| 操作 | 快捷键 / 方式 |
|------|--------------|
| 上下移动 | `↑` / `↓` 或鼠标点击 |
| 进入子菜单 | `Enter` 或双击 |
| 返回上级 | `Esc` 或点击面包屑 |
| 切换布尔值 | `Space` 或点击复选框 |
| 编辑整数/字符串 | `Enter` 后输入值 |
| 搜索配置项 | `/` 键，输入关键字 |
| 查看帮助 | `?` 键，或鼠标悬停 |
| 保存并退出 | `S` → `Enter` 或点击 Save |

### 4.4 `sdkconfig` 可视化编辑器

VSCode 插件同时提供**非 menuconfig 的表格编辑器**：
- 以表格形式列出所有 `CONFIG_*` 项
- 支持直接修改值
- 实时显示配置项的描述（来自 Kconfig 的 `help` 文本）
- 修改后会自动触发依赖检查

**注意**：直接编辑 `sdkconfig` 文件（文本模式）虽然可行，但**不推荐**——绕过了 Kconfig 的依赖验证，可能导致配置冲突（如开启了某个功能但未开启其依赖）。始终优先使用 menuconfig 或 SDK Configuration Editor。

---

## 5. 配置流程与原理（完整链路）

### 5.1 文件加载顺序

当在 `slave/` 目录执行 `idf.py menuconfig` 或 `idf.py build` 时，ESP-IDF 构建系统按以下顺序收集 Kconfig 文件：

```
1. $IDF_PATH/Kconfig                    ← ESP-IDF 全局 Kconfig
2. $IDF_PATH/components/*/Kconfig       ← 各组件 Kconfig
3. 项目根目录 Kconfig.projbuild（如果存在）
4. 项目根目录 Kconfig（如果存在）
5. {项目根目录}/main/Kconfig.projbuild  ← slave/main/Kconfig.projbuild
6. {项目根目录}/main/Kconfig（如果存在）
7. Kconfig.projbuild 中 rsource 引用的文件
   └── slave/main/Kconfig.light_sleep
```

**注意**：对于 host 侧（`examples/` 下的示例），由于 ESP-Hosted 是以**组件**形式被引用，因此根目录的 `Kconfig` 会被作为组件 Kconfig 自动加载。

### 5.2 配置解析与生成流程

```
┌─────────────────────────────────────────────────────────────┐
│  Phase 1: Kconfig 合并                                        │
│  收集所有 Kconfig 文件，合并为统一的配置描述树                   │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Phase 2: 默认值加载                                          │
│  1. Kconfig 中的 default 值                                   │
│  2. sdkconfig.defaults.{芯片} 覆盖                            │
│  3. sdkconfig.defaults 覆盖                                   │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Phase 3: 用户交互（menuconfig）                               │
│  用户修改值，系统实时检查 depends on / select / range          │
│  冲突时自动调整或提示错误                                      │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Phase 4: 生成 sdkconfig                                      │
│  扁平化键值对文件，如：                                        │
│  CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE=y                      │
│  CONFIG_ESP_HOSTED_SPI_GPIO_MOSI=8                           │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Phase 5: 构建系统集成                                        │
│  生成 build/config/sdkconfig.h（C 头文件）                    │
│  生成 build/config/sdkconfig.json（JSON 格式）                │
│  CMake 中所有 CONFIG_* 变为可用变量                           │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 Kconfig → CMakeLists.txt → 源代码 的联动

**Kconfig 定义**：

```kconfig
# slave/main/Kconfig.projbuild
config ESP_HOSTED_USE_MEMPOOL
    bool "Use shared memory pool"
    default y
    help
        Enable custom mempool allocator for zero-copy RX/TX.
```

**CMakeLists.txt 中使用**：

```cmake
# slave/main/CMakeLists.txt
if(CONFIG_ESP_HOSTED_USE_MEMPOOL)
    list(APPEND COMPONENT_SRCS
        "${common_dir}/mempool/mempool_ll.c"
        "${common_dir}/mempool/mempool.c"
    )
endif()
```

**C 源代码中使用**：

```c
#ifdef CONFIG_ESP_HOSTED_USE_MEMPOOL
    buf = mempool_alloc(rx_pool, size);
#else
    buf = malloc(size);
#endif
```

**原理**：`sdkconfig` 中的每个 `CONFIG_XXX` 项在构建时会被转换为：
- C 预处理器宏：`#define CONFIG_XXX value`（写入 `sdkconfig.h`）
- CMake 变量：`CONFIG_XXX`（可在 `CMakeLists.txt` 中直接使用）

### 5.4 芯片特定的条件默认

本项目大量使用了**芯片条件默认**模式：

```kconfig
config ESP_SPI_HSPI_GPIO_MOSI
    depends on SPI_HSPI
    int "Slave GPIO pin for Host MOSI"
    default 20 if ESP_HOST_DEV_BOARD_P4_FUNC_BOARD
    default 8  if ESP_HOST_DEV_BOARD_P4_C5_CORE
    default 20 if ESP_HOST_DEV_BOARD_P4_C6_CORE
    default 13 if IDF_TARGET_ESP32
    default 11 if IDF_TARGET_ESP32S2 || IDF_TARGET_ESP32S3
    default 5  if IDF_TARGET_ESP32H2
    default 7
```

**原理**：Kconfig 的 `default` 支持 `if 条件` 后缀，构建系统会按顺序评估这些条件，第一个满足的条件生效。这使得：
- 选择开发板时，所有相关 GPIO **自动联动**
- 未选择任何预设时，回退到芯片级别的默认 GPIO
- 最终回退到通用默认值（如 `7`）

---

## 6. 实际工作流示例

### 6.1 为 ESP32-C6 协处理器配置 Slave 固件

```bash
cd slave
idf.py set-target esp32c6          # 设置目标芯片，触发 sdkconfig 重新生成
idf.py menuconfig                  # 可视化配置
```

此时构建系统：
1. 加载所有 Kconfig 文件，构建配置树
2. 依次应用 `sdkconfig.defaults` → `sdkconfig.defaults.esp32c6`
3. 打开 menuconfig，用户看到 C6 的默认值（160MHz CPU、BLE RAM HCI、C6 分区表等）
4. 用户可进一步修改（如启用 Network Split 或 Light Sleep）
5. 保存后生成 `sdkconfig`

### 6.2 修改配置后重新构建

```bash
# 修改 Kconfig 默认值（影响所有新构建）
vim slave/main/Kconfig.projbuild

# 修改特定芯片的默认值
vim slave/sdkconfig.defaults.esp32c6

# 清理后重新生成配置
idf.py fullclean
idf.py build
```

### 6.3 Host 示例的配置

```bash
cd examples/host_network_split__power_save
idf.py set-target esp32p4
idf.py menuconfig
```

Host 示例的 `sdkconfig.defaults.esp32p4` 会覆盖根 Kconfig 中的默认值，例如预设 ESP32-P4 开发板的 GPIO 映射。

---

## 7. 配置冲突排查

### 7.1 常见错误

| 现象 | 原因 | 解决 |
|------|------|------|
| menuconfig 中某项灰色不可选 | `depends on` 条件未满足 | 查看该项的依赖链，逐级开启前置配置 |
| `sdkconfig` 中值被自动覆盖 | 存在 `select` 强制关联 | 检查 Kconfig 中的 `select` 关系 |
| 编译时报 `CONFIG_XXX` 未定义 | Kconfig 中未声明或拼写错误 | 检查 Kconfig 文件，确认配置名 |
| `sdkconfig.defaults` 不生效 | 已存在 `sdkconfig` 文件 | 执行 `idf.py fullclean` 或删除 `sdkconfig` |

### 7.2 调试配置依赖

在 menuconfig 中：
1. 高亮选中某配置项
2. 按 `?` 键查看帮助
3. 帮助信息中会显示：
   - `Defined at ...`：定义位置（文件名和行号）
   - `Depends on:`：直接依赖
   - `Selected by:`：哪些配置会强制选中它
   - `Selects:`：它会强制选中哪些配置

---

## 8. 总结

| 文件 | 角色 | 何时修改 |
|------|------|----------|
| `Kconfig` | Host 侧配置描述 | 新增 Host 功能或传输参数 |
| `slave/main/Kconfig.projbuild` | Slave 侧配置描述 | 新增 Slave 功能或示例配置 |
| `slave/main/Kconfig.light_sleep` | 轻睡眠子模块 | 修改电源管理策略 |
| `sdkconfig.defaults` | 通用默认值 | 所有芯片共用的默认行为变更 |
| `sdkconfig.defaults.esp32c6` | 芯片特定默认值 | 特定芯片的调优参数 |
| `sdkconfig` | 最终配置文件 | **自动生成，不要手动提交到 Git** |

ESP-Hosted-MCU 的配置系统设计精妙，通过 **Kconfig 描述依赖关系** + **sdkconfig.defaults 提供芯片特定预设** + **menuconfig 提供用户友好界面**，实现了复杂多芯片、多传输、多开发板场景下的灵活配置。理解这套机制后，可以高效地进行二次开发和定制化配置。

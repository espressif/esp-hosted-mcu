# 非 ESP 主机移植指南

本指南说明了 ESP-Hosted-MCU 在主机侧的可移植性设计，以及将主机协议栈移植到新 MCU 平台时的最佳实践建议。

## 关键要点

- ESP-Hosted-MCU 具备主机移植的基础架构。对于非 ESP 主机，推荐的移植入口是 port 层，而不是直接修改传输（transport）核心。
- 仅替换底层 SPI、SDIO、SPI HD 或 UART 的传输函数通常远远不够。主机核心还依赖于操作系统原语、定时器、GPIO、中断、对齐分配、事件投递和重启钩子等能力。
- **Transport 核心（`host/drivers/transport/`）中仍存在 ESP-IDF 硬依赖**，例如 `esp_wifi_internal_reg_rxcb` 和 `esp_wifi_set_default_wifi_sta_handlers` 的调用。这意味着非 ESP 平台不仅要实现 port 层，还需要处理 transport 核心中与 ESP-IDF Wi-Fi 内部 API 的耦合。
- 当前仓库内的参考实现是 ESP + FreeRTOS 方案，见 [host/port/esp/freertos](../host/port/esp/freertos)。

## 移植边界在哪里

主要的可移植性契约由 [host/esp_hosted_os_abstraction.h](../host/esp_hosted_os_abstraction.h) 定义。

该抽象层涵盖：

- 内存分配与对齐分配
- 线程创建与销毁
- 队列、信号量、互斥锁等原语
- 睡眠与定时器服务
- GPIO 配置、中断注册与复位控制
- 事件投递与主机重启钩子
- 针对 SPI、SDIO、SPI HD、UART 的总线操作接口

该契约的参考实现位于 [host/port/esp/freertos/src/port_esp_hosted_host_os.c](../host/port/esp/freertos/src/port_esp_hosted_host_os.c)。

> **注意：** 抽象头文件 [host/esp_hosted_os_abstraction.h](../host/esp_hosted_os_abstraction.h) 内部直接 `#include "esp_event_base.h"`，且 `_h_event_post` 使用了 `esp_event_base_t` 类型。这意味着非 ESP 平台必须提供该头文件或等效的类型定义，无法完全绕过 ESP-IDF 的事件基类型。

[host/drivers/transport](../host/drivers/transport) 下的传输驱动已经通过 `g_h.funcs` 回调消费这些接口。因此，推荐的做法是实现一个新的 port 层，复用现有的传输状态机和帧格式。

## 通常可直接复用的部分

对于新 MCU 主机，以下部分通常可以直接复用或仅需极少逻辑调整：

- [common](../common) 下的通用传输头文件、协议定义和 protobuf RPC 消息（`common/proto/` 中的 protobuf-c 序列化/反序列化代码）
- [host/drivers/transport](../host/drivers/transport) 下的传输状态机、帧格式和队列逻辑

以下部分**可复用但需要适配 ESP-IDF 类型依赖**：

- RPC 核心（`host/drivers/rpc/core/`）中的请求/响应流程：protobuf 序列化层本身是平台无关的，但 RPC wrapper（`host/drivers/rpc/wrap/rpc_wrap.c`）和 slave interface（`host/drivers/rpc/slaveif/`）中大量使用了 `esp_err_t`、`esp_log.h` 和 ESP-IDF Wi-Fi 类型。非 ESP 平台需要提供这些类型的兼容定义，或仅复用 protobuf 序列化层并自行实现上层 wrapper。

## 通常需要移植或适配的部分

你需要实现或适配以下内容：

- 一个新的平台 port 层，满足 `hosted_osi_funcs_t` 契约
- 实际使用的总线类型的传输 HAL 钩子
- 类似 [host/port/esp/freertos/include/port_esp_hosted_host_config.h](../host/port/esp/freertos/include/port_esp_hosted_host_config.h) 的平台配置头文件
- 类似 [host/port/esp/freertos/include/port_esp_hosted_host_spi.h](../host/port/esp/freertos/include/port_esp_hosted_host_spi.h)、[host/port/esp/freertos/include/port_esp_hosted_host_sdio.h](../host/port/esp/freertos/include/port_esp_hosted_host_sdio.h)、[host/port/esp/freertos/include/port_esp_hosted_host_spi_hd.h](../host/port/esp/freertos/include/port_esp_hosted_host_spi_hd.h) 或 [host/port/esp/freertos/include/port_esp_hosted_host_uart.h](../host/port/esp/freertos/include/port_esp_hosted_host_uart.h) 的平台传输头文件
- mempool 锁钩子（可选）：如果定义了 `H_USE_MEMPOOL`，还需要实现 `_h_create_lock_mempool`、`_h_lock_mempool`、`_h_unlock_mempool`、`_h_destroy_lock_mempool`。如不需要，可在配置中关闭该宏以简化移植。

### ESP-IDF 类型与接口兼容层

以下 ESP-IDF 类型和接口在 host 代码中被广泛使用，非 ESP 平台必须提供兼容定义或实现：

| 类型/接口 | 用途 | 所在文件示例 |
|---|---|---|
| `esp_err_t` / `ESP_OK` / `ESP_FAIL` | 错误码体系 | `host/drivers/transport/transport_drv.h` 等 |
| `esp_event_base_t` | 事件基类型（`_h_event_post` 参数） | `host/esp_hosted_os_abstraction.h` |
| `wifi_init_config_t` / `wifi_config_t` / `WIFI_MODE_*` | Wi-Fi API 配置类型 | `host/drivers/rpc/wrap/rpc_wrap.c` |
| `esp_wifi_internal_reg_rxcb()` | 注册网络帧接收回调 | `host/drivers/transport/transport_drv.c:234` |
| `esp_wifi_set_default_wifi_sta_handlers()` | 设置默认 STA 处理函数 | `host/drivers/transport/spi/spi_drv.c:103`、`sdio_drv.c:1036` |
| `ESP_LOG*` / `esp_log.h` | 日志宏 | 几乎所有 host 源文件 |

> **注意**：`transport_drv.c` 和 `spi_drv.c`/`sdio_drv.c` 中直接调用了 `esp_wifi_internal_reg_rxcb` 和 `esp_wifi_set_default_wifi_sta_handlers`。非 ESP 平台若不需要完整的 ESP-IDF Wi-Fi 兼容层，可能需要为这些调用提供 stub 实现，或修改编译条件将其排除。

### 架构区分

- **传输状态机和帧格式**是有意设计为可移植的
- **Transport 核心中的 Wi-Fi 网络接口注册**仍与 ESP-IDF 内部 API 耦合
- **高层主机 API** 与 ESP-IDF 风格的类型和服务深度集成，如 [host/api/src/esp_hosted_api.c](../host/api/src/esp_hosted_api.c) 和 [host/drivers/rpc/wrap/rpc_wrap.c](../host/drivers/rpc/wrap/rpc_wrap.c)

这意味着完整的非 ESP 主机移植，还需要为事件循环、网络接口、Wi-Fi 类型定义等 ESP-IDF 相关接口做兼容适配。

## 推荐移植策略

### 1. 先在 ESP 主机上验证方案

在动手适配新 MCU 之前，建议先在 ESP 主机上验证同一套从机固件和传输通路，排除从机配置、GPIO 映射和协议行为的不确定性。

### 2. 只选用一种传输通路，关闭所有可选特性

首次 bring-up 建议范围尽量收敛：

- 只启用一种传输通路（如 SPI、SDIO、UART、SPI HD 任选其一）
- 蓝牙、DPP、OTA、电源管理、GPIO 扩展等所有可选特性全部关闭，等基础链路稳定后再逐步打开
- 避免同时更改传输、调度器和网络协议栈

### 3. 新建 port 目录，不要直接改传输核心

规范做法是仿照现有结构新建平台专用 port 目录，例如：

```text
host/port/<platform>/<os>/
   include/
   src/
```

以 [host/port/esp/freertos](../host/port/esp/freertos) 为参考模板。

### 4. 先实现通用 OS 与系统钩子

在关注 Wi-Fi 行为前，优先 bring-up 以下能力：

- 内存及对齐分配（`_h_malloc`、`_h_calloc`、`_h_free`、`_h_malloc_align`、`_h_free_align`）
- 线程创建与销毁（`_h_thread_create`、`_h_thread_cancel`）
- 队列、信号量、互斥锁等原语
- 睡眠与忙等待延迟（`_h_msleep`、`_h_usleep`、`_h_sleep`、**`_h_blocking_delay`**）
  - 注意：`_h_blocking_delay` 是非睡眠的忙等待延迟，与睡眠类 API 语义不同，ISR 或临界区路径可能依赖它
- 定时器 API（`_h_timer_start`、`_h_timer_stop`、`_h_get_time_ms`）
- GPIO 方向、读写与中断注册
- 事件投递钩子（`_h_event_wifi_post`、`_h_event_post`）
- 日志输出（`_h_printf`）
- 主机重启钩子（`_h_restart_host`）
- 唤醒/复位原因查询（`_h_get_host_wakeup_or_reboot_reason`）
- 如产品需要，电源管理 HAL 钩子（`_h_config_host_power_save_hal_impl`、`_h_start_host_power_save_hal_impl`）

上述原语如不完整或不稳定，即使底层协议正确，传输 bug 也会表现为随机异常。

### 5. 只实现实际需要的传输钩子

抽象层本身是按传输类型分离的。例如：

- 全双工 SPI 只需实现 `_h_bus_init`、`_h_bus_deinit`、`_h_do_bus_transfer`
- SDIO 需实现卡初始化、寄存器访问、块访问和中断等待等钩子
- SPI HD 需实现寄存器、DMA、数据线控制等钩子
- UART 需实现读写和输入刷新等钩子

不要一开始就移植所有传输类型，先把一种总线端到端打通。

### 6. 保持现有帧格式和驱动状态机不变

除非硬件强制要求，否则不要更改数据帧格式、队列规则、从机就绪信号或中断语义。最快的移植路径是保留核心逻辑，让 port 层去适配。

尤其要注意：

- 握手或数据就绪 GPIO 处理
- 复位时序
- DMA 对齐假设
- ISR 安全的信号量投递
- 超时与恢复路径

### 7. 尽早决定 ESP-IDF 兼容层级

实际移植有两种模式：

1. ESP-IDF 兼容主机 API 模式
   保持现有主机 API 接口，并为 [host/api/src/esp_hosted_api.c](../host/api/src/esp_hosted_api.c) 和 [host/drivers/rpc/wrap/rpc_wrap.c](../host/drivers/rpc/wrap/rpc_wrap.c) 所需的 ESP-IDF 风格类型、事件、网络集成提供足够的兼容层。

2. 精简集成模式
   如果产品不需要完整的 ESP-IDF Wi-Fi API，可以只移植到 transport 和 RPC 层，极大缩小移植面。

越早确定模式，越能避免返工。

### 8. 自底向上验证

推荐 bring-up 顺序：

1. 总线初始化、复位控制和中断连线
2. 从机就绪或心跳检测
3. 传输链路稳定性
4. RPC 初始化及简单控制交换（如固件/芯片信息）
5. 基本 Wi-Fi STA 流程
6. 只有前述都稳定后再逐步打开可选特性

传输链路建议先用 [故障排查指南](../troubleshooting.md#2-raw-throughput-testing) 的原始吞吐测试流程，网络调通前先确保底层无误。

## 最小 bring-up 检查清单

在尝试 Wi-Fi 关联或更高层测试前，务必确认：

- 主机能可靠复位从机
- GPIO 极性与所选传输设计一致
- 中断或轮询路径在高频流量下稳定
- 对齐分配满足 DMA/总线要求
- ISR 安全的同步原语行为正确
- 定时器和睡眠精度足够支撑传输重试和超时
- 传输缓冲区不会在重试或销毁路径中泄漏
- 销毁和重新初始化可连续多次无异常

## 示例：全双工 SPI + SoftAP + RPC 的最小移植

以下是一个具体的最小化移植示例，假设目标场景为：

- **传输**：全双工 SPI（`H_TRANSPORT_SPI`）
- **Wi-Fi 模式**：仅 SoftAP（不启用 STA）
- **功能范围**：仅 RPC 命令控制 + SoftAP 网络数据路径
- **不启用**：蓝牙、DPP、OTA、电源管理、Mempool

### 必须实现的 OS 抽象接口

按类别列出 `hosted_osi_funcs_t` 中**必须**实现的成员：

**内存**
- `_h_memcpy`、`_h_memset`
- `_h_malloc`、`_h_calloc`、`_h_free`、`_h_realloc`
- `_h_malloc_align`、`_h_free_align`（DMA/总线对齐分配）

**线程**
- `_h_thread_create`、`_h_thread_cancel`

**同步原语**
- `_h_create_queue`、`_h_queue_item`、`_h_dequeue_item`、`_h_queue_msg_waiting`、`_h_destroy_queue`、`_h_reset_queue`
- `_h_create_mutex`、`_h_lock_mutex`、`_h_unlock_mutex`、`_h_destroy_mutex`
- `_h_create_semaphore`、`_h_post_semaphore`、`_h_post_semaphore_from_isr`、`_h_get_semaphore`、`_h_destroy_semaphore`

**定时与延迟**
- `_h_msleep`、`_h_usleep`、`_h_sleep`
- `_h_blocking_delay`（忙等待，用于不能睡眠的临界延迟）
- `_h_timer_start`、`_h_timer_stop`、`_h_get_time_ms`

**GPIO**
- `_h_config_gpio`（输出/输入模式配置）
- `_h_config_gpio_as_interrupt`（边沿触发中断注册）
- `_h_teardown_gpio_interrupt`
- `_h_read_gpio`、`_h_write_gpio`

**SPI 总线**
- `_h_bus_init` — 初始化 SPI 控制器，返回句柄
- `_h_bus_deinit` — 反初始化 SPI 控制器
- `_h_do_bus_transfer` — 执行一次全双工 SPI 传输

**事件与日志**
- `_h_event_wifi_post` — 投递 Wi-Fi 事件（SoftAP 启动/停止、STA 连接/断开等）
- `_h_event_post` — 投递通用事件（协处理器初始化、心跳等）
- `_h_printf` — 日志输出

**钩子**
- `_h_hosted_init_hook` — 可在初始化前执行平台特定设置
- `_h_restart_host` — 主机重启（如产品需要）
- `_h_get_host_wakeup_or_reboot_reason` — 返回唤醒/复位原因

### 可以省略的功能

| 功能 | 处理方式 |
|---|---|
| SDIO / SPI HD / UART 传输 | 不实现对应的传输钩子，宏不启用即可 |
| 蓝牙 | 不编译 VHCI 驱动，关闭相关 Kconfig |
| DPP | 不定义 `H_DPP_SUPPORT` |
| Mempool | 不定义 `H_USE_MEMPOOL`，使用标准 `malloc`/`free` |
| 电源管理 | 不定义 `H_HOST_PS_ALLOWED`，`_h_config_host_power_save_hal_impl` 和 `_h_start_host_power_save_hal_impl` 可填桩函数 |
| `_h_pull_gpio` / `_h_hold_gpio` | 如硬件不需要上下拉或 GPIO hold，可填桩函数 |

### 必须配置的宏

```c
#define H_TRANSPORT_IN_USE    H_TRANSPORT_SPI   // 只启用 SPI
#define H_TRANSPORT_SPI       3
#define H_ESP_HOSTED_HOST     1
// #define H_USE_MEMPOOL      // 关闭以简化移植
// #define H_DPP_SUPPORT      // 关闭 DPP
// #define H_HOST_PS_ALLOWED  // 关闭电源管理
```

### SPI GPIO 需求

全双工 SPI 模式下，至少需要以下 GPIO：

| 信号 | 方向 | 说明 |
|---|---|---|
| CS | 主机输出 | 片选 |
| CLK | 主机输出 | 时钟 |
| MOSI | 主机输出 | 主机→从机数据 |
| MISO | 主机输入 | 从机→主机数据 |
| HANDSHAKE | 主机输入（中断） | 从机握手，通知主机可以发送 |
| DATA_READY | 主机输入（中断） | 从机数据就绪，通知主机接收 |
| RESET | 主机输出 | 从机复位控制 |

> 注意：HANDSHAKE 和 DATA_READY 必须配置为**边沿触发中断**，并在 ISR 中安全地投递信号量。

### 推荐 bring-up 顺序

1. **OS 抽象层验证** — 先写一个小测试验证 malloc、队列、信号量、定时器、GPIO、中断在目标 MCU 上行为正确
2. **SPI 底层验证** — 不连接 Hosted 协议栈，仅验证 `_h_do_bus_transfer` 能正确读写从机某个寄存器
3. **复位与就绪检测** — 实现 `_h_write_gpio`（复位从机）+ `_h_read_gpio` 或中断（检测从机就绪）
4. **Hosted 初始化** — 调用 `esp_hosted_init()`，观察传输线程和 RPC 线程是否正常启动
5. **链路建立** — 调用 `esp_hosted_connect_to_slave()`，确认主机与从机握手成功
6. **RPC 验证** — 调用 `esp_hosted_get_coprocessor_fwversion()` 等简单 RPC 验证命令通路
7. **Wi-Fi 初始化** — `esp_wifi_remote_init()` → `esp_wifi_remote_set_mode(WIFI_MODE_AP)`
8. **SoftAP 配置** — 通过 `esp_wifi_remote_set_config(WIFI_IF_AP, ...)` 配置 SSID/密码/通道
9. **启动 SoftAP** — `esp_wifi_remote_start()`，监听 `_h_event_wifi_post(WIFI_EVENT_AP_START, ...)`
10. **数据路径验证** — 用手机连接 SoftAP，验证网络帧能在 `ESP_AP_IF` 通道正常收发

### 最小代码骨架示例

```c
// 1. 定义宏
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI
#define H_ESP_HOSTED_HOST   1

// 2. 包含 Hosted 头文件
#include "esp_hosted.h"
#include "esp_hosted_os_abstraction.h"

// 3. 实现并注册 OS 抽象回调
hosted_osi_funcs_t g_hosted_osi_funcs = {
    ._h_malloc        = my_malloc,
    ._h_free          = my_free,
    ._h_malloc_align  = my_malloc_align,
    ._h_free_align    = my_free_align,
    // ... 其他必须接口
    ._h_bus_init      = my_spi_init,
    ._h_bus_deinit    = my_spi_deinit,
    ._h_do_bus_transfer = my_spi_transfer,
    ._h_event_wifi_post = my_wifi_event_post,
    ._h_event_post    = my_event_post,
    ._h_printf        = my_log_printf,
};
struct hosted_config_t g_h = {
    .funcs = &g_hosted_osi_funcs,
};

// 4. 初始化 Hosted
void hosted_setup(void)
{
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());

    // Wi-Fi 初始化
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_remote_init(&cfg));

    // 仅 SoftAP 模式
    ESP_ERROR_CHECK(esp_wifi_remote_set_mode(WIFI_MODE_AP));

    // 配置 SoftAP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "MySoftAP",
            .ssid_len = strlen("MySoftAP"),
            .password = "mypassword",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_remote_set_config(WIFI_IF_AP, &ap_config));

    // 启动
    ESP_ERROR_CHECK(esp_wifi_remote_start());
}
```

### 关键注意事项

- **网络数据路径不可省略**：即使场景叫"SoftAP + RPC"，SoftAP 本身需要 `ESP_AP_IF` 通道来收发网络帧。`esp_hosted_init()` 内部会自动创建该通道。
- **STA 通道可忽略使用**：`esp_hosted_init()` 同样会创建 `ESP_STA_IF` 通道，但只要不调用 STA 相关 API，该通道不会产生活跃流量，移植时可暂不关注其 netif 集成。
- **对齐分配必须可靠**：`_h_malloc_align` 的返回值必须满足从机 DMA 对齐要求（通常为 4 字节或 64 字节），否则会导致传输异常。
- **`_h_event_wifi_post` 必须能投递到应用层事件循环**：SoftAP 的启动完成、STA 连接/断开等状态都依赖该回调通知上层。

## 典型反模式

- 直接复制 [host](../host) 全部代码到新树并修改传输核心
- 为了本地驱动习惯更改传输帧格式
- 首次 bring-up 就打开所有特性
- 底层传输链路未稳定就调试 Wi-Fi
- 平台移植和新特性开发混在同一次提交

## 最佳实践总结

如需将 ESP-Hosted-MCU 移植到任意非 ESP 主机 MCU，最佳实践如下：

1. 保持现有主机核心和传输逻辑不变
2. 新建专用平台 port 层
3. 实现 [host/esp_hosted_os_abstraction.h](../host/esp_hosted_os_abstraction.h) 所需的全部 OS 与总线抽象
4. 先只移植一种传输类型
5. 网络调通前，务必先验证原始传输链路
6. ESP-IDF 兼容层只做产品实际需要的部分

简言之，本仓库具备非 ESP 主机移植的基础架构（OS 抽象层 + 可复用的传输状态机），但核心代码中仍存在部分 ESP-IDF 耦合需要额外处理。推荐的移植单元是主机 port 层，同时需关注 transport 核心中的 ESP-IDF Wi-Fi 内部 API 依赖，而非单纯的 SPI 传输函数。
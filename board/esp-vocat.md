# Espressif ESP-VoCat（喵伴 / EchoEar）—— Muse SDK 板级说明

乐鑫官方 ESP-VoCat 圆屏语音板适配到 `muse-gadget-sdk/esp32` 的板级驱动说明。
源代码与硬件契约来自 `78/xiaozhi-esp32` 的 `main/boards/espressif/esp-vocat/`。

> **不是同一块板**：SDK 里已有的 `ostb-echoear-2st` 是第三方复刻板，其文档原文写着
> "Stock ESP-VoCat GPIOs do not match"。除面板 IC、184 条初始化表、CST816S 协议、
> ES8311/ES7210 芯片型号外，GPIO 全部不同。本适配只复用它的代码骨架。

## 1. 硬件

| 部件 | 型号 / 规格 | 接口 |
|---|---|---|
| 主控 | ESP32-S3（实测 rev v0.2） | 内封 16 MB Octal PSRAM (AP_1v8) |
| Flash | MXIC OPI，eFuse 固定 **octal(8 线)**，物理 32 MB | 固件按 **16 MB** 使用（与出厂镜像头一致） |
| 显示 | 1.85" 360×360 圆屏，**ST77916** | QSPI SPI2_HOST @40 MHz，RGB565 |
| 触摸 | **CST816S** @0x15，INT=GPIO10 | 共享 I2C_NUM_0，reg 0x02 六字节 |
| 放音 | **ES8311** @0x18(7b)/0x30(8b) + **NS4150B** 3 W | I2S_NUM_0 duplex |
| 录音 | **ES7210** @0x40(7b)/0x80(8b) + 双模拟麦 | 同上 |
| 电量计 | 0x55，寄存器 0x08/0x0A/0x0C/0x14/0x2C（BQ27220 类，**型号未证实**） | 共享 I2C_NUM_0 |
| 其它 | BMI270 IMU、microSD、SAM8108 电源开关、旋转底座 UART1、LED_G | 本适配未接入 |
| USB | ESP32-S3 原生 USB-Serial/JTAG，**VID 303A / PID 1001** | 实测 COM27 |

共享 I2C：`SDA = GPIO2`，`SCL = GPIO1`，400 kHz，内部上拉。

## 2. GPIO（V1.0 / V1.2 两套 PCB，运行时自动切换）

| 功能 | V1.0 | V1.2 | 功能 | 两版共用 |
|---|---|---|---|---|
| QSPI CLK / CS / D0 / D1 / D2 / D3 | 18 / 14 / 46 / 13 / 11 / 12 | 同 | I2S MCLK / BCLK / WS / DOUT | 42 / 40 / 39 / 41 |
| LCD RST | 3（低有效） | **47（高有效）** | I2C SDA / SCL | 2 / 1 |
| I2S DIN | 15 | **3** | POWER_CTRL（低有效） | 9 |
| 功放 PA | 4 | **15** | codec 电源 / 版本探测 | 48 |
| UART1 TX / RX | 6 / 5 | 5 / 4 | 背光 / BOOT / 触摸 INT | 44 / 0 / 10 |
| 原生 TOUCH_PAD2 | NC | 6 | SD MISO / SCK / MOSI | 17 / 16 / 38 |

`config.h` 里的 `_1` / `_2` 后缀是 **PCB 版本号**，不是双麦通道。官方板只有一根 I2S DIN。

**探测逻辑必须原样保留**（`board_esp_vocat.c:detect_pcb_version()`）：

1. 先写 GPIO48 输出锁存器，再 `gpio_config()` 配成输出。顺序反了会瞬间拉低 V1.2 的 codec 电源轨，导致首次触摸/电量计读失败并重启（官方 issue #1202）。
2. 只有 `ESP_RST_POWERON` 才真正探测；其它复位读 `RTC_NOINIT` 缓存（magic `0x31434256`）。软复位重探会因电源轨残电产生假 ACK，误判成 V1.0 后进入重启循环。
3. 探测方式：GPIO48 拉低 100 ms → probe ES8311(0x18)。仍应答 = V1.0；不应答则拉高、最多等 10×200 ms 再 probe，成功 = V1.2。
4. 两次都失败时 GPIO48 保持高电平（安全兜底），且**不写缓存**，下次重新探测。
5. 缓存命中路径同样要调 `apply_v12_pins()`：`audio_init()` / `display_start()` 远晚于探测函数返回，读的是那几个全局引脚变量。
6. V1.2 的 LCD 复位是高有效，对应 `panel_cfg.flags.reset_active_high = s_pcb_version`。

## 3. 构建

```bash
# 1) 把本适配包合入本地 SDK 副本（先 dry-run 看清单）
python apply.py --sdk /path/to/muse-gadget-sdk --dry-run
python apply.py --sdk /path/to/muse-gadget-sdk

# 2) 激活 ESP-IDF，然后构建 / 烧录
cd /path/to/muse-gadget-sdk/esp32
tools/muse/board.sh build vocat
tools/muse/board.sh flash vocat COM27
```

加载顺序：`sdkconfig.defaults` → `devices/sdkconfig.muse` → `devices/sdkconfig.muse-esp-vocat`。

**版本要求**：`esp32/README.md` 明确写着 ESP-IDF **v6.0.3**，"Other versions aren't supported"，
CI 也用容器 `espressif/idf:v6.0.3`。组件 manifest 里的 `>=5.5` 只是依赖下限，不能当成兼容证据。

## 4. 本机实物预检（2026-11-06，COM27）

| 项 | 实测 |
|---|---|
| 出厂固件 | `xiaozhi` App version `2.0.4.4`，编译于 2025-12-02，ESP-IDF `v5.5.1-dirty` |
| 启动日志 | `Octal Flash Mode Enabled`、`detected chip: mxic (opi)`、`flash io: opi_str`、`Found 16MB PSRAM device`、`Speed: 80MHz`、`Detected size(32768k) larger than ... (16384k)` |
| esptool | `ESP32-S3 (QFN56) rev v0.2`、`Flash type set in eFuse: octal`、`Detected flash size: 32MB`、`USB mode: USB-Serial/JTAG` |
| PCB 版本 | **未确认**：出厂固件日志在 `Calling app_main()` 后不再输出，无法从串口读出版本。本驱动会在首次上电时自行探测 |

PCB 版本、电量计芯片型号、颜色反转方向都标为「未确认」，不写成结论。

## 5. 已知边界 / 未验证项

| 项 | 状态 |
|---|---|
| 音频 | **方案 A**：对齐 Muse 固定 16 kHz / 16-bit / STD 双槽，ES7210 只选 MIC1\|MIC2，`mic_slot=-1` 混合两路。官方 24 kHz / TDM 4 槽 / AEC 参考通道**未移植**（Muse 共享层全库无 `i2s_tdm`） |
| `codec_power` | 留 NULL 常供电。GPIO48 同时是版本探测脚，周期性断电会丢 codec 寄存器 |
| 电容触摸 / UART 底座 / IMU / SD / 摄像头 | 未接入 Muse 框架；官方 6 个可变引脚里只消费本驱动用到的 3 个 |
| 颜色反转 | 官方不 invert，OSTB 复刻板 invert=true。留编译开关 `-DESP_VOCAT_INVERT_COLOR=1` |
| 编译 | **未在本机编译通过**（本机只有 ESP-IDF v5.5.5，SDK 要求 v6.0.3）。代码为静态适配，需按第 3 节验证 |
| 上板 | **未验证**：屏亮、触摸对齐、录放、电池读数、V1.0/V1.2 各自启动，均需实物逐项走一遍 |

## 6. 验收清单（上板后）

1. `tools/muse/board.sh build vocat` 通过，`check_sizes` 应用分区空闲 >10%。
2. 逐行核对 overlay 的每一项确实进入生成的 `build-*/sdkconfig`（尤其 `OCT_FLASH` / `FLASHMODE_OPI` / `SPIRAM_MODE_OCT`）。
3. 重建至少另一块 UI 板，确认 CMake `elseif` 链没破坏别人。
4. 串口看 `PCB V1.x` 日志，确认 V1.0 / V1.2 两台板各自启动正常。
5. 屏亮 → 触摸对齐 → 配对 → 录放 → 电池读数 → 颜色是否正确（必要时开 invert）。
6. `python3 -m unittest discover -s tests -p 'test_*.py'`。

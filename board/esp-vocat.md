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

## 6. 验收清单（逐条打勾，记录「通过 / 失败 + 串口原文 + 复现步骤」）

### 6.1 烧录前（构建产物侧）

> **2026-11-06 22:54 实测结果**：第 1–4 条已在 ESP-IDF v6.0.3 容器中全部通过
> （`BUILD_RC=0`、2.1 MB `.bin`、分区余量 48%、另一块 UI 板回归 `RC=0`、0 error），
> 第 5 条 host 单测仍须在 Linux/macOS 跑。详见 `docs/构建验证记录.md` 第 7 节。


| # | 检查项 | 期望结果 / 判定 | 失败时排查 |
|---|---|---|---|
| 1 | `tools/muse/board.sh build vocat` | 退出码 0，产出 `build-muse-esp-vocat/muse-gadget.bin` | 按编译报错定位；**不要**为过编译去改共享层代码 |
| 2 | 核对 `build-muse-esp-vocat/sdkconfig` | `ESPTOOLPY_OCT_FLASH=y`、`FLASHMODE_OPI=y`、`FLASH_SAMPLE_MODE_STR=y`、`FLASHSIZE_16MB=y`、`SPIRAM_MODE_OCT=y`、`SPIRAM_SPEED_80M=y`、`MUSE_BOARD_ESP_VOCAT=y`、`MUSE_BOARD_ID="esp_vocat"` | 缺项→查 `SDKCONFIG_DEFAULTS` 加载顺序（`sdkconfig.defaults` → `sdkconfig.muse` → 本 overlay）；choice 里旧的 `=y` 必须显式写 `=n` |
| 3 | `check_sizes` | 应用分区空闲 **>10%** | 超了先裁组件，别动分区表 |
| 4 | 回归编译另一块 UI 板 | 例如 `board.sh build echoear` 通过 | 确认 CMake `elseif` 链只是多了一个分支，没有截胡别的板 |
| 5 | host 单测 | 在 **Linux/macOS** 跑 `python3 -m unittest discover -s tests -p 'test_*.py'` | Windows 上 `tools/muse/avatar.py` 依赖 `fcntl`，会有大量 import error——**这是环境性失败**，未打补丁的原始 SDK 同样报错，不能当作代码回归 |

### 6.2 上电与串口（COM27，ESP32-S3 原生 USB-Serial/JTAG）

| # | 观察点 | 期望串口输出 / 现象 | 失败时排查 |
|---|---|---|---|
| 6 | Flash 模式 | `Octal Flash Mode Enabled`、`detected chip: mxic (opi)`、`flash io: opi_str` | 探测失败→回 6.1 第 2 条；写成 DIO/QIO 会直接起不来 |
| 7 | 容量提示 | `Detected size(32768k) larger than ... (16384k). Using the size in the binary image header` | **这是预期**，物理 32 MB / 镜像按 16 MB 用，不是错误 |
| 8 | PSRAM | `Found 16MB PSRAM device`、`Speed: 80MHz` | 缺失→检查 `SPIRAM_MODE_OCT` 是否被 defaults 的 QUAD 覆盖 |
| 9 | **PCB 版本** | 驱动打印 `PCB V1.0` 或 `PCB V1.2`，且引脚按第 2 节表切换 | 无输出→查 GPIO48 探测与 I2C ACK；**必须先写输出锁存器再切输出**，否则 V1.2 codec 掉电引发重启循环 |
| 10 | 软复位 | 重启后版本**不重探**（命中 RTC 缓存），但缓存路径仍须套用 V1.2 引脚 | 若每次都重探→检查 `RTC_NOINIT` magic；残电会造成假 ACK |

### 6.3 外设逐项

| # | 外设 | 期望现象 | 判定 / 失败排查 |
|---|---|---|---|
| 11 | 显示 | 360×360 圆屏点亮，无花屏 / 撕裂 | 黑屏→查 QSPI 引脚、背光 GPIO44、`POWER_CTRL`(GPIO9) 是否置 0；`LCD_RST` V1.0=3 / V1.2=47 且**复位极性不同** |
| 12 | 颜色 | 色块正常 | 反色→加 `-DESP_VOCAT_INVERT_COLOR=1` 重编（官方不 invert，OSTB 复刻板 invert） |
| 13 | 触摸 | 点按位置与 UI 反馈对齐 | 偏移 / 无响应→查 CST816S 0x15、INT=GPIO10、reg 0x02 六字节解码 |
| 14 | 播放 | 提示音 / 语音可听 | 无声→ES8311 0x18(7b) / 0x30(8b)、PA 引脚 V1.0=4 / V1.2=15、I2S 42/40/39/41 |
| 15 | 录音 | 唤醒词 / 语音指令可用 | 无输入→ES7210 0x40/0x80、DIN V1.0=15 / V1.2=3、MIC1\|MIC2；**当前是 16 kHz STD 双槽，不是官方 TDM+AEC** |
| 16 | **电量计** | 百分比 / 充电状态读数合理 | 读数为 0 或乱跳→0x55 芯片型号**未证实**，核对 reg 0x08/0x0A/0x0C/0x14/0x2C |
| 17 | BOOT 键 | 短按 / 长按触发预期动作 | GPIO0 |
| 18 | 配对 | 能完成 Home Link 配对 | 需有效 `CONFIG_GADGET_SDK_TOKEN`；构建时无 token 会告警 "Gadgets without one will stop pairing" |

> 第 9、16 条是**只有实物才能定论**的两项：PCB 版本与电量计型号目前都来自官方源码推断，
> 上板后请把串口原文贴回 `flow/进展.md`，据此更新硬件契约文档。

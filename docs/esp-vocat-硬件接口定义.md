# ESP-VoCat（喵伴）板级接口定义
> **实物实测修正（2026-11-06，COM27）**：下表 Flash 参数来自立创页面标称，与实物不完全一致，以实测为准。
> - `esptool flash_id`：ESP32-S3 (QFN56) **rev v0.2**，`Flash type set in eFuse: octal (8 data lines)`，
>   Manufacturer `c2` / Device `8039`（MXIC OPI），**Detected flash size: 32MB**，内封 16 MB Octal PSRAM (AP_1v8)。
> - 出厂 `xiaozhi 2.0.4.4` 启动日志：`Octal Flash Mode Enabled`、`detected chip: mxic (opi)`、
>   `flash io: opi_str`、`Detected size(32768k) larger than ... (16384k). Using the size in the binary image header`。
> - 即 **物理 32 MB OPI Flash，但固件/分区表按 16 MB 使用**。overlay 必须是
>   `CONFIG_ESPTOOLPY_OCT_FLASH=y` + `CONFIG_ESPTOOLPY_FLASHMODE_OPI=y`，**不是**多数 Muse 板用的 DIO/QIO。
>   表中「16 MB Flash」应理解为「镜像使用的容量」，不是芯片容量。
> - USB 为原生 USB-Serial/JTAG `VID_303A / PID_1001`，与方案预测一致。
> - PCB 版本（V1.0 / V1.2）**无法从串口确认**：出厂固件日志停在 `Calling app_main()` 后不再输出，
>   由驱动在首次上电时自行探测。


> 目的：把 `F:\git\xiaozhi-esp32\main\boards\espressif\esp-vocat` 的板级源码，翻译成一份
> 可供 Muse SDK 适配直接查阅的硬件契约。所有 GPIO / 元件 / 组件结论以源码为最高依据。
> 整理日期：2026-11-06。

## 0. 资料来源与可信度

| 来源 | 位置 | 用途 | 可信度 |
|---|---|---|---|
| 小智官方板级源码 | `F:\git\xiaozhi-esp32\main\boards\espressif\esp-vocat\`：`config.h`、`esp_vocat.cc`(1212 行)、`config.json`、`README.md`、`touch.h` | GPIO、元件、I2C 地址、初始化顺序、PCB 版本分支 | 最高（代码即事实） |
| 小智共用音频驱动 | `F:\git\xiaozhi-esp32\main\audio\codecs\box_audio_codec.cc/.h` | I2S TDM/STD、ES8311/ES7210 配置 | 最高 |
| 立创开源硬件页 | https://oshwhub.com/esp-college/echoear | BOM、供电结构、装配、已知硬件问题 | 高（无逐引脚原理图，不用于 GPIO 判定） |
| Muse SDK | `F:\git\muse-gadget-sdk\esp32` | 目标框架、既有同类板 | 代码为准 |

**边界声明**：硬件页描述的是「乐鑫 ESP-VoCat 官方板」；Muse SDK 里已有的
`ostb-echoear-2st` 是**另一块第三方复刻板**，其文档明确写着 "Stock ESP-VoCat GPIOs do not match"。
两者不是同一块 PCB，引脚不可混用，详见第 10 节。

---

## 1. 主控与存储

| 项 | 值 | 依据 |
|---|---|---|
| 模组 | ESP32-S3-WROOM-1-**N16R16VA** | 立创页「功能展示」 |
| Flash | 16 MB | 立创页；`README.md`「使用16MB Flash，需要专门分区表」 |
| PSRAM | 16 MB，**Octal**（R16VA） | 型号后缀 R16VA；Muse 的同类 overlay 用 `CONFIG_SPIRAM_MODE_OCT=y` |
| 无线 | 2.4 GHz Wi-Fi + BLE 5 (LE) | 立创页 |
| target | `esp32s3` | `config.json` |
| Console | 原生 USB Serial/JTAG（USB-C，兼供电与下载） | 立创页；Muse 同类 overlay 用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` |
| 供电 | 5V USB-C / 3.7V 700mAh 锂电（902530）/ 磁吸 Pogopin | 立创页「供电方式」 |
| 开关机 | SAM8108 开关机芯片，底部物理电源键，单击切换开关机 | 立创页 BOM |

> 注意：Muse 的 `devices/sdkconfig.muse-ostb-echoear-2st` 标的是 **8 MB** octal PSRAM @80MHz。
> 官方喵伴是 16 MB。容量由 IDF 自动探测，overlay 关键是 `SPIRAM_MODE_OCT` / `SPIRAM_SPEED_80M`
> 与 16 MB flash 分区表，不能把对方的数字照抄过来当成官方板参数。

---

## 2. 元件清单（BOM / 组件）

| 类别 | 型号 / 规格 | 接口 | 地址 / 关键参数 | 依据 |
|---|---|---|---|---|
| 主控 | ESP32-S3-WROOM-1-N16R16VA | — | 16MB Flash + 16MB Octal PSRAM | 立创页 |
| 显示屏 | 1.85" 圆形 TFT，**360×360**，RGB565 | QSPI（SPI2_HOST）@40 MHz | 驱动 IC **ST77916** | `config.h` / 立创页 |
| 触摸屏 | **CST816S**（电容触摸） | I2C | **0x15**（7-bit），INT=GPIO10 | `esp_vocat.cc:834` / `config.h` |
| 原生触摸 | ESP32-S3 touch pad + 铜箔（Touch 铜箔 GH1.25-2P） | 芯片内部 touch sensor | PAD1=GPIO7；PAD2=V1.0 无 / V1.2=GPIO6 | `config.h` / `esp_vocat.cc:924-1004` |
| 音频 DAC | **ES8311** | I2C 控制 + I2S 数据 | 7-bit **0x18** / 8-bit **0x30** | `config.h` / `esp_vocat.cc:757` |
| 功放 | **NS4150B**，4Ω 3W 2828 方形腔体喇叭 | PA 使能 GPIO | V1.0=GPIO4 / V1.2=GPIO15 | 立创页 / `config.h` |
| 音频 ADC | **ES7210** | I2C 控制 + I2S 数据 | 7-bit **0x40** / 8-bit **0x80** | `config.h` / Muse echoear 板 |
| 麦克风 | 2× **LMA3729T381-OY3S** 模拟麦（兼容 MSM381A3729H9BPC），间距 45 mm | 接 ES7210 MIC1/MIC2 | 支持声源定位 | 立创页「音频」 |
| 电量计 | **推断为 BQ27220 / BQ27441 类**（源码寄存器与之吻合，硬件页未列型号，**未证实**） | I2C | **0x55**；reg 0x08 Voltage / 0x0A BatteryStatus / 0x0C Current / 0x14 AverageCurrent / 0x2C SOC | `esp_vocat.cc:310-375, 828` |
| IMU | **BMI270**（仅启用加速度，用于「摇晃」表情） | I2C（共享总线） | 地址未在板级源码显式给出（驱动默认 0x68/0x69） | `esp_vocat.cc:45-76, 849` |
| microSD | 最大 32 GB | SDSPI | MISO=17 / SCK=16 / MOSI=38；板级源码**未初始化** | `config.h` / 立创页 |
| 状态灯 | 绿色 LED | GPIO | LED_G = GPIO43；板级源码未使用 | `config.h` |
| 电源域 1 | **POWER_CTRL = GPIO9** | GPIO 输出 | 控制 SD 卡 / LCD 背光 / LCD 驱动电源；源码置 **0**（低有效使能） | 立创页 / `esp_vocat.cc:1072-1080` |
| 电源域 2 | **CORDEC_POWER_CTRL = GPIO48** | GPIO 输出 | V1.2 的 codec 电源轨（SY8088），高有效；兼作 PCB 版本探测 | `esp_vocat.cc:709-789` |
| 旋转底座 | ESP32-C3-MINI-1U-H4 + SV01A103AEA01R00 电位器 + 0.96" 屏 + 6 mm 减速电机 | **UART1** 串口 | V1.0 TX=6/RX=5；V1.2 TX=5/RX=4；板级源码**未初始化 UART** | `config.h` / 立创页 |
| 摄像头 | 仅 USB UVC 外接（条件编译 `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`） | USB Host | 非板载 | `esp_vocat.cc:1082-1107` |
| 按键 | BOOT = GPIO0；VOL+/VOL- = NC；BUILTIN_LED = NC | GPIO | — | `config.h` |

**I2C 总线**：`I2C_NUM_0`，SDA=GPIO2，SCL=GPIO1，**400 kHz**，内部上拉。
CST816S、电量计、BMI270、ES8311、ES7210 全部挂在这一条总线上（`esp_vocat.cc:664-695`）。

> 认知坑：`config.h` 里还有 `TP_PORT (I2C_NUM_1)`、`TP_PIN_NUM_RST (NC)`，那是给 `touch.h` 的
> `bsp_touch_new()` 预留的 BSP 宏；板级源码**没有调用** `bsp_touch_new()`，所有外设实际跑在 I2C_NUM_0。
> 以代码为准，不要按 `TP_PORT` 去建第二条总线。

> 认知坑：7-bit / 8-bit 地址有两套。`i2c_master_probe()` 用 7-bit（0x18 / 0x40 / 0x15 / 0x55），
> `audio_codec_i2c_cfg_t.addr` 用 8-bit（ES8311 = 0x30、ES7210 = 0x80）。二者是同一个地址左移一位的关系。

---

## 3. GPIO 完整分配表（含 PCB V1.0 / V1.2 双版本）

`config.h` 中带 `_1` / `_2` 后缀的宏**表示 PCB 版本（V1.0 / V1.2），不是通道编号**。
`esp_vocat.cc:277-282` 定义 6 个可写全局 `gpio_num_t`，默认取 `_1`；
`ApplyV12Pins()`（`:700-707`）在检测到 V1.2 时整体切换。

| 功能 | 宏 | V1.0 | V1.2 | 备注 |
|---|---|---|---|---|
| QSPI PCLK | `QSPI_PIN_NUM_LCD_PCLK` | **18** | 18 | SPI2_HOST |
| QSPI CS | `QSPI_PIN_NUM_LCD_CS` | **14** | 14 | DC = NC |
| QSPI D0 | `QSPI_PIN_NUM_LCD_DATA0` | **46** | 46 | |
| QSPI D1 | `QSPI_PIN_NUM_LCD_DATA1` | **13** | 13 | |
| QSPI D2 | `QSPI_PIN_NUM_LCD_DATA2` | **11** | 11 | |
| QSPI D3 | `QSPI_PIN_NUM_LCD_DATA3` | **12** | 12 | |
| LCD RST | `QSPI_PIN_NUM_LCD_RST_1/2` | **3** | **47** | V1.2 复位极性也反相，见第 5 节 |
| LCD 背光 | `QSPI_PIN_NUM_LCD_BL` | **44** | 44 | PWM，`OUTPUT_INVERT=false` |
| I2S MCLK | `AUDIO_I2S_GPIO_MCLK` | **42** | 42 | |
| I2S BCLK | `AUDIO_I2S_GPIO_BCLK` | **40** | 40 | |
| I2S WS | `AUDIO_I2S_GPIO_WS` | **39** | 39 | |
| I2S DOUT | `AUDIO_I2S_GPIO_DOUT` | **41** | 41 | → ES8311 |
| I2S DIN | `AUDIO_I2S_GPIO_DIN_1/2` | **15** | **3** | ← ES7210；V1.2 把 GPIO3 从 LCD RST 改给了 DIN |
| 功放使能 PA | `AUDIO_CODEC_PA_PIN_1/2` | **4** | **15** | V1.2 把 GPIO15 从 DIN 改给了 PA |
| I2C SDA | `AUDIO_CODEC_I2C_SDA_PIN` | **2** | 2 | 400 kHz |
| I2C SCL | `AUDIO_CODEC_I2C_SCL_PIN` | **1** | 1 | |
| 电源域使能 | `POWER_CTRL` | **9** | 9 | 输出，置 0 |
| Codec 电源域 | `CORDEC_POWER_CTRL` | **48** | 48 | V1.2 有效；V1.0 未用 |
| 触摸 INT | `TP_PIN_NUM_INT` | **10** | 10 | ANYEDGE 中断 |
| 原生触摸 PAD1 | `TOUCH_PAD1` | **7** | 7 | 两版共用 |
| 原生触摸 PAD2 | `TOUCH_PAD2_1/2` | **NC** | **6** | V1.0 单按钮 / V1.2 双通道滑条 |
| UART1 TX | `UART1_TX_1/2` | **6** | **5** | 旋转底座；源码未初始化 |
| UART1 RX | `UART1_RX_1/2` | **5** | **4** | 同上 |
| SD MISO / SCK / MOSI | `SD_MISO/SCK/MOSI` | **17 / 16 / 38** | 同 | 源码未初始化 |
| 绿色 LED | `LED_G` | **43** | 43 | 源码未使用 |
| BOOT 按键 | `BOOT_BUTTON_GPIO` | **0** | 0 | 单击：配网 / 切换对话 |

版本切换的 6 个变量（`esp_vocat.cc:701-706`）：
`AUDIO_I2S_GPIO_DIN`、`AUDIO_CODEC_PA_PIN`、`QSPI_PIN_NUM_LCD_RST`、
`TOUCH_PAD2`、`UART1_TX`、`UART1_RX`。

---

## 4. I2C 设备地址表

| 设备 | 7-bit（probe / i2c device） | 8-bit（`audio_codec_i2c_cfg_t.addr`） | 用途 |
|---|---|---|---|
| ES8311 | 0x18 | 0x30 | 扬声器 DAC + 功放控制 |
| ES7210 | 0x40 | 0x80 | 双麦克风 ADC |
| CST816S | 0x15 | — | 触摸，reg 0x02 六字节 |
| 电量计（BQ27220 类，推断） | 0x55 | — | 电压 / SOC / 电流 |
| BMI270 | 驱动默认（0x68 / 0x69） | — | 加速度 |

`AUDIO_CODEC_ES8311_ADDR` / `AUDIO_CODEC_ES7210_ADDR` 在 `config.h` 里指向
`esp_codec_dev_defaults.h` 的 `ES8311_CODEC_DEFAULT_ADDR` / `ES7210_CODEC_DEFAULT_ADDR`
（宏本体不在本仓库内；Muse 的 echoear 板用 0x30 / 0x80，与 7-bit 左移一位一致）。

---

## 5. 显示子系统（ST77916 QSPI）

来源：`esp_vocat.cc:1007-1060`，`config.h`。

| 项 | 值 |
|---|---|
| 总线 | `SPI2_HOST`，`spi_bus_initialize(..., SPI_DMA_CH_AUTO)` |
| 引脚映射 | `sclk=PCLK(18)`，`data0=D0(46)`，`data1=D1(13)`，`data2=D2(11)`，`data3=D3(12)` |
| `max_transfer_sz` | `360 × 80 × sizeof(uint16_t)` = 57,600 B |
| panel IO | CS=14，DC=NC，spi_mode=0，**pclk 40 MHz**，trans_queue_depth=10，**lcd_cmd_bits=32**，lcd_param_bits=8，`flags.quad_mode=1` |
| vendor config | `init_cmds = vendor_specific_init_yysj[]`，**184 条**，`flags.use_qspi_interface=1` |
| panel config | reset_gpio = RST（版本相关），RGB 顺序，16 bpp，**`flags.reset_active_high = pcb_version`**：V1.0 低有效 / V1.2 高有效 |
| 上电动作 | `reset → init → disp_on_off(true) → swap_xy(false) → mirror(false,false)` |
| 尺寸 / 镜像 | 360×360，offset(0,0)，mirror X/Y=false，swap_xy=false |
| 背光 | GPIO44，`PwmBacklight`，`OUTPUT_INVERT=false` |

**184 条 init 表校验**：已用脚本逐条比对官方 `vendor_specific_init_yysj[]` 与 Muse 的
`components/muse/boards/ostb_echoear_display_init.h` 中 `echoear_init_cmds[]`，
结果 **184 == 184 且完全一致（identical: True）**。该文件头部已保留 MIT 许可声明并注明
来源 commit `0d576d3d4c049c6f55eaf879725dc23e516511b4`，可直接复用，无需重抄。

**与 Muse echoear 板的显示差异（不可照抄）**：
- Muse 侧调用了 `esp_lcd_panel_invert_color(s_panel, true)`；官方源码**没有** invert_color。
- 顺序不同：官方先 `disp_on_off(true)` 再 swap/mirror；Muse 先 invert/swap/mirror 再 `disp_on_off(true)`。
- Muse 侧没有 `reset_active_high`，用的是固定 RST=9。
- 两者 `esp_lcd_st77916` 版本一致（官方 `^2.0.2`，Muse `2.0.2`），因此 `reset_active_high` 字段可用；
  但 Muse 全库尚无板子用过该字段，需实机确认。

---

## 6. 音频子系统

来源：`config.h`、`esp_vocat.cc:1187-1194`、`main/audio/codecs/box_audio_codec.cc`。

| 项 | 官方小智固件 | 约束说明 |
|---|---|---|
| 采样率 | in/out 均 **24000 Hz** | `AUDIO_INPUT_SAMPLE_RATE` / `AUDIO_OUTPUT_SAMPLE_RATE` |
| AEC 参考 | `AUDIO_INPUT_REFERENCE = true` | `BoxAudioCodec` 的 `input_reference_`，`input_channels_=2` |
| I2S 端口 | `I2S_NUM_0`，duplex，MCLK multiple 256 | |
| TX（播放） | **STD 模式**，16-bit stereo，`DOUT=41`，`DIN=UNUSED` | 实际以 1 通道打开 |
| RX（录音） | **TDM 模式**，16-bit，**4 槽 SLOT0..3**，`bclk_div=8`，`DIN=15/3` | `ES7210_SEL_MIC1|2|3|4` |
| TDM 槽顺序 | **MIC1, MIC3, MIC2, MIC4**（源码注释明确「与物理 MIC 编号不同」） | `box_audio_codec.cc:220-224` 附近 |
| ES8311 | `codec_mode=DAC`，`pa_pin=PA`，`use_mclk=true`，`pa_voltage`（默认 5.0），`codec_dac_voltage=3.3` | |
| I2C ctrl | `.port = (i2c_port_t)1`，`.bus_handle = i2c_bus_`（实际总线是 I2C_NUM_0） | port 字段只是标识，传输走 bus_handle |

**Muse 框架的硬约束**（`components/muse/muse_audio.h:25-32`）：
`MUSE_AUDIO_RATE = 16000`、16-bit、**2 slots STD**、对外只暴露单声道 `muse_audio_read/write`；
全仓库 `components/muse/boards/` **没有任何 `i2s_tdm` 使用**（已 grep 确认）。
`muse_board_t.mic_slot` 只能取 0 / 1 / -1（混合）。

因此 24 kHz / TDM 4 槽 / AEC 参考通道**无法原样搬进 Muse**，见迁移方案文档的音频决策。

---

## 7. 触摸与原生触摸

### 7.1 CST816S（主触摸）

`esp_vocat.cc:377-476, 791-847`：

- 读 **reg 0x02，6 字节**：`num = raw[0] & 0x0F`，
  `x = ((raw[1] & 0x0F) << 8) | raw[2]`，`y = ((raw[3] & 0x0F) << 8) | raw[4]`。
- 事件状态机：PRESS / RELEASE / HOLD；RELEASE 时若设备处于 Starting 则进配网模式，否则 `ToggleChatState()`。
- INT=GPIO10，`GPIO_INTR_ANYEDGE` + ISR 给二值信号量 + `touch_task`（core1，prio5，4 KB）。
- 单次读失败直接放弃本次事件，不合成按下/松开（README「Peripheral I2C error handling」）。
- 读超时 50 ms，最多 3 次重试、10/20 ms 退避；失败日志每 10 s 限一次。

Muse 侧 `ostb_echoear_touch.h` 的 `ostb_echoear_touch_decode()` 是同一报文格式，且更严格
（要求 `num==1` 且 `x,y < 360`，拒绝多点与越界），**可直接复用**。
差别：Muse echoear 板用 LVGL 轮询、不用 INT；官方用中断驱动。

### 7.2 原生 touch pad（可选功能）

`esp_vocat.cc:860-1004`，仅在 `ESP_IDF_VERSION < 6.0.0` 时编译：
- V1.0：PAD2=NC → 单按钮，通道 = GPIO7，阈值 0.004，debounce 2。
- V1.2：PAD1=GPIO7 + PAD2=GPIO6 → 双通道滑条，阈值 0.004 / 0.006，
  `position_range=10000`、`swipe_threshold=28`、`swipe_hysterisis=22`、`swipe_alpha=0.9`。
- 20 ms 轮询任务，左右滑 / 松手触发「happy」表情。
- Muse SDK **没有** `driver/touch_sensor` 抽象（已 grep 确认），首批适配建议不做。

---

## 8. 电源域、电池与 PCB 版本检测（最关键部分）

### 8.1 两个电源域

| 引脚 | 方向 | 有效电平 | 控制对象 | 源码 |
|---|---|---|---|---|
| `POWER_CTRL` = GPIO9 | 输出 | **低有效**（源码置 0） | SD 卡 / LCD 背光 / LCD 驱动电源 | `esp_vocat.cc:1072-1080` |
| `CORDEC_POWER_CTRL` = GPIO48 | 输出 | **高有效** | V1.2 的 codec 电源轨（SY8088） | `esp_vocat.cc:709-789` |

### 8.2 PCB 版本检测算法（`DetectPcbVersion()`，`:709-789`）——必须迁移

```
1. reset_reason = esp_reset_reason()
2. 【先写输出锁存器，再配置引脚为输出】
   gpio_set_level(GPIO48, reset_reason != ESP_RST_POWERON ? 1 : 0)
   gpio_config(GPIO48 = OUTPUT, 无上下拉)
3. 若「非上电复位」且 RTC_NOINIT 缓存有效（magic == 0x31434256 "VBC1"）：
     若缓存 == 1（V1.2）：拉高 GPIO48，循环最多 10×200ms 探测 0x15 触摸芯片应答，
                          然后 ApplyV12Pins()
     返回缓存值（不再探测）
4. 若「上电复位」：
     拉低 GPIO48 → 延时 100 ms → probe ES8311(0x18)
       有 ACK  → V1.0（codec 电源不受 GPIO48 控制）
       无 ACK  → 拉高 GPIO48，循环最多 10×200 ms 再 probe 0x18
                  成功 → V1.2，ApplyV12Pins()
                  仍失败 → 保持 GPIO48 高（安全兜底），不写缓存，返回 0
5. 结果写入 RTC_NOINIT（magic + version）
```

**为什么这么设计（源码注释与 issue #1202）**：
- 外部复位后 GPIO 输出锁存器默认为 0；若先把 GPIO48 切成输出，会瞬间拉低 V1.2 的 codec 电源，
  导致首次触摸/电量计读失败并重启。所以必须**先写锁存器再切模式**。
- 软复位后 codec 电源轨有残余电荷，GPIO48 拉低时 ES8311 仍会假 ACK，会被误判成 V1.0；
  随后 codec 掉电、麦克风/喇叭失效，未供电芯片还会通过 ESD 二极管钳位共享 I2C 总线 → 重启循环。
  所以软复位一律复用 RTC_NOINIT 缓存。
- `GetAudioCodec()` 是 **lazy `static` 构造**（`:1187-1194`），在构造函数之后才读那 6 个全局引脚变量，
  因此**缓存路径也必须调用 `ApplyV12Pins()`**，否则 V1.2 会用 V1.0 的 DIN/PA。

### 8.3 电量计（`Charge` 类，`:310-375, 827-831`）

- I2C 0x55，读 16-bit 小端寄存器：`0x2C` SOC、`0x0A` BatteryStatus、`0x14` AverageCurrent、`0x0C` Current。
- `BatteryStatus` 的 `BIT0` = DSG（放电）。
- `charging = !DSG || (int16_t)AverageCurrent > 30 mA || (int16_t)Current > 30 mA`（电流作兜底，
  因为 AverageCurrent 对插充电器有滞后）。
- 传输 50 ms 超时；超时 / INVALID_RESPONSE / FAIL 最多重试 3 次，退避 10/20 ms；其它错误立即返回。
- 快照用 mutex 保护，**10 秒过期**，过期返回 false 而不是编造空电池；UI 回调不做 I2C。
- `battery_task`（core0，prio6，3 KB）：成功 2 s 采样一次，失败退避 5 s 且不触发表情。
- 首个有效样本建立初始充电状态，避免假的「插入充电器」事件。
- 低电量表情：<10%、<5% 分档，最多 3 次、间隔 5 分钟（属业务表现，Muse 侧无对应钩子）。

---

## 9. 上电初始化顺序（`EspVocat()` 构造函数，`:1160-1185`）

```
esp_timer_create(emotion_rst)
InitializeI2c()                  I2C_NUM_0 / SDA2 / SCL1 / 400 kHz + 片内温度传感器
DetectPcbVersion()               GPIO48 / RTC 缓存 / ApplyV12Pins()
InitializeCharge()               0x55 + battery_task(core0, prio6, 3 KB)
InitializeCst816sTouchPad()      0x15 + touch_task(core1, prio5, 4 KB) + INT GPIO10 ISR
InitializeBmi270()               IMU + imu_task(core1, prio4, 4 KB)
InitializeSpi()                  QSPI SPI2_HOST 总线
InitializeSt77916Display(ver)    panel + Display + PwmBacklight(GPIO44)
InitializeButtons()              BOOT GPIO0 + POWER_CTRL GPIO9 输出置 0
InitializeCapacitiveTouchPads()  原生 touch（仅 IDF < 6.0）
InitializeCamera()               仅 USB UVC 条件编译
```

`GetAudioCodec()` 由框架在构造完成后按需调用（lazy static），所以 PCB 版本切换必须在此之前完成。

---

## 10. 与 Muse SDK 已有 `ostb_echoear_2st` 的逐项差异（为何不能直接复用）

| 功能 | 官方 ESP-VoCat（本文目标） | Muse `ostb-echoear-2st` |
|---|---|---|
| QSPI CLK / CS | 18 / 14 | 8 / 3 |
| QSPI D0-D3 | 46 / 13 / 11 / 12 | 4 / 5 / 6 / 7 |
| LCD RST | 3 (V1.0) 或 47 (V1.2) | 9 |
| LCD 背光 | 44 | 41 |
| I2C SDA / SCL | 2 / 1 | 12 / 11 |
| 触摸 INT | 10（中断驱动） | 42（未使用，LVGL 轮询） |
| I2S MCLK / BCLK / WS | 42 / 40 / 39 | 10 / 15 / 16 |
| I2S DOUT / DIN | 41 / 15 或 3 | 14 / 13 |
| 功放 PA | 4 或 15 | 18 |
| 电源域控制 | GPIO9 + GPIO48（含版本检测） | 无 |
| 电量计 | 0x55，已实现完整采样 | 未实现（文档标注 unverified） |
| IMU | BMI270，已实现 | 无 |
| SD 卡 | 引脚已定义（17/16/38） | 无 |
| 原生 touch pad | 有（GPIO7 / GPIO6） | 无 |
| 旋转底座 UART | 有（6/5 或 5/4） | 无 |
| PSRAM | 16 MB octal | 8 MB octal |
| 音频格式 | 24 kHz / TDM 4 槽 / AEC 参考 | 16 kHz / STD 2 槽 / 无 AEC |

**相同点（可直接复用）**：
1. ST77916 面板 IC 与 **184 条 init 表逐条一致**（脚本校验 identical）。
2. CST816S 触摸 0x15 + reg 0x02 六字节报文格式。
3. ES8311 / ES7210 芯片组合与 7/8-bit 地址。
4. ESP32-S3 + 360×360 圆屏 + Octal PSRAM 的整体形态。

---

## 11. 立创页记录的已知硬件问题

1. **V1.0 需删除 BaseBoard 的 C1、C10 与 CoreBoard 的 C4（三个 10 μF 电容）**：
   会影响板上 VCC 电源域供电能力，导致概率性芯片重启。
2. **旋转底座噪声**：电机振动经固体结构传导至麦克风，严重干扰声源识别；
   底座固件的规避是「转动过程中不接收本体发来的任何角度指令」。
3. **部分批次屏幕触摸位置偏移**：需烧录触摸升级固件
   （`vocat_touch_update_5c18.bin`；绿灯常亮表示成功，快闪表示失败）。
4. 立创页声明：复刻需自行验证电路可行性。

---

## 12. 未证实 / 待实机确认项（不要当结论用）

- 电量计具体型号：源码寄存器与 BQ27220/BQ27441 吻合，但硬件页 BOM 未列出，**属推断**。
- `TP_PORT = I2C_NUM_1` 与实际使用 `I2C_NUM_0` 的不一致（见第 2 节），以代码为准。
- SD 卡 / UART1 底座 / LED_G：仅 `config.h` 定义了引脚，板级源码未初始化，功能是否可用未知。
- ST77916 的 `invert_color` 官方源码未调用，而 Muse 同类板调用了；颜色是否需要反转需上板确认。
- `reset_active_high` 组件字段在 Muse 侧尚无使用先例，V1.2 支持需实机验证。
- ES7210 在 16 kHz / STD 双槽下的实机录音效果（见迁移方案音频决策）。

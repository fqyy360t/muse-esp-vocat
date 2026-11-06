# ESP-VoCat → Muse ESP32 Device SDK 迁移方案

> 目标：在 `F:\git\muse-gadget-sdk\esp32` 框架内，**新编一块板级适配**，让喵伴官方 PCB
> （`espressif/esp-vocat`）跑起 Muse 的完整 UI / 语音 / 设置。
> 配套硬件契约见 `docs/esp-vocat-硬件接口定义.md`。整理日期：2026-11-06。
> 本文件是设计与迁移方案。**代码已于 2026-11-06 落地**，但不在 SDK 仓库内：完整适配包在
> `F://git//miaoban-muse`（`board/` + `apply.py` + 板文档），需对 SDK 副本运行 `apply.py` 才生效。
> 当前状态与未完成项见第 10 节。

---

## 1. 框架速览（决定「新编程序」的正确形态）

```
main/main.c  app_main()
  ├─ app_run()                Home Link：Wi-Fi / BLE / 配对 / 凭据 / OTA / 状态（不碰板级 GPIO）
  └─ muse_glue_start()  ──►  muse_app_run(muse_board_get())     ← 板级代码唯一注入点
                                components/muse/muse_app.c
                                  board->init() → settings → state → battery → UI → input → voice → wifi/ble
```

- **两层 + 一个接口**：`main/` = Home Link；`components/muse/` = Muse UI/语音；
  二者靠 `muse_board_t`（`components/muse/muse_board.h:79-173`）解耦。
- 每块板只实现 `components/muse/boards/board_<id>.c` 里的
  `static const muse_board_t s_board` + `muse_board_get()`；
  `components/muse/CMakeLists.txt:82-163` 的 `if/elseif` 链保证**同一时刻只链接一个板文件**。
- 板选择宏是 `CONFIG_MUSE_BOARD_<NAME>`（定义在 `components/muse/Kconfig` 的 `choice MUSE_BOARD`），
  板 ID 字符串是 `MUSE_BOARD_ID`（给 `idf_component.yml` 的 rules 用）。
- `devices/` 目录**只有文档和 sdkconfig overlay，没有 C 代码**。

**结论**：所谓「新编一个适配 vocat 的程序」，在这个框架里的正确形态不是另起一个工程，
而是**新增一个板级驱动文件 + 一份 overlay + 若干注册项**；业务层（UI/语音/网络）全部复用，
一行都不用重写。这也是仓库 `devices/AGENTS.md:194-228` 规定的六步。

---

## 2. 核心判断：不能复用 `ostb-echoear-2st`，但可以复用它的 4 样东西

`board_ostb_echoear_2st.c` 与官方喵伴**只有面板 IC、init 表、触摸协议、codec 芯片相同，
GPIO 全部不同**（逐项对照见硬件定义文档第 10 节）。直接改 overlay 里的宏是错的。

可复用资产：

| 资产 | 路径 | 复用方式 | 许可 |
|---|---|---|---|
| 板文件骨架（275 行，结构最干净） | `components/muse/boards/board_ostb_echoear_2st.c` | 复制后逐项替换 | Apache-2.0 |
| ST77916 184 条 init 表 | `components/muse/boards/ostb_echoear_display_init.h` | **直接 include，不改**；已脚本校验与官方逐条一致 | **MIT**（文件头已保留声明与来源 commit，务必不要删） |
| CST816S 报文解码 | `components/muse/boards/ostb_echoear_touch.h` | 直接 include | Apache-2.0 |
| BQ27220 电量计裸寄存器读取 | `components/muse/boards/board_deotaland_rorolee.c:89-92, 252-258, 494-508` | 抄 `gauge_read()` + `read_power()` 模式 | Apache-2.0 |

---

## 3. 硬件 → `muse_board_t` 接口映射表

建议命名：板 ID `esp_vocat`，Kconfig `MUSE_BOARD_ESP_VOCAT`，overlay `sdkconfig.muse-esp-vocat`，
板文件 `board_esp_vocat.c`，工具别名 `vocat`（与 `echoear` 区分，避免混淆两块 PCB）。

| 官方机制 | 落到哪个字段 / 函数 | 具体做法 |
|---|---|---|
| `POWER_CTRL` GPIO9（电源域，低有效） | `init()` | **最先**配输出并置 0，否则屏/SD 无电 |
| `CORDEC_POWER_CTRL` GPIO48 + PCB 版本探测 | `init()`（检测）+ `codec_power(bool)`（供电开关） | 见第 5 节；V1.0 上该脚不用 |
| I2C_NUM_0 / SDA2 / SCL1 / 400 kHz | `init()` | `i2c_new_master_bus`，内部上拉，glitch_ignore_cnt=7 |
| QSPI ST77916 360×360 | `display_start()` | 换成 18/14/46/13/11/12，RST 与极性随 PCB 版本 |
| 背光 GPIO44 | `set_brightness()` | LEDC 20 kHz / 10 bit；`OUTPUT_INVERT=false` |
| 面板睡眠 | `panel_sleep()` | `esp_lcd_panel_disp_sleep` |
| CST816S 0x15 / reg2 | `display_start()` 注册 LVGL pointer indev | 复用 `ostb_echoear_touch_decode`；`.touch=true`、`.touch_talk=true` |
| 触摸 INT GPIO10 | 可选 | 首批用 LVGL 轮询（echoear 已验证路径），INT 留后续做低功耗唤醒 |
| ES8311 + ES7210 | `audio_init()` | `esp_codec_dev`，8-bit 地址 0x30 / 0x80，PA 随版本，`.mic_slot=-1` |
| 双麦 | `mic_slot = -1` | 框架把立体声两槽混成单声道 |
| 电量计 0x55 | `read_power()` | 填 `battery_pct / battery_mv / charging / usb`；失败返回 `ESP_ERR_NOT_FOUND` |
| BOOT GPIO0 | `poll_buttons()` | **建议返回 0**（与 echoear 一致），talk 走屏上麦克风 PTT；官方 BOOT 是「单击切对话」，与 Muse 的「按住说话」语义不匹配 |
| 物理 SAM8108 电源键 | `.power_off` | 显式返回 `ESP_ERR_NOT_SUPPORTED`；`devices/AGENTS.md:254` 明令**不要猜 latch GPIO** |
| BMI270 / 原生 touch pad / SD / UART 底座 / LED_G | **无对应抽象** | 首批不做；Muse 无 `driver/touch_sensor`、无 IMU 抽象、无底座抽象 |
| 24 kHz / TDM 4 槽 / AEC | 无对应抽象 | 改为 16 kHz / STD 2 槽，见第 4 节 |

`s_board` 建议值：`name="Espressif ESP-VoCat"`、`width=height=360`、`round=true`、
`touch=true`、`touch_talk=true`、`diagonal_in=1.85f`、`frame_ms=40`、
`talk_hint={LV_ALIGN_BOTTOM_MID,0,-40}`、`talk_button="screen microphone"`。

---

## 4. 音频决策（本次迁移最大的技术约束）

| | 官方小智 | Muse 框架 |
|---|---|---|
| 采样率 | 24 kHz | **固定 16 kHz**（`MUSE_AUDIO_RATE`） |
| RX 模式 | **TDM 4 槽** SLOT0..3，ES7210 选 MIC1/2/3/4 | **STD 2 槽**（全库无 `i2s_tdm`） |
| AEC | `input_reference=true`，4 槽中一路是功放回采 | 无 AEC |

两个方案：

- **方案 A（推荐）｜让硬件迁就框架**：`audio_init()` 用 STD stereo 16 kHz，
  `es7210_codec_cfg_t.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2`，`.mic_slot = -1`。
  现成范例：`board_ostb_echoear_2st.c:86-132`、`board_waveshare_s3_206.c:256`。
  优点：不动 `components/muse/muse_audio.*` 共享层，不影响其它 40+ 块板，改动最小、可编译可验证。
  代价：放弃 TDM 4 槽与 AEC 回声参考，全双工体验弱于小智固件。
- **方案 B｜扩展框架**：给 `muse_audio` 增加 TDM / 4 通道 / AEC 支持。
  代价：改共享层，牵动所有板，需重跑全套 host unittest 与多板回归；风险显著更高。
  建议作为后续独立里程碑，不放在首次适配里。

**必须如实标注**：方案 A 在同类板上标注为 "live audio test still required"；
ES7210 在 16 kHz / STD 双槽下的实机录音电平、左右槽顺序、`mic_slot=-1` 混合是否正确，
都属于**上板验证项**，不能从源码推导为「已可用」。

---

## 5. 板文件关键实现要点（C 语言骨架，非最终代码）

### 5.1 `init()` —— 顺序不能改

```c
static esp_err_t init(void)
{
    /* 1) 主电源域：先给屏/SD 上电，低有效 */
    gpio_set_level(GPIO_NUM_9, 0);              /* 先写锁存器再切输出模式 */
    gpio_config(&(gpio_config_t){ .pin_bit_mask = BIT64(GPIO_NUM_9),
                                  .mode = GPIO_MODE_OUTPUT });

    /* 2) Codec 电源域 + PCB 版本探测（RTC_NOINIT 缓存），见 5.2 */
    s_pcb_v12 = detect_pcb_version();           /* 返回 0=V1.0, 1=V1.2 */

    /* 3) 共享 I2C：SDA=2 SCL=1 400kHz，probe 0x18/0x40/0x15/0x55 */
    i2c_new_master_bus(...);  i2c_master_bus_add_device(/* touch 0x15 */);
    i2c_master_bus_add_device(/* gauge 0x55 */);

    /* 4) 按版本确定 6 个可变引脚（DIN/PA/RST/TOUCH_PAD2/UART1_TX/UART1_RX） */
    return ESP_OK;
}
```

### 5.2 `detect_pcb_version()` —— 原样移植官方算法，别简化

必须保留四件事（硬件定义文档第 8.2 节有完整伪代码）：
1. **先 `gpio_set_level(GPIO48, ...)` 再 `gpio_config()` 切成输出**（外部复位后锁存器默认 0，
   瞬间拉低会切断 V1.2 的 codec 电源 → 重启循环，官方 issue #1202）。
2. `RTC_NOINIT_ATTR` + magic `0x31434256` 缓存；**软复位一律复用缓存，不重新探测**
   （codec 电源轨残余电荷会造成假 ACK，误判成 V1.0）。
3. 上电复位才探测：拉低 48 → 100 ms → probe `0x18`；无 ACK 则拉高 48，
   循环最多 10×200 ms 再 probe。
4. **两条路径（探测到 V1.2、以及缓存命中 V1.2）都要调用 `apply_v12_pins()`**，
   因为 `audio_init()` / `display_start()` 是之后才被框架调用的。

V1.2 还需在拉高 48 后循环探测 `0x15`（最多 10×200 ms）等触摸芯片上电，否则首次 I2C 读会 abort。

### 5.3 `codec_power(bool)` —— 需要单独评估，不要机械绑定 GPIO48

`muse_board.h:155-160` 的 `codec_power` 语义是「codec 自身供电：开之前 on，两个都关掉后 off」，
而官方板 GPIO48 同时是 **V1.2 的电源轨 + 版本探测脚**。若把它直接挂上去，
`muse_audio_power(false)` 会拉低 48，下次上电时 ES8311/ES7210 寄存器全部丢失，
框架重新 open 前还需要 200 ms 级的上电等待与重新配置。

建议：
- 首次适配**先不提供 `codec_power`（留 NULL）**，`init()` 探测完成后把 GPIO48 固定保持为
  V1.2 需要的高电平；让 codec 常供电，与 V1.0 行为一致。
- 待上板确认 `muse_audio_power()` 的调用频率后，再决定要不要实现带「上电等待 + 重新初始化」的
  `codec_power`。这是**降低首次适配风险**的取舍，不是遗漏。

### 5.4 `audio_init()`

```c
/* clk: MUSE_AUDIO_RATE(16000), STD 16-bit stereo, mclk_multiple 256 */
.gpio_cfg = { .mclk = 42, .bclk = 40, .ws = 39,
              .dout = 41, .din = (v12 ? 3 : 15) };
ctrl_cfg.addr = 0x30;  /* ES8311, 8-bit */
ctrl_cfg.addr = 0x80;  /* ES7210, 8-bit */
es8311_codec_cfg_t dac = { .pa_pin = (v12 ? 15 : 4), .use_mclk = true,
                           .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3} };
es7210_codec_cfg_t adc = { .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 };
```

注意 `audio_codec_i2c_cfg_t.port`：官方 `BoxAudioCodec` 写 `(i2c_port_t)1` 但 bus_handle 指向
I2C_NUM_0；Muse echoear 板写 `I2C_NUM_0`。传输实际走 bus_handle，**跟随 Muse 惯例用 I2C_NUM_0**。

### 5.5 `display_start()`

- `spi_bus_config_t`：`sclk=18, data0=46, data1=13, data2=11, data3=12`
  （`TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG` 的映射：sclk→PCLK、d0→DATA0 …），
  `max_transfer_sz = 360*10*2`（Muse 用 `DRAW_BUF_LINES=10`，官方用 80 行；按框架惯例取 10）。
- `panel_io`：CS=14、DC=-1、40 MHz、cmd_bits=32、param_bits=8、quad_mode。
- `vendor_config.init_cmds = echoear_init_cmds`（直接复用 MIT 文件）。
- `panel_cfg.reset_gpio_num = v12 ? 47 : 3`，
  **`panel_cfg.flags.reset_active_high = v12`**（官方源码 `:1041`）。
- 官方未调 `invert_color`；Muse echoear 板调了 `invert_color(true)`。
  **首批跟随官方（不 invert）**，上板若颜色反了再改，并在验证记录里留痕。
- 背光 LEDC 用 GPIO44；`ESP_LV_ADAPTER_ROTATE_0`、hor/ver_res=360、double buffer。
- 注册 LVGL pointer indev，read_cb 复用 `ostb_echoear_touch_decode()`。

### 5.6 `read_power()`

抄 `board_deotaland_rorolee.c:252-258, 494-508` 的 `gauge_read()`：
`i2c_master_transmit_receive(dev, &reg, 1, b, 2, 50)` → `b[0] | b[1]<<8`。
填 `0x08`→`battery_mv`、`0x2C`→`battery_pct`（>100 记 -1）、`0x0C`→`charging`（>10/30 mA）。
可保留官方的 3 次重试 + 10 s 快照过期语义，避免 I2C 抖动传导到 UI。

---

## 6. 新增一块板的最小改动清单（依 `devices/AGENTS.md:194-228`）

| # | 文件 | 动作 |
|---|---|---|
| 1 | `components/muse/Kconfig` | `choice MUSE_BOARD`（:17-173）加 `config MUSE_BOARD_ESP_VOCAT`（`depends on IDF_TARGET_ESP32S3`）；`MUSE_BOARD_ID`（:182-222）加 `default "esp_vocat" if MUSE_BOARD_ESP_VOCAT`（放在 `default "none"` 之前） |
| 2 | `components/muse/CMakeLists.txt` | :82-163 链加 `elseif(CONFIG_MUSE_BOARD_ESP_VOCAT) list(APPEND srcs "boards/board_esp_vocat.c")`。沿用既有组件则 `PRIV_REQUIRES` 无需改（**新组件必须无条件列出，requirements 先于 Kconfig 解析**） |
| 3 | `components/muse/idf_component.yml` | 把 `esp_vocat` 追加进 `esp_lvgl_adapter`(:44)、`esp_lcd_st77916`(:130) 的 `MUSE_BOARD_ID` 列表。**rules 只能用 `MUSE_BOARD_ID`，不能用 `CONFIG_MUSE_BOARD_*`** |
| 4 | `components/muse/boards/board_esp_vocat.c` | **新建**，实现第 3/5 节所有回调；init 表与触摸解码用 include 复用 |
| 5 | `devices/sdkconfig.muse-esp-vocat` | **新建**：target=esp32s3、`CONFIG_MUSE_BOARD_ESP_VOCAT=y`、**Octal Flash（实测 eFuse 固定 octal、物理 32 MB，固件按 16 MB）**：`CONFIG_ESPTOOLPY_OCT_FLASH=y`、`CONFIG_ESPTOOLPY_FLASHMODE_OPI=y`（**不是 DIO**）、`SPIRAM_MODE_OCT`/`SPEED_80M`、native USB console、`CONFIG_HOMEHUB_BUTTON_GPIO=0`、16 MB 用 `partitions_muse.csv` |
| 6 | `tools/muse/board.sh` | 加 `vocat) profile=esp-vocat; target=esp32s3 ;;`，并更新 usage(:17) 与 `${2:?...}`(:32) |
| 7 | `tools/muse/ports.py` / `tools/muse/avatar.py` | USB VID/PID（native USB 303A:1001）与 `name → alias` 映射 |
| 8 | `devices/esp-vocat.md` | **新建**板文档：硬件映射表、双 PCB 版本、加载顺序、构建命令、未验证项 |
| 9 | `devices/README.md` / 根 `AGENTS.md` / 根 `README.md` | Supported devices / Features / Build 三张表各加一行 |

**不需要改**：`main/*`、`components/muse/muse_*.c`（UI/audio/input/battery 共享层）、
`partitions_muse.csv`、`sdkconfig.defaults`、`main/Kconfig.projbuild`。

---

## 7. 风险与约束清单

| 风险 | 说明 | 处置 |
|---|---|---|
| 音频格式降级 | 24 kHz TDM4 + AEC → 16 kHz STD2 | 采用方案 A；上板验证双麦录音，方案 B 留作后续里程碑 |
| `codec_power` 与 GPIO48 双重身份 | 反复断电会丢 codec 寄存器 | 首次留 NULL，常供电；见 5.3 |
| `reset_active_high` | Muse 全库无先例，V1.2 复位极性相反 | 组件版本两侧同为 2.0.2，字段应可用；**必须实机验证 V1.2** |
| `invert_color` / `disp_on` 顺序 | 官方与 Muse echoear 板不一致 | 跟随官方，上板核对颜色 |
| PSRAM 容量 | 官方 16 MB，同类 overlay 写 8 MB | 按官方模组写 overlay，不照抄 8 MB 注释 |
| 许可 | init 表是 MIT（来自 78/xiaozhi-esp32），主体是 Apache-2.0 | **保留 `ostb_echoear_display_init.h` 文件头 MIT 声明与来源 commit** |
| `idf_component.yml` rules | 用 `CONFIG_MUSE_BOARD_*` 会在其它 target 上未定义 | 只用 `MUSE_BOARD_ID` |
| CMake requirements | 在 Kconfig 之前解析 | 新组件依赖无条件列 `PRIV_REQUIRES` |
| 并发构建 | 各板共享 `managed_components/` 与 `dependencies.lock` | 一次只构建一个 profile，切换前清理 |
| 编译环境 | 仓库要求 **ESP-IDF v6.0.3** + `CONFIG_GADGET_SDK_TOKEN` | 本机是否具备待确认；不具备时只交付代码、不宣称编译通过 |
| 硬件已知问题 | V1.0 三个 10 μF 电容、底座振动噪声、部分批次触摸偏移 | 属板子本身，写在 `devices/esp-vocat.md` 里，别在软件里打补丁 |
| 上游仓库 | `F:\git\muse-gadget-sdk` 可能非本人仓库 | **只改本地功能分支 / fork，不 push 上游**；推送前确认工作树与分支 |

---

## 8. 构建与验证（落地代码后执行）

**本机环境实测（2026-11-06，编码前先读这段）**：

| 项 | 实测结果 | 影响 |
|---|---|---|
| ESP-IDF 安装 | `C:\Espressif\frameworks\esp-idf-v5.5.5`，`export.*`、`xtensa-esp-elf`、`python_env/idf5.5_py3.11_env` 齐全；`idf.py --version` → `ESP-IDF v5.5.5` 可执行 | 工具链完整，但**版本不是 SDK 要求的那个** |
| SDK 要求版本 | `esp32/README.md:85-99` 原文 "built with ESP-IDF **version 6.0.3**. Other versions aren't supported"；CI `.github/workflows/esp32.yml:35,112` 用容器 `espressif/idf:v6.0.3` | **v6.0.3 是唯一受支持版本**，本机未安装 |
| 组件 manifest | `components/muse/idf_component.yml:16` → `idf: version: ">=5.5"`；`main/idf_component.yml:24` → `">=5.1"` | 只代表组件管理器的**下限**，与 README 不矛盾；v5.5.5 能过依赖解析，不代表能编译通过 |
| 环境变量 | `IDF_PATH` 为空、`idf.py` 不在 PATH；手动设 `IDF_PATH` 跑 idf.py 会报 `IDF_PYTHON_ENV_PATH is missing` | 真正构建必须先执行 `export.bat` / `export.ps1` 激活，不能只设 `IDF_PATH` |
| 构建缓存 | `esp32/` 下无 `build/`、`sdkconfig`、`managed_components/` | 首次构建会联网拉取全部组件，需保证代理可用 |
| 仓库归属 | `muse-gadget-sdk` origin = `facebookincubator/muse-gadget-sdk`（上游），`xiaozhi-esp32` origin = `78/xiaozhi-esp32`（上游），两者均干净 `main`；`miaoban-muse` 已 `git init` + 本地提交 `67b116b`，**未添加远程、未 push** | 适配包只落工作区，SDK 仓库零改动；绝不 push 两个上游 |

=> 编译验证分两级：先用现有 v5.5.5 跑 `reconfigure` 快速探底（能过就继续）；若因 v6.0 API 报错，则
需要另行安装 ESP-IDF v6.0.3（约 GB 级下载 + 工具链，属需用户同意的大动作），
**不要为了过编译去改共享层代码迁就 v5.5.5**。

**2026-11-06 复核补充**：`tools/muse/board.sh` 自身也会在 `$HOME/.espressif/esp-idf-v6.0.3` 等
路径找 `export.sh`，找不到就直接报 `idf.py not found; activate ESP-IDF v6.0.3 first`。本机 Docker
Desktop 虽已安装，但 **daemon 未运行**（`npipe:////./pipe/dockerDesktopLinuxEngine` 连不上），
因此 `espressif/idf:v6.0.3` 容器路径当前也不可用。**截至本次复核仍未做任何编译**。

```bash
# 助手脚本（推荐）
tools/muse/board.sh build vocat

# 等价手写
idf.py -B build-muse-esp-vocat -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-esp-vocat/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-esp-vocat" build
```

按 `devices/AGENTS.md:284-315` 逐项验收：
1. 逐行核对 overlay 的每一项确实进入生成的 `build-*/sdkconfig`（C5 调优的 defaults 在 S3 上会被静默丢弃）。
2. `check_sizes`：应用分区空闲 <10% 要警惕。
3. 重建至少另一块 UI 板，确认 `elseif` 链没破坏别人。
4. `python3 -m unittest discover -s tests -p 'test_*.py'`。
5. 上板 `tools/muse/monitor.py PORT 30`：屏亮、触摸对齐、配对、录放、电池读数、V1.0/V1.2 各自启动。

---

## 9. 三个决策（2026-11-06 已定）

> **最终选择：1B / 2A / 3「保持最小集」**。代码**只写在 `F://git//miaoban-muse` 工作区**，不修改
> `muse-gadget-sdk` 仓库，由用户单独提交到自己的 GitHub。交付形态为「新增板文件 + overlay +
> 板文档 + `apply.py` 锚点补丁包」，用户对本地 SDK 副本运行 `apply.py` 完成合入。
> 音频按方案 A（16 kHz / STD 双槽），协作骨架保持最小集。

1. **代码落点**：(A) 直接在 `F:\git\muse-gadget-sdk\esp32` 新增板级文件（推荐，可编译可验证）；
   (B) 只在 `F:\git\miaoban-muse` 产出补丁包 / 独立工程，不改 SDK 仓库；
   (C) 先只交设计文档，代码下一轮再说。
2. **音频方案**：(A) 对齐 Muse 框架 16 kHz / STD 双槽（推荐）；(B) 扩展共享层支持 TDM+AEC。
3. **协作骨架**：是否按 `project-flow-cy` 全量接入 `flow/` + `docs/` + hook + AGENTS.md/CLAUDE.md 入口
   （当前只建了 `docs/` 两份文档与 `flow/进展.md` 最小交接集）。

---

## 10. 当前状态 / 未完成清单（2026-11-06 复核）

> **21:58 复核更新（覆盖下表第 1、3 项）**：已在独立 SDK 副本上完成真实构建链验证——
> `reconfigure` 通过、overlay 全部生效、**`board_esp_vocat.c` 编译通过**（`.obj` 126 KB）；
> 全量链接停在**上游共享层** `main/link_pairing.c:31` 的 `mbedtls/private/gcm.h`
> （v5.5.5 无此头文件，原始 SDK 同样有该 include），因此仍需 **IDF v6.0.3** 才能产出 `.bin`。
> 单测已用对照实验归因为 Windows 环境性。完整证据见 `docs/构建验证记录.md`。
> Docker 容器路径本机不可用（manifest `Bad Gateway`）。

| # | 未完成项 | 阻塞条件 | 谁来做 |
|---|---|---|---|
| 1 | **编译验证**：`apply.py` 应用到 SDK 副本 → `board.sh build vocat` → 核对生成的 `sdkconfig` → `check_sizes` | 本机只有 IDF v5.5.5；SDK/CI/board.sh 均要求 **v6.0.3**，Docker daemon 未运行 | 需用户装 v6.0.3 或启动 Docker 后，我可继续 |
| 2 | **回归编译另一块 UI 板**，确认 CMake `elseif` 链没破坏其它板 | 同 1 | 同上 |
| 3 | **跑 SDK 单测** `python3 -m unittest discover -s tests -p 'test_*.py'`（位于 `<sdk>/esp32/tests`） | 同 1（部分单测为纯 host 测试，可提前跑） | 我可先跑 host 单测 |
| 4 | **上板验收**：屏亮 / 触摸对齐 / 配对 / 录放 / 电池读数 / 颜色反转 / V1.0 与 V1.2 各自启动 | 需编译产物 + 实物（喵伴已在 COM27） | 用户烧录后配合串口日志 |
| 5 | **确认 PCB 版本与电量计型号**：目前 GPIO48 探测与 0x55 寄存器是按官方代码移植，未用实物确认 | 需上板串口输出 | 同 4 |
| 6 | **应用补丁到真实 SDK 副本**：`apply.py` 只在临时骨架副本上验证过（19/19 + 幂等），真实副本尚未应用 | 用户指定副本路径 | 用户执行或授权我执行 |
| 7 | **GitHub 提交**：已本地 commit `67b116b`，未加远程、未 push | 用户提供仓库地址并授权 | 用户 |
| 8 | 音频方案 B（24 kHz / TDM 4 槽 / AEC 参考通道） | Muse 共享层无 `i2s_tdm`，需改共享层 | 后续里程碑，当前方案 A 可用即可 |
| 9 | BMI270 IMU / microSD / UART 旋转底座 / 摄像头接入 | Muse 框架无对应抽象 | 后续里程碑，非必需 |
| 10 | `project-flow-cy` 全量协作骨架（charter/plan/decisions/踩坑记录/tasks/hook/AGENTS 入口） | 用户已选「保持最小集」 | 明确不做 |

**本次复核补做（2026-11-06 21:10）**：`apply.py --check` 复跑仍 19/19；在临时骨架副本上
应用补丁后，对被改的 `ports.py` / `avatar.py` 做 `ast.parse` **语法检查通过**。
`board.sh` 的 shell 语法检查（`bash -n`）被本机安全策略拦截（禁止启动该程序），
**未执行、也不绕过**；其内容与 CRLF 已在前一棒的临时副本验证中确认。

**已完成**：硬件契约、迁移方案、板级 C 驱动、自包含面板表/触摸解码、Octal Flash overlay、
板文档、19 处锚点补丁脚本、静态自检（括号/字段/184 条 init 表/`--check` 19/19/临时副本幂等）、
实物预检、文档与许可证、工作区 git 初始化与本地提交、SDK 上游零改动。

# ESP-VoCat（喵伴 / EchoEar）→ Muse ESP32 板级适配包

把乐鑫官方 **ESP-VoCat / EchoEar** 圆屏语音板，适配到
[`facebookincubator/muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk)
的 ESP32 固件框架。

这个仓库**只包含新增的板级代码和一份锚点式补丁**，不是 SDK 的 fork，也不是可直接编译的完整工程。
用法是：在你自己的 `muse-gadget-sdk` 副本里跑一次 `apply.py`，把板文件与注册项合进去，然后照常构建。

## 目录

| 路径 | 内容 |
|---|---|
| `board/board_esp_vocat.c` | 核心板级驱动：`muse_board_t` + 双 PCB 版本探测 + 显示/触摸/音频/电量计 |
| `board/esp_vocat_display_init.h` | ST77916 的 184 条初始化命令（已脚本校验与官方逐条一致） |
| `board/esp_vocat_touch.h` | CST816S reg 0x02 六字节解码 |
| `board/sdkconfig.muse-esp-vocat` | 板级 sdkconfig overlay（**Octal Flash / OPI**） |
| `board/esp-vocat.md` | 板级说明：GPIO、版本差异、构建、验收、未验证项 |
| `apply.py` | 一键应用：复制 5 个新文件 + 修改 8 个上游文件（共 19 处） |
| `docs/esp-vocat-硬件接口定义.md` | 完整硬件契约（元件 / 组件 / GPIO / 电源域 / 初始化顺序） |
| `docs/muse-sdk-迁移方案.md` | 迁移设计、接口映射、风险与构建验证 |
| `flow/进展.md` | 按 `project-flow-cy` 写的接力交接记录 |

## 快速开始

```bash
# 1) 只验证补丁锚点是否还能对上你的 SDK 版本（只读）
python apply.py --sdk /path/to/muse-gadget-sdk --check

# 2) 预览 / 实际应用
python apply.py --sdk /path/to/muse-gadget-sdk --dry-run
python apply.py --sdk /path/to/muse-gadget-sdk

# 3) 用 ESP-IDF v6.0.3 构建（先执行 IDF 的 export 脚本激活环境）
cd /path/to/muse-gadget-sdk/esp32
tools/muse/board.sh build vocat
tools/muse/board.sh flash vocat COM27
```

`apply.py` 特性：只改你显式传入的目录、锚点必须唯一命中、失败即停、可重复运行（第二次全部跳过）、
保留上游文件原本的换行风格。`--force` 才会覆盖已存在的同名新文件。

> 上游两个仓库（`facebookincubator/muse-gadget-sdk`、`78/xiaozhi-esp32`）都不是本人仓库。
> **请在自己的副本 / 功能分支里操作，不要 push 上游。**

## 关键事实（实物实测，2026-11-06 · COM27）

| 项 | 结果 |
|---|---|
| USB | `VID_303A & PID_1001`，ESP32-S3 原生 USB-Serial/JTAG |
| Flash | eFuse 固定 **octal 8 线**，MXIC OPI，物理 **32 MB**；出厂固件按 **16 MB** 使用 |
| PSRAM | 内封 **16 MB Octal** (AP_1v8) @ 80 MHz |
| 芯片 | ESP32-S3 (QFN56) rev **v0.2** |
| 出厂固件 | `xiaozhi` 2.0.4.4，编译于 2025-12-02，ESP-IDF v5.5.1-dirty |
| PCB 版本 | **V1.0 / V1.2 两套 GPIO**，运行时靠 GPIO48 探测 + RTC 缓存切换 |

所以 overlay 用的是 `CONFIG_ESPTOOLPY_OCT_FLASH=y` + `FLASHMODE_OPI` + `SPIRAM_MODE_OCT`，
**不是** Muse 其它板通用的 DIO。写错会烧录后起不来。

## 状态

- ✅ 硬件契约、迁移方案、板级 C 代码、overlay、板文档、一键应用脚本
- ✅ `apply.py --check` 19/19 锚点通过；独立 SDK 副本上真实应用 + 二次运行 19 项全 skip
- ✅ C 代码静态自检：括号平衡、20 个 `muse_board_t` 字段全部存在、184 条 init 表一致
- ✅ **真实构建链**：`idf.py reconfigure` 通过、overlay 全部进入 `sdkconfig`、
  **`board_esp_vocat.c` 编译通过**（`.obj` 126 KB，退出码 0）——详见 [`docs/构建验证记录.md`](docs/构建验证记录.md)
- ✅ **ESP-IDF v6.0.3 容器全量编译通过**：`BUILD_RC=0`，产出 `muse-gadget.bin` 2.1 MB、
  `muse-gadget.elf`、bootloader 与分区表；分区余量 **48%**、**0 error**。
  另一块 UI 板 `ostb-echoear-2st` 回归编译同样 `RC=0`，确认新分支没破坏其它板。
  详见 [`docs/构建验证记录.md`](docs/构建验证记录.md) 第 7 节。
  （本机 v5.5.5 曾卡在上游 `main/link_pairing.c:31` 的 `mbedtls/private/gcm.h`，换 v6.0.3 后解决，
  该 include 在未打补丁的原始 SDK 里同样存在，与适配包无关）
- ✅ **已烧录并上板启动**（2026-11-06 23:05，COM27）：用户明确不用备份原厂固件，
  4 个镜像逐段 `Hash of data verified`；启动日志确认 `board: Espressif ESP-VoCat`、
  `PCB V1.0 (cached)`、`mxic (opi) / opi_str`、16 MB Octal PSRAM、360×360 UI、
  ES8311/ES7210 16 kHz 双声道、双麦自检有信号、WiFi 扫到 16 个 AP、BLE 广播，
  12 秒无重启无 panic。详见 [`docs/构建验证记录.md`](docs/构建验证记录.md) 第 8 节。
- ⚠️ **人工验收仍待做**：设备停在 `waiting for BLE provision`，需 App 配网 + 按键配对；
  颜色、触摸、外放、唤醒词、电量计读数、BOOT 键、V1.2 实物仍需逐项走一遍。
  固件与日志在 `.verify/out/`（不入库）

音频采用**方案 A**：对齐 Muse 固定的 16 kHz / 16-bit / STD 双槽，ES7210 只选 MIC1|MIC2 并混合两路。
官方的 24 kHz / TDM 4 槽 / AEC 参考通道未移植（Muse 共享层全库无 `i2s_tdm`），留作后续里程碑。

## 许可证与来源

三段来源，许可证声明都已保留在对应文件头，详见 [LICENSES.md](LICENSES.md)：

- 硬件契约 / GPIO / 双版本逻辑 / 面板初始化表：`78/xiaozhi-esp32`（MIT）
- `muse_board_t` 接口与装配骨架：`facebookincubator/muse-gadget-sdk`（Apache-2.0）
- 本仓库新增的适配代码与脚本：按上述两者兼容的条款提供，未声明为本仓库原创的部分请沿用原声明

提交到 GitHub 前请确认没有把串口日志、设备 MAC、Wi-Fi 凭据等硬件/个人信息带进去。

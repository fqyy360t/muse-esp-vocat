# ESP-VoCat（喵伴 / EchoEar）→ Muse ESP32 板级适配包

把乐鑫官方 **ESP-VoCat / EchoEar** 圆屏语音板，适配到
[`facebookincubator/muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk)
的 ESP32 固件框架。

> ⚠️ **这个仓库不含 Muse SDK 源码，clone 下来不能直接编译。**
> 它是**适配包（overlay）**：只有 16 个文件 / 约 220 KB —— 新增的板级代码、sdkconfig overlay、
> 文档，以及一份 19 处锚点的 `apply.py` 补丁。上游 `esp32/` 的 948 个文件 / 28 MB **不在这里**，
> 由 `setup.py` 在固定 commit 上自动拉到 `vendor/`（已 gitignore）后再打补丁。
>
> 为什么这样做：① 上游是 Apache-2.0（面板初始化表另源自 `78/xiaozhi-esp32` 的 MIT），
> 整包复制会把别人的代码和许可证责任一起搬进仓库；② 保持上游仓库零改动，补丁能对任意一份
> SDK 副本生效；③ 仓库轻量，上游更新后只需重跑 `--check` 确认锚点。
>
> 固定的上游版本：`463bd2a05380fea4211b0099ad0f70933c108b20`。
> 若上游后续改了被补丁的文件，`setup.py --check-only` 会提前报锚点失配。

## 目录

| 路径 | 内容 |
|---|---|
| `board/board_esp_vocat.c` | 核心板级驱动：`muse_board_t` + 双 PCB 版本探测 + 显示/触摸/音频/电量计 |
| `board/esp_vocat_display_init.h` | ST77916 的 184 条初始化命令（已脚本校验与官方逐条一致） |
| `board/esp_vocat_touch.h` | CST816S reg 0x02 六字节解码 |
| `board/sdkconfig.muse-esp-vocat` | 板级 sdkconfig overlay（**Octal Flash / OPI**） |
| `board/esp-vocat.md` | 板级说明：GPIO、版本差异、构建、验收、未验证项 |
| `apply.py` | 锚点补丁：复制 5 个新文件 + 修改 8 个上游文件（共 19 处） |
| `setup.py` / `setup.sh` | **一键准备**：拉取固定版本上游 SDK 到 `vendor/` 校验锚点并打补丁 |
| `docs/esp-vocat-硬件接口定义.md` | 完整硬件契约（元件 / 组件 / GPIO / 电源域 / 初始化顺序） |
| `docs/muse-sdk-迁移方案.md` | 迁移设计、接口映射、风险与构建验证 |
| `docs/构建验证记录.md` | 补丁、编译、烧录、启动日志的逐项证据 |
| `flow/进展.md` | 按 `project-flow-cy` 写的接力交接记录 |

## 现成固件（Release，无需编译）

[`v0.1.0` Release](https://github.com/fqyy360t/muse-esp-vocat/releases/tag/v0.1.0)
附 ESP-IDF v6.0.3 构建的**完整可烧录固件**：四件套、0x0 起始的合并镜像、
ELF 符号与构建日志。解压 `muse-esp-vocat-v0.1.0-flash.zip` 后：

```bash
esptool.py --chip esp32s3 --port COM27 --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dout --flash_size keep --flash_freq 80m \
  0x0 bootloader.bin 0x10000 partition-table.bin \
  0x1d000 ota_data_initial.bin 0x20000 muse-gadget.bin
```

也可以用合并镜像单文件 `write_flash 0x0 muse-esp-vocat-merged-v0.1.0.bin`
（该镜像做过逐字节校验，**未单独实烧验证**；四文件方式是 COM27 实测路径）。
参数、校验和与硬件前提见 flash 包内 `README-FLASH.md` / `SHA256SUMS.txt`。
**烧录会覆盖原厂固件。**

## 快速开始

```bash
# 1) 一键准备：拉取上游固定 commit 到 vendor/（gitignore），校验锚点并打补丁
python setup.py
#    Git Bash / macOS / Linux 也可以用：./setup.sh

# 2) 构建（需 ESP-IDF v6.0.3，先执行 IDF 的 export 脚本激活环境）
cd vendor/muse-gadget-sdk/esp32
tools/muse/board.sh build vocat
tools/muse/board.sh flash vocat COM27
```

已有 SDK 副本、或只想验证锚点：

```bash
python setup.py --sdk-dir /path/to/muse-gadget-sdk   # 用已有副本，跳过 clone
python setup.py --check-only                         # 只校验锚点，不改动任何文件
python setup.py --mirror https://gh-proxy.com/https://github.com/facebookincubator/muse-gadget-sdk.git
# 脚本已内置 gh-proxy.com 镜像自动回退 + HTTP/1.1（避开 Windows 代理断流），一般无需手动指定
```

手动方式（不跑 `setup.py`）：

```bash
python apply.py --sdk /path/to/muse-gadget-sdk --check
python apply.py --sdk /path/to/muse-gadget-sdk --dry-run
python apply.py --sdk /path/to/muse-gadget-sdk
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
- ✅ **GitHub 发布**：仓库 `fqyy360t/muse-esp-vocat`、tag `v0.1.0`，Release 附
  四件套 + 0x0 合并镜像 + ELF/日志（脱敏）+ `SHA256SUMS.txt`；
  资产清单与校验见 [`docs/构建验证记录.md`](docs/构建验证记录.md) 第 10 节。
- ⚠️ **人工验收仍待做**：设备停在 `waiting for BLE provision`，需 App 配网 + 按键配对；
  颜色、触摸、外放、唤醒词、电量计读数、BOOT 键、V1.2 实物仍需逐项走一遍。
  未脱敏的原始日志只留本地 `.verify/out/`（不入库）

音频采用**方案 A**：对齐 Muse 固定的 16 kHz / 16-bit / STD 双槽，ES7210 只选 MIC1|MIC2 并混合两路。
官方的 24 kHz / TDM 4 槽 / AEC 参考通道未移植（Muse 共享层全库无 `i2s_tdm`），留作后续里程碑。

## 许可证与来源

三段来源，许可证声明都已保留在对应文件头，详见 [LICENSES.md](LICENSES.md)：

- 硬件契约 / GPIO / 双版本逻辑 / 面板初始化表：`78/xiaozhi-esp32`（MIT）
- `muse_board_t` 接口与装配骨架：`facebookincubator/muse-gadget-sdk`（Apache-2.0）
- 本仓库新增的适配代码与脚本：按上述两者兼容的条款提供，未声明为本仓库原创的部分请沿用原声明

**发布前检查（2026-11-06 已执行）**：所有二进制产物（含 21 MB ELF）、sdkconfig
与依赖锁逐字节扫描，不含设备 MAC、Wi-Fi 凭据、SDK token 或本机用户目录；
Release 日志与入库文档中的设备标识（MAC、BLE 广播名、PnP 实例 ID）已统一替换为占位符，
规则见 debug 包内 `REDACTION.md`。文档中的 `F:\git\...` 是本地源码位置示例，不含用户名；
原始串口日志仅保留在本地，不随仓库或 Release 发布。

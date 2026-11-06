# 许可证与来源说明

本仓库是对两个上游项目的**板级适配**，不是独立原创固件。各文件头已保留原始声明，
这里汇总三段来源，方便单独提交到 GitHub 时说明清楚。

| 内容 | 上游 | 许可证 | 本仓库中的落点 |
|---|---|---|---|
| ESP-VoCat 硬件契约、GPIO、V1.0/V1.2 版本探测算法、电源时序、电量计寄存器 | [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32) `main/boards/espressif/esp-vocat/` | **MIT** | `board/board_esp_vocat.c`、`docs/esp-vocat-硬件接口定义.md` |
| ST77916 面板的 184 条初始化命令 | 同上（经 [`muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk) 的 `ostb_echoear_display_init.h` 转引，原文件头保留完整 MIT 文本） | **MIT** | `board/esp_vocat_display_init.h` |
| `muse_board_t` 接口定义、LVGL/codec 装配骨架、`muse_gpio_button_*`、sdkconfig 约定、工具脚本 | [`facebookincubator/muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk) `esp32/components/muse/` | **Apache-2.0** | `board/board_esp_vocat.c`、`board/sdkconfig.muse-esp-vocat`、`apply.py` 的补丁目标 |
| CST816S 六字节解码（原 OSTB 板文件，作者 Mihir Jadhav） | 同上 | **Apache-2.0** | `board/esp_vocat_touch.h` |

## 需要遵守的点

1. `board/esp_vocat_display_init.h` 的 **MIT 全文声明必须原样保留**；数组只是从
   `echoear_init_cmds` 改名为 `esp_vocat_init_cmds`，184 条命令逐条未改。
2. `board/esp_vocat_touch.h` 与 `board/board_esp_vocat.c` 头部保留 Apache-2.0 / MIT 来源说明。
3. 合入 SDK 后，构建期拉取的托管组件（`managed_components/`）各有自己的许可证，
   不在本仓库范围内，见上游 `README.md` 的 "Dependencies fetched at build time"。
4. 本仓库不含任何 SDK 完整副本，也没有复制上游的业务代码；`apply.py` 只在你自己的
   副本里插入注册项。

## 提交 GitHub 前

- 不要提交串口原始日志（可能含设备 MAC、Wi-Fi 凭据、令牌）。
- 不要提交 `build/`、`managed_components/`、`sdkconfig`（已由 `.gitignore` 排除）。
- 上游两个仓库都不是本人仓库；**只推自己的仓库 / 功能分支，不要 push 上游**。

#!/usr/bin/env python3
"""
apply.py — 把 ESP-VoCat 适配包合入一份本地 muse-gadget-sdk 副本。

这个仓库只包含「新增文件」和「对上游文件的锚点式补丁」，不是 SDK 的 fork。
脚本不会碰本仓库以外的任何路径，也不会执行 git 操作。

用法:
    python apply.py --sdk /path/to/muse-gadget-sdk --check       # 只验证锚点
    python apply.py --sdk /path/to/muse-gadget-sdk --dry-run     # 预览动作
    python apply.py --sdk /path/to/muse-gadget-sdk               # 实际应用
    python apply.py --sdk /path/to/muse-gadget-sdk --force       # 允许覆盖已存在的同名新文件

注意: 上游两个仓库的 origin 分别是 facebookincubator/muse-gadget-sdk 与
78/xiaozhi-esp32，都是别人的仓库。请在自己的副本/功能分支里运行，别 push 上游。
"""
import argparse
import shutil
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PACK = Path(__file__).resolve().parent
BOARD = PACK / "board"

# 新增文件: (本仓库内的文件, SDK 里的目标相对路径)
COPIES = [
    ("board_esp_vocat.c", "esp32/components/muse/boards/board_esp_vocat.c"),
    ("esp_vocat_display_init.h", "esp32/components/muse/boards/esp_vocat_display_init.h"),
    ("esp_vocat_touch.h", "esp32/components/muse/boards/esp_vocat_touch.h"),
    ("sdkconfig.muse-esp-vocat", "esp32/devices/sdkconfig.muse-esp-vocat"),
    ("esp-vocat.md", "esp32/devices/esp-vocat.md"),
]


def edit(path, marker, find, replace, required=True, note=""):
    """在唯一锚点 find 处做一次替换；marker 已存在则视为已应用（幂等）。"""
    return dict(path=path, marker=marker, find=find, replace=replace,
                required=required, note=note)


EDITS = [
    # --- 1. Kconfig: choice 里加板选项 ---
    edit(
        "esp32/components/muse/Kconfig",
        "config MUSE_BOARD_ESP_VOCAT",
        '        config MUSE_BOARD_WAVESHARE_S3_LCD19\n'
        '            bool "Waveshare ESP32-S3-Touch-LCD-1.9 (170x320 SH8601, one button)"\n'
        '            depends on IDF_TARGET_ESP32S3\n'
        '    endchoice\n',
        '        config MUSE_BOARD_WAVESHARE_S3_LCD19\n'
        '            bool "Waveshare ESP32-S3-Touch-LCD-1.9 (170x320 SH8601, one button)"\n'
        '            depends on IDF_TARGET_ESP32S3\n'
        '\n'
        '        config MUSE_BOARD_ESP_VOCAT\n'
        '            bool "Espressif ESP-VoCat / EchoEar (ESP32-S3, 360x360 round ST77916 QSPI)"\n'
        '            depends on IDF_TARGET_ESP32S3\n'
        '    endchoice\n',
        note="Kconfig choice: 新增 MUSE_BOARD_ESP_VOCAT",
    ),
    # --- 2. Kconfig: MUSE_BOARD_ID 字符串 ---
    edit(
        "esp32/components/muse/Kconfig",
        'default "esp_vocat" if MUSE_BOARD_ESP_VOCAT',
        '        default "none"\n',
        '        default "esp_vocat" if MUSE_BOARD_ESP_VOCAT\n'
        '        default "none"\n',
        note="Kconfig: MUSE_BOARD_ID 加 esp_vocat（rules 只能用这个 ID）",
    ),
    # --- 3. CMakeLists: elseif 链 ---
    edit(
        "esp32/components/muse/CMakeLists.txt",
        "CONFIG_MUSE_BOARD_ESP_VOCAT",
        '    elseif(CONFIG_MUSE_BOARD_WAVESHARE_S3_LCD19)\n'
        '        list(APPEND srcs "boards/board_waveshare_s3_lcd19.c")\n'
        '    endif()\n',
        '    elseif(CONFIG_MUSE_BOARD_WAVESHARE_S3_LCD19)\n'
        '        list(APPEND srcs "boards/board_waveshare_s3_lcd19.c")\n'
        '    elseif(CONFIG_MUSE_BOARD_ESP_VOCAT)\n'
        '        list(APPEND srcs "boards/board_esp_vocat.c")\n'
        '    endif()\n',
        note="CMakeLists: 单链接新的板文件",
    ),
    # --- 4/5. idf_component.yml: 两条 rules 加 esp_vocat ---
    edit(
        "esp32/components/muse/idf_component.yml",
        '\\"viewe_smartring_plus\\", \\"esp_vocat\\"]',
        '\\"deotaland_rorolee\\", \\"waveshare_s3_lcd19\\", \\"waveshare_s3_143c\\", \\"viewe_smartring_plus\\"]',
        '\\"deotaland_rorolee\\", \\"waveshare_s3_lcd19\\", \\"waveshare_s3_143c\\", \\"viewe_smartring_plus\\", \\"esp_vocat\\"]',
        note="idf_component.yml: esp_lvgl_adapter 规则",
    ),
    edit(
        "esp32/components/muse/idf_component.yml",
        '\\"ostb_echoear_2st\\", \\"viewe_smartring_plus\\", \\"esp_vocat\\"]',
        '"$CONFIG{MUSE_BOARD_ID} in [\\"waveshare_s3_185c\\", \\"ostb_echoear_2st\\", \\"viewe_smartring_plus\\"]"',
        '"$CONFIG{MUSE_BOARD_ID} in [\\"waveshare_s3_185c\\", \\"ostb_echoear_2st\\", \\"viewe_smartring_plus\\", \\"esp_vocat\\"]"',
        note="idf_component.yml: esp_lcd_st77916 规则",
    ),
    # --- 6/7/8. board.sh: usage、参数提示、case ---
    edit(
        "esp32/tools/muse/board.sh",
        "|smartring-plus|vocat>",
        "|lcd19|smartring-plus>",
        "|lcd19|smartring-plus|vocat>",
        note="board.sh: usage 注释",
    ),
    edit(
        "esp32/tools/muse/board.sh",
        "|smartring-plus|vocat}",
        "|lcd19|smartring-plus}",
        "|lcd19|smartring-plus|vocat}",
        note="board.sh: ${2:?...} 提示",
    ),
    edit(
        "esp32/tools/muse/board.sh",
        "vocat)   profile=esp-vocat",
        "    lcd19)   profile=waveshare-s3-lcd19;   target=esp32s3 ;;\n",
        "    lcd19)   profile=waveshare-s3-lcd19;   target=esp32s3 ;;\n"
        "    vocat)   profile=esp-vocat;           target=esp32s3 ;;\n",
        note="board.sh: vocat -> profile=esp-vocat",
    ),
    # --- 9/10. ports.py: USB 设备与串口命令白名单 ---
    edit(
        "esp32/tools/muse/ports.py",
        '    "vocat": USJ,',
        '    "lcd19": USJ,\n',
        '    "lcd19": USJ,\n    "vocat": USJ,\n',
        note="ports.py: USB 303A:1001 映射",
    ),
    edit(
        "esp32/tools/muse/ports.py",
        '"smartring-plus", "vocat")',
        '"lcd19", "smartring-plus")',
        '"lcd19", "smartring-plus", "vocat")',
        note="ports.py: COMMANDS 白名单",
    ),
    # --- 11/12. avatar.py: 板名映射与 CHAT_BOARDS ---
    edit(
        "esp32/tools/muse/avatar.py",
        '"Espressif ESP-VoCat": "vocat"',
        '    "OSTB EchoEar-2ST": "echoear",\n',
        '    "OSTB EchoEar-2ST": "echoear",\n    "Espressif ESP-VoCat": "vocat",\n',
        note="avatar.py: @status 板名 -> board.sh 别名",
    ),
    edit(
        "esp32/tools/muse/avatar.py",
        '"smartring-plus", "vocat")',
        '"lcd19", "smartring-plus")',
        '"lcd19", "smartring-plus", "vocat")',
        note="avatar.py: CHAT_BOARDS",
    ),
    # --- 13/14. 文档表格（可选，找不到锚点只警告） ---
    edit(
        "esp32/devices/README.md",
        "**Espressif ESP-VoCat (experimental)**",
        '| **OSTB EchoEar-2ST (experimental)** | ESP32-S3 rev 0.2 | 1.85" 360×360 round ST77916 LCD, touch | 16 MB / 8 MB octal (measured) | [Product](https://docs.yishierniao.cn/details/ai/echoear-2st/product.html), [setup](ostb-echoear-2st.md) | — |\n',
        '| **OSTB EchoEar-2ST (experimental)** | ESP32-S3 rev 0.2 | 1.85" 360×360 round ST77916 LCD, touch | 16 MB / 8 MB octal (measured) | [Product](https://docs.yishierniao.cn/details/ai/echoear-2st/product.html), [setup](ostb-echoear-2st.md) | — |\n'
        '| **Espressif ESP-VoCat (experimental)** | ESP32-S3 rev 0.2 | 1.85" 360×360 round ST77916 LCD, touch | 32 MB OPI (16 MB used) / 16 MB octal | [xiaozhi board](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/espressif/esp-vocat), [setup](esp-vocat.md) | — |\n',
        required=False,
        note="devices/README.md: Supported devices 表",
    ),
    edit(
        "esp32/AGENTS.md",
        "sdkconfig.muse-esp-vocat",
        "| OSTB EchoEar-2ST (experimental) | `esp32s3` | `devices/sdkconfig.muse;devices/sdkconfig.muse-ostb-echoear-2st` | `tools/muse/board.sh build echoear` |\n",
        "| OSTB EchoEar-2ST (experimental) | `esp32s3` | `devices/sdkconfig.muse;devices/sdkconfig.muse-ostb-echoear-2st` | `tools/muse/board.sh build echoear` |\n"
        "| Espressif ESP-VoCat (experimental) | `esp32s3` | `devices/sdkconfig.muse;devices/sdkconfig.muse-esp-vocat` | `tools/muse/board.sh build vocat` |\n",
        required=False,
        note="esp32/AGENTS.md: 板表",
    ),
]


def resolve_root(raw):
    root = Path(raw).expanduser().resolve()
    # 允许直接传 .../muse-gadget-sdk/esp32
    if root.name == "esp32" and (root / "components/muse/Kconfig").is_file():
        root = root.parent
    return root


def main():
    ap = argparse.ArgumentParser(description="Apply the ESP-VoCat board adapter to a local muse-gadget-sdk copy")
    ap.add_argument("--sdk", required=True, help="path to your local muse-gadget-sdk checkout (or its esp32/ dir)")
    ap.add_argument("--dry-run", action="store_true", help="print planned changes without writing")
    ap.add_argument("--check", action="store_true", help="only verify that every anchor matches exactly once")
    ap.add_argument("--force", action="store_true", help="overwrite existing added files if their content differs")
    args = ap.parse_args()
    dry = args.dry_run or args.check

    root = resolve_root(args.sdk)
    sentinel = root / "esp32/components/muse/Kconfig"
    if not sentinel.is_file():
        print(f"ERROR: not a muse-gadget-sdk checkout: {root}\n  (missing {sentinel})")
        return 2
    print(f"SDK root: {root}")
    print(f"Mode    : {'CHECK (read-only)' if args.check else 'DRY-RUN' if args.dry_run else 'APPLY'}\n")

    failures, warnings, done, skipped = [], [], [], []

    # --- 新增文件 ---
    print("== New files ==")
    for src_name, rel in COPIES:
        src, dst = BOARD / src_name, root / rel
        if not src.is_file():
            failures.append(f"package file missing: {src}")
            continue
        if dst.is_file():
            if dst.read_bytes() == src.read_bytes():
                skipped.append(rel)
                print(f"  skip   {rel} (identical)")
                continue
            if not args.force:
                warnings.append(f"{rel} exists with different content; use --force to overwrite")
                print(f"  WARN   {rel} exists, left untouched")
                continue
        if not dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
        done.append(rel)
        print(f"  copy   {rel}")

    # --- 已有文件的锚点补丁 ---
    print("\n== Patches to existing files ==")
    for e in EDITS:
        p = root / e["path"]
        if not p.is_file():
            msg = f"{e['path']}: file missing"
            (failures if e["required"] else warnings).append(msg)
            print(f"  {'FAIL' if e['required'] else 'WARN'}  {msg}")
            continue
        raw = p.read_bytes()
        text = raw.decode("utf-8")
        nl = "\r\n" if "\r\n" in text else "\n"  # 保留原文件换行风格
        marker = e["marker"].replace("\n", nl)
        find = e["find"].replace("\n", nl)
        replace = e["replace"].replace("\n", nl)

        if marker in text:
            skipped.append(e["path"])
            print(f"  skip   {e['note']} (already applied)")
            continue
        hits = text.count(find)
        if hits != 1:
            msg = f"{e['note']}: anchor matched {hits} times in {e['path']} (expected 1)"
            (failures if e["required"] else warnings).append(msg)
            print(f"  {'FAIL' if e['required'] else 'WARN'}  {msg}")
            continue
        if not dry:
            p.write_bytes(text.replace(find, replace, 1).encode("utf-8"))
        done.append(e["path"])
        print(f"  edit   {e['note']}")

    # --- 汇总 ---
    print(f"\n== Summary ==")
    print(f"  changed/skipped/failed/warned: {len(done)}/{len(skipped)}/{len(failures)}/{len(warnings)}")
    for m in failures:
        print(f"  FAIL: {m}")
    for m in warnings:
        print(f"  WARN: {m}")
    if failures:
        print("\nNothing was reported as complete: fix the anchors above (upstream file drifted)"
              " or apply the corresponding hunk by hand.")
        return 1
    if dry:
        print("\nDry run only: no file was written.")
    else:
        print("\nDone. Verify with: git -C <sdk> diff --stat")
        print("Then: tools/muse/board.sh build vocat")
    return 0


if __name__ == "__main__":
    sys.exit(main())

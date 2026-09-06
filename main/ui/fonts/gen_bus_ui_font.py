#!/usr/bin/env python3
"""生成 BUS HMI 简体中文字库（覆盖界面全部汉字，避免 LVGL 内置 CJK 子集缺字）。"""

import os
import re
import subprocess

# main/ui/fonts -> ESP32P4_BUS_APP（向上 4 级）
APP_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)
UI_DIR = os.path.join(APP_ROOT, "main", "ui")
BUS_DIR = os.path.join(APP_ROOT, "main", "bus")
NET_DIR = os.path.join(APP_ROOT, "main", "net")
FONT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_C = os.path.join(FONT_DIR, "lv_font_bus_ui_16.c")
SYMBOLS_FILE = os.path.join(FONT_DIR, "ui_cn_symbols.txt")
SYMBOLS_OUT = os.path.join(FONT_DIR, "_symbols_line.txt")

SYSTEM_NOTO = os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts", "NotoSansSC-VF.ttf")
SYSTEM_YAHEI = os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts", "msyh.ttc")

SCAN_DIRS = [UI_DIR, BUS_DIR, NET_DIR, os.path.join(APP_ROOT, "main", "APP")]


def collect_symbols() -> str:
    chars = set()
    if os.path.isfile(SYMBOLS_FILE):
        with open(SYMBOLS_FILE, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                for ch in line:
                    if ord(ch) > 127:
                        chars.add(ch)
    pat = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"')
    for base in SCAN_DIRS:
        if not os.path.isdir(base):
            continue
        for dirpath, _, files in os.walk(base):
            if "fonts" in dirpath.replace("\\", "/"):
                continue
            for fn in files:
                if not fn.endswith((".c", ".h", ".cpp")):
                    continue
                path = os.path.join(dirpath, fn)
                with open(path, encoding="utf-8", errors="ignore") as f:
                    for m in pat.finditer(f.read()):
                        for ch in m.group(1):
                            if ord(ch) > 127:
                                chars.add(ch)
    extra = (
        "通信设置串口加载就绪请输入清零缓冲允许发送只听切换"
        "清空暂停录音卡录总线扩展分析仪总览监视热点本机"
        "连接等待繁忙空闲中等负载吞吐帧错误波特率扫描传输"
        "输出频率占空比点按修改取消确定无效空值超出范围"
        "保存重连无线网络存储未挂载已挂载开启关闭电压毫伏"
        "原始采样横向纵向布局通道列表波形历史十秒监听模式"
        "发送许可芯片引脚说明解码选看数据载荷重放周期过滤"
        "失败成功仅听开关键…—°±≥ω"
    )
    for ch in extra:
        chars.add(ch)
    return "".join(sorted(chars, key=lambda c: ord(c)))


def pick_font() -> str:
    for p in (SYSTEM_NOTO, SYSTEM_YAHEI):
        if os.path.isfile(p):
            return p
    raise FileNotFoundError("未找到 NotoSansSC-VF.ttf 或 msyh.ttc，请安装简体中文字体")


def main():
    symbols = collect_symbols()
    print(f"共 {len(symbols)} 个 Unicode 字符")

    font_path = pick_font()
    print(f"使用字体: {font_path}")

    with open(SYMBOLS_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(symbols)

    npx = r"F:\nodejs\npx.cmd" if os.path.isfile(r"F:\nodejs\npx.cmd") else "npx"
    cmd = [
        npx,
        "--yes",
        "lv_font_conv",
        "--no-compress",
        "--no-prefilter",
        "--bpp",
        "4",
        "--size",
        "16",
        "--font",
        font_path,
        "-r",
        "0x20-0x7F",
        "--symbols",
        symbols,
        "--format",
        "lvgl",
        "--lv-font-name",
        "lv_font_bus_ui_16",
        "-o",
        OUT_C,
        "--force-fast-kern-format",
    ]
    print("运行 lv_font_conv …")
    subprocess.run(cmd, check=True)

    with open(OUT_C, encoding="utf-8") as f:
        text = f.read()
    text = text.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    # lv_font_conv uses the font name as both enable-macro and variable; rename
    # the guard so `#define lv_font_bus_ui_16 1` does not break the definition.
    text = text.replace(
        "#ifndef lv_font_bus_ui_16\n#define lv_font_bus_ui_16 1\n#endif\n\n#if lv_font_bus_ui_16",
        "#ifndef LV_FONT_BUS_UI_16\n#define LV_FONT_BUS_UI_16 1\n#endif\n\n#if LV_FONT_BUS_UI_16",
    )
    text = text.replace(
        "#endif /*#if lv_font_bus_ui_16*/",
        "#endif /*#if LV_FONT_BUS_UI_16*/",
    )
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    print(f"已生成 {OUT_C}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""重新生成 DeepSeek 的 64px 瓷贴（卡片/列表头像用）。

上一版是紧裁到 bbox 后直接铺满整块 64×64，画到 23pt 时鲸鱼顶到四边、尾鳍贴着圆角，
和旁边 Qoder / WorkBuddy 那种「图形留一圈底」的观感不一致。这一版按画布 62.5% 宽居中，
底色仍是品牌蓝 #4D6BFE。

用法：/opt/homebrew/bin/python3 Tools/make_dsh_tile.py ds.svg > hex.txt
（需要 Chrome 做 SVG 栅格化：本机没有 SVG 解码库，走无头浏览器出 alpha）
"""
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

N = 64                # 输出边长
ART_W = 40            # 鲸鱼宽度（= 画布 62.5%），留下与邻居一致的呼吸
BG = (0x4D, 0x6B, 0xFE)


def rasterize(svg_path: str, px: int = 960) -> Image.Image:
    svg = Path(svg_path).read_text()
    # simple-icons 的 path 没有 fill，默认黑；这里要白鲸
    svg = svg.replace("<path ", '<path fill="#ffffff" ')
    html = (
        '<!doctype html><meta charset="utf-8">'
        '<style>html,body{margin:0;background:transparent}'
        f'svg{{width:{px}px;height:{px}px;display:block}}</style>' + svg
    )
    with tempfile.TemporaryDirectory() as td:
        h = Path(td) / "i.html"
        o = Path(td) / "i.png"
        h.write_text(html)
        subprocess.run(
            [
                "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                "--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                "--default-background-color=00000000",
                f"--screenshot={o}", f"--window-size={px},{px}",
                f"file://{h}",
            ],
            check=True, capture_output=True,
        )
        return Image.open(o).convert("RGBA")


def main() -> None:
    im = rasterize(sys.argv[1])
    alpha = im.split()[3]
    bbox = alpha.point(lambda v: 255 if v > 16 else 0).getbbox()
    art = im.crop(bbox)
    # 等比缩到 ART_W 宽
    art_h = round(art.height * ART_W / art.width)
    art = art.resize((ART_W, art_h), Image.LANCZOS)
    canvas = Image.new("RGBA", (N, N), BG + (255,))
    canvas.alpha_composite(art, ((N - ART_W) // 2, (N - art_h) // 2))
    canvas.save("/tmp/dsh-tile.png")
    print("".join(f"{r:02X}{g:02X}{b:02X}{a:02X}" for r, g, b, a in canvas.getdata()))


if __name__ == "__main__":
    main()

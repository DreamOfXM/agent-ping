#!/usr/bin/env python3
"""从 E4 的像素里取出他那颗星芒的轮廓，配一圈轨道，出菜单栏 18pt 模板图。

不能直接拿 icns 缩：状态项走 isTemplate，系统只取 alpha 染色，渐变与深蓝底全丢；
18pt 下多圈轨道会糊成一团，所以只留一圈。
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, '..', 'assets', 'icon-1024.png')
SX, SY = 572, 548          # 实测星芒中心（probe_star3.py）
LO, HI = 150, 232          # 亮度门：低于 LO 判背景，高于 HI 全实


def sparkle(size):
    """他画的那颗四角星：轮廓直接读原图像素，不重画。"""
    crop = Image.open(BASE).convert('RGB').crop((SX - 170, SY - 170, SX + 170, SY + 170))
    lum = crop.split()[0]
    for ch in crop.split()[1:]:
        lum = ImageChops.lighter(lum, ch)
    mask = lum.point(lambda v: 255 if v >= HI else int(255 * max(0, v - LO) / (HI - LO)))
    mask = mask.filter(ImageFilter.GaussianBlur(1.0)).crop((35, 35, 305, 305))
    return mask.resize((size, size), Image.LANCZOS)


def make(px):
    big = Image.new('L', (px * 2, px * 2), 0)
    d = ImageDraw.Draw(big)
    d.ellipse((px * 0.10, px * 0.42, px * 1.90, px * 1.26), outline=255, width=max(1, px // 18))
    star = sparkle(int(px * 0.92))
    big.paste(255, (px - star.width // 2, px - star.height // 2), star)
    alpha = big.resize((px, px), Image.LANCZOS)
    return Image.merge('RGBA', (*Image.new('RGB', (px, px), 'white').split(), alpha))


def main():
    for px, name in ((18, 'menubar-icon.png'), (36, 'menubar-icon@2x.png')):
        img = make(px)
        img.save(os.path.join(HERE, '..', 'assets', name))
        print(name, img.size)


if __name__ == '__main__':
    main()

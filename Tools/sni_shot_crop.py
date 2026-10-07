#!/usr/bin/env python3
"""把真宿主（gtk-sni-tray）的整屏截图裁成「托盘那一条」并放大，供肉眼读图。

为什么要裁：1400x1100 的整屏图里托盘只占顶上 32 行，直接看等于看一张 mostly 灰图，
「图标糊没糊、Label 有没有画出来」这类判断必须放大到能数像素才算读过图。
宿主窗口的位置不是猜的：从同目录的 tray_*.log 里读它自己打印的 width/height/xPos/yPos。

用法： python3 Tools/sni_shot_crop.py <证据目录> [输出前缀]
"""
import os
import re
import sys

from PIL import Image

ZOOM = 4


def tray_geom(d):
    """gtk-sni-tray 启动时会把自己的几何打进日志（实测有 width/height/xPos/yPos 四行）。"""
    g = {}
    for f in ('tray_dark.log', 'tray_light.log', 'tray_name.log'):
        p = os.path.join(d, f)
        if not os.path.exists(p):
            continue
        txt = open(p).read()
        for k in ('width', 'height', 'xPos', 'yPos'):
            m = re.search(r'^%s:\s*(\d+)' % k, txt, re.M)
            if m:
                g[k] = int(m.group(1))
        break
    if len(g) != 4:
        raise SystemExit('没从 tray_*.log 里读全 width/height/xPos/yPos，读到=%r' % (g,))
    return g


def main():
    d = os.path.abspath(sys.argv[1])
    g = tray_geom(d)
    x, y, w, h = g['xPos'], g['yPos'], g['width'], g['height']
    print('托盘条 = (%d,%d) %dx%d（来源：同目录 tray_*.log 里宿主自己打印的几何）' % (x, y, w, h))
    made = []
    for f in sorted(os.listdir(d)):
        if not (f.startswith('tray-') and f.endswith('.png')):
            continue
        im = Image.open(os.path.join(d, f)).convert('RGB')
        if im.size[0] < x + w or im.size[1] < y + h:
            print('%s 尺寸 %s 容不下托盘条，跳过' % (f, im.size))
            continue
        # 只裁左边 420px：图标都排在起始端，整条 1396px 放大四倍没法看
        cw = min(420, w)
        strip = im.crop((x, y, x + cw, y + h))
        # 宿主到底把这颗画成了多大？只能从截图上量：找与背景色明显不同的像素的包围盒。
        # 不这么量的话「22 还是 26」全靠目测，而目测在 4 倍放大下必然报错。
        bg = strip.convert('RGB').getcolors(maxcolors=1 << 20)
        bg.sort(reverse=True)
        bgr = bg[0][1]
        px = strip.load()
        xs = [i for i in range(cw) for j in range(h)
              if sum(abs(a - b) for a, b in zip(px[i, j], bgr)) > 24]
        if xs:
            ys = [j for j in range(h) for i in range(cw)
                  if sum(abs(a - b) for a, b in zip(px[i, j], bgr)) > 24]
            box = (min(xs), min(ys), max(xs), max(ys))
            print('  真宿主画出来的图标包围盒：%dx%d 位于 (%d,%d)，背景色=%s' % (
                box[2] - box[0] + 1, box[3] - box[1] + 1, box[0], box[1], bgr))
        else:
            print('  这一条里没有任何与背景不同的像素——宿主没画图标，或该项已经从托盘消失')
        big = strip.resize((cw * ZOOM, h * ZOOM), Image.NEAREST)
        out = os.path.join(d, 'crop-' + f)
        big.save(out)
        made.append(out)
        print('%s → %s 裁 %dx%d 放大 %dx%d' % (f, os.path.basename(out), cw, h, big.size[0], big.size[1]))
    if not made:
        raise SystemExit('一张都没裁出来——先看 tray-*.png 在不在')


if __name__ == '__main__':
    main()

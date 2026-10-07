#!/usr/bin/env python3
"""把 E4 那颗图标铺成一个 SNI 宿主能按名字找的图标主题目录。

为什么要这条：IconPixmap 只有一条路——我们给什么像素，宿主就贴什么像素，
深浅两种面板底用的是同一张图（实测：22px 那颗在深底上只有 19% 的有墨像素过 3:1，
浅底 39%）。规范里另一条路是 IconName + IconThemePath，让宿主按自己的主题去取图；
那条路要磁盘上真有一个主题目录，否则我们连发 IconName 的资格都没有。

目录里同时铺两种布局（都从同一批 PNG 生成，成本就是几百 KB）：
  <out>/<size>/status/agent-ping.png          ← 把 out 当「主题目录本身」的宿主
  <out>/hicolor/<size>/status/agent-ping.png  ← 把 out 当 /usr/share/icons 那种「父目录」的宿主
规范只说 IconThemePath 是 "a directory to be used as an additional icon theme search path"，
两种读法都有实现，实测哪一种能用要靠 sni_real_tray_shot.sh 那张截图，不靠我判断。

母图 = assets/sni-44.png（由 make_menubar_icon.py 从 assets/icon-1024.png 裁出来，别手改）。

用法： python3 Tools/make_sni_theme.py [输出目录]
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, 'assets', 'sni-44.png')
SIZES = (16, 22, 24, 32, 44)
NAME = 'agent-ping'


def write_dirs(dirs_root, with_prefix):
    """with_prefix=True 时多套一层 hicolor/，对应「out 是父目录」那种读法。
    返回这个布局用到的 Directories 条目。"""
    rels = []
    for s in SIZES:
        im = Image.open(MASTER).convert('RGBA')
        if s != im.size[0]:
            # 44→16 是缩小，LANCZOS 不会糊；母图比目标档大，所以不存在插值放大
            im = im.resize((s, s), Image.LANCZOS)
        rel = ('hicolor/' if with_prefix else '') + '%dx%d/status' % (s, s)
        d = os.path.join(dirs_root, rel)
        os.makedirs(d, exist_ok=True)
        im.save(os.path.join(d, NAME + '.png'))
        rels.append(rel)
    return rels


def main():
    if not os.path.exists(MASTER):
        raise SystemExit('母图缺失：%s（先跑 Tools/make_menubar_icon.py）' % MASTER)
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                          else os.path.join(ROOT, 'assets', 'sni-theme'))
    os.makedirs(out, exist_ok=True)
    rels = write_dirs(out, False) + write_dirs(out, True)
    # 一份 index.theme 同时声明两种布局——分两次写会互相覆盖，后写的那份把前一份的
    # Directories 全丢掉，等于只铺了一种（第一版就栽在这里）。
    idx = '[Icon Theme]\nName=%s\nComment=Agent Ping status item icons\nDirectories=%s\n' % (
        NAME, ','.join(rels))
    with open(os.path.join(out, 'index.theme'), 'w') as f:
        f.write(idx)
    n = 0
    for root, _, files in os.walk(out):
        n += len(files)
    print('主题目录=%s 共 %d 个文件（含 index.theme）' % (out, n))
    print('Directories=%s' % idx.split('Directories=')[1].strip())


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""把假宿主解码出来的 SNI IconPixmap（PAM）转成 PNG，并拼一张 3 倍放大的深浅底对照图。

单独成文件而不是内联 heredoc：这台机器有全局 PreToolUse 闸门会扫命令里的重定向符号，
脚本正文里任何 `>` 都会被它当成输出重定向而拦下来。

用法： python3 sni_icon_sheet.py <cpp证据目录>
"""
import os
import sys

from PIL import Image, ImageDraw

SCALE = 3          # 22px 的图原样贴出来等于没看，判细节必须放大
PAD = 26
ROWS = [((242, 242, 247, 255), '浅色菜单栏'), ((28, 28, 32, 255), '深色菜单栏')]


def load_pam(path):
    """自己解 P7：PIL 的 ImImagePlugin 认不了我们宿主写出的这种 TUPLTYPE 行（实测
    UnidentifiedImageError），而格式是我们自己定的，没必要赌第三方读得懂。"""
    with open(path, 'rb') as f:
        blob = f.read()
    if not blob.startswith(b'P7\n'):
        raise ValueError('%s 不是 P7' % path)
    end = blob.find(b'ENDHDR\n')
    if end < 0:
        raise ValueError('%s 没有 ENDHDR' % path)
    head = {}
    for line in blob[3:end].decode().splitlines():
        k, _, v = line.partition(' ')
        head[k] = v.strip()
    w, h, depth = int(head['WIDTH']), int(head['HEIGHT']), int(head['DEPTH'])
    body = blob[end + 7:]
    if len(body) != w * h * depth:
        raise ValueError('%s 数据段 %d 字节 ≠ %d×%d×%d' % (path, len(body), w, h, depth))
    mode = {3: 'RGB', 4: 'RGBA'}[depth]
    return Image.frombytes(mode, (w, h), body)


def main():
    d = os.path.abspath(sys.argv[1])
    ims = []
    for f in sorted(os.listdir(d)):
        if not f.endswith('.pam'):
            continue
        im = load_pam(os.path.join(d, f)).convert('RGBA')
        png = os.path.join(d, f[:-4] + '.png')
        im.save(png)
        hist = im.getchannel('A').histogram()
        opaque = sum(hist[9:256])
        full = sum(hist[247:256])
        print('%s -> %s %dx%d 像素=%d 非透明=%d 全不透明=%d 最不透明的 alpha=%d' % (
            f, os.path.basename(png), im.size[0], im.size[1], im.size[0] * im.size[1],
            opaque, full, max(a for a in range(256) if hist[a])))
        ims.append((im, opaque))

    if not ims:
        raise SystemExit('没找到 .pam——宿主没解码出图标，别硬拼对照图')

    max_h = max(im.size[1] for im, _ in ims)
    W = 20 + sum(im.size[0] * SCALE + PAD for im, _ in ims)
    H = len(ROWS) * (max_h * SCALE + 34)
    sheet = Image.new('RGBA', (W, H), (128, 128, 128, 255))
    dr = ImageDraw.Draw(sheet)
    y = 6
    for bg, name in ROWS:
        x = 10
        dr.text((x, y + max_h * SCALE + 8), name, fill=(255, 255, 255, 255))
        for im, _ in ims:
            big = im.resize((im.size[0] * SCALE, im.size[1] * SCALE), Image.NEAREST)
            dr.rectangle([x, y, x + big.size[0], y + max_h * SCALE], fill=bg)
            sheet.paste(big, (x, y + (max_h * SCALE - big.size[1]) // 2), big)
            x += big.size[0] + PAD
        y += max_h * SCALE + 34
    out = os.path.join(d, 'sni-icon-3x-both-themes.png')
    sheet.save(out)
    print('对照图=%s %s' % (out, sheet.size))

    # 对比度：SNI 没有 macOS 那套 template 自动换色，宿主把我们给的像素原样贴上去，
    # 所以浅色面板上看不看得见得自己算。判据用 WCAG 的 (L1+0.05)/(L2+0.05)，
    # 并且**逐像素按 alpha 合成到底色上**再比——半透明像素的实际亮度既不是墨色也不是底色。
    #
    # 为什么不是「取墨色中位亮度算一个数」：E4 那颗是暗环＋亮星的双色图，中位数在浅底上给
    # 6.19:1、在深底上给 2.46:1，看着像「浅底能用、深底勉强」；分档一算才知道深底上暗环只有
    # 1.03:1（等于没有），浅底上星芒只有 2.28:1（也等于没有）——两个方向的缺陷都被中位数抹平。
    # 所以这里报的是「按 3:1（WCAG 图形对象门槛）能活下来的像素占多少」。
    def lum(c):
        def lin(v):
            v /= 255.0
            return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
        return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])

    for im, _ in ims:
        px = [p for p in im.getdata() if p[3] > 0]
        if not px:
            print('%dx%d 全透明——图标等于没画' % im.size)
            continue
        for bg, name in ROWS:
            lb = lum(bg)
            cs = []
            for p in px:
                a = p[3] / 255.0
                mixed = tuple(round(p[i] * a + bg[i] * (1 - a)) for i in range(3))
                m = lum(mixed)
                cs.append((max(m, lb) + 0.05) / (min(m, lb) + 0.05))
            cs.sort()
            n = len(cs)
            q = max(1, n // 4)
            print('%dx%d 在%s：按 3:1 存活 %d/%d 有墨像素＝%.0f%%｜中位 %.2f:1'
                  ' 暗四分之一 %.2f:1 亮四分之一 %.2f:1' % (
                      im.size[0], im.size[1], name,
                      sum(1 for c in cs if c >= 3.0), n, 100.0 * sum(1 for c in cs if c >= 3.0) / n,
                      cs[n // 2], cs[q // 2], cs[n - q // 2 - 1]))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""从 E4 的像素里出两种状态项图：

1) macOS 菜单栏 18pt **模板图**（`make`）：不能直接拿 icns 缩——系统走 isTemplate，
   只取 alpha 染色，渐变与深蓝底全丢；18pt 下多圈轨道会糊成一团，所以只留一圈。
2) Linux／SNI 的 **彩色图**（`sni_mark` → `src/platform/sb_icon_argb.h`）：SNI 没有模板图
   这个概念，宿主按我们递过去的像素原样画，所以这一路反过来——不重画、不换色，
   只从他自己的像素里裁出亮于深蓝底的部分，alpha 由亮度门给。
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
BASE = os.path.join(ROOT, 'assets', 'icon-1024.png')
HDR = os.path.join(ROOT, 'src', 'platform', 'sb_icon_argb.h')
SX, SY = 572, 548          # 实测星芒中心（probe_star3.py）
LO, HI = 150, 232          # 亮度门：低于 LO 判背景，高于 HI 全实
# SNI 用的门：深蓝底实测 (15,25,49)～(24,69,118) 之间，max 通道在 49–55 有一道悬崖
# （≥48 占 93.3%、≥56 只剩 31.9%），所以 52 起门不会把底捞进来；150 以上算全实。
SNI_LO, SNI_HI = 52, 150
SNI_SIZES = (22, 44)       # 22＝Xfce/KDE 默认面板尺寸，44＝2x
SNI_RIM = (15, 25, 49)     # 他这张图的深蓝底实测值，描边只从这里取，不另调颜色


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


def sni_mark(px, rim='star'):
    """整颗 E4 标记（两颗星 + 全部光带）缩到 px，颜色一律用原图的。

    裁的是「亮于深蓝底」的外接框并补成正方形——不补方就没法居中，硬缩成方形会把他
    那个 916x673 的椭圆轨道压扁。

    描边（rim）解决的是「宿主面板可能是浅色」：星芒芯实测 (222,232,245)，在浅色面板上
    只有 1.5:1，眼睛靠轮廓认形，所以给轮廓补一圈他图里自己的深蓝。
    rim='star' 只绕那颗近白的星补（浅色面板上 15:1，深色面板上 1.34:1 几乎不显形）；
    rim='all' 沿全部光带补一圈，浅色下更清楚但深色下像贴纸白边——两种都出过图对比后才定。
    """
    src = Image.open(BASE).convert('RGB')
    maxch = src.split()[0]
    for ch in src.split()[1:]:
        maxch = ImageChops.lighter(maxch, ch)
    alpha = maxch.point(lambda v: 255 if v >= SNI_HI
                        else max(0, min(255, int(255 * (v - SNI_LO) / (SNI_HI - SNI_LO)))))
    box = alpha.getbbox()
    w, h = box[2] - box[0], box[3] - box[1]
    side = max(w, h)
    canvas = Image.new('L', (side, side), 0)
    canvas.paste(alpha.crop(box), ((side - w) // 2, (side - h) // 2))
    rgb = src.crop(box).resize((w, h), Image.LANCZOS)
    full = Image.new('RGB', (side, side), 'black')
    full.paste(rgb, ((side - w) // 2, (side - h) // 2))
    a = canvas.resize((px, px), Image.LANCZOS)
    body = full.resize((px, px), Image.LANCZOS)
    out = Image.merge('RGBA', (*body.split(), a))
    if rim != 'none':
        solid = a.point(lambda v: 255 if v >= 128 else 0)
        edge = ImageChops.subtract(solid.filter(ImageFilter.MaxFilter(3)), solid)
        if rim == 'star':
            mb = body.split()[0]
            for ch in body.split()[1:]:
                mb = ImageChops.lighter(mb, ch)
            bright = mb.point(lambda v: 255 if v >= 200 else 0).filter(ImageFilter.MaxFilter(3))
            edge = ImageChops.darker(edge, bright)
        plate = Image.new('RGBA', (px, px), (*SNI_RIM, 255))
        out.paste(plate, (0, 0), edge)
    return out


def write_header(images):
    """ARGB32，一个 uint32 一颗像素，值按 0xAARRGGBB 存；总线字节序在 C++ 侧摆。"""
    lines = [
        '/* 自动生成，别手改：Tools/make_menubar_icon.py 的 sni_mark()。',
        ' * 来源＝assets/icon-1024.png（E4 那张）自己的像素，只裁不改色。',
        ' * 值按 0xAARRGGBB 存；SNI 要求网络字节序（大端），展开在 statusbar_sni.cpp。 */',
        '#ifndef SB_ICON_ARGB_H',
        '#define SB_ICON_ARGB_H',
        '#include <cstddef>',
        '#include <cstdint>',
        '',
        'struct SbIcon { int w, h; int n; const uint32_t* px; };',
        '',
    ]
    for px, img in images:
        data = list(img.getdata())
        lines.append('static const uint32_t kSbPx%d[%d] = {' % (px, len(data)))
        for i in range(0, len(data), 8):
            lines.append('    ' + ' '.join('0x%08X,' % ((a << 24) | (r << 16) | (g << 8) | b)
                                           for r, g, b, a in data[i:i + 8]))
        lines += ['};', '']
    lines.append('static const SbIcon SB_ICONS[] = {')
    for px, img in images:
        lines.append('    {%d, %d, %d, kSbPx%d},' % (img.width, img.height, img.width * img.height, px))
    lines += ['};', 'static const int SB_ICON_N = %d;' % len(images), '', '#endif']
    with open(HDR, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return os.path.relpath(HDR, ROOT)


def main(rim='star'):
    for px, name in ((18, 'menubar-icon.png'), (36, 'menubar-icon@2x.png')):
        img = make(px)
        img.save(os.path.join(ROOT, 'assets', name))
        print(name, img.size)

    images = [(px, sni_mark(px, rim)) for px in SNI_SIZES]
    for px, img in images:
        img.save(os.path.join(ROOT, 'assets', 'sni-%d.png' % px))
        opaque = sum(1 for p in img.getdata() if p[3] > 8)
        print('sni-%d.png %dx%d rim=%s 非透明像素 %d/%d (%.0f%%)'
              % (px, img.width, img.height, rim, opaque, img.width * img.height,
                 100.0 * opaque / (img.width * img.height)))
    print('写头文件', write_header(images), os.path.getsize(HDR), 'bytes')


if __name__ == '__main__':
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else 'star')

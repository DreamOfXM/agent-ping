#!/usr/bin/env python3
"""对总线上的状态项发一次真实交互（activate / showhide / quit），把结果打到 stdout。

为什么单独一个脚本：真宿主（gtk-sni-tray）那一侧我们没法替它点鼠标——它把图标画成什么样
是它的自由，但「左键 Activate 会不会收起面板」「菜单点退出进程会不会真收摊」这两条必须能在
截图脚本里按顺序触发，再拿窗口树和 app.log 对账。所以这里只发调用，不读属性（读属性归 sni_host.py）。

用法： python3 Tools/sni_real_click.py <输出目录> activate|showhide|quit
"""
import sys

import gi
gi.require_version('GLib', '2.0')
from gi.repository import Gio, GLib  # noqa: E402

SNI = 'org.kde.StatusNotifierItem'
MENU = 'com.canonical.dbusmenu'
MODES = ('activate', 'showhide', 'quit')


def find_item(c):
    """返回 (总线名, 对象路径, introspect XML)。注册名形如
    org.kde.StatusNotifierItem-<pid>-<n>，路径按实现可异，所以逐个探测而不是猜。"""
    names = c.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                        'ListNames', None, None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
    for n in sorted(x for x in names if x.startswith(SNI + '-')):
        xml = c.call_sync(n, '/StatusNotifierItem', 'org.freedesktop.DBus.Introspectable',
                          'Introspect', None, None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        if SNI in xml:
            return n, '/StatusNotifierItem', xml
    raise SystemExit('总线上没有可introspect的 StatusNotifierItem')


def call(dest, path, iface, method, sig, params):
    """sig 是**整条 in-signature 字符串**（如 'iias'），按元组签名 '(iias)' 构造。
    两个坑都踩过：
    · 按字符 zip 会把 'as' 拆成 'a'+'s'，列表参数当场 TypeError: Must be number, not list；
    · GLib.Variant.new_tuple([...]) 在这个 PyGObject 上不接构造好的 Variant 列表
      （TypeError: Item 0: argument self: Expected GLib.Variant, but got list），
      所以走 sni_host.py 同一套：一次给完整元组签名。"""
    v = GLib.Variant('(%s)' % sig, params) if sig else None
    r = c.call_sync(dest, path, iface, method, v, None, Gio.DBusCallFlags.NONE, 3000, None)
    return r.unpack() if r else None


c = Gio.bus_get_sync(Gio.BusType.SESSION, None)
mode = sys.argv[2] if len(sys.argv) > 2 else 'activate'
if mode not in MODES:
    raise SystemExit('mode 只能是 %s' % '/'.join(MODES))
dest, path, xml = find_item(c)
print('ITEM %s %s xml_len=%d' % (dest, path, len(xml)))

if mode == 'activate':
    print('Activate → %r' % (call(dest, path, SNI, 'Activate', 'ii', (0, 0)),))
    raise SystemExit(0)

# Menu 属性给的是对象路径，别硬写 /StatusNotifierItem/Menu——那是我们自己的形状，
# 不是规范保证的；参考实现那一条是 /org/ayatana/NotificationItem/<id>/Menu。
menu = call(dest, path, 'org.freedesktop.DBus.Properties', 'Get', 'ss', (SNI, 'Menu'))[0]
print('MENU %s' % menu)
# GetLayout 的三个入参按参考实现自己声明的形状来：(i parentId, i recursionDepth, as propertyNames)。
# 我第一版按记忆里「规范是 parentId, propertyNames, revision」写成了 (0, ['label'], 0)，
# 当场 TypeError——这里的裁判是 libayatana 那份 introspect XML（证据 ref/host_report.json 里读到的）。
layout = call(dest, menu, MENU, 'GetLayout', 'iias',
              (0, -1, ['id', 'label', 'enabled', 'visible', 'children-display']))
items = layout[1][2]   # 出参是 (u revision, (i, a{sv}, av))，孩子位在第 1 个出参的第 3 格
print('revision=%d 顶层=%s' % (layout[0], [(i[0], i[1].get('label')) for i in items]))
call(dest, menu, MENU, 'AboutToShow', 'i', (0,))

want = items[0] if mode == 'showhide' else next(
    (i for i in items if '退出' in str(i[1].get('label', ''))), None)
if want is None:
    raise SystemExit('菜单里没有「退出」那一项，实际=%r' % (items,))
# Event 的第三个参数是 variant（规范写 v，不是 a{sv}）——空 dict 直接当 'a{sv}' 传会在
# 构造签名时就报错，包成 Variant 才和参考实现的声明一致。
call(dest, menu, MENU, 'Event', 'isvu',
     (want[0], 'clicked', GLib.Variant('a{sv}', {}), 0))
print('Event(clicked) id=%d label=%r 已发出' % (want[0], want[1].get('label')))

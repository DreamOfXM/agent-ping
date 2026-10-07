#!/usr/bin/env python3
"""参考实现：一个真的 Ayatana AppIndicator3 托盘项，用规范里那套写法摆出我们的三档数。
它的存在不是为了好看，是为了让假宿主能读到一个**别人写的**实现，把事实口径抄下来：
Label 的签名、Menu 是对象路径还是字符串、GetLayout 的回包形状、NewToolTip 带不带参数。

跑法（要和假宿主同一条总线，见 Tools/sni_ref_vs_host.sh）：
  xvfb-run -a dbus-run-session -- python3 Tools/sni_reference_item.py 10
"""
import sys

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gio, GLib, Gtk  # noqa: E402

# Debian 上装的是 Ayatana 分支的 gir，命名空间名可能是 AppIndicator3（兼容别名）
# 或 AyatanaAppIndicator3（真名）；哪个能用就用哪个，并把用了哪个打进日志——
# 否则「参考实现跑不起来」会被当成对方坏了。
try:
    gi.require_version('AppIndicator3', '0.1')
    from gi.repository import AppIndicator3 as AI
    WHICH = 'AppIndicator3'
except ValueError:
    gi.require_version('AyatanaAppIndicator3', '0.1')
    from gi.repository import AyatanaAppIndicator3 as AI
    WHICH = 'AyatanaAppIndicator3'

SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 8

ind = AI.Indicator.new('agent-ping-ref', '', AI.IndicatorCategory.APPLICATION_STATUS)
ind.set_status(AI.IndicatorStatus.ACTIVE)
ind.set_title('Agent Ping')
# 三档数：需要你介入 / 运行中 / 失败中断 —— 和 macOS 那档用的是同一串。
# 第二个参数（guide）按本机 /usr/share/gir-1.0/AyatanaAppIndicator3-0.1.gir 的原文
# "A guide to size the label correctly." 只是**撑宽用的占位**，所以给一个不会再变宽的
# 最坏读数；人话说明在这里没有通道可发（同一份宿主读参考项的 ToolTip 是「读失败」），
# 这点和我们的 C++ 项不一样，正是对比要抓出来的差异。
ind.set_label('3 · 7 · 1', '99 · 99 · 99')
ind.set_icon_full('agent-ping', 'Agent Ping')

menu = Gtk.Menu()
menu.append(Gtk.MenuItem(label='显示／隐藏面板'))
menu.append(Gtk.MenuItem(label='退出 Agent Ping'))
menu.show_all()
ind.set_menu(menu)

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
print('SELF=%s' % bus.get_unique_name(), flush=True)
print('NAMESPACE=%s' % WHICH, flush=True)
names = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                      'ListNames', None, None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
print('WATCHER_ON_BUS=%s' % ('org.kde.StatusNotifierWatcher' in names), flush=True)
print('SNI_NAMES=%s' % [n for n in names if 'StatusNotifier' in n], flush=True)

GLib.timeout_add_seconds(SECS, Gtk.main_quit)
Gtk.main()
print('REF_DONE', flush=True)

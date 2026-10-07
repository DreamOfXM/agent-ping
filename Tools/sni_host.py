#!/usr/bin/env python3
"""假宿主（StatusNotifierItem 协议里的 Watcher）：真的占有 org.kde.StatusNotifierWatcher，
等托盘项来注册，然后把它的属性、图标、菜单全读回来落盘。

存在的理由是「自己写客户端再自己写宿主来验＝拿自己的误解考自己，一定满分」。
所以这一个文件同时对两个对象跑：
  1. 参考实现（Ayatana AppIndicator3，见 Tools/sni_reference_item.py）—— 它的回包就是事实口径；
  2. 我们的 pin_imgui_shell —— 读回来的每一条都要能和 1 对齐。
凡两边形状不一致，错的一方按 1 改。

用法： python3 sni_host.py <输出目录> [等待秒数]
退出码：0 至少注册并读回了一个项 / 1 超时没人来注册 / 2 连不上总线或参数错
"""
import json
import os
import re
import struct
import sys
import time

import gi
gi.require_version('Gio', '2.0')
gi.require_version('GLib', '2.0')
from gi.repository import Gio, GLib

ITEM_IFACE = 'org.kde.StatusNotifierItem'
MENU_IFACE = 'com.canonical.dbusmenu'

XML = """
<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem">
      <arg type="s" direction="in" name="service"/>
    </method>
    <method name="UnregisterStatusNotifierItem">
      <arg type="s" direction="in" name="service"/>
    </method>
    <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
    <property name="ProtocolVersion" type="i" access="read"/>
    <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
    <signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
    <signal name="StatusNotifierHostRegistered"/>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg type="s" direction="in" name="interface_name"/>
      <arg type="s" direction="in" name="property_name"/>
      <arg type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg type="s" direction="in" name="interface_name"/>
      <arg type="a{sv}" direction="out" name="properties"/>
    </method>
  </interface>
</node>
"""

# 属性名按 freedesktop 规范全列一遍，另外补上 KDE 扩展 Label：
# 读失败不抛，逐条记下错误串——「这个属性不存在」本身就是要对比的结论，
# 只报成功的那几条等于把差异抹平。
PROPS = [
    'Category', 'Id', 'Title', 'Status', 'WindowId',
    'IconName', 'IconThemePath', 'AttentionMovieName',
    'OverlayIconName', 'AttentionIconName',
    'ItemIsMenu', 'Menu', 'ToolTip', 'IconPixmap',
    'OverlayIconPixmap', 'AttentionIconPixmap',
    'Label',
    # 三档数字有两套方言：KDE 侧的 Label/NewLabel 和 Ayatana 侧的 XAyatanaLabel。
    # 我们两套都发，那宿主就得两套都读——只读 Label 就把「另一套没接上」这种故障
    # 留在看不见的地方（本轮第一版正是这样，props.txt 里压根没有 XAyatana 三行）。
    'XAyatanaLabel', 'XAyatanaLabelGuide', 'XAyatanaOrderingIndex',
]

OUTDIR, WAIT, loop, conn = '', 6, None, None
DRIVE = False        # --drive：读完之后真的点一次（Activate／菜单 Event／退出）
found = []
report = {}
T0 = time.time()


def err(text):
    # 带相对秒：上一轮宿主在 14s 探测、App 只挂 9s，却照样读到了全部属性——
    # 没有时间戳就说不清「属性是在进程还活着时被读走的」还是「进程没了还在回」。
    print('[宿主 %6.2fs] %s' % (time.time() - T0, text), flush=True)


def to_bytes(x):
    if isinstance(x, (bytes, bytearray)):
        return bytes(x)
    if hasattr(x, 'get_data'):          # GLib.Bytes（ay 解出来可能是它）
        return bytes(x.get_data())
    return bytes(bytearray(x))


def call(dest, path, iface, method, sig=None, val=None, single=False):
    """同步调一次，返回 (True, 解包值) 或 (False, 错误串)。

    single=True 用于「只有一个出参」的调用：GVariant 解包出来是**一层列表**，
    不解这层就会把 ['Active'] 读成 'Active'，判据会静默指错方向。
    """
    params = GLib.Variant(sig, val) if sig else None
    try:
        v = conn.call_sync(dest, path, iface, method, params, None,
                           Gio.DBusCallFlags.NONE, 3000, None)
    except GLib.Error as e:
        return False, '%s(%s)' % (e.message, e.code)
    except Exception as e:  # noqa: BLE001
        return False, repr(e)
    if v is None:
        return True, None
    # GVariant 的元组解包出来是 **tuple** 不是 list，只判 list 就会漏掉一层，
    # 然后把 ('<node …>',) 当字符串传下去（实测报 expected string, got tuple）。
    got = v.unpack()
    if single and isinstance(got, (list, tuple)) and len(got) == 1:
        got = got[0]
    return True, got


def get_prop(dest, path, name):
    ok, v = call(dest, path, 'org.freedesktop.DBus.Properties', 'Get',
                 '(ss)', (ITEM_IFACE, name), single=True)
    return (True, v) if ok else (False, v)


def introspect(dest, path):
    ok, v = call(dest, path, 'org.freedesktop.DBus.Introspectable', 'Introspect', single=True)
    return (True, v) if ok else (False, v)


def find_item_paths(dest):
    """从 / 起递归找实现了 org.kde.StatusNotifierItem 的对象路径。"""
    hits = []

    def walk(p):
        ok, xml = introspect(dest, p)
        if not ok:
            return
        if ITEM_IFACE in xml:
            hits.append(p)
        for m in re.finditer(r'<node\s+name="([^"]+)"\s*/>', xml):
            walk('/' + m.group(1) if p == '/' else p + '/' + m.group(1))

    walk('/')
    return hits


def pixmap_to_pam(w, h, argb, out):
    """SNI 的 a(iiay) 是网络字节序（大端）ARGB32，这里转 RGBA 写 P7，供 PIL 读。"""
    head = ('P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'
            % (w, h)).encode()
    buf = bytearray(head)
    for i in range(w * h):
        px = struct.unpack('>I', argb[i * 4:i * 4 + 4])[0]
        buf += bytes(((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, (px >> 24) & 0xFF))
    with open(out, 'wb') as f:
        f.write(bytes(buf))


def brief(v):
    if isinstance(v, (list, tuple)):
        return '（%d 项）' % len(v)
    if isinstance(v, bool):
        return '是' if v else '否'
    return v


def probe_item(dest, path):
    d = {'bus': dest, 'path': path, 'props': {}, 'pixmaps': [], 'errors': {}}
    for name in PROPS:
        ok, val = get_prop(dest, path, name)
        if not ok:
            d['errors'][name] = val
            d['props'][name] = '读失败 → ' + val
            continue
        if name.endswith('Pixmap'):
            rows = []
            for si, (w, h, data) in enumerate(val):
                raw = to_bytes(data)
                fn = os.path.join(OUTDIR, 'pam-%s-%s-%dx%d.pam' % (re.sub(r'\W+', '_', dest), name, w, h))
                try:
                    if len(raw) == w * h * 4:
                        pixmap_to_pam(w, h, raw, fn)
                        d['pixmaps'].append((fn, name, w, h, len(raw)))
                    else:
                        d['errors'][name + '_len'] = '%d 字节 ≠ %d×%d×4' % (len(raw), w, h)
                    rows.append('%dx%d=%d字节' % (w, h, len(raw)))
                except Exception as e:  # noqa: BLE001
                    d['errors'][name + '_pam'] = repr(e)
            d['props'][name] = '%d 张：%s' % (len(val), ', '.join(rows))
        elif name == 'ToolTip':
            tt = list(val)
            d['props'][name] = '图标%d项 | 标题=%r | 正文=%r' % (
                len(to_bytes(tt[1])) if not isinstance(tt[1], list) else len(tt[1]), tt[2], tt[3])
            d['tooltip_raw'] = [tt[0], len(tt[1]), tt[2], tt[3]]
        else:
            d['props'][name] = brief(val)

    menu = d['props'].get('Menu')
    if isinstance(menu, str) and not menu.startswith('读失败'):
        ok, xml = introspect(dest, menu)
        d['menu_xml'] = (xml or '')[:6000] if ok else xml
        ok, res = call(dest, menu, MENU_IFACE, 'GetLayout', '(iias)', (0, -1, []))
        d['menu_getlayout'] = str(res)[:4000] if ok else 'GetLayout 失败 → ' + res


    ok, xml = introspect(dest, path)
    d['item_xml'] = xml if ok else xml
    if ok:
        d['signals'] = re.findall(r'<signal name="([^"]+)"\s*(/>)?(>)?', xml)
        d['method_outargs'] = re.findall(r'<method name="(New\w+|GetLayout)">(.*?)</method>', xml, re.S)
    return d


def on_method_call(connection, sender, object_path, interface_name, method_name,
                   parameters, invocation, user_data=None):
    # user_data 给默认值：PyGObject 在传 None 时只回调 7 个位置参数，
    # 写死第 8 个必填参数＝注册请求一进来就抛 TypeError，然后被总线吞掉——
    # 表现是「参考实现明明在跑，宿主一个项都没收到」。
    # user_data 给默认值：PyGObject 在 user_data=None 时只传 7 个位置参数，
    # 少这一句就是「回调一进来就抛 TypeError，注册请求被静默吞掉」。
    if method_name == 'RegisterStatusNotifierItem':
        svc = parameters.unpack()[0]
        err('收到注册请求 service=%r sender=%r' % (svc, sender))
        found.append((svc, sender))
        invocation.return_value(None)
        GLib.timeout_add(400, do_probe)
    elif method_name == 'UnregisterStatusNotifierItem':
        invocation.return_value(None)
    elif interface_name == 'org.freedesktop.DBus.Properties':
        args = parameters.unpack()
        if method_name == 'Get':
            val = {'RegisteredStatusNotifierItems': GLib.Variant('as', [s for s, _ in found]),
                   'IsStatusNotifierHostRegistered': GLib.Variant('b', True),
                   'ProtocolVersion': GLib.Variant('i', 0)}.get(args[1])
            if val is None:
                invocation.return_error_literal(GLib.Error(
                    Gio.DBusError, Gio.DBusError.UNKNOWN_PROPERTY, args[1]))
            else:
                invocation.return_value(GLib.Variant('(v)', (val,)))
        elif method_name == 'GetAll':
            invocation.return_value(GLib.Variant('(a{sv})', ({
                'RegisteredStatusNotifierItems': GLib.Variant('as', [s for s, _ in found]),
                'IsStatusNotifierHostRegistered': GLib.Variant('b', True),
                'ProtocolVersion': GLib.Variant('i', 0),},)))
        else:
            invocation.return_value(None)
    else:
        invocation.return_error_literal(
            GLib.Error(Gio.DBusError, Gio.DBusError.UNKNOWN_METHOD, method_name))


def dump():
    with open(os.path.join(OUTDIR, 'host_report.json'), 'w') as f:
        json.dump(report, f, indent=1, ensure_ascii=False, default=str)
    with open(os.path.join(OUTDIR, 'props.txt'), 'w') as f:
        for key, d in report.items():
            if key == 'drive':
                continue
            f.write('=== %s ===\n' % key)
            for k in PROPS:
                f.write('%-20s %s\n' % (k, d['props'].get(k, '（没这条键）')))
            if d.get('errors'):
                f.write('读失败：%s\n' % json.dumps(d['errors'], ensure_ascii=False))
            for fn, name, w, h, nb in d.get('pixmaps', []):
                f.write('%-20s %s %dx%d bytes=%d\n' % (name + '->', os.path.basename(fn), w, h, nb))
            if 'menu_getlayout' in d:
                f.write('menu_getlayout: %s\n' % d['menu_getlayout'])
            f.write('item_xml_len=%d\n' % len(d.get('item_xml') or ''))
        drv = report.get('drive')
        if drv:
            f.write('\n=== 宿主驱动（点了会怎样）===\n')
            for key, rows in drv.items():
                f.write('--- %s ---\n' % key)
                for name, st, res in rows:
                    f.write('%-42s %-4s %s\n' % (name, st, res))
    items = [k for k in report if k != 'drive']
    err('落盘完成：%d 个项 → %s' % (len(items), OUTDIR))


def split_service(svc, sender):
    """把注册串拆成 (总线名, 对象路径)。

    规范允许三种写法，实测 Ayatana 用的是第二种：
      'org.kde.StatusNotifierItem-1234-1'        纯总线名 → 路径默认 /StatusNotifierItem
      'org.kde...-1234-1/StatusNotifierItem'     名 + 路径拼一起
      '/org/ayatana/NotificationItem/xxx'        只给对象路径 → 总线名取消息发送方
    只按第一种写宿主，就会把第二种/第三种当成非法名直接调用失败（本轮实测踩过）。
    """
    if svc.startswith('/'):
        return sender, svc
    if '/' in svc:
        name, path = svc.split('/', 1)
        return name, '/' + path
    return svc, '/StatusNotifierItem'


def walk_layout(node, out):
    """layout ＝ (id, props, 子节点数组)，子节点每个都是 variant 包着一个同形状结构。
    收成 {id: label}，供 drive() 按标签认那两项（中英都能认，不写死 id）。"""
    try:
        pid, props, kids = node[0], node[1], node[2]
    except Exception:  # noqa: BLE001
        return out
    if isinstance(props, dict):
        lab = props.get('label')
        if isinstance(lab, (str, bytes)):
            out[int(pid)] = lab.decode() if isinstance(lab, bytes) else lab
    for k in kids or []:
        walk_layout(k, out)
    return out


def drive(dest, path, d):
    """真的当一次托盘：左键 Activate、菜单项 Event(clicked)，最后点「退出」。

    只读属性证明不了「点了会怎样」。这一步的判据全在 app.log 里数得出来：
    Activate + Event(显示／隐藏) 应各打一行「[面板]」，Event(退出) 之后进程应当消失——
    所以末尾再读一次属性，期望它**报错**（总线名已随进程走掉）。读还成功＝没退。
    """
    log = []

    def rec(name, okx, res):
        # 当场打时间戳：上一版把六次调用攒到函数末尾统一 err()，结果 host.log 里
        # 六行全是同一个秒数，中间那两次 0.7s 睡眠在日志上凭空消失——
        # 我据此以为容器时钟跳了，实跑对照后才确认是日志自己把顺序压平了。
        err('驱动 %s → %s %s' % (name, 'OK' if okx else 'ERR', str(res)[:120]))
        log.append((name, 'OK' if okx else 'ERR', str(res)[:400]))

    menu = d['props'].get('Menu')
    ids = {}
    if isinstance(menu, str) and menu and not menu.startswith('读失败'):
        ok, _r = call(dest, menu, MENU_IFACE, 'AboutToShow', '(i)', (0,))
        rec('AboutToShow(0)', ok, _r)
        ok, res = call(dest, menu, MENU_IFACE, 'GetLayout', '(iias)',
                       (0, -1, ['id', 'label', 'enabled', 'visible', 'type', 'children-display']))
        if ok:
            walk_layout(res[1], ids)
        rec('GetLayout(带属性名)', ok, res)
    ok, r = call(dest, path, ITEM_IFACE, 'Activate', '(ii)', (0, 0))
    rec('Activate(0,0)', ok, r)
    time.sleep(0.7)

    def find(*keys):
        for pid, lab in ids.items():
            if any(k in lab for k in keys):
                return pid, lab
        return None, None

    # 键表要覆盖六语：托盘菜单的文案跟着系统语言走，宿主只写中英的话，
    # 换到 de 就变成「布局里没找到该项」——10-08 那轮 LC_ALL=de_DE.UTF-8 实测就是这样，
    # 总线上明明有 'Panel ein-/ausblenden'／'Agent Ping beenden'，是宿主认不出，不是 App 没挂上。
    sid, slab = find('显示', '隐藏', 'Show', 'hide', 'toggle', 'ein-', 'ausblenden',
                     'Mostra', 'nascondi', '表示', '표시', '숨기기')
    if sid is not None:
        ok, r = call(dest, menu, MENU_IFACE, 'Event', '(isvu)',
                     (sid, 'clicked', GLib.Variant('a{sv}', {}), 0))
        rec('Event(%d=%r, clicked)' % (sid, slab), ok, r)
        time.sleep(0.7)
    else:
        rec('Event(显示／隐藏)', False, '布局里没找到该项')

    qid, qlab = find('退出', 'Quit', 'exit', 'beenden', 'Esci', '終了', '종료')
    if qid is not None:
        ok, r = call(dest, menu, MENU_IFACE, 'Event', '(isvu)',
                     (qid, 'clicked', GLib.Variant('a{sv}', {}), 0))
        rec('Event(%d=%r, clicked) → 期望进程收摊' % (qid, qlab), ok, r)
        time.sleep(0.9)
        ok2, r2 = get_prop(dest, path, 'Status')
        rec('退出后再读 Status', ok2,
            r2 if ok2 else str(r2) + '（读不到＝名字随进程走了，符合期望）')
    else:
        rec('Event(退出)', False, '布局里没找到该项')
    return log


def do_probe():
    seen = set()
    for svc, sender in found:
        dest, path = split_service(svc, sender)
        err('注册串 %r → 总线名=%s 路径=%s' % (svc, dest, path))
        key = '%s %s' % (dest, path)
        if key in seen:
            continue
        seen.add(key)
        try:
            ok, xml = introspect(dest, path)
            if ok and ITEM_IFACE in xml:
                paths = [path]
            else:
                # 注册串里的路径没实现该接口：退回全树扫描，别直接判「参考实现坏了」
                paths = find_item_paths(dest)
                err('注册路径没有 %s，扫描到 %s' % (ITEM_IFACE, paths))
        except Exception as e:  # noqa: BLE001
            err('%s 扫描失败 %r' % (dest, e))
            continue
        err('%s 上有 %d 个 StatusNotifierItem 路径：%s' % (dest, len(paths), paths))
        for p in paths:
            try:
                d = probe_item(dest, p)
                report['%s %s' % (dest, p)] = d
            except Exception as e:  # noqa: BLE001
                err('探测 %s %s 崩了 %r' % (dest, p, e))
                continue
            if DRIVE:
                try:
                    report.setdefault('drive', {})['%s %s' % (dest, p)] = drive(dest, p, d)
                except Exception as e:  # noqa: BLE001
                    err('驱动 %s %s 崩了 %r' % (dest, p, e))
                    report.setdefault('drive', {})[('%s %s' % (dest, p))] = [('异常', 'ERR', repr(e))]
    dump()
    loop.quit()
    return False


def main():
    global OUTDIR, WAIT, loop, conn, DRIVE
    argv = [a for a in sys.argv[1:] if a != '--drive']
    # 比的是「去掉 --drive 之后还剩几个」，分母必须也是 [1:]：写成 len(sys.argv) 会永远多出
    # 脚本名那 1，于是 --drive 变成默认常开——上一轮参考实现就是被这样「顺手点了一下」，
    # 而它的菜单/Activate 形状和我们的不同，读回来的东西就不再是干净的对照基线。
    DRIVE = len(argv) != len(sys.argv) - 1
    if not argv:
        err('用法：sni_host.py <输出目录> [等待秒数] [--drive]')
        sys.exit(2)
    OUTDIR = argv[0]
    WAIT = int(argv[1]) if len(argv) > 1 else 6
    os.makedirs(OUTDIR, exist_ok=True)
    try:
        conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    except GLib.Error as e:
        err('连不上会话总线：' + str(e))
        sys.exit(2)
    # RequestName 的签名是 (su)，flags=0 ＝不做 REPLACE/NONEXISTENT 替换；
    # 回包 1 才是真的拿到名字，写成 owner!=1 就退是为了把「别人也在抢」和「压根没抢」分开。
    v = GLib.Variant('(su)', ('org.kde.StatusNotifierWatcher', 0))
    r = conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
                       'org.freedesktop.DBus', 'RequestName', v, None,
                       Gio.DBusCallFlags.NONE, 3000, None)
    owner = r.unpack()[0]
    if owner != 1:   # 1 = PRIMARY_OWNER
        err('占有名字失败，RequestName 返回 %d（2=已在手上 3=他人 4=归属被换）' % owner)
        sys.exit(2)
    node = Gio.DBusNodeInfo.new_for_xml(XML)
    conn.register_object('/StatusNotifierWatcher',
                         node.interfaces[0],
                         on_method_call, None, None)
    err('watcher 就位（org.kde.StatusNotifierWatcher 已占有），等注册，%d 秒后收口' % WAIT)
    loop = GLib.MainLoop()
    GLib.timeout_add_seconds(WAIT, do_probe)
    loop.run()
    sys.exit(0 if report else 1)


if __name__ == '__main__':
    main()

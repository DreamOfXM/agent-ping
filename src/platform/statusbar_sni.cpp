// Linux／其它 POSIX 桌面的状态项：StatusNotifierItem（SNI）。
//
// 为什么走 SNI 而不是 libappindicator／GTK：SNI 是 KDE、GNOME（装 AppIndicator 扩展）、
// XFCE、LXQt 共用的一条 D-Bus 协议，libappindicator 本身也只是它的一个客户端。
// 直接说协议只依赖 libdbus-1，不必为了菜单栏一颗图标把整个 GTK3 拉进依赖里。
//
// 为什么不是老的 XEmbed 系统托盘：那条协议要求桌面提供一个 _NET_SYSTEM_TRAY_S0 的
// selection owner，现代桌面基本撤了；而且本机这种纯 Xvfb 环境里没有宿主，写了也验不了。
//
// 取证口径：这一档没有真机桌面可看（README 里那条「真机 Linux 桌面零台」），
// 所以每个数都靠总线对端读回来。工具是 Tools/sni_host.py（扮演宿主：占有
// org.kde.StatusNotifierWatcher、等注册、把属性/图标/菜单读回并解码 PAM），
// 一键对照跑法见 Tools/sni_ref_vs_host.sh。
//
// 为什么必须拿参考实现来对：自己写客户端再自己写宿主＝拿自己的误解考自己，一定满分。
// 本轮实测就是这么抓到三处错的（Menu 该是对象路径不是字符串、NewToolTip 是无参信号、
// GetLayout 只有两个出参），三条在自己写的宿主上都会「通过」。
#include "statusbar.h"

#include <dbus/dbus.h>

#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>   // stat + S_ISDIR：找可执行文件旁边的图标主题目录
#include <unistd.h>   // getpid：总线名里的 PID 段是宿主用来找我们的；readlink /proc/self/exe

#include "sb_icon_argb.h"   // 由 Tools/make_menubar_icon.py 从 E4 那颗图标生成，别手改

static DBusConnection* g_c = nullptr;
static std::string g_bus;          // org.kde.StatusNotifierItem-<pid>-1
static std::string g_label;        // 三个数字
static std::string g_guide;        // 占位宽度（Ayatana 的 XAyatanaLabelGuide，不是第二行说明）
static std::string g_tip;          // 悬停说明
static std::string g_showhide = "显示／隐藏面板";
static std::string g_quit = "退出 Agent Ping";
static sb_click_fn g_click = nullptr;
static void* g_ud = nullptr;
static bool g_host_seen = false;   // 注册成功过没有——只报一次状态翻转，别每 2 秒刷日志
static std::string g_icon_theme;   // 空＝只发 IconPixmap；非名＝同时发 IconName + IconThemePath
static bool g_no_pixmap = false;   // 取证开关：只走名字那条路，验证宿主真能按名字取到图

/// 找图标主题目录（Tools/make_sni_theme.py 铺的那个）。三处按顺序试：
///   ① 可执行文件旁边（打包好的目录里）
///   ② <prefix>/share/agent-ping/sni-theme（cmake --install 之后的布局）
///   ③ 仓库里的 assets/sni-theme（源码树里直接跑）
/// 走 /proc/self/exe 而不是 argv[0]：从桌面启动器里拉起来时 argv[0] 可以没有目录段。
static std::string FindThemeDir() {
    const char* env = getenv("AGENTPING_SNI_THEME_DIR");
    if (env && *env) return env;   // 取证时显式指定，别让它去猜我们要测哪一个
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    std::string exe(buf, static_cast<size_t>(n));
    size_t slash = exe.rfind('/');
    if (slash == std::string::npos) return "";
    std::string dir = exe.substr(0, slash);          // 可执行文件所在目录
    std::string up = dir.substr(0, dir.rfind('/')); // 再上一层（仓库内跑时是 build-linux 的父目录）
    // 这里刻意不用 "<dir>/../assets/…" 这种写法：拼出来的字符串会原样发到总线上，
    // 宿主拿到的是一个带 .. 的路径（实测 gtk-sni-tray 能读，但没必要赌每个宿主都能）。
    const std::string cand[] = {dir + "/sni-theme", up + "/share/agent-ping/sni-theme",
                                up + "/assets/sni-theme"};
    for (const std::string& p : cand) {
        struct stat st;
        if (!stat(p.c_str(), &st) && S_ISDIR(st.st_mode)) return p;
    }
    return "";
}

/// 码点数（不是字节数）：面板按字形排宽，「·」在 UTF-8 里占 3 字节但只占 1 格。
static size_t Utf8Cells(const std::string& s) {
    size_t n = 0;
    for (unsigned char ch : s)
        if ((ch & 0xC0) != 0x80) n++;   // 跳掉续字节，只数首字节
    return n;
}

static const char* kIface = "org.kde.StatusNotifierItem";
static const char* kWatcher = "org.kde.StatusNotifierWatcher";
static const char* kWatcherPath = "/StatusNotifierWatcher";
static const char* kItemPath = "/StatusNotifierItem";
// 菜单路径挂在项路径下面，和参考实现（Ayatana 给的是 <项路径>/Menu）同一形状；
// 宿主有的会按「子节点名叫 Menu」去找，写成平级的 /StatusNotifierItemMenu 就找不到。
static const char* kMenuPath = "/StatusNotifierItem/Menu";
static const char* kMenuIface = "com.canonical.dbusmenu";
static const char* kAppId = "agent-ping";

// ── 小工具：libdbus 的容器写法啰嗦到必须包一层 ──────────────────────────────
static void AppendStr(DBusMessageIter* it, const char* s) {
    const char* p = s ? s : "";
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &p);
}
static void SvStr(DBusMessageIter* arr, const char* key, const char* val) {
    DBusMessageIter e, v;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    AppendStr(&e, key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
    AppendStr(&v, val);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(arr, &e);
}
static void SvBool(DBusMessageIter* arr, const char* key, bool val) {
    DBusMessageIter e, v;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    AppendStr(&e, key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "b", &v);
    dbus_bool_t b = val ? TRUE : FALSE;
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(arr, &e);
}

/// IconPixmap：a(iiay)，像素是 ARGB32 网络字节序（＝大端）。
/// 头文件里的数已经按 0xAARRGGBB 存好，这里只负责摆成总线要的形状。
static void AppendPixmaps(DBusMessageIter* parent, const char* of) {
    DBusMessageIter arr;
    dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, of, &arr);
    for (int i = 0; i < SB_ICON_N; i++) {
        DBusMessageIter st, bytes;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &st);
        dbus_int32_t w = SB_ICONS[i].w, h = SB_ICONS[i].h;
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &bytes);
        // n 已经是像素个数（22x22＝484），每像素出 4 字节，所以循环上界就是 n。
        // 这里曾写成 n*4：一次回包读越界到数组尾巴后面 4 倍长度，
        // 宿主读回的字节数是 16/像素——是假宿主量出来的，不是自己看出来的。
        for (size_t p = 0; p < SB_ICONS[i].n; p++) {
            const uint32_t px = SB_ICONS[i].px[p];
            unsigned char b[4] = {
                (unsigned char)((px >> 24) & 0xff), (unsigned char)((px >> 16) & 0xff),
                (unsigned char)((px >> 8) & 0xff), (unsigned char)(px & 0xff)
            };
            for (int k = 0; k < 4; k++) dbus_message_iter_append_basic(&bytes, DBUS_TYPE_BYTE, &b[k]);
        }
        dbus_message_iter_close_container(&st, &bytes);
        dbus_message_iter_close_container(&arr, &st);
    }
    dbus_message_iter_close_container(parent, &arr);
}

/// ToolTip：(sa(iiay)ss) —— 图标名、pixmap、标题、正文。宿主只画正文这一格，
/// 所以三个数字的说明必须落在正文里，不能指望图标名。
static void AppendTooltip(DBusMessageIter* parent) {
    DBusMessageIter st;
    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, nullptr, &st);
    AppendStr(&st, "");
    {
        DBusMessageIter pm;
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &pm);
        dbus_message_iter_close_container(&st, &pm);   // 正文里不放第二份图，省 4KB/次
    }
    AppendStr(&st, "Agent Ping");
    AppendStr(&st, g_tip.c_str());
    dbus_message_iter_close_container(parent, &st);
}

/// 属性的「类型串 + 值」各自只在这里定义一次，GetAll 由同一张表生成。
/// 分成两张手写表时漂移过一次：宿主启动时读走的是形状 A，之后单条 Get 读回形状 B，
/// 表现是某个字段在某些宿主上永远是空的，而本地自测全绿。
/// 名单＝参考实现有的（Ayatana introspect 实测）∪ 规范要求的（freedesktop 原文），
/// 少一项就少一条宿主可能走的显示路。
static const char* const kItemProps[] = {
    "Category", "Id", "Title", "Status", "WindowId",
    "IconName", "IconThemePath", "IconAccessibleDesc",
    "AttentionIconName", "AttentionAccessibleDesc", "AttentionMovieName", "OverlayIconName",
    "ItemIsMenu", "Menu",
    "Label", "XAyatanaLabel", "XAyatanaLabelGuide", "XAyatanaOrderingIndex",
    "IconPixmap", "OverlayIconPixmap", "AttentionIconPixmap", "ToolTip", nullptr};

/// 签名串填错 libdbus 会直接把回包扔掉（不报错），所以这张表就是
/// 「哪个属性能被读回来、按什么类型读」的唯一出处。
static const char* ItemPropSig(const char* name) {
    if (!strcmp(name, "Menu")) return "o";
    if (!strcmp(name, "ItemIsMenu")) return "b";
    if (!strcmp(name, "WindowId") || !strcmp(name, "XAyatanaOrderingIndex")) return "u";
    if (!strcmp(name, "IconPixmap") || !strcmp(name, "OverlayIconPixmap")
        || !strcmp(name, "AttentionIconPixmap")) return "a(iiay)";
    if (!strcmp(name, "ToolTip")) return "(sa(iiay)ss)";
    return "s";   // 其余全是字符串
}

static void AppendObjPath(DBusMessageIter* out, const char* path) {
    dbus_message_iter_append_basic(out, DBUS_TYPE_OBJECT_PATH, &path);
}

static bool ReadItemProp(const char* name, DBusMessageIter* out) {
    if (!strcmp(name, "Category")) { AppendStr(out, "ApplicationStatus"); return true; }
    if (!strcmp(name, "Id")) { AppendStr(out, kAppId); return true; }
    if (!strcmp(name, "Title")) { AppendStr(out, "Agent Ping"); return true; }
    if (!strcmp(name, "Status")) { AppendStr(out, "Active"); return true; }
    if (!strcmp(name, "Menu")) { AppendObjPath(out, kMenuPath); return true; }
    if (!strcmp(name, "Label") || !strcmp(name, "XAyatanaLabel")
        || !strcmp(name, "IconAccessibleDesc")) { AppendStr(out, g_label.c_str()); return true; }
    if (!strcmp(name, "XAyatanaLabelGuide")) { AppendStr(out, g_guide.c_str()); return true; }
    if (!strcmp(name, "WindowId") || !strcmp(name, "XAyatanaOrderingIndex")) {
        // WindowId 规范允许 0：常驻项没有对应 X 窗口时就这样报，别编一个。
        dbus_uint32_t u = 0;
        dbus_message_iter_append_basic(out, DBUS_TYPE_UINT32, &u);
        return true;
    }
    if (!strcmp(name, "ItemIsMenu")) {
        // false＝左键走 Activate（我们要一点就开面板），不是只弹菜单。
        dbus_bool_t f = FALSE;
        dbus_message_iter_append_basic(out, DBUS_TYPE_BOOLEAN, &f);
        return true;
    }
    if (!strcmp(name, "IconPixmap")) {
        if (g_no_pixmap) {   // 名字那条路的对照：不发像素，宿主只能按 IconName 去主题目录里找
            DBusMessageIter empty;
            dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "(iiay)", &empty);
            dbus_message_iter_close_container(out, &empty);
            return true;
        }
        AppendPixmaps(out, "(iiay)");
        return true;
    }
    if (!strcmp(name, "OverlayIconPixmap") || !strcmp(name, "AttentionIconPixmap")) {
        DBusMessageIter empty;
        dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "(iiay)", &empty);
        dbus_message_iter_close_container(out, &empty);
        return true;
    }
    if (!strcmp(name, "ToolTip")) { AppendTooltip(out); return true; }
    // IconName / IconThemePath 是规范里的第二条取图路：宿主按名字去我们带来的目录里找，
    // 好处是它想换色／换尺寸时有余地（我们给的像素它只能原样贴）。
    // 找不到主题目录就老实发空串——发一个指不到东西的名字比不发更糟。
    if (!strcmp(name, "IconName")) {
        AppendStr(out, g_icon_theme.empty() ? "" : "agent-ping");
        return true;
    }
    if (!strcmp(name, "IconThemePath")) { AppendStr(out, g_icon_theme.c_str()); return true; }
    if (!strcmp(name, "AttentionIconName") || !strcmp(name, "AttentionAccessibleDesc")
        || !strcmp(name, "AttentionMovieName") || !strcmp(name, "OverlayIconName")) {
        AppendStr(out, "");   // 我们不用这些：图标是编译期从 E4 那张生成的像素
        return true;
    }
    return false;
}

static void ItemProps(DBusMessageIter* arr) {
    for (int i = 0; kItemProps[i]; i++) {
        DBusMessageIter e, v;
        dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
        AppendStr(&e, kItemProps[i]);
        dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, ItemPropSig(kItemProps[i]), &v);
        ReadItemProp(kItemProps[i], &v);
        dbus_message_iter_close_container(&e, &v);
        dbus_message_iter_close_container(arr, &e);
    }
}

// ── dbusmenu：右键那两项（收／放、退出）──────────────────────────────────────

/// 往 parent 里铺一个 (ia{sv}av) 结构体。parent 可以是三种位置：
/// 回包的参数位（GetLayout 的第二个出参）、variant 里（每个子项）、或数组元素位。
/// children 是 **av**＝「variant 数组」，每个 variant 装一个结构体；
/// 之前在这里少开了一层 variant、又多变了一层数组，自建宿主照跟着错所以测不出来，
/// 参考实现的回包形状是：(0, {'children-display':'submenu'}, [variant(1,{...},[]), variant(2,{...},[])])。
static void AppendLeaf(DBusMessageIter* parent, int id, const char* label) {
    DBusMessageIter st, props, kids;
    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, nullptr, &st);
    dbus_int32_t i32 = id;
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &i32);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &props);
    SvStr(&props, "label", label);
    SvBool(&props, "enabled", true);
    SvBool(&props, "visible", true);
    dbus_message_iter_close_container(&st, &props);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &kids);
    dbus_message_iter_close_container(&st, &kids);   // 叶子：没有下一级
    dbus_message_iter_close_container(parent, &st);
}

static void AppendRoot(DBusMessageIter* parent) {
    DBusMessageIter st, props, kids, var;
    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, nullptr, &st);
    dbus_int32_t zero = 0;
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &zero);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &props);
    SvStr(&props, "children-display", "submenu");
    dbus_message_iter_close_container(&st, &props);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &kids);
    const int ids[2] = {1, 2};
    const char* labels[2] = {g_showhide.c_str(), g_quit.c_str()};
    for (int i = 0; i < 2; i++) {
        dbus_message_iter_open_container(&kids, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
        AppendLeaf(&var, ids[i], labels[i]);
        dbus_message_iter_close_container(&kids, &var);
    }
    dbus_message_iter_close_container(&st, &kids);
    dbus_message_iter_close_container(parent, &st);
}

/// GetGroupProperties 的出参是 a(ia{sv})（注意：这里**没有**孩子位），
/// 宿主批量刷属性时走它；不回＝菜单打开但每一项都是空的。
static void AppendGroupItem(DBusMessageIter* arr, int id, const char* label) {
    DBusMessageIter st, props;
    dbus_message_iter_open_container(arr, DBUS_TYPE_STRUCT, nullptr, &st);
    dbus_int32_t i32 = id;
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &i32);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &props);
    if (id == 0) SvStr(&props, "children-display", "submenu");
    else SvStr(&props, "label", label ? label : "");
    SvBool(&props, "enabled", true);
    SvBool(&props, "visible", true);
    dbus_message_iter_close_container(&st, &props);
    dbus_message_iter_close_container(arr, &st);
}

// 宿主是先读 introspect 再决定读哪些属性的（KDE 的代理尤其依赖这份名单），
// 所以这张 XML 必须和 ItemPropSig/ReadItemProp 一一对上：Menu 写 type="o" 不是 "s"，
// GetLayout 只有两个出参。名单形状照抄参考实现（它的 XML 是 GDBus 从真代码生成的），
// 再补上规范要求、Ayatana 没实现的那几项（ToolTip/IconPixmap/ItemIsMenu/WindowId）。
static const char* kIntrospectItem =
    "<node>"
    "<interface name=\"org.freedesktop.DBus.Properties\">"
    "<method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"out\"/></method>"
    "<method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"a{sv}\" direction=\"out\"/></method></interface>"
    "<interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface>"
    "<interface name=\"org.kde.StatusNotifierItem\">"
    "<method name=\"Activate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"SecondaryActivate\"><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"ContextMenu\"><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"Scroll\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
    "<signal name=\"NewTitle\"/><signal name=\"NewIcon\"/><signal name=\"NewAttentionIcon\"/>"
    "<signal name=\"NewOverlayIcon\"/><signal name=\"NewToolTip\"/>"
    "<signal name=\"NewStatus\"><arg type=\"s\"/></signal>"
    "<signal name=\"NewLabel\"><arg type=\"s\"/><arg type=\"a{sv}\"/></signal>"
    "<signal name=\"XAyatanaNewLabel\"><arg type=\"s\"/><arg type=\"s\"/></signal>"
    "<property name=\"Category\" type=\"s\" access=\"read\"/>"
    "<property name=\"Id\" type=\"s\" access=\"read\"/>"
    "<property name=\"Title\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"WindowId\" type=\"u\" access=\"read\"/>"
    "<property name=\"IconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconThemePath\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconAccessibleDesc\" type=\"s\" access=\"read\"/>"
    "<property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"AttentionAccessibleDesc\" type=\"s\" access=\"read\"/>"
    "<property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>"
    "<property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
    "<property name=\"Menu\" type=\"o\" access=\"read\"/>"
    "<property name=\"Label\" type=\"s\" access=\"read\"/>"
    "<property name=\"XAyatanaLabel\" type=\"s\" access=\"read\"/>"
    "<property name=\"XAyatanaLabelGuide\" type=\"s\" access=\"read\"/>"
    "<property name=\"XAyatanaOrderingIndex\" type=\"u\" access=\"read\"/>"
    "<property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
    "</interface>"
    "<node name=\"Menu\"/>"
    "</node>";
static const char* kIntrospectMenu =
    "<node>"
    "<interface name=\"org.freedesktop.DBus.Properties\">"
    "<method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"out\"/></method>"
    "<method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"a{sv}\" direction=\"out\"/></method></interface>"
    "<interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface>"
    "<interface name=\"com.canonical.dbusmenu\">"
    // 出参两个：u + (ia{sv}av)。原来这里多写了一个 s 出参（把菜单路径也回出去），
    // 是照着自己写的宿主猜的形状；参考实现的同名方法没有那一段。
    "<method name=\"GetLayout\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"u\" name=\"revision\" direction=\"out\"/>"
    "<arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/></method>"
    "<method name=\"GetGroupProperties\"><arg type=\"ai\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"a(ia{sv})\" direction=\"out\"/></method>"
    "<method name=\"GetProperty\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"out\"/></method>"
    "<method name=\"Event\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>"
    "<method name=\"EventGroup\"><arg type=\"a(isvu)\" direction=\"in\"/>"
    "<arg type=\"ai\" direction=\"out\"/></method>"
    "<method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"b\" direction=\"out\"/></method>"
    "<method name=\"AboutToShowGroup\"><arg type=\"ai\" direction=\"in\"/>"
    "<arg type=\"ai\" direction=\"out\"/><arg type=\"ai\" direction=\"out\"/></method>"
    "<signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>"
    "<signal name=\"ItemsPropertiesUpdated\"><arg type=\"a(ia{sv})\"/>"
    "<arg type=\"a(ias)\"/></signal>"
    "<signal name=\"ItemActivationRequested\"><arg type=\"i\"/><arg type=\"u\"/></signal>"
    "<property name=\"Version\" type=\"u\" access=\"read\"/>"
    "<property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
    "</interface></node>";

// ── 收消息 ───────────────────────────────────────────────────────────────────
static void RunClick() { if (g_click) g_click(g_ud); }

/// 回一条 D-Bus 错误。宿主拿到错误才会退回去试别的属性名，静默不回包会让它一直等。
static void ReplyError(DBusConnection* c, DBusMessage* m, const char* name, const char* text) {
    DBusMessage* e = dbus_message_new_error(m, name, text);
    if (!e) return;
    dbus_connection_send(c, e, nullptr);
    dbus_message_unref(e);
}

static DBusHandlerResult OnItem(DBusConnection* c, DBusMessage* m, const char* member) {
    if (!strcmp(member, "Introspect")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it;
        dbus_message_iter_init_append(r, &it);
        AppendStr(&it, kIntrospectItem);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "Get") || !strcmp(member, "GetAll")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it, v;
        dbus_message_iter_init_append(r, &it);
        if (!strcmp(member, "Get")) {
            DBusMessageIter in;
            dbus_message_iter_init(m, &in);
            const char* iface = nullptr;
            dbus_message_iter_get_basic(&in, &iface);
            dbus_message_iter_next(&in);
            const char* name = nullptr;
            dbus_message_iter_get_basic(&in, &name);
            if (strcmp(iface, kIface)) {
                dbus_message_unref(r);
                ReplyError(c, m, "org.freedesktop.DBus.Error.UnknownInterface",
                           "这个对象只实现 org.kde.StatusNotifierItem");
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            // 类型串和值同源（ItemPropSig），单条 Get 不可能和 GetAll 给出不一样形状
            const char* sig = ItemPropSig(name);
            dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, sig, &v);
            if (!ReadItemProp(name, &v)) {
                dbus_message_iter_close_container(&it, &v);
                dbus_message_unref(r);
                ReplyError(c, m, "org.freedesktop.DBus.Error.UnknownProperty",
                           "没有这个属性");
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            dbus_message_iter_close_container(&it, &v);
        } else {
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &v);
            ItemProps(&v);
            dbus_message_iter_close_container(&it, &v);
        }
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "Activate") || !strcmp(member, "SecondaryActivate")) {
        RunClick();
        DBusMessage* r = dbus_message_new_method_return(m);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "ContextMenu") || !strcmp(member, "Scroll")) {
        // ContextMenu＝右键已经自己弹了本地菜单，我们只挂总线菜单，回空即可；
        // Scroll 我们没有可滚的内容。两条都必须回包，不回会让宿主以为项挂了。
        DBusMessage* r = dbus_message_new_method_return(m);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/// dbusmenu 的属性类型表（Version 规范是 u，不是 i——参考实现 introspect 出 type="u"）。
static const char* MenuPropSig(const char* name) {
    if (!strcmp(name, "Version")) return "u";
    if (!strcmp(name, "IconThemePath")) return "as";
    return "s";
}

static bool ReadMenuProp(const char* name, DBusMessageIter* out) {
    if (!strcmp(name, "Version")) {
        dbus_uint32_t v = 3;
        dbus_message_iter_append_basic(out, DBUS_TYPE_UINT32, &v);
        return true;
    }
    if (!strcmp(name, "Status")) { AppendStr(out, "normal"); return true; }
    if (!strcmp(name, "TextDirection")) { AppendStr(out, "ltr"); return true; }
    if (!strcmp(name, "IconThemePath")) {
        DBusMessageIter a;
        dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "s", &a);
        dbus_message_iter_close_container(out, &a);
        return true;
    }
    return false;
}

/// 菜单对象也要接 org.freedesktop.DBus.Properties：宿主刷菜单前先读 Version，
/// 这里不回（之前只认我自己发明的 PropertiesGet）会让部分宿主直接放弃拉布局。
static DBusHandlerResult OnMenuProps(DBusConnection* c, DBusMessage* m, const char* member) {
    DBusMessage* r = dbus_message_new_method_return(m);
    DBusMessageIter it, v;
    dbus_message_iter_init_append(r, &it);
    if (!strcmp(member, "Get")) {
        DBusMessageIter in;
        dbus_message_iter_init(m, &in);
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&in, &iface);
        dbus_message_iter_next(&in);
        const char* name = nullptr;
        dbus_message_iter_get_basic(&in, &name);
        if (strcmp(iface, kMenuIface)) {
            dbus_message_unref(r);
            ReplyError(c, m, "org.freedesktop.DBus.Error.UnknownInterface",
                       "这个对象只实现 com.canonical.dbusmenu");
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, MenuPropSig(name), &v);
        if (!ReadMenuProp(name, &v)) {
            dbus_message_iter_close_container(&it, &v);
            dbus_message_unref(r);
            ReplyError(c, m, "org.freedesktop.DBus.Error.UnknownProperty", "菜单没有这个属性");
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        dbus_message_iter_close_container(&it, &v);
    } else {
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &v);
        SvStr(&v, "Status", "normal");
        SvStr(&v, "TextDirection", "ltr");
        DBusMessageIter arr;
        dbus_message_iter_open_container(&v, DBUS_TYPE_DICT_ENTRY, nullptr, &arr);
        AppendStr(&arr, "Version");
        DBusMessageIter iv;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_VARIANT, "u", &iv);
        dbus_uint32_t three = 3;
        dbus_message_iter_append_basic(&iv, DBUS_TYPE_UINT32, &three);
        dbus_message_iter_close_container(&arr, &iv);
        dbus_message_iter_close_container(&v, &arr);
        {
            DBusMessageIter e, va, a;
            dbus_message_iter_open_container(&v, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
            AppendStr(&e, "IconThemePath");
            dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "as", &va);
            dbus_message_iter_open_container(&va, DBUS_TYPE_ARRAY, "s", &a);
            dbus_message_iter_close_container(&va, &a);
            dbus_message_iter_close_container(&e, &va);
            dbus_message_iter_close_container(&v, &e);
        }
        dbus_message_iter_close_container(&it, &v);
    }
    dbus_connection_send(c, r, nullptr);
    dbus_message_unref(r);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult OnMenu(DBusConnection* c, DBusMessage* m, const char* member) {
    if (!strcmp(member, "Get") || !strcmp(member, "GetAll")) return OnMenuProps(c, m, member);
    if (!strcmp(member, "Introspect")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it;
        dbus_message_iter_init_append(r, &it);
        AppendStr(&it, kIntrospectMenu);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "GetLayout")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it;
        dbus_message_iter_init_append(r, &it);
        dbus_uint32_t rev = 1;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &rev);
        // 出参就两个：u + (ia{sv}av)，布局结构体**直接铺在回包里，不套 variant**，
        // 也没有第三个「路径」出参——那是我照着自己写的宿主猜出来的，参考实现里没有。
        AppendRoot(&it);   // 第二个出参就是那个结构体本体
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "GetGroupProperties")) {
        // 宿主（尤其 KDE 的 dbusmenu 代理）会在 AboutToShow 后走这条批量取属性，
        // 不给回包它就整张菜单空白，而这一步在自建宿主上根本不会被调到。
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(r, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
        AppendGroupItem(&arr, 0, nullptr);
        AppendGroupItem(&arr, 1, g_showhide.c_str());
        AppendGroupItem(&arr, 2, g_quit.c_str());
        dbus_message_iter_close_container(&it, &arr);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "GetProperty")) {
        DBusMessageIter in;
        dbus_message_iter_init(m, &in);
        dbus_int32_t id = 0;
        dbus_message_iter_get_basic(&in, &id);
        dbus_message_iter_next(&in);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&in, &key);
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it, v;
        dbus_message_iter_init_append(r, &it);
        const char* sig = !strcmp(key, "Version") ? "u"
                        : (!strcmp(key, "children-display") ? "s" : "s");
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, sig, &v);
        if (!strcmp(key, "Version")) {
            dbus_uint32_t u3 = 3;
            dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &u3);
        } else if (!strcmp(key, "children-display")) {
            AppendStr(&v, id == 0 ? "submenu" : "");
        } else if (!strcmp(key, "label")) {
            AppendStr(&v, id == 1 ? g_showhide.c_str() : (id == 2 ? g_quit.c_str() : ""));
        } else {
            AppendStr(&v, "");
        }
        dbus_message_iter_close_container(&it, &v);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "Event")) {
        DBusMessageIter in;
        dbus_message_iter_init(m, &in);
        dbus_int32_t id = 0;
        dbus_message_iter_get_basic(&in, &id);
        dbus_message_iter_next(&in);
        const char* ev = nullptr;
        dbus_message_iter_get_basic(&in, &ev);
        if (ev && !strcmp(ev, "clicked")) {
            if (id == 2) {
                printf("[状态项] 菜单：退出\n");
                fflush(stdout);
                // 先把回包发出去再收摊：直接 _exit 会让宿主收到
                // NoReply「Message recipient disconnected without replying」，
                // 一个正常的「他点了退出」在总线上长得和「项崩了」一模一样。
                DBusMessage* r = dbus_message_new_method_return(m);
                dbus_connection_send(c, r, nullptr);
                dbus_connection_flush(c);
                dbus_message_unref(r);
                _exit(0);            // 菜单里点退出＝他亲口说的收摊，不拖到下一帧
            } else if (id == 1) {
                RunClick();
            }
        }
        DBusMessage* r = dbus_message_new_method_return(m);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!strcmp(member, "AboutToShow")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        DBusMessageIter it;
        dbus_message_iter_init_append(r, &it);
        dbus_bool_t t = TRUE;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &t);
        dbus_connection_send(c, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult Filter(DBusConnection* c, DBusMessage* m, void*) {
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_CALL) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    const char* path = dbus_message_get_path(m);
    const char* member = dbus_message_get_member(m);
    if (!path || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (!strcmp(path, kItemPath)) return OnItem(c, m, member);
    if (!strcmp(path, kMenuPath)) return OnMenu(c, m, member);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// ── 发信号（数字/说明变了要通知宿主，不然它一直显示启动那一刻的）──────────────
// 规范原文：NewToolTip 是 VOID org.freedesktop.StatusNotifierItem.NewToolTip ();
// —— 它是「你重新读一次 ToolTip 属性」的提示，不是把结构体推过去。
// 之前这里发的是整个 (sa(iiay)ss)，自建宿主按我自己的误解写的接收端，所以两边一起错。
static void EmitVoid(const char* member) {
    if (!g_c) return;
    DBusMessage* s = dbus_message_new_signal(kItemPath, kIface, member);
    if (!s) return;
    dbus_connection_send(g_c, s, nullptr);
    dbus_message_unref(s);
}

/// 数字变了要同时发两家：KDE 的 NewLabel(s, a{sv}) 和 Ayatana 的 XAyatanaNewLabel(s, s)。
/// 参考实现只有后者，规范正文两者都没有——宿主读哪一条按桌面分支决定，只发一条就是赌桌面。
static void EmitLabel() {
    if (!g_c) return;
    {
        DBusMessage* s = dbus_message_new_signal(kItemPath, kIface, "NewLabel");
        if (s) {
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(s, &it);
            AppendStr(&it, g_label.c_str());
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
            dbus_message_iter_close_container(&it, &arr);
            dbus_connection_send(g_c, s, nullptr);
            dbus_message_unref(s);
        }
    }
    {
        DBusMessage* s = dbus_message_new_signal(kItemPath, kIface, "XAyatanaNewLabel");
        if (s) {
            DBusMessageIter it;
            dbus_message_iter_init_append(s, &it);
            AppendStr(&it, g_label.c_str());
            AppendStr(&it, g_guide.c_str());
            dbus_connection_send(g_c, s, nullptr);
            dbus_message_unref(s);
        }
    }
}

static bool TryRegister() {
    DBusError err;
    dbus_error_init(&err);
    if (!dbus_bus_name_has_owner(g_c, kWatcher, &err)) {
        if (g_host_seen) {
            printf("[状态项] 指示器宿主不见了（%s 不在这条总线上）——每 2 秒重试\n", kWatcher);
            fflush(stdout);
            g_host_seen = false;
        }
        dbus_error_free(&err);
        return false;
    }
    dbus_error_free(&err);
    DBusMessage* m = dbus_message_new_method_call(kWatcher, kWatcherPath, kWatcher,
                                                  "RegisterStatusNotifierItem");
    if (!m) return false;
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    AppendStr(&it, g_bus.c_str());
    DBusMessage* r = dbus_connection_send_with_reply_and_block(g_c, m, 200, &err);
    dbus_message_unref(m);
    if (!r) {
        printf("[状态项] 注册被宿主拒了：%s\n", err.name ? err.name : "无回音");
        fflush(stdout);
        dbus_error_free(&err);
        return false;
    }
    dbus_message_unref(r);
    dbus_error_free(&err);
    if (!g_host_seen) {
        printf("[状态项] 宿主已收下条目：总线名 %s｜对象 %s｜菜单 %s\n", g_bus.c_str(), kItemPath,
               kMenuPath);
        fflush(stdout);
        g_host_seen = true;
    }
    return true;
}

extern "C" int sb_show(const char* icon_path, const char* tooltip, const char* show_hide_label,
                       const char* quit_label, sb_click_fn on_click, void* ud) {
    (void)icon_path;   // 图不是读文件，是编译期从同一张 E4 生成的 sb_icon_argb.h
    g_click = on_click;
    g_ud = ud;
    if (tooltip) g_tip = tooltip;   // guide 不在这里跟悬停说明共用一份，见 sb_set_label
    if (show_hide_label) g_showhide = show_hide_label;
    if (quit_label) g_quit = quit_label;
    // 取图路：默认「像素 + 名字」两条都发，宿主爱用哪条用哪条。
    // AGENTPING_SNI_ICON=name 只发名字（对照实验：证明主题目录真能被读到），
    // =pixmap 只发像素（回到改动前的行为）。取证脚本按这三档各跑一次。
    const char* mode = getenv("AGENTPING_SNI_ICON");
    if (mode && !strcmp(mode, "name")) g_no_pixmap = true;
    if (!mode || strcmp(mode, "pixmap")) g_icon_theme = FindThemeDir();
    std::string route;
    if (!g_no_pixmap) route = "IconPixmap";
    if (!g_icon_theme.empty())
        route += (route.empty() ? "" : " + ") + ("IconName=agent-ping @" + g_icon_theme);
    if (route.empty()) route = "无（既不发像素也找不到主题目录）";
    printf("[状态项] 取图路：%s\n", route.c_str());
    fflush(stdout);

    DBusError err;
    dbus_error_init(&err);
    g_c = dbus_bus_get(DBUS_BUS_SESSION, &err);   // 引用计数连接，不关（退出时总线自己收）
    if (!g_c) {
        printf("[状态项] 没有会话总线：%s——这一档只有窗口，没有托盘\n",
               err.message ? err.message : "原因未回");
        fflush(stdout);
        dbus_error_free(&err);
        return 1;
    }
    dbus_error_free(&err);
    dbus_connection_set_exit_on_disconnect(g_c, FALSE);
    // 没有对象路径分发的注册函数可用（libdbus 只提供 filter），所以两条对象路径在
    // Filter 里按 path 自己分。副作用：本进程在这条连接上不实现任何其它对象。
    dbus_connection_add_filter(g_c, Filter, nullptr, nullptr);
    char name[64];
    snprintf(name, sizeof name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
    g_bus = name;
    int flags = 0;
    if (dbus_bus_request_name(g_c, name, flags, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        printf("[状态项] 抢不到总线名 %s：%s\n", name, err.name ? err.name : "已被占用");
        fflush(stdout);
        dbus_error_free(&err);
        return 1;
    }
    dbus_error_free(&err);
    printf("[状态项] SNI 已挂上：总线名 %s（对象 %s，菜单 %s）\n", name, kItemPath, kMenuPath);
    fflush(stdout);
    TryRegister();
    return 0;
}

extern "C" void sb_set_label(const char* utf8) {
    const std::string want = utf8 ? utf8 : "";
    if (want == g_label) return;
    g_label = want;
    // XAyatanaLabelGuide 不是「第二行说明」，是**占位宽度**：本机装的
    // AyatanaAppIndicator3-0.1.gir 里 app_indicator_set_label 的 @guide 原文写着
    // "A guide to size the label correctly."（/usr/share/gir-1.0/ 实测读到）。
    // 之前把整句悬停说明塞进 guide，宿主会照那句长文本给标签列预留宽度。
    // 人话说明归 ToolTip（规范里那条才是悬停显示的），guide 只跟着「见过的最宽读数」长。
    if (Utf8Cells(want) >= Utf8Cells(g_guide)) g_guide = want;
    EmitLabel();
    printf("[状态项] 数字变了：Label＝「%s」占位＝「%s」\n", g_label.c_str(), g_guide.c_str());
    fflush(stdout);
}

extern "C" void sb_set_tooltip(const char* utf8) {
    const std::string want = utf8 ? utf8 : "";
    if (want == g_tip) return;
    g_tip = want;
    EmitVoid("NewToolTip");   // 规范里这条无参，宿主收到后自己重读属性
    printf("[状态项] 悬停说明变了：ToolTip＝「%s」\n", g_tip.c_str());
    fflush(stdout);
}

extern "C" int sb_capture(const char* png_path) {
    // 这一档给不了 macOS 那种「按钮自绘」：SNI 的图是我们把像素交给宿主、由宿主（KDE 面板 /
    // GNOME 扩展）画出来的，进程内没有那颗按钮的 NSView 可以 cacheDisplay。
    // 所以 Linux 的视觉证据走对端：Tools/sni_host.py 从总线上把 IconPixmap 读回来解码成 PNG，
    // 那是真正传给宿主的那份像素——但它仍不等于宿主最终画在面板上的样子（宿主可以按自己的
    // 尺寸缩放、甚至可以改用 IconName 指向的主题图）。
    (void)png_path;
    printf("[状态项] 拍不了：SNI 的按钮由宿主进程绘制，本进程没有可自绘的视图。"
           "改由对端取证（Tools/sni_host.py 读回 IconPixmap 解码）\n");
    fflush(stdout);
    return 1;
}

extern "C" int sb_float_window(void) {
    // macOS 那套「系统按 alpha 蒙版生成阴影」在 X11 上没有对应概念：阴影由合成器（KWin /
    // picom / GNOME 的 mutter）自己决定，客户端只能请求 _NET_WM_STATE_ABOVE 这一项，
    // 而 GLFW 已经在我们设 GLFW_FLOATING 时替你发了那条原子消息。这里不冒充能读回采纳结果。
    printf("[浮层] X11：GLFW_FLOATING 已请求 _NET_WM_STATE_ABOVE；是否生效由合成器决定，"
           "本机无桌面合成器，读不回——不冒充 macOS 那种 layer= 实测\n");
    fflush(stdout);
    return -1;
}

extern "C" void sb_pump(void) {
    if (!g_c) return;
    // 共享连接的官方读法：read_write_dispatch 一次把收、派、回包全走完（0＝不等待）。
    dbus_connection_read_write_dispatch(g_c, 0);
    static int tick = 0;
    if (!g_host_seen && ++tick >= 120) {   // 约 2 秒（60 帧）再问一次有没有宿主
        tick = 0;
        TryRegister();
    }
}

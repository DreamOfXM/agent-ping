// X11 这一档的取证探针：灯舱那条带到底点不点得穿，不靠我们自己说「设置了」。
//
// 判据挂在 X 服务器身上：一次真点击送给哪块窗，由每块窗的**输入形状**决定，我们改不了它的算法。
// 所以场景摆成两块窗重叠，往六个点各敲一下，数三块窗各自收到几次：
//
//   带（GLFW，右缘整条高，320x1100 @ 1080,0）  ┌───────────────┐
//   下面那块（Xlib，40,40 起 1300x700）        │  带 独占      │
//        重叠区 = x 1080..1340, y 40..740      ├──── 重叠 ─────┤←带也在这
//                                              │ 下面那块独占  │
//                                              └───────────────┘
//
//   · A 点＝(1200,390) 两块都盖得住 ⇒ 穿透开必须送给下面那块，穿透关必须送给带；
//   · B 点＝(1200,900) 只有带盖得住 ⇒ 穿透开谁都不收（落 root），穿透关带收。
//   两点的读数合起来才排得出「其实是坐标没落进带里」这种假通过——B 点在穿透关时带必须收到，
//   它就是「点击确实落在带上」这一条的对照组。
//
// 顺带量另外三条只能问 X 服务器的读数：
//   · ap_global_cursor：XTest 把指针钉到 (777,555)，读回来必须是它；
//   · ap_work_area：往 root 写一条 _NET_WORKAREA（真 WM 就是这么发布的），读回来必须是那四个数；
//   · ap_mouse_down：XTest 按住不放必须读成真，松开读成假。
//
// 只在 Linux/X11 编译（CMake 查到 x11+xext 才建这个目标，Xtst 只这一支要）。Xvfb 是一个真 X
// 服务器，Shape／XTest／XQueryPointer 都是它实现的协议；但它没有合成器也没有 WM，所以这一支
// 证的是「输入形状与指针读取」，不证「看起来透明」——后者在 macOS 那侧另有活桌面连拍。
// 第三种摆法：`x11_probe --app-under <秒> X11PROBE_MODE=dock|panel`。
// 这一档不自己建带，而是等**真二进制**把带挂上，再往同一片坐标敲——证的是「装上去的那条带
// 挡不挡桌面」，不是「我们这个 shim 能不能改输入形状」。两块窗的栈序是这一档的命门：
// 下面那块必须显式压到栈底，后映射的窗会盖住 App 那条带，点击永远归我们＝假通过。
// 所以 panel 那一列（面板盖住同一坐标）是这一档的反证：探针必须读得出「这次是 App 收到的」。
#include "platform/passthrough.h"
#include "platform/cursor.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XTest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static const int kStripW = 320;                                  // 与 kEdgeW 同形
static const int kUnderX = 40, kUnderY = 40, kUnderW = 1300, kUnderH = 700;

struct Counters {
    int under = 0;   // 下面那块收到的点击
    int strip = 0;   // GLFW 那条带收到的点击
    int root = 0;    // 谁都没盖住 ⇒ 落到 root
};

static void pump(Display* under_dpy, Display* strip_dpy, GLFWwindow* strip, Window uw,
                 Window root, Counters* c) {
    XEvent ev;
    while (under_dpy && XPending(under_dpy)) {
        XNextEvent(under_dpy, &ev);
        if (ev.type != ButtonPress) continue;
        if (ev.xbutton.window == uw) c->under++;
        else if (ev.xbutton.window == root) c->root++;
    }
    while (strip_dpy && XPending(strip_dpy)) {
        XNextEvent(strip_dpy, &ev);
        if (ev.type == ButtonPress) c->strip++;
    }
}

// 敲一下：先把指针挪过去（XTest 的事件按指针当下所在派发），再按下、松开。
static void tap(Display* dpy, int x, int y) {
    XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
    XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
    XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
    XFlush(dpy);
}

// 每块窗各自数：下面那块（uw）／root。App 那块收的那一拍我们收不到，只能由「两块都没报回来」
// 反推——所以每一拍都要检查总数不超过 1，不然「App 收到」是数出来的假数。
struct WinCounts {
    Window uw;
    Window root;
    int under = 0, root_n = 0;
};

static void pump_all(Display* dpy, WinCounts* c) {
    XEvent ev;
    while (XPending(dpy)) {
        XNextEvent(dpy, &ev);
        if (ev.type != ButtonPress) continue;
        if (ev.xbutton.window == c->uw) c->under++;
        else if (ev.xbutton.window == c->root) c->root_n++;
    }
}

// 把 root 上每一块可见窗的 xid／名字／几何都印出来，并挑出「不是我们这块」的第一座当 App 窗
// ——「这次点击到底落进了哪块窗」要能在读数里点名。注意这里**不**往别人的窗上选事件：
// 对 App 那块窗 XSelectInput 会被服务器判成 BadAccess（同一类事件只许一个客户端选，
// GLFW 已经选了），所以探针改成「数自己这块＋root，剩下那一拍只能是 App 收的」。
struct Foreign {
    bool found = false;
    Window id = 0;
    int x = 0, y = 0, w = 0, h = 0;
    char name[64] = {0};
};

static void scan_roots(Display* dpy, Window root, Window uw, Foreign* out) {
    Window r, parent, *kids = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(dpy, root, &r, &parent, &kids, &n)) return;
    printf("[窗口表] root 上 %u 块子窗：\n", n);
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, kids[i], &a)) continue;
        if (a.map_state != IsViewable) continue;
        char* name = nullptr;
        if (!(XFetchName(dpy, kids[i], &name) && name && *name)) name = nullptr;
        const bool mine = (kids[i] == uw);
        printf("           xid=%lu %dx%d@(%d,%d) %s「%s」\n", (unsigned long)kids[i],
               a.width, a.height, a.x, a.y, mine ? "下面那块" : "别人的窗",
               name ? name : "（无名）");
        if (!mine && !out->found) {
            out->found = true;
            out->id = kids[i];
            out->x = a.x; out->y = a.y; out->w = a.width; out->h = a.height;
            snprintf(out->name, sizeof out->name, "%s", name ? name : "（无名）");
        }
        XFree(name);
    }
    fflush(stdout);
    if (kids) XFree(kids);
}

static int xerr(Display*, XErrorEvent* e) {
    printf("[x11] 服务器报错 request=%d code=%d（不中断，继续数落点）\n", e->request_code, e->error_code);
    fflush(stdout);
    return 0;
}

static int run_against_app(int wait_s) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { printf("[x11] 开不了 X 显示（要 DISPLAY，容器里用 xvfb-run）\n"); return 2; }
    XSetErrorHandler(xerr);
    const Window root = DefaultRootWindow(dpy);
    const int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    const int sh = DisplayHeight(dpy, DefaultScreen(dpy));
    const bool dock = !getenv("X11PROBE_MODE") || strcmp(getenv("X11PROBE_MODE"), "dock") == 0;

    XSetWindowAttributes sa;
    sa.event_mask = ButtonPressMask;
    sa.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    const Window uw = XCreateWindow(dpy, root, 0, 0, sw, sh, 0, CopyFromParent, InputOutput,
                                    CopyFromParent, CWEventMask | CWBackPixel, &sa);
    XMapRaised(dpy, uw);
    XLowerWindow(dpy, uw);              // 栈底：让真 App 那条带压在它上面
    XSelectInput(dpy, root, ButtonPressMask);
    XSync(dpy, False);

    printf("[x11] 屏幕 %dx%d｜下面那块＝整屏 xid=%lu（已压到栈底）｜这一轮测的是%s态\n",
           sw, sh, (unsigned long)uw, dock ? "灯舱（该穿）" : "面板（该挡）");
    fflush(stdout);
    printf("[x11] 等 %d 秒让真二进制把窗挂上……\n", wait_s);
    fflush(stdout);
    for (int i = 0; i < wait_s; i++) sleep(1);
    Foreign app;
    scan_roots(dpy, root, uw, &app);
    if (!app.found) { printf("[x11] root 上没有别人的窗：App 没起来，这一轮不算\n"); return 2; }
    if (app.x < 120) { printf("[x11] App 那块窗贴着左边缘，取不出带外对照点，这一轮不算\n"); return 2; }
    printf("[x11] App 那块窗＝「%s」%dx%d@(%d,%d)\n", app.name, app.w, app.h, app.x, app.y);
    fflush(stdout);

    // 三个点都从 App 那块窗的几何推出来：P1/P2 钉在它内部的上下两端（灯舱态那两头是空白带，
    // 舱体在中间高度），P3 落在它左边的桌面。面板态下 P1/P2 都在这块窗里，正是反证要的落点。
    const int p[3][2] = {{app.x + app.w / 2, app.y + 20},
                         {app.x + app.w / 2, app.y + app.h - 20},
                         {app.x / 2, app.y + app.h / 2}};
    const char* pname[3] = {"P1 窗内·上端", "P2 窗内·下端", "P3 窗外·桌面"};
    int bad = 0;
    WinCounts base{uw, root};
    pump_all(dpy, &base);
    for (int i = 0; i < 3; i++) {
        WinCounts c{uw, root};
        tap(dpy, p[i][0], p[i][1]);
        XSync(dpy, False);
        for (int k = 0; k < 8; k++) { pump_all(dpy, &c); usleep(30000); }
        const int got = c.under + c.root_n;
        const int other = got == 0 ? 1 : 0;      // 一块窗都没报回来 ⇒ 被 App 那块吞了
        const bool want_app = !dock && (i == 0 || i == 1);
        const int u = want_app ? 0 : 1;   // 不指望 App 吞的落点，都必须由下面那块收到
        const int o = want_app ? 1 : 0;
        const int r = 0;                  // 三个点都在整屏那块或 App 那块里，root 一次都不该收到
        const bool ok = (c.under == u && other == o && c.root_n == r && got <= 1);
        if (!ok) bad++;
        printf("[落点] %s (%d,%d) → 下面那块 %d / App %d / root %d｜判据 %d/%d/%d ⇒ %s\n",
               pname[i], p[i][0], p[i][1], c.under, other, c.root_n, u, o, r,
               ok ? "对得上" : "对不上");
        fflush(stdout);
    }
    printf("[x11] 判据：灯舱态三点全穿（下面那块各收到 1）；面板态 P1/P2 必须被 App 吞掉、"
           "P3 仍穿——两列读数不同，这一档才证得出「是那条带的形状在起作用」。\n");
    printf("[x11] 不合判据的项＝%d\n", bad);
    XCloseDisplay(dpy);
    return bad ? 1 : 0;

}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--app-under"))
        return run_against_app(argc > 2 ? atoi(argv[2]) : 8);
    if (!XInitThreads()) { printf("[x11] XInitThreads 失败\n"); return 2; }
    Display* under = XOpenDisplay(nullptr);
    if (!under) { printf("[x11] 开不了 X 显示（要 DISPLAY，容器里用 xvfb-run）\n"); return 2; }
    const Window root = DefaultRootWindow(under);
    const int sw = DisplayWidth(under, DefaultScreen(under));
    const int sh = DisplayHeight(under, DefaultScreen(under));

    int shape_event = 0, shape_err = 0;
    if (!XShapeQueryExtension(under, &shape_event, &shape_err)) {
        printf("[x11] 这台服务器没有 Shape 扩展，测不了穿透\n");
        return 2;
    }
    // root 也选上 ButtonPress：两个点都落空的哪一拍要能数出来，不然读成「谁都没收到」
    // 和「事件根本没派发」分不清。
    XSelectInput(under, root, ButtonPressMask);

    XSetWindowAttributes sa;
    sa.event_mask = ButtonPressMask;
    sa.background_pixel = BlackPixel(under, DefaultScreen(under));
    const Window uw = XCreateWindow(under, root, kUnderX, kUnderY, kUnderW, kUnderH, 0,
                                    CopyFromParent, InputOutput, CopyFromParent,
                                    CWEventMask | CWBackPixel, &sa);
    XMapRaised(under, uw);
    XSync(under, False);

    if (!glfwInit()) { printf("[x11] glfwInit 失败\n"); return 2; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* strip = glfwCreateWindow(kStripW, sh, "agent-ping-x11-probe", nullptr, nullptr);
    if (!strip) { printf("[x11] 建不出 GLFW 窗（没 GL？）\n"); return 2; }
    Display* sdpy = glfwGetX11Display();
    const Window xw = glfwGetX11Window(strip);
    glfwSetWindowPos(strip, sw - kStripW, 0);
    glfwShowWindow(strip);
    XWindowAttributes wa;
    XGetWindowAttributes(sdpy, xw, &wa);
    XSelectInput(sdpy, xw, wa.your_event_mask | ButtonPressMask);   // 补上，GLFW 默认不选按钮
    XRaiseWindow(sdpy, xw);                                          // 带必须压在下面那块之上
    XSync(sdpy, False);
    XSync(under, False);

    char reason[96] = {0};
    printf("[x11] 屏幕 %dx%d｜带＝%dx%d@(%d,0) xid=%lu｜下面那块＝(%d,%d,%d,%d) xid=%lu\n",
           sw, sh, kStripW, sh, sw - kStripW, (unsigned long)xw,
           kUnderX, kUnderY, kUnderW, kUnderH, (unsigned long)uw);
    printf("[x11] 这条路＝%s｜支持问＝%d（%s）\n", ap_passthrough_backend(),
           ap_mouse_passthrough_supported(strip, reason, sizeof reason), reason);

    const int ax = 1200, ay = 390;            // A：两块重叠处
    const int bx = 1200, by = 900;            // B：只有带盖得住
    printf("[x11] A 点=(%d,%d) 重叠｜B 点=(%d,%d) 只在带内（带 x 范围 %d..%d，下面那块 y 到 %d）\n",
           ax, ay, bx, by, sw - kStripW, sw - 1, kUnderY + kUnderH - 1);
    fflush(stdout);

    struct State { const char* name; int on; } states[] = {{"穿透开", 1}, {"穿透关", 0}, {"再开", 1}};
    int bad = 0;
    for (auto& s : states) {
        char rr[96] = {0};
        const int got = ap_set_mouse_passthrough(strip, s.on, rr, sizeof rr);
        XSync(sdpy, False);
        XSync(under, False);
        Counters c;
        pump(under, sdpy, strip, uw, root, &c);          // 清掉切换那一拍攒下的事件
        // A 点
        tap(under, ax, ay);
        XSync(under, False);
        XSync(sdpy, False);        // 这条往返保证服务器把那一拍已经排进带那个客户端的缓冲
        for (int i = 0; i < 6; i++) pump(under, sdpy, strip, uw, root, &c);
        // B 点（单独数，别和 A 混在一个数里）
        Counters d;
        tap(under, bx, by);
        XSync(under, False);
        XSync(sdpy, False);
        for (int i = 0; i < 6; i++) pump(under, sdpy, strip, uw, root, &d);

        const int a_under = s.on ? 1 : 0, a_strip = s.on ? 0 : 1, a_root = 0;   // A 点两块都盖得住
        const int b_strip = s.on ? 0 : 1, b_root = s.on ? 1 : 0;
        const bool okA = (c.under == a_under && c.strip == a_strip && c.root == a_root);
        const bool okB = (d.strip == b_strip && d.root == b_root && d.under == 0);
        if (got < 0 || !okA || !okB) bad++;
        printf("[穿透] %s（返回 %d：%s）\n", s.name, got, rr);
        printf("        A 重叠点 → 下面那块 %d / 带 %d / root %d｜判据 %d/%d/%d ⇒ %s\n",
               c.under, c.strip, c.root, a_under, a_strip, a_root, okA ? "对得上" : "对不上");
        printf("        B 独占点 → 下面那块 %d / 带 %d / root %d｜判据 %d/%d/%d ⇒ %s\n",
               d.under, d.strip, d.root, 0, b_strip, b_root, okB ? "对得上" : "对不上");
        fflush(stdout);
    }

    // 指针停在带内、穿透开着时，ap_global_cursor 仍要读得到——灯舱靠这个数决定收不收摊。
    {
        char rr[96] = {0};
        ap_set_mouse_passthrough(strip, 1, rr, sizeof rr);
        XTestFakeMotionEvent(under, -1, ax, ay, CurrentTime);
        XFlush(under);
        XSync(under, False);
        double gx = -1, gy = -1;
        const bool okc = ap_global_cursor(&gx, &gy);
        const bool in_strip = okc && gx >= sw - kStripW && gx < sw;
        if (!(okc && gx == ax && gy == ay && in_strip)) bad++;
        printf("[全局点] 指针钉在带内 (%d,%d) → 读回 (%.0f,%.0f) 取得到=%d 落在带内=%d ⇒ %s\n",
               ax, ay, gx, gy, (int)okc, (int)in_strip,
               (okc && gx == ax && gy == ay && in_strip) ? "对得上" : "对不上");
    }

    {   // 一个不平凡的坐标，免得「一直读到 0,0」蒙对。
        XTestFakeMotionEvent(under, -1, 777, 555, CurrentTime);
        XFlush(under);
        XSync(under, False);
        double gx = -1, gy = -1;
        const bool okc = ap_global_cursor(&gx, &gy);
        if (!(okc && gx == 777 && gy == 555)) bad++;
        printf("[全局点] 钉到 (777,555) → 读回 (%.0f,%.0f) 取得到=%d ⇒ %s\n", gx, gy, (int)okc,
               (okc && gx == 777 && gy == 555) ? "对得上" : "对不上");
    }

    {   // 真 WM 就是这么往 root 发布工作区的；这里自己写一条，读回来必须是那四个数。
        Atom net_workarea = XInternAtom(under, "_NET_WORKAREA", False);
        Atom cardinal = XInternAtom(under, "CARDINAL", False);
        long data[4] = {0, 30, sw, sh - 30 - 80};
        XChangeProperty(under, root, net_workarea, cardinal, 32, PropModeReplace,
                        (unsigned char*)data, 4);
        XSync(under, False);
        double wx = -1, wy = -1, ww = -1, wh = -1;
        const bool okr = ap_work_area(&wx, &wy, &ww, &wh);
        const bool ok = okr && wx == data[0] && wy == data[1] && ww == data[2] && wh == data[3];
        if (!ok) bad++;
        printf("[工作区] 写上 %ldx%ld@(%ld,%ld) → 读回 %.0fx%.0f@(%.0f,%.0f) 取得到=%d ⇒ %s\n",
               data[2], data[3], data[0], data[1], ww, wh, wx, wy, (int)okr,
               ok ? "对得上" : "对不上");
    }

    {   // 穿透开着也要读得到物理按键：灯舱「按住拖」那一档靠它。
        XTestFakeButtonEvent(under, 1, True, CurrentTime);
        XFlush(under);
        XSync(under, False);
        const bool down = ap_mouse_down();
        XTestFakeButtonEvent(under, 1, False, CurrentTime);
        XFlush(under);
        XSync(under, False);
        const bool up = ap_mouse_down();
        if (!(down && !up)) bad++;
        printf("[按键] 按住＝%d 松开＝%d ⇒ %s\n", (int)down, (int)up,
               (down && !up) ? "对得上" : "对不上");
    }

    printf("\n[x11] 判据：A 点穿透开＝只有下面那块收到、穿透关＝只有带收到；B 点穿透开＝谁都不收、"
           "穿透关＝带收到；全局点/工作区/按键都要对得上。\n");
    printf("[x11] 不合判据的项＝%d\n", bad);
    glfwDestroyWindow(strip);
    glfwTerminate();
    XCloseDisplay(under);
    return bad ? 1 : 0;
}

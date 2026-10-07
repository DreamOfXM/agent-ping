/* 平台层唯一入口：状态项（macOS 菜单栏 / Linux 指示器 / Windows 托盘）。
   core 只认这个 C 接口，不许 include 任何平台头。 */
#ifndef AGENTPING_STATUSBAR_H
#define AGENTPING_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

/* 点击状态项时回调（在主线程上）。*ud 原样回传。 */
typedef void (*sb_click_fn)(void* ud);

/* icon_path: 单色 alpha 轮廓图，平台侧按模板图渲染。
   tooltip:   悬停说明，UTF-8。
   show_hide_label / quit_label: 右键菜单两项的文字，UTF-8。
   返回 0 成功；非 0 表示该平台还没实现（core 应继续跑，只是没有状态项）。 */
int sb_show(const char* icon_path, const char* tooltip, const char* show_hide_label,
            const char* quit_label, sb_click_fn on_click, void* ud);

/* 更新状态项右侧的数字/短文字（UTF-8，空串＝不显示）。 */
void sb_set_label(const char* utf8);

/* 更新悬停说明（UTF-8）。状态项上只剩三个数字时，「哪个数字是哪一档」只能靠这里说清，
   所以每轮重扫都要跟着数字一起改——只在启动时给一次的写法会把说明冻在旧数据上。 */
void sb_set_tooltip(const char* utf8);

/* 把状态项那颗按钮自己画进一张 PNG（系统渲染的真像素，不是我们照着排版重画的示意图）。
   为什么需要：菜单栏不在我们的窗口里，命令行截屏在这台机器上只会得到纯黑（没有屏幕录制
   授权），于是「数字到底挂上去没有」只能靠按钮自绘来当视觉证据。
   返回 0 成功；非 0 表示本平台没实现，调用方要把这条缺失说出来。 */
int sb_capture(const char* png_path);

/* 非 Apple 平台需要在主循环里喂一下事件；Apple 上事件由 GLFW 的 pump 带走，空实现。 */
void sb_pump(void);

/* 把 core 那块无边框面板窗口设成系统浮层：圆角由我们自己画，阴影交给系统
   （系统按 alpha 蒙版生成阴影，圆角外不会留一方黑底）。
   同时把窗口对象的真实状态打一行到 stdout——外部截屏在没拿到「屏幕录制」授权的
   环境里只会得到纯黑图，浮层到底成不成立只能由进程自己报。
   返回改了几个窗口；<0 表示该平台没有这个概念。 */
int sb_float_window(void);

#ifdef __cplusplus
}
#endif
#endif

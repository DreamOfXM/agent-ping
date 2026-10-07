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
   quit_label 退出菜单项文字，UTF-8。
   返回 0 成功；非 0 表示该平台还没实现（core 应继续跑，只是没有状态项）。 */
int sb_show(const char* icon_path, const char* tooltip, const char* quit_label,
            sb_click_fn on_click, void* ud);

/* 更新状态项右侧的数字/短文字（UTF-8，空串＝不显示）。 */
void sb_set_label(const char* utf8);

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

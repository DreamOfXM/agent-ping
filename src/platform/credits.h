#pragma once

/// 读别的 App 界面上的额度读数（AX 只读，无鉴权、无网络）。
/// 千问办公的「积分余额」只在其窗口侧栏里现拉现显，本机不落盘——这是唯一能
/// 不出「只读本机落盘」边界的取法：给目标 App 置 AXManualAccessibility（Electron
/// 默认不建无障碍树），把静态文本拖回来，调用方自己按标记挑数字。
/// 目标 App 重启前树可能不完整：读不到就如实返回 false，卡面照常「未接线/上次读取」。
#ifdef __APPLE__
/// 返回该 App 全部窗口里的 AXStaticText，竖线分隔。proc_name＝进程名（如 QwenWorkCN）。
/// bundle_id 先试（更稳），找不到再按进程名兜底。max_texts≤512。
bool ap_ax_texts(const char* proc_name, const char* bundle_id, char* out, int outlen,
                 int max_texts);
#else
inline bool ap_ax_texts(const char*, const char*, char*, int, int) { return false; }
#endif

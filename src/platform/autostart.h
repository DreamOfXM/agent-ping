/* 平台层：登录时启动。
   core 只认这个 C 接口，不许 include 任何平台头（与 open.h / statusbar.h 同规矩）。
   判据只有一条：描述「登录后跑哪个可执行文件」的那个文件在不在 ——
   macOS 是 ~/Library/LaunchAgents/io.github.dreamofxm.agentping.plist，
   Linux 是 ~/.config/autostart/agentping.desktop（XDG Autostart）。
   不往 settings.txt 另存一份开关：文件本身是唯一真源，手删文件开关就跟着关。 */
#ifndef AGENTPING_AUTOSTART_H
#define AGENTPING_AUTOSTART_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1＝已装（返回 0）；0＝没装；<0＝查不到（reason 写明是哪一步失败）。
   where 回填那个文件的实际路径，调用方可以原样显示给用户。 */
int ap_autostart_get(char* where, int out_len);

/* on!=0 写入，on==0 删除。成功 0，失败非 0 且必须在 reason 里写清哪一步、哪个路径。
   reason 一律 UTF-8，out_len 是缓冲区大小（含结尾 0）。 */
int ap_autostart_set(int on, char* reason, int out_len);

/* 登录项跟着「现在跑的这份」走：开关文件在、且里面记的可执行路径与本实例不一致时，
   按本实例路径重写。换安装方式（开发构建 → brew /Applications）后旧条目会把人拉回
   那份没公证的副本，所以文件存在就必须对齐。
   开关文件不存在＝他从没开过这个功能，这里一个字都不写。
   本实例跑在挂载盘/临时目录里（DMG、Gatekeeper 转运、AppImage 的 /tmp/.mount_*）时
   也不写——那是马上要消失的副本，把好好的条目改成死路径反而是回归。
   返回 1＝改写了，0＝不用动（含没开关、读不出、临时副本），<0＝失败且 reason 写明原因。
   reason 除「没开这个功能」那一种（此时缓冲区保持为空）外都写清为什么不动，调用方据此
   给用户一句话，不许把「已经对齐」念成「没装」。 */
int ap_autostart_repair(char* reason, int out_len);

#ifdef __cplusplus
}
#endif
#endif

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

#ifdef __cplusplus
}
#endif
#endif

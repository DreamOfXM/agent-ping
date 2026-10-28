#pragma once
#include <string>
#include <vector>

// CLI 通知 hook 的接线层：写「收件箱写入器」脚本，并把 CLI 自己的配置指过来。
//
// 只用本机文件：读/改 CLI 的 settings.json、往
// macOS ~/Library/Application Support/AgentPing/ 或 Linux ${XDG_DATA_HOME:-~/.local/share}/agentping/
// 写脚本。改配置前先备份，写文件走临时文件 + rename，解析失败就一个字节都不动。
//
// 收件箱契约（文件名/字段）唯一真源在 scan.cpp 的 apply_hook_signals；这里只负责把它接上。
namespace ap {

struct HookAgent {
    std::string id;         // claude / qoder / codex / …
    std::string name;       // 屏幕上显示的名字
    std::string config;     // CLI 配置文件路径；空＝这家没有可接的配置
    bool supported = false; // 这家有我们能写的 hook 位（claude / qoder）
    bool installed = false; // 我们的命令已经在配置里，且指向的脚本真的存在
    bool stale = false;     // 配置里引用了 Agent Ping 的脚本，但那个脚本已经不在了
    bool occupied = false;  // 这个位置被别的工具占着（codex 的 notify 只能有一个）
    std::string script;     // 配置里引用到的脚本路径（stale 时就是那条失效路径）
    std::string note;       // 给人看的一句原因
};

/// 写入器脚本该落在哪：与 App 所在路径无关，重装/换版本不失效。
std::string hook_script_path();
/// 写入器的正文（工具脚本与面板写的是同一份字节）。
std::string hook_script_body();
/// 落盘并 chmod 0755。err 非空时带上原因。
bool write_hook_script(std::string* err);

/// 逐家探一遍：配置解析失败也算一条（note 里说清），不静默当「没装」。
std::vector<HookAgent> probe_hooks();

/// 接线：先写脚本，再改配置（备份 → 原子写）。成功时 backup 回填备份路径（没备份＝空）。
bool install_hook(const std::string& agent_id, std::string* backup, std::string* err);

/// 从配置里摘掉 Agent Ping 的条目（脚本文件保留）。没有可摘的也算成功。
bool uninstall_hook(const std::string& agent_id, std::string* backup, std::string* err);

}  // namespace ap

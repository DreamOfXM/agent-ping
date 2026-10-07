#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// 只依赖标准库的 JSONL 读取层。macOS 与 Linux 共用：POSIX pread + <ctime>。
namespace ap {

struct Json {
    enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    bool boolean = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json* at(const char* key) const;
    bool has(const char* key) const { return at(key) != nullptr; }
    const std::string* str_at(const char* key) const;
    bool is_null() const { return kind == NUL; }
};

bool json_parse(const char* s, std::size_t n, Json& out);

/// 实测同一份日志混着两种时间形状：assistant/user 是 ISO 8601 字符串，
/// Qoder 的 active-leaf 与 WorkBuddy 全部是 epoch 毫秒整数。两种都认。
/// 返回 epoch 秒；0 = 取不到。
double json_date(const Json& v);
double json_date_at(const Json& rec, const char* key);

/// 文件尾部若干条记录（按文件顺序，解析失败的行丢掉）。
std::vector<Json> tail_records(const std::string& path,
                               std::size_t max_bytes = 128 * 1024);

/// 只读头部：首条 user 正文的首行（当会话标题）+ 首条记录的 cwd。
struct HeadFacts {
    std::string cwd;
    std::string prompt;   // 空 = 没有可用的人类正文（注入块不算）
};
HeadFacts head_facts(const std::string& path, std::size_t max_bytes = 256 * 1024);

/// 从文件尾按块向前回退，把 needle 命中的行解析出来交给 fn。
/// 各家 CLI 的用量藏在不同键下（Qoder/Claude 是 message.usage，WorkBuddy 是
/// providerData.usage），所以这里不预设形状，只负责「窗内 + 便宜」这一段。
/// 时间戳实测会回跳，越过 since 后再多读一截才收手。
void scan_window(const std::string& path, double since_epoch, const char* needle,
                 const std::function<void(const Json&, double)>& fn,
                 std::size_t cap_bytes = 64ull * 1024 * 1024);

std::string basename_last(const std::string& path);
std::string strip_ext(const std::string& name);

}  // namespace ap

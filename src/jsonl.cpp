#include "jsonl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ap {

const Json* Json::at(const char* key) const {
    if (kind != OBJ) return nullptr;
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

const std::string* Json::str_at(const char* key) const {
    const Json* v = at(key);
    return (v && v->kind == STR) ? &v->str : nullptr;
}

// ── 解析器 ────────────────────────────────────────────────────────────────────
namespace {

struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    bool ws() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
            return true;
        }
        return false;
    }

    bool value(Json& out) {
        if (!ws()) return false;
        if (++depth > 96) return false;      // 日志里嵌套不深，超深即视为畸形行
        struct Guard { int& d; ~Guard() { d--; } } g{depth};
        char c = *p;
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') { out.kind = Json::STR; return string(out.str); }
        if (c == 't') { if (end - p < 4 || memcmp(p, "true", 4)) return false; p += 4; out.kind = Json::BOOL; out.boolean = true; return true; }
        if (c == 'f') { if (end - p < 5 || memcmp(p, "false", 5)) return false; p += 5; out.kind = Json::BOOL; out.boolean = false; return true; }
        if (c == 'n') { if (end - p < 4 || memcmp(p, "null", 4)) return false; p += 4; out.kind = Json::NUL; return true; }
        return number(out);
    }

    bool number(Json& out) {
        char* stop = nullptr;
        double d = strtod(p, &stop);
        if (stop == p) return false;
        p = stop;
        out.kind = Json::NUM;
        out.num = d;
        return true;
    }

    void utf8(unsigned int cp, std::string& out) {
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    }

    bool hex4(unsigned int& cp) {
        if (end - p < 4) return false;
        cp = 0;
        for (int i = 0; i < 4; i++) {
            char c = p[i];
            unsigned int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return false;
            cp = cp * 16 + v;
        }
        p += 4;
        return true;
    }

    bool string(std::string& out) {
        out.clear();
        if (*p != '"') return false;
        p++;
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        const char* save = p;
                        p += 2;
                        unsigned int lo = 0;
                        if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p = save;
                    }
                    utf8(cp, out);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool object(Json& out) {
        out.kind = Json::OBJ;
        out.obj.clear();
        p++;
        if (!ws()) return false;
        if (*p == '}') { p++; return true; }
        for (;;) {
            if (*p != '"') return false;
            std::string key;
            if (!string(key)) return false;
            if (!ws() || *p != ':') return false;
            p++;
            Json v;
            if (!value(v)) return false;
            out.obj.emplace_back(std::move(key), std::move(v));
            if (!ws()) return false;
            if (*p == ',') { p++; if (!ws()) return false; continue; }
            if (*p == '}') { p++; return true; }
            return false;
        }
    }

    bool array(Json& out) {
        out.kind = Json::ARR;
        out.arr.clear();
        p++;
        if (!ws()) return false;
        if (*p == ']') { p++; return true; }
        for (;;) {
            Json v;
            if (!value(v)) return false;
            out.arr.push_back(std::move(v));
            if (!ws()) return false;
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            return false;
        }
    }
};

/// 公历 → Unix 秒。自己算而不是 timegm：timegm 是 BSD/glibc 扩展，
/// 换平台编译时它可能要求 _GNU_SOURCE，而这条路每天都跑。
double civil_to_epoch(int y, int mo, int d, int h, int mi, int s) {
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (double)(era * 146097 + (int)doe - 719468) * 86400 + h * 3600 + mi * 60 + s;
}

}  // namespace

bool json_parse(const char* s, std::size_t n, Json& out) {
    Parser p{s, s + n};
    if (!p.value(out)) return false;
    return true;
}

// ── 时间 ──────────────────────────────────────────────────────────────────────
namespace {

bool all_digits(const char* s, const char* e) {
    if (s == e) return false;
    for (; s < e; s++) if (*s < '0' || *s > '9') return false;
    return true;
}

double digits_to_epoch(const char* s, const char* e) {
    double v = 0;
    for (; s < e; s++) v = v * 10 + (*s - '0');
    return v > 1e11 ? v / 1000.0 : v;      // 毫秒 / 秒
}

/// 2026-10-06T15:23:11.456Z、2026-10-06T15:23:11+08:00 都吃。
double parse_iso(const char* s, std::size_t n) {
    if (n < 19) return 0;
    int Y, Mo, D, h, m, sec;
    if (!(all_digits(s, s + 4) && s[4] == '-' && all_digits(s + 5, s + 7) && s[7] == '-' &&
          all_digits(s + 8, s + 10) && (s[10] == 'T' || s[10] == ' ') &&
          all_digits(s + 11, s + 13) && s[13] == ':' && all_digits(s + 14, s + 16) &&
          s[16] == ':' && all_digits(s + 17, s + 19)))
        return 0;
    Y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    Mo = (s[5] - '0') * 10 + (s[6] - '0');
    D = (s[8] - '0') * 10 + (s[9] - '0');
    h = (s[11] - '0') * 10 + (s[12] - '0');
    m = (s[14] - '0') * 10 + (s[15] - '0');
    sec = (s[17] - '0') * 10 + (s[18] - '0');
    double frac = 0;
    std::size_t i = 19;
    if (i < n && s[i] == '.') {
        i++;
        double scale = 0.1;
        while (i < n && s[i] >= '0' && s[i] <= '9') { frac += (s[i] - '0') * scale; scale *= 0.1; i++; }
    }
    int offset = 0;
    if (i < n) {
        if (s[i] == 'Z' || s[i] == 'z') { i++; }
        else if (s[i] == '+' || s[i] == '-') {
            int sign = s[i] == '+' ? 1 : -1;
            i++;
            if (n - i < 2 || !all_digits(s + i, s + i + 2)) return 0;
            int oh = (s[i] - '0') * 10 + (s[i + 1] - '0');
            i += 2;
            int om = 0;
            if (i < n && s[i] == ':') i++;
            if (n - i >= 2 && all_digits(s + i, s + i + 2)) { om = (s[i] - '0') * 10 + (s[i + 1] - '0'); i += 2; }
            offset = sign * (oh * 3600 + om * 60);
        }
    }
    double base = civil_to_epoch(Y, Mo, D, h, m, sec);
    if (base <= 0) return 0;
    return base + frac - offset;             // 'Z' 只是「偏移为零」，不用另算
}

}  // namespace

double json_date(const Json& v) {
    if (v.kind == Json::NUM) return v.num > 1e11 ? v.num / 1000.0 : v.num;
    if (v.kind != Json::STR) return 0;
    const std::string& s = v.str;
    if (s.size() >= 8 && all_digits(s.data(), s.data() + s.size())) return digits_to_epoch(s.data(), s.data() + s.size());
    return parse_iso(s.data(), s.size());
}

double json_date_at(const Json& rec, const char* key) {
    const Json* v = rec.at(key);
    return v ? json_date(*v) : 0;
}

// ── 文件读取 ──────────────────────────────────────────────────────────────────
namespace {

off_t file_size(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0) return -1;
    return st.st_size;
}

/// 解析 [data, data+n) 里完整的行；cut_head = 起点落在半条记录上，先跳过一个换行。
template <class Fn>
void each_line(char* data, std::size_t n, bool cut_head, Fn fn) {
    char* p = data;
    char* e = data + n;
    if (cut_head) {
        char* nl = (char*)memchr(p, '\n', (size_t)(e - p));
        if (!nl) return;
        p = nl + 1;
    }
    while (p < e) {
        char* nl = (char*)memchr(p, '\n', (size_t)(e - p));
        char* le = nl ? nl : e;
        if (le > p) fn(p, (std::size_t)(le - p));
        if (!nl) break;
        p = nl + 1;
    }
}

bool contains(const char* s, std::size_t n, const char* needle) {
    std::size_t m = strlen(needle);
    if (n < m) return false;
    for (std::size_t i = 0; i + m <= n; i++)
        if (s[i] == needle[0] && memcmp(s + i, needle, m) == 0) return true;
    return false;
}

}  // namespace

std::vector<Json> tail_records(const std::string& path, std::size_t max_bytes) {
    std::vector<Json> out;
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return out;
    off_t size = file_size(fd);
    if (size <= 0) { close(fd); return out; }
    std::size_t want = (std::size_t)size;
    off_t start = 0;
    if (want > max_bytes) { start = size - (off_t)max_bytes; want = (std::size_t)max_bytes; }
    std::vector<char> buf(want);
    ssize_t got = pread(fd, buf.data(), want, start);
    close(fd);
    if (got <= 0) return out;
    each_line(buf.data(), (std::size_t)got, start > 0, [&](char* l, std::size_t n) {
        Json j;
        if (json_parse(l, n, j) && j.kind == Json::OBJ) out.push_back(std::move(j));
    });
    return out;
}

HeadFacts head_facts(const std::string& path, std::size_t max_bytes) {
    HeadFacts hf;
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return hf;
    off_t size = file_size(fd);
    if (size <= 0) { close(fd); return hf; }
    std::size_t want = (std::size_t)size < max_bytes ? (std::size_t)size : max_bytes;
    std::vector<char> buf(want);
    ssize_t got = pread(fd, buf.data(), want, 0);
    close(fd);
    if (got <= 0) return hf;

    static const char* kNeedle = "\"cwd\"";
    each_line(buf.data(), (std::size_t)got, false, [&](char* l, std::size_t n) {
        if (hf.first_ts == 0 && contains(l, n, "\"timestamp\"")) {
            Json jt;
            if (json_parse(l, n, jt)) hf.first_ts = json_date_at(jt, "timestamp");
        }
        if (!hf.cwd.empty() && !hf.prompt.empty()) return;
        if (hf.cwd.empty() && contains(l, n, kNeedle)) {
            Json j;
            if (json_parse(l, n, j)) {
                if (const std::string* c = j.str_at("cwd")) if (!c->empty()) hf.cwd = *c;
            }
        }
        if (!hf.prompt.empty()) return;
        Json j;
        if (!json_parse(l, n, j)) return;
        const std::string* type = j.str_at("type");
        if (!type || *type != "user") return;
        const Json* msg = j.at("message");
        if (!msg) return;
        std::string text;
        const Json* content = msg->at("content");
        if (content && content->kind == Json::STR) text = content->str;
        else if (content && content->kind == Json::ARR) {
            for (const Json& blk : content->arr) {
                const std::string* bt = blk.str_at("type");
                if (!bt || *bt != "text") continue;
                if (const std::string* t = blk.str_at("text")) { if (!text.empty()) text += ' '; text += *t; }
            }
        }
        std::size_t nl = text.find('\n');
        if (nl != std::string::npos) text.erase(nl);
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.pop_back();
        // 有些会话第一条 user 记录不是人打的字，是外部注入的信封；这种串当标题既不指认
        // 「哪条对话」又看着像报错，退回上层去用目录名。
        if (!text.empty() && text[0] != '<' && text[0] != '[') hf.prompt = text;
    });
    return hf;
}

void scan_window(const std::string& path, double since_epoch, const char* needle,
                 const std::function<void(const Json&, double)>& fn, std::size_t cap_bytes) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return;
    off_t size = file_size(fd);
    if (size <= 0) { close(fd); return; }

    const std::size_t chunk = 1 << 20;
    const std::size_t backstop = 8u << 20;    // 时间戳实测会回跳，越过 since 后再多读一截才收手
    std::string carry;
    off_t hi = size;
    std::size_t scanned = 0;
    bool done = false;
    while (hi > 0 && !done) {
        off_t lo = hi - (off_t)chunk < 0 ? 0 : hi - (off_t)chunk;
        std::size_t want = (std::size_t)(hi - lo);
        std::vector<char> buf(want + carry.size() + 1);
        ssize_t got = pread(fd, buf.data(), want, lo);
        if (got <= 0) break;
        memcpy(buf.data() + want, carry.data(), carry.size());
        std::size_t n = (std::size_t)got + carry.size();
        scanned += (std::size_t)got;
        carry.clear();

        // 这一段最前面那半条记录属于更早的块，先摘出来存着
        char* first_nl = (char*)memchr(buf.data(), '\n', n);
        std::size_t head = 0;
        if (!first_nl) { carry.assign(buf.data(), n); head = n; }
        else { carry.assign(buf.data(), (std::size_t)(first_nl - buf.data())); head = (std::size_t)(first_nl - buf.data()) + 1; }

        each_line(buf.data() + head, n - head, false, [&](char* l, std::size_t len) {
            if (done || !contains(l, len, needle)) return;
            Json j;
            if (!json_parse(l, len, j)) return;
            double ts = json_date_at(j, "timestamp");
            if (ts > 0 && ts < since_epoch) {
                if (scanned >= backstop) done = true;
                return;
            }
            fn(j, ts);
        });
        hi = lo;
        if (done || scanned >= cap_bytes) break;
    }
    close(fd);
}

std::string basename_last(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string strip_ext(const std::string& name) {
    std::size_t dot = name.rfind('.');
    return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

}  // namespace ap

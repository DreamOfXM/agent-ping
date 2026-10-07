#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "platform/statusbar.h"
#include "platform/open.h"
#include "platform/notify.h"
#include "platform/lang.h"
#include "alert.h"
#include "scan.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// 渲染层：数据一律来自 scan.cpp 的活扫描（后台线程每 3s 一轮），
// 本文件里没有任何写死的会话。状态判据、采集口径都在 scan.cpp，那里逐条对齐
// Swift 侧带实测注释的常量；这里只负责「怎么摆」和「六语怎么念」。
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// 界面文案：六语。字体分档是 JSON 管不到的部分，见下面 PickFace。
// ─────────────────────────────────────────────────────────────────────────────
static const char* L0 = "zh", *L1 = "en", *L2 = "de", *L3 = "it", *L4 = "ja", *L5 = "ko";
static const int kLocales = 6;
static const char* kLocTag[kLocales] = {L0, L1, L2, L3, L4, L5};

struct Key { const char* k; const char* t[6]; };
static const Key kStr[] = {
    {"needs_you",      {"等你回复", "Needs you", "Deine Antwort", "Attende te", "応答待ち", "응답 대기"}},
    {"running",        {"运行中", "Running", "Läuft", "In corso", "実行中", "실행 중"}},
    {"awaiting",       {"等你授权", "Awaiting permission", "Berechtigung", "Autorizzazione", "許可待ち", "권한 대기"}},
    {"queued",         {"排队中", "Queued", "In Warteschlange", "In coda", "キュー待ち", "대기열"}},
    {"failed",         {"失败中断", "Failed", "Fehlgeschlagen", "Fallito", "エラー中断", "오류 중단"}},
    {"st_done",        {"已完成", "Done", "Fertig", "Completato", "完了", "완료"}},
    // 分组标题：稿子 render.mjs 的 PIN_GROUPS，五组按这个顺序摆
    {"grp_await",      {"需要你介入", "Needs you", "Eingriff nötig", "Intervieni tu", "対応が必要", "대응 필요"}},
    {"grp_run",        {"运行中", "Running", "Läuft", "In corso", "実行中", "실행 중"}},
    {"grp_queue",      {"排队中", "Queued", "In Warteschlange", "In coda", "キュー待ち", "대기열"}},
    {"grp_fail",       {"失败中断", "Failed", "Fehlgeschlagen", "Fallito", "エラー中断", "오류 중단"}},
    {"grp_done",       {"今日已结束", "Done today", "Heute beendet", "Conclusi oggi", "本日完了", "오늘 종료"}},
    // 状态项（菜单栏／系统托盘）的悬停说明：那里只有图标＋两三个数字，
    // 「哪个数字是哪一档」和「点了会怎样」只能靠这一句说清。
    {"sb_hint",        {"点一下收／放面板，右键出菜单",
                        "Click to show or hide the panel · right-click for the menu",
                        "Klick zeigt bzw. versteckt das Panel · Rechtsklick öffnet das Menü",
                        "Clicca per mostrare o nascondere il pannello · tasto destro per il menu",
                        "クリックでパネル開閉、右クリックでメニュー",
                        "클릭으로 패널 열기／닫기, 우클릭은 메뉴"}},
    {"sb_queued",      {"含排队 %d", "(%d queued)", "(%d in Warteschlange)",
                        "(%d in coda)", "（うちキュー %d）", "(대기 %d)"}},
    // 状态项右键菜单的两项。菜单是系统画的，字只能由我们喂进去，所以六语都得有。
    {"sb_show_hide",   {"显示／隐藏面板", "Show / hide panel", "Panel ein-/ausblenden",
                        "Mostra / nascondi pannello", "パネル表示／非表示", "패널 표시／숨기기"}},
    {"sb_quit",        {"退出 Agent Ping", "Quit Agent Ping", "Agent Ping beenden",
                        "Esci da Agent Ping", "Agent Ping を終了", "Agent Ping 종료"}},
    {"cost",           {"今日消耗", "Today cost", "Heutige Kosten", "Costo di oggi", "本日の消費", "오늘 비용"}},
    {"tokens",         {"输入 tokens", "Input tokens", "Eingegebene Tokens", "Token immessi", "入力トークン", "입력 토큰"}},
    {"present",        {"今日在场", "Present today", "Heute anwesend", "Presente oggi", "本日在席", "오늘 재석"}},
    {"all",            {"全部", "All", "Alle", "Tutti", "すべて", "전체"}},
    {"quiet_agents",   {"今天没活动", "Idle today", "Heute inaktiv", "Inattivo oggi", "本日未活動", "오늘 미활동"}},
    {"unreadable",     {"读不到", "Unreadable", "Nicht lesbar", "Non leggibile", "読取不可", "읽을 수 없음"}},
    {"sessions_today", {"今天会话", "Sessions today", "Sitzungen heute", "Sessioni oggi", "本日セッション", "오늘 세션"}},
    {"expand",         {"展开完整面板", "Expand full panel", "Volles Panel", "Espandi pannello", "パネル展開", "패널 확장"}},
    {"turns",          {"轮", "Turns", "Runden", "Giri", "ターン", "턴"}},
    {"segs",           {"段", "Spans", "Abschnitte", "Intervalli", "区間", "구간"}},
    {"last_seen",      {"最近一次", "Last seen", "Zuletzt", "Ultima volta", "最終確認", "마지막 확인"}},
    {"more",           {"还有 %d 条", "+%d more", "+%d weitere", "+%d altri", "他 %d 件", "+%d건"}},
    // 点名某一组之后唯一那条回路。六语都自带「←」：屏幕上认出它是返回靠的是这个箭头＋动词，
    // 只把返回挂在别处（上级名/悬停）＝他明确否过的糊弄形状。
    {"back_overview",  {"← 返回总览", "← Back to all", "← Zurück zur Übersicht",
                        "← Torna a tutti", "← 全体に戻る", "← 전체로 돌아가기"}},
    // await 行专属的三颗（稿子 SessionRow.tsx:190 / 状态胶囊「等待确认」）
    {"await_confirm",  {"等待确认", "Waiting to confirm", "Warten auf Zusage", "In attesa di conferma", "確認待ち", "확인 대기"}},
    {"ask_confirm",    {"需要你确认", "needs your confirmation", "braucht deine Bestätigung", "richiede la tua conferma", "確認が必要です", "확인 필요"}},
    {"mark_read",      {"已读", "Read", "Gelesen", "Letto", "既読", "읽음"}},
    {"later",          {"稍后", "Later", "Später", "Dopo", "後で", "나중에"}},
    {"queue_pos",      {"队列第 %d 位", "Queue #%d", "Warteschlange #%d", "Coda #%d", "キュー %d 番目", "대기열 %d 번째"}},
    {"vs_yesterday",   {"较昨日", "vs yesterday", "vs gestern", "vs ieri", "昨日比", "어제 대비"}},
    // 口径脚注：屏幕上唯一会被读成「钱」的数字必须自己说清它不是钱。
    {"cost_note",      {"cr＝Qoder CLI 自记的 credits 计数，非金额",
                        "cr = Qoder CLI's own credits counter, not money",
                        "cr = Qoder CLIs eigener Credits-Zähler, kein Geldbetrag",
                        "cr = contatore crediti di Qoder CLI, non denaro",
                        "cr＝Qoder CLI が自記する credits 計数。金額ではない",
                        "cr = Qoder CLI가 직접 기록하는 credits 값, 금액 아님"}},
    // ── 设置屏（r14/settings.html）。节头/开关行/数据源格/脚注各一条，六语齐全，
    //    缺一门语言就会在屏幕上露出「??」。
    {"set_title",      {"设置", "Settings", "Einstellungen", "Impostazioni", "設定", "설정"}},
    {"back",           {"返回", "Back", "Zurück", "Indietro", "戻る", "돌아가기"}},
    {"sec_tools",      {"监听工具", "Monitored tools", "Beobachtete Tools", "Strumenti monitorati",
                        "監視ツール", "모니터링 도구"}},
    {"sec_alerts",     {"提醒规则", "Alert rules", "Hinweisregeln", "Regole di avviso",
                        "通知ルール", "알림 규칙"}},
    {"sec_window",     {"小窗行为", "Popover behaviour", "Mini-Panel-Verhalten",
                        "Comportamento pannello", "小窓の挙動", "창 동작"}},
    {"sec_sources",    {"数据源", "Data sources", "Datenquellen", "Origini dati",
                        "データソース", "데이터 소스"}},
    {"alert_states",   {"需要提醒的状态", "Alert on these states", "Diese Zustände melden",
                        "Questi stati segnalati", "通知する状態", "다음 상태 알림"}},
    {"sound",          {"提示音", "Alert sound", "Hinweiston", "Suono di avviso",
                        "通知音", "알림 소리"}},
    {"sys_notify",     {"系统通知", "System notifications", "Systemmitteilungen",
                        "Notifiche di sistema", "システム通知", "시스템 알림"}},
    {"on_top",         {"常驻置顶", "Keep on top", "Immer oben halten", "Sempre in primo piano",
                        "常に手前に表示", "항상 최상단"}},
    {"max_rows",       {"每组最多显示", "Rows per group", "Zeilen pro Gruppe", "Righe per gruppo",
                        "グループ別最大件数", "그룹당 최대 표시"}},
    {"src_scan",       {"本地日志扫描", "Local log scan", "Lokaler Log-Scan", "Scansione log locale",
                        "ローカルログ走査", "로컬 로그 스캔"}},
    {"src_hook",       {"Hook 收件箱", "Hook inbox", "Hook-Postfach", "Casella hook",
                        "Hook 受信箱", "훅 수신함"}},
    // 稿子这一格写的是「新增的工具会立即出现在导航与分组中」——我们按内置采集器编译，
    // 没有增删这条路，所以脚注说的是真话而不是稿子那句。
    {"note_tools",     {"工具由内置采集器决定，这里不能增删",
                        "Collectors are built in — tools can't be added here",
                        "Sammler sind fest eingebaut, hier nicht ergänzbar",
                        "I raccoglitori sono integrati, qui non si aggiungono",
                        "ツールは内蔵収集器で決まり、ここでは追加不可",
                        "도구는 내장 수집기가 결정, 여기서 추가 불가"}},
    {"note_snooze",    {"稍后提醒默认 20 分钟后恢复", "Snoozed rows come back after 20 min",
                        "Zurückgestellte Meldungen nach 20 Min", "Righe rimandate dopo 20 min",
                        "後で通知は 20 分後に復帰", "잠시 미룸은 20분 후 복구"}},
    {"absent",         {"本机没有", "Not installed", "Nicht installiert", "Non installato",
                        "未インストール", "미설치"}},
    {"more_sec",       {"屏幕不够高，还有 %d 组没摆", "%d sections hidden (window too short)",
                        "%d Bereiche verdeckt (Fenster zu niedrig)",
                        "%d sezioni nascoste (finestra bassa)", "画面が低く %d 項目未表示",
                        "창이 낮아 %d 섹션 미표시"}},
    // 相对时间：数字由外面填，所以这四条是格式串
    {"just_now",       {"刚刚", "just now", "gerade eben", "adesso", "たった今", "방금"}},
    {"min_ago",        {"%d 分钟前", "%d min ago", "vor %d Min", "%d min fa", "%d 分前", "%d분 전"}},
    {"hour_ago",       {"%d 小时前", "%d h ago", "vor %d Std", "%d h fa", "%d 時間前", "%d시간 전"}},
    {"day_ago",        {"%d 天前", "%d d ago", "vor %d Tg", "%d gg fa", "%d 日前", "%d일 전"}},
};
static const char* T(const char* key, int loc) {
    for (const auto& s : kStr) if (!strcmp(s.k, key)) return s.t[loc];
    return "??";
}

// 「多久以前」翻成该语言的说法。窗口只回话到「天」，更久的面板不展示。
static std::string Ago(double age_sec, int loc) {
    if (age_sec < 60) return T("just_now", loc);
    char buf[64];
    if (age_sec < 3600) { snprintf(buf, sizeof buf, T("min_ago", loc), (int)(age_sec / 60)); return buf; }
    if (age_sec < 86400) { snprintf(buf, sizeof buf, T("hour_ago", loc), (int)(age_sec / 3600)); return buf; }
    snprintf(buf, sizeof buf, T("day_ago", loc), (int)(age_sec / 86400));
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// 字体：按 cmap 覆盖率分档 + 逐 run 回退。ImGui 没有这个能力，得自己搭。
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<unsigned int> DecodeUTF8(const char* s) {
    std::vector<unsigned int> out;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        unsigned int cp = c; int extra = 0;
        if (c >= 0xF0) { cp &= 0x07; extra = 3; }
        else if (c >= 0xE0) { cp &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp &= 0x1F; extra = 1; }
        for (int i = 0; i < extra && *s; i++) cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
        out.push_back(cp);
    }
    return out;
}

enum Face { LATIN, SC, JP, KR, MONO_SB, MONO_R, FACE_N };
// 稿子的 --font-mono 首选就是 JetBrains Mono（OFL-1.1，已连许可证一起进包），
// 所以等宽档不是近似，是照抄；粗体留给 num-xl 的 font-weight:600。
static const char* kFace[FACE_N] = {"NotoSans.ttf", "NotoSansSC.ttf", "NotoSansJP.ttf", "NotoSansKR.ttf",
                                    "JetBrainsMono-SemiBold.ttf", "JetBrainsMono-Regular.ttf"};

// 一档 = 稿子里一处真实字号声明（px，与 340pt 面板宽同一尺度），外加它的 family/weight。
// 「等宽/粗细」不能由字号推：同一档尺寸在稿子里既可能写 class="mono" 也可能跟正文走
// （9.5pt：状态胶囊名是 sans，「多久以前」是 mono；10.5pt：失败原文是 mono，「稍后」按钮不是）。
// 于是同尺寸拆成两档，谁 mono 直接照 SessionRow.trim.mjs / render.mjs 的 className 抄。
// JetBrains Mono 的 cmap 只有拉丁，mono 档里的中文照样按 run 回退到 SC —— 它只决定拉丁那半截。
enum Tier { T_NUM, T_TITLE, T_CHIP, T_TURNS, T_FAIL, T_BTN, T_GLYPH, T_GLYPH_SM, T_SM, T_PILL,
            T_TIME, T_MICRO,
            // 设置屏（r14/settings.html）另有的四处字号声明，逐条对着稿子的 class 抄：
            // T_MLBL 节头/chips/数据源格/脚注的 mono 10；T_SEG 分段控件的 mono 11；
            // T_LABEL 开关行文案的 sans 11.5；T_TOOL 工具名的 sans 12；
            // T_GLYPH9 20pt 头像里的首字母 9（稿子 font-size:9px;font-weight:600 ——
            // NotoSans 系只有常规字重，这一档按常规画，与 T_GLYPH 同一笔已知差异）。
            T_MLBL, T_SEG, T_LABEL, T_TOOL, T_GLYPH9, TIER_N };
static const float kTierPx[TIER_N]   = {26.0f, 12.5f, 11.0f, 11.0f, 10.5f, 10.5f, 10.0f, 6.0f,
                                        10.0f, 9.5f, 9.5f, 9.0f,
                                        10.0f, 11.0f, 11.5f, 12.0f, 9.0f};
static const bool  kTierMono[TIER_N] = {true,  false, false, true,  true,  false, false, false,
                                        true,  false, true,  true,
                                        true,  true,  false, false, false};
// 稿子里的粗体只有两处落在 mono 上（num-xl 600、「N 轮」semibold），本机有
// JetBrainsMono-SemiBold 可以直接对应。头像首字母（ToolGlyph）也是 600，但它的 family
// 走 --font-sans，而 NotoSans 系只有常规字重 —— 那两档只能按常规画，差异记在 RESULT.md。
static const bool  kTierBold[TIER_N] = {true,  false, false, true,  false, false, false, false,
                                        false, false, false, false,
                                        false, false, false, false, false};
static bool MonoTier(int t) { return kTierMono[t]; }
static int  MonoFace(int t) { return kTierBold[t] ? MONO_SB : MONO_R; }
// 这张脸在这一档有没有可能被选中：mono 档只可能用到「它那一号等宽脸 + 四张正文脸」
// （等宽档里的中文按 run 退到正文脸），sans 档则永远碰不到等宽脸。
// 12×6 全烘是 72 张 ImFont，其中 18 张一次都不会被读，所以按这条筛掉（实际烘了多少张
// 由下面的 [字形] 行报出来，别照注释推算）。
static bool FaceUsed(int t, int f) {
    if (f == MONO_SB || f == MONO_R) return MonoTier(t) && MonoFace(t) == f;
    return true;
}
static ImFont* g_font[TIER_N][FACE_N] = {};
static std::set<unsigned int> g_cover[FACE_N];   // 该字体 cmap 实际有的码点
// ImGui/stb 的 size_pixels 不是 CSS 的 font-size：它按「行高」解释，
// 即 (hhea.ascender - hhea.descender) / unitsPerEm 个 em。不换算的话同一份稿子
// 里的 26px 会被画成 26/1.29=20px，整屏文字小一圈（实测 0.74 倍）。
static float g_line[FACE_N] = {1};
// 同档字混排时 CSS 只有一条基线（行框由 font-family 里第一个可用字体决定）。
// 各脸的 ascender/upem 不一样（拉丁 1.069、中文 1.069、等宽 1.011…），
// 所以逐 run 画之前要把 y 按各自基线挪一次，否则中英夹排会上下错开。
static float g_asc[FACE_N] = {1};

static void LoadCoverage(FT_Library ft, const char* dir) {
    char path[1024];
    for (int f = 0; f < FACE_N; f++) {
        snprintf(path, sizeof path, "%s/%s", dir, kFace[f]);
        FT_Face face = nullptr;
        if (FT_New_Face(ft, path, 0, &face)) { fprintf(stderr, "打不开 %s\n", path); continue; }
        for (unsigned long cp = 0x20; cp < 0x2FFFF; cp++)
            if (FT_Get_Char_Index(face, (FT_ULong)cp)) g_cover[f].insert((unsigned int)cp);
        // 只给宽度：FreeType 会把 y_scale 取成 x_scale，于是 ascender/descender
        // 直接是「每 em 多少」，量出来的就是 stb 用的那个 (asc-desc)/upem。
        // 给高度的话它会把 (asc-desc) 强行归一到那个高度，量出来恒等于 1。
        FT_Set_Char_Size(face, 1000 * 64, 0, 72, 72);
        g_line[f] = (float)((face->size->metrics.ascender - face->size->metrics.descender)
                            / (double)(1000 * 64));
        g_asc[f] = (float)(face->size->metrics.ascender / (double)(1000 * 64));
        FT_Done_Face(face);
    }
}

// 这一档字号、这一张脸，真正要烘/要传的 size_pixels
static float Bake(int tier, int face) { return kTierPx[tier] * g_line[face]; }
// 该脸的基线到 ImGui 行框顶的距离（AddText 的 y 就是行框顶）
static float AscentPx(int tier, int face) { return kTierPx[tier] * g_asc[face]; }
// 决定行框的那张脸 = 该档 CSS font-family 的第一位：
// .num-xl/.micro-label/.mono 都写死 var(--font-mono)，其余走 var(--font-sans)。
static int RefFace(int tier, int loc) {
    if (MonoTier(tier)) return MonoFace(tier);
    return (loc == 4) ? JP : (loc == 5) ? KR : (loc == 0) ? SC : LATIN;
}
// 把一行字居中进高 boxH 的行框：CSS 的半 leading 与 ImGui 行框顶正好抵消，
// 化简后只剩 (行框高 - 基准脸的像素行高)/2 —— 用 kTierPx 会整体偏下约 0.2em。
static float TextTop(float box_top, float boxH, int tier, int loc) {
    return box_top + (boxH - Bake(tier, RefFace(tier, loc))) * 0.5f;
}
static float TextMid(float cy, int tier, int loc) {
    return cy - Bake(tier, RefFace(tier, loc)) * 0.5f;
}

// 这个码点该由哪档字体画：先 locale 主字体，缺就按 SC→KR→拉丁退。
// 等宽面不参与退体链 —— 只有 MonoTier 的档在 SplitRuns 里显式优先它。
static int PickFace(unsigned int cp, int locale) {
    int primary = (locale == 4) ? JP : (locale == 5) ? KR : (locale == 0) ? SC : LATIN;
    int order[4] = {primary, SC, KR, LATIN};
    for (int f : order) if (g_cover[f].count(cp)) return f;
    return primary;
}

// 把一段 UTF-8 文本按"同一档字体"切成 run
struct Run { int face; std::string text; };
static std::vector<Run> SplitRuns(const char* text, int locale, int tier) {
    bool mono = MonoTier(tier);
    int monoFace = MonoFace(tier);   // num-xl / 「N 轮」是 600，其余 mono 档是常规
    std::vector<Run> runs;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        const unsigned char* start = p;
        unsigned char c = *p;
        int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        unsigned int cp = DecodeUTF8(std::string((const char*)p, (size_t)len).c_str())[0];
        int face = (mono && g_cover[monoFace].count(cp)) ? monoFace : PickFace(cp, locale);
        while (*p) {
            unsigned char d = *p;
            int l2 = (d >= 0xF0) ? 4 : (d >= 0xE0) ? 3 : (d >= 0xC0) ? 2 : 1;
            unsigned int cp2 = DecodeUTF8(std::string((const char*)p, (size_t)l2).c_str())[0];
            int f2 = (mono && g_cover[monoFace].count(cp2)) ? monoFace : PickFace(cp2, locale);
            if (f2 != face) break;
            p += l2;
        }
        if (!runs.empty() && runs.back().face == face) runs.back().text.append((const char*)start, (size_t)(p - start));
        else runs.push_back({face, std::string((const char*)start, (size_t)(p - start))});
    }
    return runs;
}

// 微标签是大写体（draft 的 .micro-label 带 text-transform:uppercase）；
// 六语里只有拉丁文有大小写，中文/韩文原样返回。
static std::string UpAscii(const std::string& s) {
    std::string out = s;
    for (auto& c : out) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return out;
}

// 一行混排的真实宽度（逐 run 量，跨档字体才不会拿错字号去估）。
// track 是「字距」，draft 用 em 表达（微标签 +0.16em、大数字 −0.03em）。
static float TextWidth(const std::string& text, int locale, int tier, float track = 0.0f) {
    float w = 0;
    for (const auto& r : SplitRuns(text.c_str(), locale, tier)) {
        float px = Bake(tier, r.face);
        w += g_font[tier][r.face]->CalcTextSizeA(px, 1e9f, 0.0f, r.text.c_str()).x + track;
    }
    if (!text.empty()) w -= track;
    return w > 0 ? w : 0;
}

static float TextHeight(int tier) { return kTierPx[tier] * 1.2f; }

// 绝对定位写一行：混排下 ImGui 的 SameLine 会把 x 记账搞乱（实测字形重叠），
// 而且这一版面板要画卡框/胶囊/条码，本来就得自己管坐标。
static float DrawTextRun(ImDrawList* dl, float x, float y, const std::string& text, int locale,
                         ImU32 col, int tier, float track = 0.0f) {
    float cx = x;
    float ref_asc = AscentPx(tier, RefFace(tier, locale));
    for (const auto& r : SplitRuns(text.c_str(), locale, tier)) {
        float px = Bake(tier, r.face);
        ImVec2 sz = g_font[tier][r.face]->CalcTextSizeA(px, 1e9f, 0.0f, r.text.c_str());
        dl->AddText(g_font[tier][r.face], px, ImVec2(cx, y + ref_asc - AscentPx(tier, r.face)),
                    col, r.text.c_str());
        cx += sz.x + track;
    }
    if (!text.empty()) cx -= track;
    return cx;
}

// 右对齐画一行，返回起笔 x（好让调用方接着在左边摆别的）
static float DrawTextRight(ImDrawList* dl, float x1, float y, const std::string& text, int locale,
                           ImU32 col, int tier, float track = 0.0f) {
    return DrawTextRun(dl, x1 - TextWidth(text, locale, tier, track), y, text, locale, col, tier, track);
}

// ── 配色：逐条由 r14/meoo-styles.css 的 oklch token 换算（换算式 = evidence-1005/oklch.py，
//    与 Theme.swift 已拍的同一口径）。改颜色请先改稿子，这里只跟。────────
static const ImU32 kBg       = IM_COL32(11, 16, 23, 255);   // --background oklch(.17 .018 258) → Chrome 实测 (11,16,23)
static const ImU32 kFg        = IM_COL32(236, 241, 245, 255);
static const ImU32 kMutedFg   = IM_COL32(144, 153, 165, 255);
static const ImU32 kStAwait   = IM_COL32(255, 180, 7, 255);   // --st-await  .82 .17 78
static const ImU32 kStRun     = IM_COL32(0, 200, 202, 255);   // --st-run    .74 .16 195
static const ImU32 kStQueue   = IM_COL32(146, 160, 177, 255); // --st-queue  .70 .03 255
static const ImU32 kStFail    = IM_COL32(255, 81, 90, 255);   // --st-fail   .68 .21 22
static const ImU32 kStDone    = IM_COL32(62, 212, 120, 255);  // --st-done   .77 .18 152
static const ImU32 kInk       = IM_COL32(11, 13, 18, 255);    // #0b0d12，选中胶囊上的反色字
static const ImU32 kHair      = IM_COL32(229, 240, 252, 23);  // --hairline oklch(.95 .02 250 / 9%)
// 设置屏（r14/settings.html）另用的五支：全部由 evidence/1007-settings/colors-chrome.txt
// 里那条「Chrome 无头 canvas 逐支涂 1×1 读回设备像素」的量法落数，不是按公式手推。
// 同一条量法对 --background / --hairline / --st-await 三个已上线的值复算全等，才敢用在这里。
static const ImU32 kSurface1  = IM_COL32(16, 22, 31, 255);    // --surface-1  .2 .02 258，节的底
static const ImU32 kSurface2  = IM_COL32(28, 35, 45, 255);    // --surface-2  .255 .022 255，工具行/关轨的底
static const ImU32 kPrimary   = IM_COL32(0, 209, 218, 255);   // --primary    .78 .14 200，开关与选中段
static const ImU32 kPrimaryFg = IM_COL32(14, 20, 29, 255);    // --primary-foreground .19 .02 258
static const ImU32 kStIdle    = IM_COL32(92, 100, 111, 255);  // --st-idle    .5 .02 258，节头与未启用
static const ImU32 kBorder    = IM_COL32(229, 240, 252, 28);  // --border   同色 11%
static const ImU32 kInput     = IM_COL32(229, 240, 252, 41);  // --input    同色 16%
// 白墨分档（draft 用 white/xx 表达层级）
static ImU32 W(int a255) { return IM_COL32(255, 255, 255, a255); }
static const ImU32 kTitle      = W(235);   // white/92
static const ImU32 kMicro      = W(97);    // white/38
static const ImU32 kProject    = W(115);   // white/45
static const ImU32 kTime       = W(76);    // white/30
// 给色带上透明度（状态胶囊边框 40%、底 16%、光晕 60%…）
static ImU32 WithA(ImU32 c, int a255) { return (c & 0x00FFFFFFu) | ((ImU32)a255 << 24); }

// 每个采集器一个色相，卡片的头像环 / 项目点 / 筛选胶囊共用。
// Qoder(200) 与 Claude(46) 是稿子里写死的两个，其余三个是同 L/C 档上挑的相邻色相。
static ImU32 HueFor(const std::string& name) {
    if (name == "Qoder CLI")  return IM_COL32(0, 209, 219, 255);    // oklch(.78 .14 200)
    if (name == "Claude Code") return IM_COL32(226, 139, 98, 255);  // oklch(.72 .12 46)
    if (name == "Codex")      return IM_COL32(104, 156, 232, 255);  // oklch(.74 .13 250)
    if (name == "ZCode")      return IM_COL32(104, 205, 130, 255);  // oklch(.77 .13 152)
    if (name == "WorkBuddy")  return IM_COL32(222, 116, 137, 255);  // oklch(.68 .14 17)
    return kMutedFg;
}

// 头像取名的首字母（稿子里 Q / C 就是这么来的），CJK 名取第一个码点。
static std::string InitialOf(const std::string& name) {
    if (name.empty()) return "?";
    unsigned int cp = DecodeUTF8(name.c_str())[0];
    std::string s;
    if (cp < 0x80) s = std::string(1, (char)std::toupper((int)cp));
    else {
        unsigned char c0 = (unsigned char)(0xC0 | (cp >> 6)), c1 = (unsigned char)(0x80 | (cp & 0x3F));
        if (cp > 0x7FF) { s += (char)(0xE0 | (cp >> 12)); s += (char)c0; } else s += (char)c0;
        s += (char)c1;
    }
    return s;
}

// 截到 n 个码点（按字节截会把 UTF-8 中间砍断，实测会画出半个字）
static std::string TakeCP(const std::string& s, int n) {
    const unsigned char* p = (const unsigned char*)s.c_str();
    size_t off = 0; int i = 0;
    while (p[off] && i < n) {
        unsigned char c = p[off];
        size_t l = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (off + l > s.size()) break;
        off += l; i++;
    }
    return s.substr(0, off);
}
static int CPCount(const std::string& s) { return (int)DecodeUTF8(s.c_str()).size(); }

// 会话标题来自磁盘，长度不设防。超出可用宽度就按码点二分截断 + 省略号。
static std::string ClipToWidth(const std::string& s, float maxw, int locale, int tier,
                               float track = 0.0f) {
    if (TextWidth(s, locale, tier, track) <= maxw) return s;
    const char* ell = g_cover[LATIN].count(0x2026) ? "\xE2\x80\xA6" : "...";
    int total = CPCount(s);
    int lo = 0, hi = total, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (TextWidth(TakeCP(s, mid) + ell, locale, tier, track) <= maxw) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return TakeCP(s, best) + ell;
}

// 从可执行文件位置反推 Contents/Resources；不在 .app 里则返回空
static std::string BundleResources() {
    std::string exe;
#ifdef __APPLE__
    char buf[2048];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return {};
    exe = buf;
#else
    char buf[2048];
    ssize_t r = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (r <= 0) return {};
    buf[r] = 0;
    exe = buf;
#endif
    size_t slash = exe.rfind('/');
    if (slash == std::string::npos) return {};
    std::string bin_dir = exe.substr(0, slash);                  // .../Contents/MacOS
    size_t up = bin_dir.rfind('/');
    if (up == std::string::npos) return {};
    return bin_dir.substr(0, up) + "/Resources";
}

// 双击 .app 启动时没有参数：字体跟着包走
static std::string BundleFontDir() {
    std::string fonts = BundleResources() + "/fonts";
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) return {};
    FT_Face face = nullptr;      // 打不开时 FreeType 不写回这个出参，留栈上垃圾就会在 FT_Done_Face 里段塞
    bool ok = FT_New_Face(ft, (fonts + "/" + kFace[SC]).c_str(), 0, &face) == 0;
    if (face) FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return ok ? fonts : std::string();
}

static void AddChromeText(ImFontGlyphRangesBuilder& rb);

// ─────────────────────────────────────────────────────────────────────────────
// 活数据：后台线程扫，主线程只读一份发布出来的快照。
// 扫描会读几十上百个文件（本机实测冷扫 0.54s），绝不能放在渲染帧里。
// ─────────────────────────────────────────────────────────────────────────────
static const int kPanelW = 340;                 // #win{width:340px}
static const double kScanEvery = 3.0;

// 几何逐条抄 r14/pin.html：main padding 12/12/16、区块 gap 12、行 py-8 pl-11 pr-8、
// rail w-3px 上下各缩 6、头像 22、列间 gap-2=8、表头三格 gap-2.5=10。
static const float kPadX = 12.0f, kPadTop = 12.0f, kPadBottom = 16.0f;
static const float kSectionGap = 12.0f, kGroupGap = 16.0f, kLabelRowGap = 6.0f;
static const float kRowGap = 6.0f;              // 每组 space-y-1.5：组头与行、行与行之间都是 6
static const float kRailW = 3.0f, kRowPl = 11.0f, kRowPr = 8.0f, kRowPadY = 8.0f;
static const float kAvatarD = 22.0f, kColGap = 8.0f;
// 行框高度不是拍的，是稿子的 line-height 乘以字号：body 1.5、leading-tight 1.25、
// leading-snug 1.375、leading-none 1。用「字号×1.2」这类估法会把每行都压掉 2~3pt，
// 六行一组差出来就是整屏比稿子矮。
static const float kTitleLine = kTierPx[T_TITLE] * 1.25f;                 // 15.625
static const float kMetaGap = 4.0f;                                       // mt-[4px]
static const float kMetaLine = kTierPx[T_SM] * 1.5f;                       // 15
static const float kDetailLine = kTierPx[T_CHIP] * 1.375f, kDetailPadY = 4.0f, kDetailGap = 6.0f;
static const float kBarH = 6.0f;
static const float kCanvasH = 760.0f;   // #win{height:760px}：画布按稿子，窗口随后收到内容高

// 设置屏的几何，逐条抄 r14/settings.html 里 SettingsPopover.tsx 跑出来的 DOM class：
//   section = rounded-xl(12px 圆角) + border 1 + bg-surface-1 + p-3(12)
//   节头    = mono 10px + .16em + uppercase + text-st-idle，行框＝10×1.5＝15
//   节头到内容 mt-2＝8；内容行之间 space-y-1.5＝6；脚注 mt-2＝8 + 10px leading-snug(1.375)
//   开关行  = flex items-center：左 label 11.5px（行框 17.25）、右轨 18×32 → 行高取 18
//   工具行  = li flex items-center gap-2 px-2 py-1.5 + 20px 头像 → 20+12+2border＝34
//   胶囊    = mono 10px px-1.5 py-0.5 + 未选中那两条各 1px 边 → 15+4+2＝21
//   分段    = mono 11px px-2 py-0.5 + 边 1 → 16.5+4+2＝22.5
//   格子    = mono 10px leading-tight px-1.5 py-1.5 + 边 1 → 12.5+12+2＝26.5
// 稿子 #win 是 420×660，正文区＝420-2(卡边)-24(main padding)＝394，节内＝394-2-24＝368。
static const float kSecPad = 12.0f, kSecR = 12.0f, kSecHeadGap = 8.0f, kSecRowGap = 6.0f;
static const float kHead10Line = kTierPx[T_MLBL] * 1.5f;        // 15
static const float kNoteLine   = kTierPx[T_MLBL] * 1.375f;      // 13.75
static const float kLabelLine  = kTierPx[T_LABEL] * 1.5f;       // 17.25
static const float kToolNameLine = kTierPx[T_TOOL] * 1.5f;      // 18
static const float kToggleH = 18.0f, kToggleW = 32.0f, kKnobD = 12.0f;
static const float kChipH = kHead10Line + 2 * 2.0f + 2 * 1.0f;  // 21
static const float kSegLine = kTierPx[T_SEG] * 1.5f;            // 16.5
static const float kSegH = kSegLine + 2 * 2.0f + 2 * 1.0f;      // 22.5
static const float kTileLine = kTierPx[T_MLBL] * 1.25f;         // 12.5（leading-tight）
static const float kTileH = kTileLine + 2 * 6.0f + 2 * 1.0f;    // 26.5
static const float kToolLiH = 20.0f + 2 * 6.0f + 2 * 1.0f;      // 34

static std::mutex g_mtx;
static ap::Snapshot g_snap;          // 只在 g_mtx 下读写
static std::atomic<long> g_seq{0};   // 每发布一轮 +1
static std::atomic<bool> g_quit{false};

static void ScanLoop() {
    while (!g_quit.load()) {
        ap::Snapshot s = ap::scan_all(std::time(nullptr));
        { std::lock_guard<std::mutex> lk(g_mtx); g_snap = std::move(s); }
        g_seq.fetch_add(1);
        for (int i = 0; i < (int)(kScanEvery * 20) && !g_quit.load(); i++)
            usleep(50000);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 面板一行「摆好的样子」：只存数据与口径，截断和翻译留到渲染时按 locale 现做
// （同一份数据要摆六种语言，写死一种截法另外五种就越界）。
// ─────────────────────────────────────────────────────────────────────────────
struct UIRow {
    int state;
    std::string title, project, agent, initial, detail, question, tool;
    std::vector<std::string> options;
    int turns = 0, queue_pos = 0;
    double age;
    bool inferred;
    bool is_new = false;                 // 稿子 collect.py:413：age_s < 900 → 标题前挂辉光点
    std::vector<int> spark;              // 24 格 × 15 分钟的活跃度，稿子 run 行的 Spark 输入
    ImU32 hue;
    /// 一行的身份：agent id + 该 agent 里的会话 id。已读/稍后/落点都按这个键记，
    /// 单独用会话 id 会跨采集器撞名（两家都用 uuid，但不同家同名 uuid 不是没可能）。
    std::string key, agent_id, native_id;
    /// 「已读」＝本进程内不再催；「稍后」＝20 分钟后自动恢复（settings.html:「20 分钟后恢复」）。
    bool acked = false, snoozed = false;
    bool muted() const { return acked || snoozed; }
};
struct Chip { std::string name, initial; int n; ImU32 hue; };
/// 设置屏「监听工具」一行的数据源：本轮 scan 真返回的采集器，一个不多一个不少。
/// 三个布尔互斥（本机没有／读不通／今天没动静），全 false 才是「正常在听」。
struct Tool {
    std::string name, initial;
    bool available = false, unreadable = false, quiet = false;
    int rows = 0;
    ImU32 hue;
};
struct PanelData {
    std::vector<UIRow> rows;
    std::vector<Chip> chips;             // 每个采集器一格（数字＝本轮真摆出来的行）
    std::vector<Tool> tools;             // 全部采集器，含本机没有的那几路
    std::vector<std::string> quiet;      // 今天没活动的采集器名
    std::vector<std::string> errors;     // 「某采集器：读失败原因」
    int hook_live = 0;                   // Hook 收件箱里这轮真数得到的信号文件数（数据源那一格）
    int needs = 0, run = 0;              // 顶栏两个数（needs＝介入+失败，run＝运行+排队）
    int await_n = 0, fail_n = 0;         // 状态项那三个数：介入与失败各算一档，不并成 needs
    int queue_n = 0;                     // run 里有多少条其实在排队——悬停说明要摊开这一层
    double credits = 0;
    double yday_credits = 0;             // 昨天同窗用量，只为第三格的「较昨日」环比
    int live = 0;                        // 状态项上的数字：非静置会话数
};
static PanelData g_pd;
static long g_pd_seq = -1;
static bool g_relayout = false;
static bool g_panelVisible = true;
static GLFWwindow* g_win = nullptr;      // 点击要跳走时得先把这块面板收起来
/// 出图模式：这一轮是给取证的，点完不许真把窗口藏了（那样拍不到点击后的状态），也不许跳走。
static const char* g_shot_to = getenv("AGENTPING_SHOT");

// ── 设置屏接的三个活口子 ────────────────────────────────────────────────────
// g_cfg 是「屏幕上那一屏」与 alert.cpp 的持久化之间唯一的桥：启动时 load、每次点击 save。
// 面板不再每轮 load（原来 RunAlertRound 每 3s 读一次盘）——那样设置屏上刚点下去的档
// 会被下一轮读盘盖掉，屏幕上亮着、账上却没改，是最难查的一种分叉。
static ap::AlertCfg g_cfg;
/// 0＝pin 列表，1＝设置屏。同一块窗口换内容，不另开窗口（另开就得多块卡框、多处收窗）。
static int g_view = 0;
/// 这一轮界面用哪一语（kStr 六列的下标）。启动时由平台层读一次，之后不改：
/// 换语言要重烘字形，而这是常驻进程，运行时改口只会让半屏旧字半屏新字。
/// 初值只在「main 还没跑到检测」时生效，取值口径见 [语言] 那行打印。
static int g_loc = AP_LANG_ZH;
/// 上一轮「还在动」（非静置）的会话身份键。设置屏打开「已完成」那一档后，只有
/// 从这条集合里掉出来的会话才允许响 —— 没有它，第一次打开就会把几十条几天前静置的
/// 会话当成跳变，连吵十几轮（alert.h 里同一条门）。
static std::set<std::string> g_active;
static const int kSetW = 420;            // settings.html #win{width:420px}

// ─────────────────────────────────────────────────────────────────────────────
// 画原子：全部是 r14/pin.html 里那一套（胶囊 / 头像环 / 左侧状态条 / 引用块）
// ─────────────────────────────────────────────────────────────────────────────
static const float kPillH = kTierPx[T_PILL] + 2 * 2.0f + 2 * 1.0f;   // 15.5：leading-none 的 9.5 + py-[2px] + 上下 1px 边
static const float kCardR = 16.0f;   // #win{border-radius:16px}

static void PillRect(ImDrawList* dl, float x, float y, float w, float h, ImU32 fill, ImU32 border, float r) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, r);
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), border, r, 0, 1.0f);
}

// 稿子的胶囊/头像底不是纯色，是 background-image:linear-gradient(180deg, A, B)。
// ImGui 没有「圆角 + 渐变」这一档：AddRectFilledMultiColor 只能画直角四角渐变，
// 圆形/胶囊若拆成半盘 + 中段，接缝会落在圆心变成硬边。这里按竖向切带，
// 每带的左右边界由圆角方程解出，带色取该带中心的插值 —— 15pt 高切 20 带足够连续。
static ImU32 MixA(ImU32 a, ImU32 b, float t) {
    auto ch = [&](int i) {
        int av = (a >> (i * 8)) & 0xFF, bv = (b >> (i * 8)) & 0xFF;
        return (int)(av + (bv - av) * t + 0.5f);
    };
    return IM_COL32(ch(0), ch(1), ch(2), ch(3));
}

// 把带 alpha 的 CSS 色合成到不透明底上，拿回一个不透明色。设置屏满屏都是
// 「bg-primary/25」「border-hairline」这类半支，而两端的过渡（transition-colors）要按
// 颜色插值：直接 MixA 两支带 alpha 的色会把透明度也从 9% 插到 50%，Chrome 不是这么算的
// —— 它先把两支各自拍到底上，再在拍好的结果之间插。底取节的 kSurface1，与屏幕上真叠的那层一致。
static ImU32 Over(ImU32 base, ImU32 src) {
    const float a = (float)(src >> 24) / 255.0f;
    return MixA(base, (src & 0x00FFFFFFu) | (255u << 24), a);
}

static void GradRound(ImDrawList* dl, float x, float y, float w, float h, float r,
                      ImU32 rgbTop, ImU32 rgbBot, ImU32 border) {
    const int kN = 20;
    float rr = r > h * 0.5f ? h * 0.5f : r;
    float rr2 = r > w * 0.5f ? w * 0.5f : r;
    for (int i = 0; i < kN; i++) {
        float t0 = (float)i / kN, t1 = (float)(i + 1) / kN;
        float by0 = y + h * t0, by1 = y + h * t1;
        float bym = 0.5f * (by0 + by1) - (y + h * 0.5f);      // 到竖向圆心的距离
        float inset = 0.0f;
        float lim = h * 0.5f - rr;
        if (std::fabs(bym) > lim) {                            // 落在上下圆角区
            float d = std::fabs(bym) - lim;
            if (d >= rr) { inset = rr2; }
            else inset = rr - std::sqrt(rr * rr - d * d);
        }
        float x0 = x + inset, x1 = x + w - inset;
        if (x1 - x0 < 0.5f) continue;
        dl->AddRectFilled(ImVec2(x0, by0), ImVec2(x1, by1), MixA(rgbTop, rgbBot, 0.5f * (t0 + t1)));
    }
    if (border)
        dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), border, r, 0, 1.0f);
}

// 圆形头像底：稿子是 22px 圆 + linear-gradient(180deg, hue 26%, hue 8%) + 55% 描边。
static void GradCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 rgbTop, ImU32 rgbBot, ImU32 border) {
    GradRound(dl, c.x - r, c.y - r, 2.0f * r, 2.0f * r, r, rgbTop, rgbBot, 0);
    if (border) dl->AddCircle(c, r - 0.5f, border, 24, 1.0f);
}

// CSS 的 box-shadow = 元素形状（按 spread 外扩）被 σ=blur/2 的各向同性高斯核模糊。
// ImGui 没有模糊，只能沿「到原形边缘的距离」把浓度曲线切成一圈一圈画上去。两处都得讲对：
// ① 浓度必须取**模糊后的轮廓覆盖率**，不能取半平面解 0.5·erfc(d/(σ√2))。那条式子只在
//    「原形比 σ 大得多」时成立，而稿子里这些发光体全都比 σ 小——5px 点半径 2.5pt 对 σ=2pt、
//    3pt 宽的竖条对 σ=3pt。拿半平面去套会把边缘第一带算到 0.241，稿子实测只有 0.157；
//    旧版为此把 σ 手工放大到 0.64·blur 去凑，凑出来的结论「Chrome 比自己的规范画得肥」
//    是模型错，不是浏览器错。换成精确轮廓后没有自由参数：σ 就是 blur/2、spread 就是 CSS 写的值。
// ② 画的方式是「从外向内叠不透明实心形 + 每层浓度按还差多少补多少」，不是等宽描边圆环：
//    环宽 0.5pt 在 Retina 上只有 1 个设备像素，会被路径抗锯齿吃掉一半。
// 用稿子渲出的 2× 图核对：5px 元信息点（blur 4、α60%）12 个半径点最大偏差 0.0055、rms 0.0041；
// 3pt 竖条（blur 6、α55%）前 6 个点最大偏差 0.006。
static const float kSqrt2 = 1.41421356f;

// 宽度 2·halfW 的实心长条，一维高斯模糊后在带外距 d 处的覆盖率。halfW→∞ 退化成半平面。
static float SlabCov(float d, float halfW, float sig) {
    return 0.5f * ((float)std::erf((2.0f * halfW + d) / (sig * kSqrt2)) -
                   (float)std::erf(d / (sig * kSqrt2)));
}

static float BesselI0(float x) {
    float s = 1.0f, t = 1.0f;
    for (int k = 1; k < 64; ++k) {
        t *= (x * x) / (4.0f * k * k);
        s += t;
        if (t < 1e-7f * s) break;
    }
    return s;
}

// 半径 R 的实心圆，二维高斯模糊后在距圆心 r 处的覆盖率（先对角度积掉，剩这条一维积分）。
static float DiscCov(float r, float R, float sig) {
    const int n = 128;                       // Simpson 步数
    const float h = R / n, s2 = sig * sig, rr = r * r;
    float acc = 0.0f;
    for (int i = 0; i <= n; ++i) {
        float rho = i * h;
        float f = (rho / s2) * std::exp(-(rho * rho + rr) * (0.5f / s2)) * BesselI0(rho * r / s2);
        acc += f * ((i == 0 || i == n) ? 1.0f : (i % 2 ? 4.0f : 2.0f));
    }
    return acc * h / 3.0f;
}

static void GlowDisc(ImDrawList* dl, ImVec2 c, float R, float blur, float spread, ImU32 col) {
    const float sig = blur * 0.5f;
    const float a0 = (float)(col >> 24) / 255.0f;
    if (sig < 0.3f || a0 <= 0.0f) return;
    const float Rs = R + spread;
    const float step = 0.5f;
    // 从最外圈往里画**实心**圆盘，每圈的 alpha 取「目标覆盖 − 已画覆盖」/(1 − 已画覆盖)，
    // 于是一个像素上从外到内的所有次混合正好凑成模糊后的轮廓覆盖率。
    // 不用描边圆环：环宽 0.5pt 在 Retina 上只有 1 个设备像素，ImGui 的路径抗锯齿两边各
    // 羽化 1px，实测峰值只有该带应有浓度的一半（5px 点左沿第一像素量到 0.066，目标 0.152）。
    float done = 0.0f;
    for (float e = 3.5f * sig; e > 0.0f; e -= step) {
        float t = a0 * DiscCov(R + e + step * 0.5f, Rs, sig);
        if (t <= done) continue;
        int a = (int)(255.0f * (t - done) / (1.0f - done) + 0.5f);
        if (a <= 0) continue;
        dl->AddCircleFilled(c, R + e + step * 0.5f, (col & 0x00FFFFFFu) | ((ImU32)a << 24), 0);
        done += (1.0f - done) * ((float)a / 255.0f);   // 按真画上去的浓度反算，量化偏差不累积
    }
    dl->AddCircleFilled(c, R, col, 0);                 // 原形内部，随后被实心元素盖住
}

// 同一条曲线，原形换成长方条。取短边算覆盖率：稿子的 Rail 是 3pt 宽、39pt 高，
// 长边方向 σ(=3pt) 远小于半边长 19.5pt，那一维的覆盖率在所有画得出的带上都≈1。
static void GlowRect(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax, float rounding, float blur,
                     float spread, ImU32 col) {
    const float sig = blur * 0.5f;
    const float a0 = (float)(col >> 24) / 255.0f;
    if (sig < 0.3f || a0 <= 0.0f) return;
    const float halfW = std::min(pmax.x - pmin.x, pmax.y - pmin.y) * 0.5f + spread;
    const float step = 0.5f;
    float done = 0.0f;
    for (float e = 3.5f * sig; e > 0.0f; e -= step) {
        float t = a0 * SlabCov(e + step * 0.5f, halfW, sig);
        if (t <= done) continue;
        int a = (int)(255.0f * (t - done) / (1.0f - done) + 0.5f);
        if (a <= 0) continue;
        dl->AddRectFilled(ImVec2(pmin.x - e - step * 0.5f, pmin.y - e - step * 0.5f),
                          ImVec2(pmax.x + e + step * 0.5f, pmax.y + e + step * 0.5f),
                          (col & 0x00FFFFFFu) | ((ImU32)a << 24), rounding + e + step * 0.5f);
        done += (1.0f - done) * ((float)a / 255.0f);
    }
    dl->AddRectFilled(pmin, pmax, col, rounding);
}

// 稿子 .glass-pill 的受光：box-shadow 的 inset 0 1px 0 white/26% + ::after 那条
// 「左右各缩 10%、高 44%、白 30%→0%」的圆角高光带。backdrop-filter 那层在 ImGui 里
// 拿不到（要窗口背后的像素），但让胶囊读起来像玻璃的其实是这两条高光，不是模糊。
static void GlassSheen(ImDrawList* dl, float x, float y, float w, float h) {
    float hy = y + 1.0f, hh = h * 0.44f;
    GradRound(dl, x + w * 0.1f, hy, w * 0.8f, hh, hh * 0.5f,
              IM_COL32(255, 255, 255, 77), IM_COL32(255, 255, 255, 0), 0);
    dl->AddLine(ImVec2(x + 4.0f, y + 0.5f), ImVec2(x + w - 4.0f, y + 0.5f), IM_COL32(255, 255, 255, 66), 1.0f);
    dl->AddLine(ImVec2(x + 4.0f, y + h - 0.5f), ImVec2(x + w - 4.0f, y + h - 0.5f), IM_COL32(0, 0, 0, 66), 1.0f);
}

// 一颗玻璃胶囊：竖向渐变底 + 1px 描边 + 受光。top/bot 是 color-mix 的两个百分比。
static void GlassPill(ImDrawList* dl, float x, float y, float w, float h, float r,
                      ImU32 rgbTop, ImU32 rgbBot, ImU32 border) {
    GradRound(dl, x, y, w, h, r, rgbTop, rgbBot, border);
    GlassSheen(dl, x, y, w, h);
}

// 稿子 .flow-bar：repeating-linear-gradient(115deg, st-run 0 8px, st-run/30% 8px 14px)，
// background-size:28px 100%，pb-flow 0.9s 把 background-position 推到 28px。
// 115° 的渐变轴在屏幕坐标里是 (sin115, -cos115) = (0.906, 0.423)：条纹垂直于轴，
// 所以竖着一格高要水平偏 tan(25°)=0.467 倍；沿轴 14px 的一格换算到水平是 14/0.906=15.45px。
static void FlowStripe(ImDrawList* dl, float x0, float y0, float x1, float y1,
                       ImU32 solid, double tn) {
    const float h = y1 - y0;
    if (x1 - x0 < 1.0f || h < 1.0f) return;
    dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
    // 缝里不是凹槽底色：稿子的 repeating-gradient 第二段是 st-run 30%，铺满整条。
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), WithA(solid, 77));
    const float period = 15.45f;                // 沿轴 8px 实 + 6px 虚 → 水平 8.83 + 6.62
    const float wide = 8.83f;
    const float skew = h * 0.467f;              // 115deg 的水平切变
    float off = (float)std::fmod(tn / 0.9, 1.0) * 28.0f;   // 一格 28px / 0.9s，向右
    for (float sx = x0 - period * 3.0f - off; sx < x1 + period; sx += period) {
        ImVec2 p[4] = { {sx, y1}, {sx + wide, y1}, {sx + wide + skew, y0}, {sx + skew, y0} };
        dl->AddConvexPolyFilled(p, 4, solid);
    }
    dl->PopClipRect();
}

// CSS 的 animation-timing-function 是一条三次贝塞尔，而且**逐关键帧段各套一次**：
// 0%→50%→100% 的往复，上升段 k=ease(p)、下降段 k=1−ease(p2)（不是把整条正弦摊平，
// 也不是 ease(1−p2)——那会把曲线镜像到错误的一侧）。稿子里挂这条曲线的四个原子：
//   .anim-breathe 1.9s ease-out / .anim-halo 1.9s ease-in-out
//   .anim-ping    1.9s ease-out / .typing-dot 1.1s ease-in-out
// 二分 24 次解 x(s)=p，误差 <1e-7，远小于 8bit 量化。
static float CssBezier(float x1, float y1, float x2, float y2, float p) {
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    float lo = 0.0f, hi = 1.0f, s = p;
    for (int i = 0; i < 24; i++) {
        s = (lo + hi) * 0.5f;
        float u = 1.0f - s;
        float bx = 3.0f * u * u * s * x1 + 3.0f * u * s * s * x2 + s * s * s;
        if (bx < p) lo = s; else hi = s;
    }
    float u = 1.0f - s;
    return 3.0f * u * u * s * y1 + 3.0f * u * s * s * y2 + s * s * s;
}

static const float kEaseOut[4] = { 0.0f, 0.0f, 0.58f, 1.0f };
static const float kEaseInOut[4] = { 0.42f, 0.0f, 0.58f, 1.0f };

// 往复包络：tn 落在 [0,period) 的前半段从 0 涨到 1、后半段按同一条曲线落回 0。
static float PingK(double tn, float period, const float* ez) {
    float p = (float)std::fmod(tn < 0 ? 0.0 : tn, period) / period;
    return p < 0.5f ? CssBezier(ez[0], ez[1], ez[2], ez[3], p * 2.0f)
                    : 1.0f - CssBezier(ez[0], ez[1], ez[2], ez[3], (p - 0.5f) * 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 交互层。稿子里 await 行每个零件都写着它是 <button>（SessionRow.tsx:190 的「已读 / 稍后」、
// :137 的选项按钮），各自带 hover/active 样式；面板这边此前只画不接——一颗都点不动。
// 这里补上「点到了 → 到底做什么」，动作逐条对齐 Swift 已上线的口径（PinBoard.swift:228-250）。
//
// 命中框由「绘制」那一趟顺手登记（g_hits），下一帧拿它判悬停与点击：几何只许一处计算，
// 测量那趟不许另算一套（否则量出来的行框与点下去的框会分叉）。代价是悬停滞后一帧＝16ms，
// 比稿子自己那条 150ms 过渡短一个数量级，看不出来。
// ─────────────────────────────────────────────────────────────────────────────
enum HitAct { HIT_OPEN = 0, HIT_READ = 1, HIT_LATER = 2, HIT_OPTION = 3,
              HIT_SET = 4, HIT_SETTINGS = 5, HIT_BACK = 6, HIT_MORE = 7,
              HIT_BACKOV = 8 };
/// 设置屏上每一颗控件的身份。`Hit::row` 装这个，`Hit::opt` 装分段控件的取值。
/// 顺序与设置屏从上到下一致，改了要把 alert.cpp 的 key 表一起看一遍。
enum SetCtl { SC_NEEDS = 0, SC_AWAIT, SC_FAIL, SC_DONE, SC_SOUND, SC_NOTIFY,
              SC_TOP, SC_ROWS };
struct Hit {
    float x0, y0, x1, y1;
    int act = 0, row = 0, opt = 0;
    /// 这一颗当前是不是「开着」：开关/胶囊/分段都靠它驱动 150ms 的状态过渡（稿子的
    /// transition-colors / transition-all）。命中区是绘制那一趟登记的，状态也在那一趟给出。
    int state = 0;
    long zid = 0;
    std::string key, agent_id, native_id, label, title;
};
static std::vector<Hit> g_hits;                  // 本帧登记、下一帧消费
static std::set<std::string> g_ack;              // 「已读」：只在内存，与 Swift 的 acknowledged 同一口径
// 组号枚举在 DoAction（点入口时认是哪一组）与绘制两处都要用，定义提到这里；
// 原来只在下半部分，DoAction 够不着（第一次编译就是这么报的 G_N 未声明）。
enum { G_AWAIT, G_RUN, G_QUEUE, G_FAIL, G_DONE, G_N };
/// 面板没有滚动条（挂的是 NoScrollbar），屏幕高度就是硬上限，所以「还有 N 条」不能只是句说明：
/// 点它＝把版面换给那一组（被点名那组之外的组只留「标题＋入口」，点它们各自的入口就换过去），
/// 在同一组里连着点＝往后翻页。翻页步长取上一帧这组真摆出来的条数（g_shown）。
/// 点名那一屏顶部另有一条「返回总览」（HIT_BACKOV，row＝kFoldRow＋G_N）把 focus 清回 -1——
/// 除「那组自己清空」之外，那是回平铺的唯一一条路，所以它必须常驻屏面而不是挤不下就不画。
/// 都只在内存，与 g_ack 同一口径。
static int g_focus = -1;      // 点名要看的那一组，-1＝按稿子顺序平铺
static int g_page = 0;        // 这一组已经翻过去几条
static int g_shown[G_N];      // 上一帧每组真摆了几条（翻页步长）
// 取证口径：「收起来的条数 == 总数 − 摆出来的」这句话外部只能靠命中区表反推，而入口那条
// 的文案只有数字（两组都收 3 条时「还有 3 条」长得一样，钉坐标就分不清点的是哪一组）。
// 所以每帧把这组账写成一行诊断，配合 AGENTPING_HITS 的命中区表一起读。
static char g_fold_diag[256];
static std::map<std::string, double> g_later;    // 「稍后提醒」：会话键 → 恢复时刻（epoch 秒）

/// Tailwind 的 transition-* 默认曲线 cubic-bezier(.4,0,.2,1) + duration-150。
static const float kEaseTail[4] = { 0.4f, 0.0f, 0.2f, 1.0f };
struct Tx { float cur = 0, from = 0, target = 0; double t0 = 0, seen = 0; };
static std::map<long, Tx> g_tx;

/// 走完 0.15s 的过渡值：target 变了就从当前值重新起算，不然就是硬开硬关。
static float TxEval(long zid, float target, double now) {
    Tx& x = g_tx[zid];
    if (x.target != target) { x.from = x.cur; x.target = target; x.t0 = now; }
    x.seen = now;
    float p = (float)((now - x.t0) / 0.15);
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    x.cur = x.from + (x.target - x.from) *
          CssBezier(kEaseTail[0], kEaseTail[1], kEaseTail[2], kEaseTail[3], p);
    return x.cur;
}

/// 稿子里 await 行的行框是 `hover:bg-[var(--menu-accent)]`，而 --menu-accent 在
/// meoo-styles.css:4488 是实色 #2f6fed（class 里没有 alpha 修饰），Swift 那边换成了 white/6%。
/// 这里照抄稿子：悬停就是这块菜单选中蓝。
static const ImU32 kHoverRow = IM_COL32(47, 111, 237, 255);
static const int kHoverReadBg = 66;    // 已读 hover:bg-[var(--st-await)]/26 → 26%×255＝66
static const int kHoverLaterBg = 31;   // 稍后 hover:bg-white/12 → 31
static const int kHoverLaterFg = 255;  // 稍后 hover:text-white；常态是 text-white/70 → 179
static const int kOptScaleUp = 104;    // hover:scale-[1.04]
static const int kOptScaleDown = 95;   // active:scale-95

/// 一个可点零件的编号：六语并排时同一行会出现六次，必须带上 locale 才不会串味。
static long Zid(int loc, int row, int act, int opt) {
    return (long)loc * 1000000L + (long)row * 1000L + (long)act * 100L + (long)opt;
}
/// 设置屏的零件没有 UIRow（一行开关不对应任何会话），所以命中区另走这条：身份字段留空，
/// 只带 label 供 AGENTPING_CLICK 按名字钉。
static void AddHit(std::vector<Hit>& v, float x0, float y0, float x1, float y1,
                   int act, int row, int opt, int loc, const std::string& label, int state) {
    Hit h;
    h.x0 = x0; h.y0 = y0; h.x1 = x1; h.y1 = y1;
    h.act = act; h.row = row; h.opt = opt; h.state = state;
    h.zid = Zid(loc, row, act, opt);
    h.label = label;
    v.push_back(h);
}
static void AddHit(std::vector<Hit>& v, float x0, float y0, float x1, float y1,
                   int act, int row, int opt, const UIRow& r, int loc, const std::string& label) {
    Hit h;
    h.x0 = x0; h.y0 = y0; h.x1 = x1; h.y1 = y1;
    h.act = act; h.row = row; h.opt = opt;
    h.zid = Zid(loc, row, act, opt);
    h.key = r.key; h.agent_id = r.agent_id; h.native_id = r.native_id; h.title = r.title;
    h.label = label;
    v.push_back(h);
}
/// 过渡的当前值（不改动目标）：绘制那一趟只读，目标由输入那一趟按上帧的命中区算。
static float TxPeek(long zid) {
    auto it = g_tx.find(zid);
    return it == g_tx.end() ? 0.0f : it->second.cur;
}
/// 命中区刚出现（切屏、重扫后新增的行）时 g_tx 里还没有这条过渡，TxPeek 回 0 ——
/// 那会让一颗本来就开着的开关入场时从「关」补一段动画，看着像刚被谁点开。给它当前真值。
static float TxPeekNew(long zid, float fresh) {
    auto it = g_tx.find(zid);
    return it == g_tx.end() ? fresh : it->second.cur;
}
/// 过渡的**目标**（0/1）：稿子里有些属性不参与 transition —— 竖轨的 group-hover:opacity-0
/// 那颗 span 只有 pointer-events/absolute/w/rounded，没有 transition-*，行一悬停它就当帧消失。
/// 拿 cur 会跟着行的 150ms 底色一起淡，那是我给一个「瞬时开关」凭空加的过渡。
static bool TxOn(long zid) {
    auto it = g_tx.find(zid);
    return it != g_tx.end() && it->second.target > 0.5f;
}

static bool Inside(const Hit& h, const ImVec2& p) {
    return p.x >= h.x0 && p.x < h.x1 && p.y >= h.y0 && p.y < h.y1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 设置屏（r14/settings.html 那一屏，DOM 由稿子组件 SettingsPopover.tsx 在 Node 里跑出来）。
// 五个原子各对应稿子里一节 DOM，class 逐条抄在上面的几何常量里：
//   toolRow = :38 的采集器 <li>（20px 头像 + truncate 名字 + 右侧那颗图标按钮）
//   chipRow = :82 的「需要提醒的状态」一排胶囊        toggle = :146/:148/:149 的开关行
//   segRow  = :99 的 3/5/8 分段                      tile   = :113 的「数据源」网格
// 两处方向相反的动作，都记在这里：
// ① 稿子那些开关是 React 的本地 state，刷新就回默认档 —— 这里每一颗都真写进
//    ap::AlertCfg 并落盘（settings.txt），下一轮重扫、下一次重启都按屏幕上亮着的那样生效。
// ② 稿子里我们没有对应通路的东西一律不画：自定义工具名 + 色卡 + 「添加」、「模拟断开连接」、
//    「模拟事件流」、「插件上报 · 未启用」、「Webhook · 未启用」。摆上去就是一格假的。
//    代价是这屏比稿子少四处表面，且「数据源」从三列变成两列。
// ─────────────────────────────────────────────────────────────────────────────
static const int kNavRow = 90;    // 屏头那两颗导航胶囊占的 row 位：不挤 SetCtl 的 0..7，Zid 才不撞号
static const int kFoldRow = 80;   // 「还有 N 条」占 80..84（一组一颗），85＝返回总览，与 kNavRow=90 错开

struct SecBox { float x0, y0, x1, y1; };   // 一节「rounded-xl border bg-surface-1 p-3」占的方框

/// 卡框 + 节底，全部提交到 background 层（它在所有窗口之前合成，所以末尾补画也盖不住字）。
/// 叠的顺序是「卡底 → 每一节的底与边 → 卡描边」；反过来节底会把卡边抹掉。
static void CardFrame(float x, float y, float w, float h, const std::vector<SecBox>* secs = nullptr) {
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), kBg, kCardR);
    if (secs)
        for (const auto& s : *secs) {
            bg->AddRectFilled(ImVec2(s.x0, s.y0), ImVec2(s.x1, s.y1), kSurface1, kSecR);
            bg->AddRect(ImVec2(s.x0 + 0.5f, s.y0 + 0.5f), ImVec2(s.x1 - 0.5f, s.y1 - 0.5f),
                        kHair, kSecR, 0, 1.0f);
        }
    bg->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), kHair, kCardR, 0, 1.0f);
}

/// 一行里 items-center 的落笔点：先把 CSS 行框（lineH）在行高（rowH）里居中，再把字在行框里居中。
/// 直接 TextTop(y, rowH) 等于把「行框高」当成「字形高」，差出来是 0.4~1.6pt 的下坠。
static float CellTop(float y, float rowH, float lineH, int tier, int loc) {
    return TextTop(y + (rowH - lineH) * 0.5f, lineH, tier, loc);
}

/// 稿子分段控件里「未选中」那一档（SettingsPopover.tsx:101：rounded px-2 py-0.5 text-[11px]
/// border border-hairline text-muted-foreground hover:text-foreground）。
/// 切屏那两颗胶囊（pin 屏的「设置」、设置屏的「返回」）在稿子里不存在——稿子靠页面外层 nav 切屏，
/// 这块面板没有外层。但也不许凭空造样式，所以复用这一档现成的配方：同圆角、同边色、同字号与同 hover 变色。
static float GhostPillW(const char* key, int loc) {
    return 2 * 8.0f + 2 * 1.0f + TextWidth(T(key, loc), loc, T_SEG);
}
static void GhostPill(ImDrawList* dl, float x, float y, float w, int loc, const char* key, long z) {
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + kSegH - 0.5f), kHair, 4.0f, 0, 1.0f);
    DrawTextRun(dl, x + 9.0f, CellTop(y, kSegH, kSegLine, T_SEG, loc), T(key, loc), loc,
                MixA(kMutedFg, kFg, TxPeek(z)), T_SEG);
}

/// 点设置屏的一颗：改内存 → 落盘 → 要现场生效的立刻生效。
/// 落盘失败必须喊出来：屏幕上亮着、盘上没改，重启就回旧档，那是最难查的一种分叉。
static void FlipSet(int ctl, int opt) {
    const char* key = "";
    bool b = false;                       // 「屏上那一颗」改完是不是亮着
    switch (ctl) {
        case SC_NEEDS:  g_cfg.needs_user = !g_cfg.needs_user; b = g_cfg.needs_user; key = "needs_you"; break;
        case SC_AWAIT:  g_cfg.await_perm = !g_cfg.await_perm; b = g_cfg.await_perm; key = "await_confirm"; break;
        case SC_FAIL:   g_cfg.failed     = !g_cfg.failed;     b = g_cfg.failed;     key = "failed"; break;
        case SC_DONE:   g_cfg.done       = !g_cfg.done;       b = g_cfg.done;       key = "st_done"; break;
        case SC_SOUND:  g_cfg.sound      = !g_cfg.sound;      b = g_cfg.sound;      key = "sound"; break;
        // 屏上那颗叫「系统通知」，账上那项叫 dnd（免打扰）：同一件事的两个方向，所以这里必须取反。
        // 日志把两边都念出来，对账时才不会把「一个开一个关」误读成 bug。
        case SC_NOTIFY: g_cfg.dnd        = !g_cfg.dnd;        b = !g_cfg.dnd;       key = "sys_notify"; break;
        case SC_TOP:    g_cfg.on_top     = !g_cfg.on_top;      b = g_cfg.on_top;    key = "on_top"; break;
        case SC_ROWS:   g_cfg.max_rows   = opt;               b = true;             key = "max_rows"; break;
        default: return;
    }
    char val[64];
    if (ctl == SC_ROWS)      snprintf(val, sizeof val, "%d 条", g_cfg.max_rows);
    else if (ctl == SC_NOTIFY) snprintf(val, sizeof val, "%s（账上 dnd＝%d）", b ? "开" : "关", g_cfg.dnd ? 1 : 0);
    else                     snprintf(val, sizeof val, "%s", b ? "开" : "关");
    std::string err;
    const bool wrote = ap::save_alert_cfg(g_cfg, &err);
    printf("[设置] %s ＝ %s｜%s\n", T(key, 0), val,
           wrote ? "已落盘" : (std::string("落盘失败：") + (err.empty() ? "原因未回" : err)).c_str());
    fflush(stdout);
    if (ctl == SC_TOP && g_win) {
        // 「常驻置顶」不是画出来的：GLFW 的 FLOATING 在 Cocoa 落到 NSFloatingWindowLevel，
        // 在 X11 落到 _NET_WM_STATE_ABOVE。生效与否由 sb_float_window() 直接向系统要窗口层级——
        // 那是可反驳的读数，不是我们在日志里自己说「我置顶了」。
        glfwSetWindowAttrib(g_win, GLFW_FLOATING, g_cfg.on_top ? GLFW_TRUE : GLFW_FALSE);
        sb_float_window();
    }
    if (ctl == SC_ROWS) g_relayout = true;      // 每组条数变了，卡高跟着变
}

/// 画设置屏，返回内容底部 y。每一节先按同一套算法量一遍高度（measure-then-draw，
/// 与 DrawSessionRow 同口径）：够摆才真画并登记命中区，摆不下的整节收进末行那条读数。
static float drawSettings(ImDrawList* dl, float L, float R, float maxBot, int loc, float cy,
                          std::vector<SecBox>* secs) {
    const float ix0 = L + 13.0f, ix1 = R - 13.0f;      // 节框是 border-box：1px 边在宽内，内容再进 1 + p-3
    const float track = kTierPx[T_MLBL] * 0.16f;       // 节头那条 tracking-[0.16em]（脚注没有，别顺手加）

    // ── 屏头：标题在左、返回在右（与 pin 屏那侧的「设置」同一个角）──────────────
    {
        const long z = Zid(loc, kNavRow, HIT_BACK, 0);
        const float pw = GhostPillW("back", loc);
        DrawTextRun(dl, L, CellTop(cy, kSegH, kHead10Line, T_MLBL, loc), UpAscii(T("set_title", loc)),
                    loc, kStIdle, T_MLBL, track);
        GhostPill(dl, R - pw, cy, pw, loc, "back", z);
        AddHit(g_hits, R - pw, cy, R, cy + kSegH, HIT_BACK, kNavRow, 0, loc, T("back", loc), 0);
        cy += kSegH + kSectionGap;
    }

    // ── 采集器行：右那一格在稿子里是「模拟断开连接」的图标按钮。采集器是编译进去的，
    //    断开这条路不存在，所以同一格换成有真数据源的读数：本机没有／读不到／今天没活动，
    //    三者都不占的就是今天该采集器真摆出来的会话数。判据与 pin 屏底部那两行同源（RebuildPanel）。
    auto toolRow = [&](float y, const Tool& t, bool d) -> float {
        if (d) {
            dl->AddRectFilled(ImVec2(ix0, y), ImVec2(ix1, y + kToolLiH), kSurface2, 8.0f);
            dl->AddRect(ImVec2(ix0 + 0.5f, y + 0.5f), ImVec2(ix1 - 0.5f, y + kToolLiH - 0.5f),
                        Over(kSurface2, kHair), 8.0f, 0, 1.0f);
            const float acx = ix0 + 1.0f + 8.0f + 10.0f, acy = y + kToolLiH * 0.5f;
            GradCircle(dl, ImVec2(acx, acy), 10.0f, WithA(t.hue, 66), WithA(t.hue, 20), WithA(t.hue, 140));
            DrawTextRun(dl, acx - TextWidth(t.initial, loc, T_GLYPH9) * 0.5f,
                        TextMid(acy, T_GLYPH9, loc), t.initial, loc, t.hue, T_GLYPH9);
            std::string rs;
            ImU32 rc = kStIdle;
            if (!t.available)        rs = T("absent", loc);
            else if (t.unreadable) { rs = T("unreadable", loc); rc = WithA(kStFail, 210); }
            else if (t.quiet)        rs = T("quiet_agents", loc);
            else { char nb[64]; snprintf(nb, sizeof nb, "%s · %d", T("sessions_today", loc), t.rows); rs = nb; }
            const float rw = TextWidth(rs, loc, T_MLBL);
            const float nx = ix0 + 1.0f + 8.0f + 20.0f + 8.0f;
            const float rx = ix1 - 1.0f - 8.0f - rw;
            DrawTextRun(dl, nx, CellTop(y, kToolLiH, kToolNameLine, T_TOOL, loc),
                        ClipToWidth(t.name, rx - 8.0f - nx, loc, T_TOOL), loc, kFg, T_TOOL);
            DrawTextRun(dl, rx, CellTop(y, kToolLiH, kHead10Line, T_MLBL, loc), rs, loc, rc, T_MLBL);
        }
        return kToolLiH;
    };

    // 四档状态：屏上的名目与 alert.cpp 的字段一一对应，顺序就是 SetCtl 的顺序。
    const int kChipCtl[4] = {SC_NEEDS, SC_AWAIT, SC_FAIL, SC_DONE};
    const char* kChipKey[4] = {"needs_you", "await_confirm", "failed", "st_done"};
    auto chipOn = [&](int ctl) {
        switch (ctl) {
            case SC_NEEDS: return g_cfg.needs_user;
            case SC_AWAIT: return g_cfg.await_perm;
            case SC_FAIL:  return g_cfg.failed;
            default:       return g_cfg.done;
        }
    };
    // 「需要提醒的状态」那一排：稿子是 <div class="mono flex gap-1"> 里三颗 span，
    // `rounded px-1.5 py-0.5 text-[10px]`，亮着的＝bg-st-await/14 + text-st-await，灭的＝border-hairline + text-st-idle。
    // 两条实测细节照抄：① 未选中那颗多了 1px 边，但父 flex 默认 align-items:stretch 把两颗一起
    // 拉到 21 高，于是文字起笔差 1pt（亮 +2、灭 +3）；② 亮着的四档共用同一支琥珀色，不是一档一色。
    // 唯一一处「加东西」：稿子里它们是点不动的 span，这里必须是能改档的按钮，
    // 并把稿子里没有的「等你回复」补成第一档（should_alert 早就认这一档，屏上却没有）。
    auto chipRow = [&](float y, bool d) -> float {
        const float labW = TextWidth(T("alert_states", loc), loc, T_LABEL);
        float cw[4], rowW = 0;
        for (int i = 0; i < 4; i++) {
            cw[i] = 2 * 6.0f + TextWidth(T(kChipKey[i], loc), loc, T_MLBL)
                  + (chipOn(kChipCtl[i]) ? 0.0f : 2.0f);
            rowW += cw[i];
        }
        rowW += 3 * 4.0f;                                     // flex gap-1 ＝ 4
        const bool wrap = ix0 + labW + 8.0f + rowW > ix1;      // 稿子靠 #win{overflow:hidden} 截掉，我们不截
        const float rowH = wrap ? kLabelLine + kSecRowGap + kChipH : std::max(kLabelLine, kChipH);
        if (d) {
            // 换行时标签占第一行，必须顶到 y；只有单行才按 items-center 与胶囊互居。
            const float labTop = wrap ? TextTop(y, kLabelLine, T_LABEL, loc)
                                      : CellTop(y, rowH, kLabelLine, T_LABEL, loc);
            DrawTextRun(dl, ix0, labTop,
                        ClipToWidth(T("alert_states", loc), ix1 - ix0, loc, T_LABEL), loc, kMutedFg, T_LABEL);
            float cx = wrap ? ix0 : ix1 - rowW;
            const float chy = wrap ? y + kLabelLine + kSecRowGap : y;
            for (int i = 0; i < 4; i++) {
                const int ctl = kChipCtl[i];
                const bool on = chipOn(ctl);
                const ImU32 fill = on ? Over(kSurface1, WithA(kStAwait, 36)) : kSurface1;   // /14 ＝ 36/255
                if (fill != kSurface1)
                    dl->AddRectFilled(ImVec2(cx, chy), ImVec2(cx + cw[i], chy + kChipH), fill, 4.0f);
                if (!on)
                    dl->AddRect(ImVec2(cx + 0.5f, chy + 0.5f), ImVec2(cx + cw[i] - 0.5f, chy + kChipH - 0.5f),
                                Over(kSurface1, kHair), 4.0f, 0, 1.0f);
                DrawTextRun(dl, cx + 6.0f, TextTop(chy + (on ? 2.0f : 3.0f), kHead10Line, T_MLBL, loc),
                            T(kChipKey[i], loc), loc,
                            on ? kStAwait : MixA(kStIdle, kFg, TxPeek(Zid(loc, ctl, HIT_SET, 0))), T_MLBL);
                AddHit(g_hits, cx, chy, cx + cw[i], chy + kChipH, HIT_SET, ctl, 0, loc,
                       T(kChipKey[i], 0), on ? 1 : 0);
                cx += cw[i] + 4.0f;
            }
        }
        return rowH;
    };
    // 开关行：整行 <button>（label 与轨在同一个 button 里，点哪儿都算）＋ 18×32 轨 ＋ 12px knob。
    // knob 的 left 是 2（关）／16（开），相对 padding box，所以圆心＝轨左 +3+6 / +17+6。
    auto toggle = [&](float y, int ctl, const char* key, bool on, bool d) -> float {
        const float tx = ix1 - kToggleW;
        if (d) {
            const ImU32 f_off = kSurface2, f_on = Over(kSurface1, WithA(kPrimary, 64));   // bg-primary/25
            const float k = TxPeekNew(Zid(loc, ctl, HIT_SET, 0) | (1L << 41), on ? 1.0f : 0.0f);
            dl->AddRectFilled(ImVec2(tx, y), ImVec2(tx + kToggleW, y + kToggleH), MixA(f_off, f_on, k),
                              kToggleH * 0.5f);
            dl->AddRect(ImVec2(tx + 0.5f, y + 0.5f), ImVec2(tx + kToggleW - 0.5f, y + kToggleH - 0.5f),
                        MixA(Over(f_off, kHair), Over(f_on, WithA(kPrimary, 128)), k),        // border-primary/50
                        kToggleH * 0.5f, 0, 1.0f);
            dl->AddCircleFilled(ImVec2(tx + 9.0f + 14.0f * k, y + kToggleH * 0.5f),
                                kKnobD * 0.5f, MixA(kStIdle, kPrimary, k));
            DrawTextRun(dl, ix0, CellTop(y, kToggleH, kLabelLine, T_LABEL, loc),
                        ClipToWidth(T(key, loc), tx - 8.0f - ix0, loc, T_LABEL), loc, kMutedFg, T_LABEL);
            AddHit(g_hits, ix0, y, ix1, y + kToggleH, HIT_SET, ctl, 0, loc, T(key, 0), on ? 1 : 0);
        }
        return kToggleH;
    };
    // 3/5/8 分段：选中那颗 bg-primary + 反色字、没有边；未选中有 1px 边。于是亮着的比灭的窄 2、
    // 文字起笔 +2 vs +3。几何按当前档硬切（稿子只写了 transition-colors，宽度和边的有无都不在
    // 过渡名单里），只有背景色与字色插那 150ms。
    auto segRow = [&](float y, int ctl, const char* key, const int* vals, int n, int cur, bool d) -> float {
        float bw[4], rowW = 0;
        char nb[8];
        for (int i = 0; i < n; i++) {
            snprintf(nb, sizeof nb, "%d", vals[i]);
            bw[i] = 2 * 8.0f + TextWidth(nb, loc, T_SEG) + (vals[i] == cur ? 0.0f : 2.0f);
            rowW += bw[i];
        }
        rowW += (n - 1) * 4.0f;
        const float labW = TextWidth(T(key, loc), loc, T_LABEL);
        const bool wrap = ix0 + labW + 8.0f + rowW > ix1;
        const float rowH = wrap ? kLabelLine + kSecRowGap + kSegH : std::max(kLabelLine, kSegH);
        if (d) {
            // 同 chipRow：换行时标签顶到 y，单行才与分段按钮互居。
            const float labTop = wrap ? TextTop(y, kLabelLine, T_LABEL, loc)
                                      : CellTop(y, rowH, kLabelLine, T_LABEL, loc);
            DrawTextRun(dl, ix0, labTop,
                        ClipToWidth(T(key, loc), ix1 - ix0, loc, T_LABEL), loc, kMutedFg, T_LABEL);
            float cx = wrap ? ix0 : ix1 - rowW;
            const float by = wrap ? y + kLabelLine + kSecRowGap : y;
            for (int i = 0; i < n; i++) {
                const bool on = vals[i] == cur;
                snprintf(nb, sizeof nb, "%d", vals[i]);
                const long z = Zid(loc, ctl, HIT_SET, vals[i]);
                const float k = TxPeekNew(z | (1L << 41), on ? 1.0f : 0.0f);
                dl->AddRectFilled(ImVec2(cx, by), ImVec2(cx + bw[i], by + kSegH),
                                  MixA(kSurface1, kPrimary, k), 4.0f);
                if (!on)
                    dl->AddRect(ImVec2(cx + 0.5f, by + 0.5f), ImVec2(cx + bw[i] - 0.5f, by + kSegH - 0.5f),
                                Over(kSurface1, kHair), 4.0f, 0, 1.0f);
                DrawTextRun(dl, cx + 9.0f, TextTop(by + (on ? 2.0f : 3.0f), kSegLine, T_SEG, loc),
                            nb, loc, MixA(MixA(kMutedFg, kFg, TxPeek(z)), kPrimaryFg, k), T_SEG);
                AddHit(g_hits, cx, by, cx + bw[i], by + kSegH, HIT_SET, ctl, vals[i], loc, nb, on ? 1 : 0);
                cx += bw[i] + 4.0f;
            }
        }
        return rowH;
    };
    // 「数据源」那一格：稿子是 grid-cols-3，三格里只有一格本机真有通路。
    // 另两格（插件上报／Webhook）没有实现，按空态门整格不画，所以这里只剩两格、改成 2 列铺满
    // ——列数是我改的，格内的配方（10px leading-tight 居中 + px-1.5 py-1.5 + rounded-md）没改。
    auto tile = [&](float x, float y, float w, const char* key, bool on, bool d) {
        if (d) {
            const ImU32 fill = on ? Over(kSurface1, WithA(kPrimary, 26)) : kSurface1;      // bg-primary/10
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + kTileH), fill, 6.0f);
            dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + kTileH - 0.5f),
                        on ? Over(fill, WithA(kPrimary, 115)) : Over(kSurface1, kHair),       // border-primary/45
                        6.0f, 0, 1.0f);
            const std::string s = ClipToWidth(T(key, loc), w - 12.0f - 2.0f, loc, T_MLBL);
            DrawTextRun(dl, x + (w - TextWidth(s, loc, T_MLBL)) * 0.5f,
                        TextTop(y + 7.0f, kTileLine, T_MLBL, loc), s, loc, on ? kPrimary : kStIdle, T_MLBL);
        }
    };

    const int kSegVals[3] = {3, 5, 8};
    auto head = [&](float& iy, const char* key, bool d) {
        if (d) DrawTextRun(dl, ix0, TextTop(iy, kHead10Line, T_MLBL, loc), UpAscii(T(key, loc)),
                           loc, kStIdle, T_MLBL, track);
        iy += kHead10Line + kSecHeadGap;
    };
    auto foot = [&](float& iy, const char* key, bool d) {
        iy += kSecHeadGap;                                    // 稿子里脚注是 mt-2
        if (d) DrawTextRun(dl, ix0, TextTop(iy, kNoteLine, T_MLBL, loc),
                           ClipToWidth(T(key, loc), ix1 - ix0, loc, T_MLBL), loc, kStIdle, T_MLBL);
        iy += kNoteLine;
    };
    auto box = [&](float y, float iy, bool d) {
        const float h = iy - y + kSecPad + 1.0f;              // 内容高 + p-3 + 上下两条 1px 边
        if (d) secs->push_back({L, y, R, y + h});
        return h;
    };

    auto secTools = [&](float y, bool d) -> float {
        float iy = y + 1.0f + kSecPad;
        head(iy, "sec_tools", d);
        for (size_t i = 0; i < g_pd.tools.size(); i++) {
            if (i) iy += kSecRowGap;                          // space-y-1.5
            iy += toolRow(iy, g_pd.tools[i], d);
        }
        foot(iy, "note_tools", d);
        return box(y, iy, d);
    };
    auto secAlerts = [&](float y, bool d) -> float {
        float iy = y + 1.0f + kSecPad;
        head(iy, "sec_alerts", d);
        iy += chipRow(iy, d) + kSecRowGap;
        iy += toggle(iy, SC_SOUND, "sound", g_cfg.sound, d) + kSecRowGap;
        iy += toggle(iy, SC_NOTIFY, "sys_notify", !g_cfg.dnd, d);
        foot(iy, "note_snooze", d);
        return box(y, iy, d);
    };
    auto secWindow = [&](float y, bool d) -> float {
        float iy = y + 1.0f + kSecPad;
        head(iy, "sec_window", d);
        iy += toggle(iy, SC_TOP, "on_top", g_cfg.on_top, d) + kSecRowGap;
        iy += segRow(iy, SC_ROWS, "max_rows", kSegVals, 3, g_cfg.max_rows, d);
        return box(y, iy, d);
    };
    auto secSources = [&](float y, bool d) -> float {
        float iy = y + 1.0f + kSecPad;
        head(iy, "sec_sources", d);
        const float gap = 4.0f, tw = (ix1 - ix0 - gap) / 2.0f;   // 两列 gap-1
        tile(ix0, iy, tw, "src_scan", true, d);                  // 本地日志扫描：这一屏活着就是它在供数
        tile(ix0 + tw + gap, iy, tw, "src_hook", g_pd.hook_live > 0, d);
        iy += kTileH;
        return box(y, iy, d);
    };
    auto sec = [&](int i, float y, bool d) -> float {
        if (i == 0) return secTools(y, d);
        if (i == 1) return secAlerts(y, d);
        if (i == 2) return secWindow(y, d);
        return secSources(y, d);
    };

    // 「还有 N 组」那一行排在循环之外，不受 maxBot 约束：不预先给它留落点，
    // 越界就会从「少摆一组」变成「提示行自己画到窗口外」。留不留取决于到底有没有组摆不下，
    // 所以先纯量一趟定这个_bool，再按预留后的预算真正摆；第二趟只会更少摆，不会反过来。
    const float kMoreSecH = kSectionGap + kNoteLine;
    {
        float ty = cy; int tskip = 0;
        for (int i = 0; i < 4; i++) {
            const float h = sec(i, ty, false);
            if (!tskip && ty + h <= maxBot) { if (i) ty += kSectionGap; ty += h; }
            else tskip++;
        }
        if (tskip) maxBot -= kMoreSecH;
    }
    int skip = 0;
    for (int i = 0; i < 4; i++) {
        const float h = sec(i, cy, false);
        if (!skip && cy + h <= maxBot) {
            if (i) cy += kSectionGap;                           // space-y-3
            sec(i, cy, true);
            cy += h;
        } else skip++;
    }
    if (skip) {
        char buf[128];
        snprintf(buf, sizeof buf, T("more_sec", loc), skip);
        cy += kSectionGap;
        DrawTextRun(dl, ix0, TextTop(cy, kNoteLine, T_MLBL, loc), buf, loc, kStIdle, T_MLBL);
        cy += kNoteLine;
    }
    return cy;
}

// ── 动作层 ───────────────────────────────────────────────────────────────────
// 稿子给的语义只有两条能诚实兑现：
//   已读   → 本进程记住这一行不再催（呼吸环、选项行、已读按钮一起收起，引用块留着）；
//   稍后   → 20 分钟后自动恢复（settings.html:「20 分钟后恢复」）。
// 选项那颗不能「把答案提交回去」：agent-ping-hook.sh 只是 cat 一份副本就 exit 0，
// 通路是单向的（hook 的退出码会被 CLI 当成放行/拦截信号，我们永远得说「我什么都没干」）。
// 所以点选项＝文案进剪贴板 + 跳到那会话，两件事都在稿子的文案里写明白了。
static const char* ActName(int act) {
    switch (act) {
        case HIT_OPEN:     return "整行";
        case HIT_READ:     return "已读";
        case HIT_LATER:    return "稍后提醒";
        case HIT_SET:      return "设置项";
        case HIT_SETTINGS: return "设置";
        case HIT_BACK:     return "返回";
        case HIT_MORE:     return "展开";
        case HIT_BACKOV:   return "收起";
        default:           return "选项";
    }
}

/// 跳到那会话：先按方案 A 收面板（面板挡在目标窗口上时，跳过去也只是看见我们自己），
/// 再把落点打出来。ap_open 自己会逐级退让并在 reason 里写清最后落在了哪一层。
static void JumpTo(const Hit& h) {
    // 先收面板：这块浮层是 topmost，跳过去的窗口会被我们自己盖住（Swift 那边 dismiss() 同一件事）。
    if (g_panelVisible) {
        g_panelVisible = false;
        if (g_win) glfwHideWindow(g_win);
    }
    char why[512];
    int rc = ap_open(h.agent_id.c_str(), h.native_id.c_str(), why, (int)sizeof why);
    fprintf(stderr, "[落点] %s/%s → %s（ap_open %s）\n", h.agent_id.c_str(), h.native_id.c_str(),
            why[0] ? why : "无说明", rc == 0 ? " ok" : "失败");
    fflush(stderr);
}

/// 落点认的是「这个 App 自己那个 id」：scan.cpp 给 zcode/codex/workbuddy 的会话 id 加了
/// `<agent>-` 前缀防撞名，deep link 里必须摘掉。面板点击和横幅点击两处都要这条，
/// 所以只写一份 —— 两处各抄一遍迟早出现「点行能跳、点横幅跳不到」。
static std::string NativeId(const std::string& agent_id, const std::string& session_id) {
    std::string out = session_id;
    const std::string pre = agent_id + "-";
    if (out.compare(0, pre.size(), pre) == 0) out.erase(0, pre.size());
    return out;
}

// ── 点横幅要能回到那一条会话 ────────────────────────────────────────────────
// 平台侧的点击回调不保证落在主线程（macOS 的 UN delegate 由系统队列投递），
// 而 ap_open 全是 AppKit 调用，所以这里只登记，真跳转排在主循环那一趟。
static std::mutex g_bmtx;
static std::string g_bagent, g_bses;
static bool g_bpending = false;

static void OnBannerClick(const char* agent, const char* session, void*) {
    std::lock_guard<std::mutex> lk(g_bmtx);
    g_bagent = agent ? agent : "";
    g_bses = session ? session : "";
    g_bpending = !g_bagent.empty();
    fprintf(stderr, "[横幅点击] 登记 %s/%s\n", g_bagent.c_str(), g_bses.c_str());
    fflush(stderr);
}

static void DrainBannerClick() {
    std::string agent, ses;
    {
        std::lock_guard<std::mutex> lk(g_bmtx);
        if (!g_bpending) return;
        g_bpending = false;
        agent = g_bagent; ses = g_bses;
    }
    Hit h;
    h.agent_id = agent;
    h.native_id = NativeId(agent, ses);
    JumpTo(h);
}

/// 就地翻这一行的状态：下一轮重扫（3s）才会走 RebuildPanel，点下去必须马上看见反应。
static void MarkRow(const Hit& h, bool ack, bool later) {
    for (auto& r : g_pd.rows) {
        if (r.key != h.key) continue;
        if (ack) r.acked = true;
        if (later) r.snoozed = true;
    }
    g_relayout = true;
}

static void DoAction(const Hit& h, const ImVec2& at) {
    std::string where = std::string(ActName(h.act)) + " · " + h.key +
                        " @ (" + std::to_string((int)at.x) + "," + std::to_string((int)at.y) + ")" +
                        (h.label.empty() ? std::string() : " 「" + h.label + "」");
    fprintf(stderr, "[点击] %s\n", where.c_str());
    fflush(stderr);
    switch (h.act) {
        case HIT_READ:
            g_ack.insert(h.key);
            MarkRow(h, true, false);
            return;
        case HIT_LATER:
            g_later[h.key] = (double)std::time(nullptr) + 20 * 60;
            MarkRow(h, false, true);
            return;
        case HIT_OPTION: {
            // 稿子的选项是「点它＝把这个答案带去 CLI」。单向通路给不了答案，所以能诚实做的
            // 只有两件：文案进剪贴板（到那儿直接粘），面板跳到那会话。
            if (!h.label.empty()) ImGui::SetClipboardText(h.label.c_str());
            if (!g_shot_to) JumpTo(h);
            return;
        }
        case HIT_SETTINGS:                    // 换屏：同一块窗口换内容，不另开窗口
            g_view = 1;
            g_relayout = true;
            return;
        case HIT_BACK:
            g_view = 0;
            g_relayout = true;
            return;
        case HIT_MORE: {                        // 「还有 N 条」：把版面换给这一组；在同一组上连着点＝翻页
            int g = h.row - kFoldRow;
            if (g < 0 || g >= G_N) return;
            if (g_focus == g) g_page += g_shown[g] > 0 ? g_shown[g] : 1;
            else { g_focus = g; g_page = 0; }
            g_relayout = true;
            return;
        }
        case HIT_BACKOV:                       // 「返回总览」：把版面从这一组换回平铺
            g_focus = -1;
            g_page = 0;
            g_relayout = true;
            return;
        case HIT_SET:                          // 设置屏的一颗：改档 + 落盘 + 该生效的立刻生效
            FlipSet(h.row, h.opt);
            return;
        default:                        // HIT_OPEN：整行 / 引用块都只负责跳
            if (!g_shot_to) JumpTo(h);
            return;
    }
}

static long g_press = -1;               // 按下时命中的那颗区，抬起时要在同一颗里才算点击
static bool g_was_down = false;         // 上一帧的按下态：抬起那一帧才判一次点击

/// 取证的钉法允许直接写零件名（「已读」「稍后提醒」「选项2」），不必抄坐标：坐标由绘制那一趟
/// 算出来，行数一漂就变了，抄死就会钉到按钮旁边的空白上，然后把「没亮」误判成命中失效。
static bool ResolveZone(const char* want, ImVec2& out) {
    float x = 0, y = 0;
    if (sscanf(want, "%f,%f", &x, &y) == 2) { out = ImVec2(x, y); return true; }
    int nth = 0;
    if (sscanf(want, "选项%d", &nth) == 1 && nth > 0) {
        int seen = 0;
        for (const auto& h : g_hits)
            if (h.act == HIT_OPTION && ++seen == nth) { out = ImVec2((h.x0 + h.x1) * 0.5f, (h.y0 + h.y1) * 0.5f); return true; }
        return false;
    }
    // 「展开N」＝按组号钉第 N 组那条入口（0 介入 1 运行 2 排队 3 失败 4 已结束）。
    // 为什么非要有这一形：坐标钉法在点第二颗时会失效——第一颗已经把版面换了，那颗入口跟着
    // 挪位置，"170,847;170,847" 的第二颗实测钉到了别的行上。组号是唯一跟着语义走的地址。
    int grp = -1;
    if (sscanf(want, "展开%d", &grp) == 1 && grp >= 0 && grp < G_N) {
        for (const auto& h : g_hits)
            if (h.act == HIT_MORE && h.row == kFoldRow + grp) { out = ImVec2((h.x0 + h.x1) * 0.5f, (h.y0 + h.y1) * 0.5f); return true; }
        return false;
    }
    for (const auto& h : g_hits)
        if (!strcmp(want, ActName(h.act)) || h.label == want) {
            out = ImVec2((h.x0 + h.x1) * 0.5f, (h.y0 + h.y1) * 0.5f);
            return true;
        }
    return false;
}

/// 合成输入：取证时鼠标不会自己飘到那颗按钮上，所以允许用环境变量钉一个面板内坐标。
/// AGENTPING_HOVER="x,y"／零件名 只算悬停；AGENTPING_DOWN=… 再多按着一动不动（拍稿子的
/// active:scale-95 那一帧——没有抬起就不算一次点击，所以它不会跳走）；AGENTPING_CLICK=… 走完整
/// 「按下→抬起」，动作真发生。三条都钉在真命中区上，不另搭假状态。
///
/// AGENTPING_CLICK 还吃 ';' 分隔的序列（"170,847;170,847"），一颗点完空 8 帧（≈130ms，
/// 够重摆行＋150ms 过渡落定）再点下一颗。为什么要序列：折叠入口的「同一组连着点＝往后翻页」
/// 这条，一颗点完就退出的进程根本答不了；而 g_focus/g_page 只在内存，第二次启动又回到平铺，
/// 于是那句主张永远只是设计意图而不是验过的事。
static std::vector<std::string> g_click_seq = []() {
    std::vector<std::string> out;
    const char* e = getenv("AGENTPING_CLICK");
    if (!e) return out;
    for (const char* p = e; *p;) {
        const char* q = strchr(p, ';');
        if (!q) q = p + strlen(p);
        if (q > p) out.emplace_back(p, (size_t)(q - p));
        p = (*q == ';') ? q + 1 : q;
    }
    return out;
}();
static int g_click_at = 0;      // 序列走到第几颗
static int g_click_gap = 0;     // 当前这颗已经占了几帧

/// 真实指针为什么不取 io.MousePos：这块面板是系统浮层，指针在它上面时窗口往往不是 key，
/// GLFW 的 motion 回调只在窗口收到事件时才更新，浮层上实测会漏。Apple 侧直接问系统要全局
/// 指针位置再换算进窗口内容框——ImGui 的坐标系就是窗口的「点」尺寸（DisplaySize 取
/// glfwGetWindowSize，Retina 倍率走 DisplayFramebufferScale），而 CG 给的也是点，所以直接减。
/// 非 Apple 分支退回 io.MousePos（Linux 侧本机还没实机跑过，这条只是不阻断编译）。
static ImVec2 MousePos(const ImGuiIO& io) {
    ImVec2 p;
    // 序列还没走完就钉在当前这颗上；走完了钉在最后一颗（与单颗点击原本的行为一致：
    // 点完之后合成指针就留在那儿，出图轮的真指针另外被 g_shot_to 关掉）。
    if (!g_click_seq.empty()) {
        size_t i = g_click_at < (int)g_click_seq.size() ? (size_t)g_click_at : g_click_seq.size() - 1;
        if (ResolveZone(g_click_seq[i].c_str(), p)) return p;
    }
    static const char* env = []() {
        const char* e = getenv("AGENTPING_HOVER");
        if (!e) e = getenv("AGENTPING_DOWN");
        return e;
    }();
    if (env && ResolveZone(env, p)) return p;
    if (g_shot_to) return ImVec2(-1.0e6f, -1.0e6f);   // 出图轮不许被真指针带跑，见 HandleInput 里那条
#ifdef __APPLE__
    if (g_win) {
        CGEventRef ev = CGEventCreate(nullptr);
        if (ev) {
            CGPoint g = CGEventGetLocation(ev);
            CFRelease(ev);
            int px, py;
            glfwGetWindowPos(g_win, &px, &py);
            return ImVec2((float)(g.x - px), (float)(g.y - py));
        }
    }
#endif
    return io.MousePos;
}

/// 每帧在绘制之前跑：拿上一帧登记的命中区算悬停/按下，把过渡目标推给 TxEval，
/// 顺带判一次点击。命中区是上一帧的几何，滞后一帧＝16ms，比 150ms 的过渡短一个数量级。
static void HandleInput(const ImGuiIO& io, double now) {
    ImVec2 mp = MousePos(io);
    static const char* kDown = getenv("AGENTPING_DOWN");
    bool down = g_shot_to ? false : io.MouseDown[0];
    // 出图轮只认合成输入：实测他人在桌面上的真点击会落在面板上（这块是系统浮层，340×993 占着
    // 右上角），一张「悬停」证据就这么被串成了「已读之后」。真指针照常驱动，只在拍图那一轮关掉。
    //
    // 一颗点击占 8 帧：第 1 帧按下，第 2 帧抬起，DoAction 在抬起之后的第 3 帧真发生
    // （见下面 g_was_down 那段），第 4~8 帧空转，让重摆行和 150ms 过渡都走完，
    // 下一颗才钉得到新版面而不是旧几何。
    if (!g_click_seq.empty() && g_click_at < (int)g_click_seq.size()) {
        if (!g_hits.empty()) {
            down = (g_click_gap == 0);
            if (g_click_gap == 7) { g_click_at++; g_click_gap = 0; }
            else g_click_gap++;
        }
    } else if (kDown && !g_hits.empty()) {
        down = true;                    // 一直按着：稿子里这就是 :active，抬起才是一次点击
    }

    // 悬停与按下各自判自己的命中区：CSS 的 :hover/:active 会随祖先一起亮，
    // 所以行框的命中区天然包含它里面的按钮——逐区独立判包含，正好是这个语义。
    for (size_t i = 0; i < g_hits.size(); i++) {
        Hit& h = g_hits[i];
        bool inside = Inside(h, mp);
        // 选项那颗的 :hover 与 :active 抢的是同一个 transform：Tailwind 把 active 排在 hover 之后，
        // 所以按住时是 0.95 而不是 1.04×0.95。按住就不许再算悬停，否则按下态会停在 0.988。
        bool hov = inside && !(down && h.act == HIT_OPTION);
        TxEval(h.zid, hov ? 1.0f : 0.0f, now);
        if (h.act != HIT_LATER)          // 稍后那颗稿子里没有 active 缩放
            TxEval(h.zid | (1L << 40), inside && down ? 1.0f : 0.0f, now);
        // 开关／胶囊／分段的「开↔关」也走 150ms（稿子的 transition-colors 与 knob 的 transition-all）。
        // 必须挂另一个位：同一颗的悬停过渡已经占了 zid 本身，active 占了 bit40。
        if (h.act == HIT_SET)
            TxEval(h.zid | (1L << 41), h.state ? 1.0f : 0.0f, now);
    }
    // 命中区消失了（行被重扫挤掉、操作区收起了）也得退回 0，不然会亮在那儿不走。
    // 但「开↔关」那一位不许退：它是数据状态，不是指针状态。切到 pin 屏再切回来时
    // 设置屏的命中区消失过一轮，若一起归零，回来就会看见满屏开关自己从关补到开。
    for (auto& kv : g_tx)
        if (kv.second.seen != now && kv.second.target != 0.0f && !(kv.first & (1L << 41)))
            TxEval(kv.first, 0.0f, now);

    if (down) {
        g_press = -1;
        for (size_t i = g_hits.size(); i-- > 0;)      // 从后往前：按钮登记在行之后，优先级天然高于行
            if (Inside(g_hits[i], mp)) { g_press = g_hits[i].zid; break; }
    } else if (!g_was_down) {
        for (size_t i = g_hits.size(); i-- > 0;)      // 抬起也得落在同一颗上才算一次点击
            if (g_hits[i].zid == g_press && Inside(g_hits[i], mp)) { DoAction(g_hits[i], mp); break; }
        g_press = -1;
    }
    g_was_down = down;
}

// 稿子 Viz.tsx 的 Spark，SessionRow 传的是 { data: spark, w: 32, h: 12, color: 行状态色 }：
// 24 个 15 分钟格 → 折线（stroke 1.25 圆角接头）+ 线下竖向渐变面积（色 34%→0%）+ 末端 ping 点。
// 渐变面积在 ImDrawList 里没有现成 API：按段切成梯形，四个顶点各自按自己的 y 取渐变色，
// 顶点色插值后整条渐变仍是逐像素线性的，和 SVG 的 linearGradient 同一形状。
static void DrawSpark(ImDrawList* dl, float x, float y, const std::vector<int>& data,
                      ImU32 color, double tn) {
    const float W = 32.0f, H = 12.0f;
    const int n = (int)data.size();
    if (n < 2 || n > 64) return;
    int lo = data[0], hi = data[0];
    for (int v : data) { if (v < lo) lo = v; if (v > hi) hi = v; }
    const float span = (float)(hi - lo ? hi - lo : 1);
    const float stepX = W / (float)(n - 1);
    ImVec2 pts[64];
    for (int i = 0; i < n; i++)
        pts[i] = ImVec2(x + (float)i * stepX,
                        y + H - 1.5f - (float)(data[i] - lo) / span * (H - 3.0f));
    auto grad = [&](float py) { return WithA(color, (int)(87.0f * (1.0f - (py - y) / H) + 0.5f)); };
    const float bot = y + H;
    for (int i = 0; i + 1 < n; i++) {
        dl->PrimReserve(6, 4);
        ImDrawIdx b = dl->_VtxCurrentIdx;
        for (int k : {0, 1, 2, 0, 2, 3}) dl->PrimWriteIdx(b + (ImDrawIdx)k);
        dl->PrimWriteVtx(pts[i], ImVec2(0, 0), grad(pts[i].y));
        dl->PrimWriteVtx(pts[i + 1], ImVec2(0, 0), grad(pts[i + 1].y));
        dl->PrimWriteVtx(ImVec2(pts[i + 1].x, bot), ImVec2(0, 0), grad(bot));
        dl->PrimWriteVtx(ImVec2(pts[i].x, bot), ImVec2(0, 0), grad(bot));
    }
    dl->AddPolyline(pts, n, color, 1.25f, ImDrawFlags_None);
    // 末端：r=2.6 的圈挂 anim-ping（1.9s，scale .5→2.3、opacity .75→0，70% 处到位后停住），
    // 上面再叠一颗 r=1.5 的实心点。SVG 上的 opacity="0.28" 是呈现属性，CSS 动画一跑就被覆盖，
    // 所以亮起的起点是 0.75 而不是 0.28×0.75。
    float ph = (float)std::fmod(tn, 1.9) / 1.9f;
    float e = ph < 0.7f ? ph / 0.7f : 1.0f;                  // 关键帧在 70% 到位后停住
    float eo = CssBezier(kEaseOut[0], kEaseOut[1], kEaseOut[2], kEaseOut[3], e);
    if (e < 1.0f)
        dl->AddCircleFilled(ImVec2(pts[n - 1].x, pts[n - 1].y), 2.6f * (0.5f + 1.8f * eo),
                            WithA(color, (int)(191.0f * (1.0f - eo))));
    dl->AddCircleFilled(ImVec2(pts[n - 1].x, pts[n - 1].y), 1.5f, color);
}

// 采集层的 ST_NEEDS_USER = 「这一轮以无错收尾、球回到你这边」，稿子里对应的胶囊是
// 「已完成」（--st-done），不是琥珀色的等待确认；琥珀只留给真卡在授权上的 ST_AWAIT。
static ImU32 StateColor(int st) {
    switch (st) {
        case ap::ST_AWAIT: return kStAwait;
        case ap::ST_NEEDS_USER: case ap::ST_RESTING: return kStDone;
        case ap::ST_QUEUED: return kStQueue;
        case ap::ST_FAILED: return kStFail;
        case ap::ST_WORKING: return kStRun;
        default: return kMutedFg;
    }
}
static const char* StateKey(int st) {
    switch (st) {
        case ap::ST_AWAIT: return "await_confirm";     // 等待确认
        case ap::ST_NEEDS_USER: case ap::ST_RESTING: return "st_done";
        case ap::ST_QUEUED: return "queued";
        case ap::ST_FAILED: return "failed";
        default: return "running";
    }
}

// 稿子 render.mjs 的 PIN_GROUPS：五组、按这个顺序，空组不摆。
// 组号枚举本身定义在 g_focus/g_shown 旁边（DoAction 也要用），这里只留取名的键表与映射。
static const char* const kGrpKey[G_N] = {"grp_await", "grp_run", "grp_queue", "grp_fail", "grp_done"};
static int GroupOf(int st) {
    switch (st) {
        case ap::ST_AWAIT: return G_AWAIT;
        case ap::ST_WORKING: return G_RUN;
        case ap::ST_QUEUED: return G_QUEUE;
        case ap::ST_FAILED: return G_FAIL;
        default: return G_DONE;      // 说完了、今天凉了的都进「今日已结束」
    }
}
static ImU32 GrpColor(int g) {
    switch (g) {
        case G_AWAIT: return kStAwait;
        case G_RUN: return kStRun;
        case G_QUEUE: return kStQueue;
        case G_FAIL: return kStFail;
        default: return kStDone;
    }
}

// 逐码点量宽后贪心折行；超过 maxlines 就在最后一行补省略号。
// （整串反复量会随文本长度平方增长，常驻面板吃不消，所以先拆成单字再累加。）
static std::vector<std::string> WrapLines(const std::string& s, float maxw, int locale, int tier, int maxlines) {
    auto cps = DecodeUTF8(s.c_str());
    std::vector<std::string> out;
    std::string line; float lw = 0; size_t used = 0;
    for (size_t i = 0; i < cps.size(); i++) {
        unsigned int cp = cps[i];
        char buf[5] = {}; size_t n = 0;
        if (cp < 0x80) buf[n++] = (char)cp;
        else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                 buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
               buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        std::string ch(buf, n);
        float w = TextWidth(ch, locale, tier);
        if (lw + w > maxw && !line.empty()) {
            if ((int)out.size() == maxlines - 1) { used = i; break; }
            out.push_back(line); line.clear(); lw = 0;
        }
        line += ch; lw += w; used = i + 1;
    }
    if (line.empty()) { if (out.empty()) out.push_back(""); return out; }
    if (used < cps.size()) out.push_back(ClipToWidth(line, maxw, locale, tier));
    else out.push_back(line);
    if ((int)out.size() > maxlines) out.resize(maxlines);
    return out;
}

// 一张会话卡 = 左侧状态条 + 头像环 + 标题/状态胶囊 + 项目/时间
//             +（有原文才画的）引用块 +（hook 给了选项才画的）选项行 +（排队中才有的）队列胶囊与打字点
//             +（正在跑才画的）进度槽与活跃度折线
// draw=false 只回高度，供上层判断这张还放不放得下。
static const float kOptGap = 6.0f;               // 选项行 mt-[6px] + gap-1.5
static const float kOptH = kTierPx[T_FAIL] + 2 * 2.0f + 2 * 1.0f;   // 16.5：行高取 MiniBtn（leading-none 10.5 + py-[2px] + 上下 1px 边），比选项胶囊高 1pt
static const float kQueueGap = 6.0f, kQueueH = kPillH;    // 队列行 mt-6 + 一颗 Pill
static const float kTrackGap = 7.0f;                       // 进度槽行 mt-[7px]
static const float kTrackH = kTierPx[T_TURNS] * 1.5f;      // 16.5：这一行最高的是「N 轮」（11 × body 1.5），5px 槽在它里面居中
static const float kFailGap = 6.0f;                        // 失败原文行 mt-[6px]
static const float kFailLine = kTierPx[T_FAIL] * 1.5f;     // 15.75：truncate 的 mono 10.5，行框按 body 1.5

static float DrawSessionRow(ImDrawList* dl, float L, float R, float ry, const UIRow& row,
                           int loc, double tn, bool draw, int idx) {
    const float rightEdge = R - kRowPr;
    const float tx = L + kRowPl + kAvatarD + kColGap;
    ImU32 st = StateColor(row.state);
    const bool is_await = row.state == ap::ST_AWAIT;
    // 已读／稍后提醒之后，这一行的「操作区」整块收起（稿子 SessionRow 的 acked 判据，
    // Swift PinBoard.swift:332/346/411 三处同一个条件）；引用块不跟着撤——原话还在那儿，
    // 收窗回来还得靠它认这是哪件事。
    const bool mute = row.muted();
    const long zrow = Zid(loc, idx, HIT_OPEN, 0);
    // 首行的行框由最高的那件东西决定：await 行是 16.5pt 的「已读」按钮，其余行是 15.625pt 的标题。
    // 稿子里这两类行本来就差 0.875pt，混在同一组里行高不齐 —— 照抄，不许抹平。
    const float headH = (is_await && !mute) ? kOptH : kTitleLine;
    const float coreH = kRowPadY * 2 + headH + kMetaGap + kMetaLine;   // 标题行 + 元信息行

    // ── 先把四个可选块的高度量齐（同一套算法两遍都走，测量与绘制不许分叉）──
    // 引用块的条件照稿子：只有 await 行且 hook 真给回了原话才画。没有原话就空着，
    // 不许拿标题或内部 detail 冒充「CLI 问的那句」。
    std::vector<std::string> lines;
    float quote_h = 0;
    if (is_await && !row.question.empty()) {
        float inner = rightEdge - tx - 8.0f - 6.0f - 2.5f;
        lines = WrapLines(row.question, inner, loc, T_CHIP, 2);
        quote_h = kDetailGap + kDetailPadY * 2 + (float)lines.size() * kDetailLine;
    }
    // 失败行在稿子里不是引用块，是一行 truncate 的 mono 原文（可选 code 胶囊我们没有源）
    float fail_h = (!row.detail.empty()) ? kFailGap + kFailLine : 0;
    // 选项：稿子是 overflow-x-auto 横滚，这里没有滚动面，宁可让它折行也不藏选项
    std::vector<std::string> optw;                       // 每个选项的文案（已按 locale 取好）
    std::vector<float> optwid;
    float opts_h = 0;
    if (!row.options.empty() && !mute) {
        for (const std::string& o : row.options) optw.push_back(o);
        optw.push_back(T("later", loc));                 // 尾部那颗「稍后」是固定动作
        float avail = rightEdge - tx;
        float x = tx, line_y = 0;
        for (size_t i = 0; i < optw.size(); i++) {
            float w = 7 + TextWidth(optw[i], loc, i + 1 == optw.size() ? T_BTN : T_PILL) + 7;
            optwid.push_back(w);
            if (i && x + 6.0f + w > tx + avail) { line_y += kOptH + kOptGap; x = tx; }
            x += 6.0f + w;
        }
        opts_h = kOptGap + line_y + kOptH;
    }
    float queue_h = row.queue_pos > 0 ? kQueueGap + kQueueH : 0;
    // 稿子的判据只有一句 status==="run"：轮数为 0 时槽照画、只是「N 轮」这个标签为空。
    float track_h = row.state == ap::ST_WORKING ? kTrackGap + kTrackH : 0;
    float h = coreH + quote_h + opts_h + queue_h + track_h + fail_h;
    if (!draw) return h;

    // 命中区登记：整行都可点（稿子的行就是 role="button"，Swift 那边是 onTapGesture(open)）。
    // 先登记行、再登记行里的按钮 —— 消费时从后往前找，按钮自然压在行之上。
    AddHit(g_hits, L, ry, R, ry + h, HIT_OPEN, idx, 0, row, loc, std::string());
    // 行悬停＝稿子的 hover:bg-[var(--menu-accent)]：从卡底过渡到 #2f6fed，150ms。
    // 必须在状态条/头像/正文之前铺，它是背景层；稿子那层还带 clipShape(7px)，所以圆角同步行框。
    float rowk = TxPeek(zrow);
    if (rowk > 0.001f)
        dl->AddRectFilled(ImVec2(L, ry), ImVec2(R, ry + h), MixA(kBg, kHoverRow, rowk), 7.0f);

    // 呼吸环：pb-breathe 是 box-shadow 0 0 0 {0→6px} 琥珀 42%→0%，1.9s 一轮 ——
    // 零模糊 + 只有 spread，所以是一圈**硬边实心带**：外沿＝行框外扩 t，内沿＝行框（圆角 7）。
    // 只有 await 行挂这个动画，且光晕色是写死的琥珀而不是行状态色 —— 照抄。
    // 不用粗描边：ImGui 给描边的内外两条边各羽化约 1 个设备像素，实测带中段比应有浓度低
    // 4/255、外沿那一列只剩一半（差 17/255）；改成「填外圆角矩形 → 用卡底回填行框」，
    // 边界是填充级硬边，和 Chrome 的 spread 同形。必须在状态条之前画，回填才不会擦掉它。
    if (is_await && !mute) {
        float k = PingK(tn, 1.9f, kEaseOut);      // .anim-breathe = pb-breathe 1.9s ease-out
        float t = 6.0f * k;
        if (t > 0.8f) {
            int a = (int)(0.42f * (1.0f - k) * 255.0f + 0.5f);   // 42%→0%，四舍五入不许截断
            dl->AddRectFilled(ImVec2(L - t, ry - t), ImVec2(R + t, ry + h + t),
                              WithA(kStAwait, a), 7.0f + t);
            dl->AddRectFilled(ImVec2(L, ry), ImVec2(R, ry + h),
                              rowk > 0.001f ? MixA(kBg, kHoverRow, rowk) : kBg, 7.0f);
        }
    }

    // 左侧状态条：稿子 Rail = w-[3px] rounded-r-full + box-shadow 0 0 6px 色 55%，上下各缩 6px。
    // 但它在行悬停时整颗消失（group-hover:opacity-0，且这颗 span 没有 transition → 当帧没了）：
    // 底色已经是同一支琥珀→蓝，再留一条琥珀竖轨就成了稿子里没有的第二根强调条。
    float railTop = ry + 6.0f, railBot = ry + h - 6.0f;
    if (!TxOn(zrow)) {
        GlowRect(dl, ImVec2(L, railTop), ImVec2(L + kRailW, railBot), 1.5f, 6.0f, 0.0f, WithA(st, 140));
        dl->AddRectFilled(ImVec2(L, railTop), ImVec2(L + kRailW, railBot), st, 1.5f);
    }

    // 头像：22px 圆，边框 55% 色相、底 26%→8% 渐变、里面是名字首字母
    float ay = ry + kRowPadY + 1.0f;
    float acx = L + kRowPl + kAvatarD * 0.5f, acy = ay + kAvatarD * 0.5f;
    GradCircle(dl, ImVec2(acx, acy), kAvatarD * 0.5f, WithA(row.hue, 66), WithA(row.hue, 20),
               WithA(row.hue, 140));
    {   // ToolGlyph 的首字母：fontSize = round(size*0.46) = 10，family 跟正文（不是 mono），
        // 在 22px 圆盘里居中 —— 稿子里它是 inline-flex items-center justify-center。
        float iw = TextWidth(row.initial, loc, T_GLYPH);
        DrawTextRun(dl, acx - iw * 0.5f, TextTop(ay, kAvatarD, T_GLYPH, loc), row.initial,
                    loc, row.hue, T_GLYPH);
    }

    // 第一行右侧：await 行是「已读」按钮 + 等待确认胶囊，其余只有状态胶囊。
    // 先量右侧这一坨的实宽，标题才知道自己剩多少（否则长标题会压在胶囊下）。
    // 宽度都要带上 1px 边框：稿子是 border-box，px-[6px] 之外还有左右各 1pt 的边。
    std::string plab = T(is_await ? "await_confirm" : StateKey(row.state), loc);
    float pillW = 7 + 5 + 4 + TextWidth(plab, loc, T_PILL) + 7;
    std::string readlab = (is_await && !mute) ? T("mark_read", loc) : "";
    float readW = readlab.empty() ? 0 : 7 + TextWidth(readlab, loc, T_BTN) + 7;
    float reserved = pillW + (readW ? 6.0f + readW : 0);
    // 稿子里 await 行的标题就是「AskUserQuestion 需要你确认」——工具名 + 这句话，
    // 不是转录本的最后一句人话（那句在引用块里才是它的位子）。hook 没给工具名就退回原标题。
    std::string ttl = row.title;
    if (is_await && !row.tool.empty()) {
        ttl = row.tool; ttl += ' '; ttl += T("ask_confirm", loc);
    }
    float ty = TextTop(ry + kRowPadY, headH, T_TITLE, loc);
    // isNew 的 4px 辉光点：稿子是这一行的第一个 flex 子（gap-1.5＝6px），
    // 色写死 STATUS_HEX.run（不跟行状态走），底色上还叠一层 34%/28% 白 75% 的高光。
    float titleX = tx;
    const float kDotD = 4.0f, kDotGap = 6.0f;      // size-[4px] + gap-1.5
    if (row.is_new) titleX += kDotD + kDotGap;
    DrawTextRun(dl, titleX, ty, ClipToWidth(ttl, rightEdge - titleX - reserved - 6.0f, loc, T_TITLE),
                loc, kTitle, T_TITLE);
    if (row.is_new) {
        float dcx = tx + kDotD * 0.5f, dcy = ry + kRowPadY + headH * 0.5f;
        // pb-halo：box-shadow 从 0 0 0 0 走到 0 0 7px 2px（currentColor 40%→55%），1.9s
        // ease-in-out 往复。关键帧之间 box-shadow 是逐字段线性插值，所以三个数都乘同一个 k：
        // blur=7k、spread=2k、alpha=0.40+0.15k。
        float k = PingK(tn, 1.9f, kEaseInOut);    // .anim-halo = pb-halo 1.9s ease-in-out
        GlowDisc(dl, ImVec2(dcx, dcy), kDotD * 0.5f, 7.0f * k, 2.0f * k,
                 WithA(kStRun, (int)(102 + 38 * k)));
        dl->AddCircleFilled(ImVec2(dcx, dcy), kDotD * 0.5f, kStRun);
        dl->AddCircleFilled(ImVec2(dcx - 0.6f, dcy - 0.7f), 0.9f, IM_COL32(255, 255, 255, 191));
    }
    float pillX = rightEdge - pillW, pillY = ry + kRowPadY + (headH - kPillH) * 0.5f;
    if (readW) {
        float bx = pillX - 6.0f - readW, byy = ry + kRowPadY;
        // hover:bg-[var(--st-await)]/26：底从 16% 过渡到 26%，边框与字色不动（稿子只有这一条 hover）
        float rk = TxPeek(Zid(loc, idx, HIT_READ, 0));
        int bg = 41 + (int)((kHoverReadBg - 41) * rk + 0.5f);
        PillRect(dl, bx, byy, readW, kOptH, WithA(kStAwait, bg), WithA(kStAwait, 115), 5.0f);
        DrawTextRun(dl, bx + 7, TextTop(byy + 3.0f, kTierPx[T_BTN], T_BTN, loc), readlab,
                    loc, kStAwait, T_BTN);
        AddHit(g_hits, bx, byy, bx + readW, byy + kOptH, HIT_READ, idx, 0, row, loc, readlab);
    }
    // 状态胶囊＝稿子的 StatusChip → Pill（非 strong 档）：22%→7% 竖向 + 40% 边 + rounded-full。
    // strong 档（38%→16%）只给可点的选项按钮，这里不是按钮，不许借。
    GlassPill(dl, pillX, pillY, pillW, kPillH, kPillH * 0.5f, WithA(st, 56), WithA(st, 18), WithA(st, 102));
    // 正在跑的那颗点按 draft 的 anim-blink 1.1s 闪。点外的弱光是 box-shadow 0 0 4px 色 70%，
    // muted（已读/稍后）才撤 —— 我们没有这两个态，所以恒画。
    int dotA = (row.state == ap::ST_WORKING && std::fmod(tn, 1.1) >= 0.5) ? 64 : 255;
    GlowDisc(dl, ImVec2(pillX + 7 + 2.5f, pillY + kPillH * 0.5f), 2.5f, 4.0f, 0.0f, WithA(st, 179));
    dl->AddCircleFilled(ImVec2(pillX + 7 + 2.5f, pillY + kPillH * 0.5f), 2.5f, WithA(st, dotA));
    DrawTextRun(dl, pillX + 7 + 5 + 4, TextTop(pillY + 3.0f, kTierPx[T_PILL], T_PILL, loc),
                plab, loc, st, T_PILL);

    // 元信息行：色点 + 项目目录 + 右侧「多久以前」
    float mtop = ry + kRowPadY + headH + kMetaGap;
    float my = TextTop(mtop, kMetaLine, T_SM, loc);
    float mcy = mtop + kMetaLine * 0.5f;
    // 元信息行的色点：5px 圆 + box-shadow 0 0 4px hue 60%（ToolGlyph 里同一写法）
    GlowDisc(dl, ImVec2(tx + 2.5f, mcy), 2.5f, 4.0f, 0.0f, WithA(row.hue, 153));
    dl->AddCircleFilled(ImVec2(tx + 2.5f, mcy), 2.5f, row.hue);
    std::string ago = Ago(row.age, loc);
    float agoW = TextWidth(ago, loc, T_TIME);
    DrawTextRun(dl, tx + 5 + 5, my, ClipToWidth(row.project, rightEdge - agoW - 8.0f - tx - 10.0f, loc, T_SM),
                loc, kProject, T_SM);
    DrawTextRight(dl, rightEdge, TextTop(mtop, kMetaLine, T_TIME, loc), ago, loc, kTime, T_TIME);

    float by = ry + coreH;
    // 引用块：bg = 状态色 9%、左边 2.5px 实色、圆角只给右侧
    if (!lines.empty()) {
        float bh = kDetailPadY * 2 + (float)lines.size() * kDetailLine;
        by += kDetailGap;
        dl->AddRectFilled(ImVec2(tx, by), ImVec2(rightEdge, by + bh), WithA(st, 23), 5.0f,
                          ImDrawFlags_RoundCornersRight);
        dl->AddRectFilled(ImVec2(tx, by), ImVec2(tx + 2.5f, by + bh), st);
        for (size_t i = 0; i < lines.size(); i++)
            DrawTextRun(dl, tx + 2.5f + 8.0f,
                        TextTop(by + kDetailPadY + (float)i * kDetailLine, kDetailLine, T_CHIP, loc),
                        lines[i], loc, W(217), T_CHIP);
        by += bh;
    }
    // 选项行：每颗都是 glass-pill（38%→16% 竖向 + 40% 边）+ hover:scale-[1.04] / active:scale-95，
    // 尾部「稍后」是普通描边按钮（hover:bg-white/12 + hover:text-white，没有缩放）。
    if (!optw.empty()) {
        by += kOptGap;
        float x = tx, line_y = by;
        for (size_t i = 0; i < optw.size(); i++) {
            bool last = i + 1 == optw.size();
            if (i && x + 6.0f + optwid[i] > rightEdge) { line_y += kOptH + kOptGap; x = tx; }
            long z = Zid(loc, idx, last ? HIT_LATER : HIT_OPTION, (int)i);
            float hk = TxPeek(z), pk = last ? 0.0f : TxPeek(z | (1L << 40));
            float w = optwid[i], hgt, px = x, py = line_y;
            if (last) {
                hgt = kOptH;
                int bg = (int)(kHoverLaterBg * hk + 0.5f);
                int fg = 179 + (int)((kHoverLaterFg - 179) * hk + 0.5f);
                PillRect(dl, px, py, w, hgt, bg ? W(bg) : 0, W(46), 5.0f);
                DrawTextRun(dl, px + 7, TextTop(py, kOptH, T_BTN, loc), optw[i], loc, W(fg), T_BTN);
            } else {
                // scale 围绕按钮自己的中心：CSS 的 transform-origin 默认就是 50% 50%。
                // 两档互斥（按住时输入层已把 hk 归零），所以这里是相减而不是相乘：静止态正好
                // 落在稿子的 1.04 / 0.95 上。
                float s = 1.0f + (kOptScaleUp - 100) * 0.01f * hk - (100 - kOptScaleDown) * 0.01f * pk;
                w = optwid[i] * s; hgt = kPillH * s;
                px = x + (optwid[i] - w) * 0.5f;
                py = line_y + (kOptH - kPillH) * 0.5f + (kPillH - hgt) * 0.5f;
                GlassPill(dl, px, py, w, hgt, hgt * 0.5f,
                          WithA(kStAwait, 97), WithA(kStAwait, 41), WithA(kStAwait, 102));
                // 字号不跟着放大：字形是按档预烘进图集的，10.5pt 没有 10.9pt 这一档。
                // 稿子的 scale 会连字一起放大 4%，这里让文字在放大后的框里保持居中，
                // 差的是那 4% 的笔画粗细，不是位置。
                float tw = TextWidth(optw[i], loc, T_PILL);
                DrawTextRun(dl, px + (w - tw) * 0.5f, TextTop(py, hgt, T_PILL, loc),
                            optw[i], loc, kStAwait, T_PILL);
            }
            AddHit(g_hits, x, line_y, x + optwid[i], line_y + kOptH,
                   last ? HIT_LATER : HIT_OPTION, idx, (int)i, row, loc, optw[i]);
            x += 6.0f + optwid[i];
        }
        by = line_y + kOptH;
    }
    // 失败原文：稿子是一行 truncate 的 mono（--st-fail 85%），不是引用块 —— 出错的原因
    // 只有一行可读，压成两块会把状态条和正文搅在一起。
    if (!row.detail.empty()) {
        by += kFailGap;
        DrawTextRun(dl, tx, TextTop(by, kFailLine, T_FAIL, loc),
                    ClipToWidth(row.detail, rightEdge - tx, loc, T_FAIL),
                    loc, WithA(kStFail, 217), T_FAIL);
        by += kFailLine;
    }
    // 队列行：一颗「队列第 N 位」胶囊（带 list 图标）+ 三颗按 1.1s 依次起落的打字点
    if (row.queue_pos > 0) {
        by += kQueueGap;
        char qb[48]; snprintf(qb, sizeof qb, T("queue_pos", loc), row.queue_pos);
        float pw = 7 + 12 + 4 + TextWidth(qb, loc, T_PILL) + 7;
        GlassPill(dl, tx, by, pw, kQueueH, kQueueH * 0.5f, WithA(kStQueue, 56), WithA(kStQueue, 18), WithA(kStQueue, 102));
        {   // lucide「list」12×12：左侧三颗点 + 右侧三条横线（viewBox 24 缩到 12）
            float ix = tx + 7, iy = by + kQueueH * 0.5f;
            for (int k = 0; k < 3; k++) {
                float yy = iy - 3.0f + k * 3.0f;
                dl->AddCircleFilled(ImVec2(ix + 1.5f, yy), 0.7f, kStQueue);
                dl->AddLine(ImVec2(ix + 4.0f, yy), ImVec2(ix + 10.5f, yy), kStQueue, 1.0f);
            }
        }
        DrawTextRun(dl, tx + 7 + 12 + 4, TextTop(by, kQueueH, T_PILL, loc), qb, loc, kStQueue, T_PILL);
        float dx = tx + pw + 6.0f;
        for (int k = 0; k < 3; k++) {
            double ph = std::fmod(tn + k * 0.16, 1.1);
            float e = PingK(ph, 1.1f, kEaseInOut);            // .typing-dot = pb-dot 1.1s ease-in-out
            dl->AddCircleFilled(ImVec2(dx + 1.5f + k * 6.0f, by + kQueueH * 0.5f - 2.0f * e), 1.5f,
                                WithA(kStQueue, (int)(64 + 191 * e)));
        }
        by += kQueueH;
    }
    // 进度槽：稿子的 turn-track —— 5px 凹槽（黑 34%→白 7% 竖向）里铺 115° 流动斜纹，
    // 右边跟一个「N 轮」。总轮数本机日志没有记录，所以 fill 宽 0%，只有流动（draft 的 !known 分支）。
    if (track_h > 0) {
        by += kTrackGap;
        // 稿子的 flex 是 items-center gap-2（8px），末尾还挂一个 fmtDuration(0) 渲染成
        // 空串的 span —— 它宽度 0，但前面的 gap 照算，所以 Spark 的右沿距行右边 8px。
        const float kSpW = 32.0f, kSpH = 12.0f, kSpGap = 8.0f;
        bool has_spark = row.spark.size() > 1;
        float spv = has_spark ? kSpW + kSpGap * 2.0f : 0.0f;
        char tb[32]; snprintf(tb, sizeof tb, "%d %s", row.turns, T("turns", loc));
        float lbW = row.turns > 0 ? TextWidth(tb, loc, T_TURNS) : 0.0f;
        float tw = rightEdge - tx - kSpGap - lbW - spv;
        if (tw < 16.0f) tw = 16.0f;
        float gy0 = by + (kTrackH - 5.0f) * 0.5f, gy1 = gy0 + 5.0f;
        GradRound(dl, tx, gy0, tw, 5.0f, 2.5f, IM_COL32(0, 0, 0, 87),
                  IM_COL32(255, 255, 255, 18), 0);
        FlowStripe(dl, tx + 1.0f, gy0 + 0.5f, tx + tw - 1.0f, gy1 - 0.5f, kStRun, tn);
        // .turn-rail 的两条 inset 是上黑下白：0 1px 2px black/55% + 0 -1px 0 white/7%
        dl->AddLine(ImVec2(tx + 3.0f, gy0 + 0.5f), ImVec2(tx + tw - 3.0f, gy0 + 0.5f),
                    IM_COL32(0, 0, 0, 140), 1.0f);
        dl->AddLine(ImVec2(tx + 3.0f, gy1 - 0.5f), ImVec2(tx + tw - 3.0f, gy1 - 0.5f),
                    IM_COL32(255, 255, 255, 18), 1.0f);
        if (lbW > 0)
            DrawTextRun(dl, rightEdge - spv - lbW, TextTop(by, kTrackH, T_TURNS, loc), tb,
                        loc, W(224), T_TURNS);
        if (has_spark)
            DrawSpark(dl, rightEdge - kSpGap - kSpW, by + (kTrackH - kSpH) * 0.5f, row.spark, st, tn);
        by += kTrackH;
    }
    return h;
}

static void RebuildPanel(const ap::Snapshot& s) {
    PanelData pd;
    const double now = s.scanned_at > 0 ? s.scanned_at : std::time(nullptr);

    for (const auto& ag : s.agents) {
        bool has_today = false;
        int shown = 0;
        for (const auto& se : ag.sessions) {
            // 「今日已结束」＝今天有过动静、但已经不跑的会话（稿子 done 组）。
            // 昨天及更早的静置会话不进面板 —— 稿子的组名就写着「今日」。
            if (se.state == ap::ST_RESTING && se.last_activity < s.window_start) continue;
            has_today = has_today || (se.last_activity >= s.window_start);
            UIRow row;
            row.state = se.state;
            row.age = now - se.last_activity;
            row.is_new = row.age < 900;            // 15 分钟内有过动静＝稿子的 isNew
            row.inferred = se.inferred_wait;
            row.title = se.title;
            row.project = se.project;
            row.agent = ag.name;
            row.agent_id = ag.id;
            row.key = ag.id + "/" + se.id;
            row.native_id = NativeId(ag.id, se.id);   // 前缀摘掉，规则与横幅点击同一份
            row.acked = g_ack.count(row.key) > 0;
            auto li = g_later.find(row.key);
            if (li != g_later.end()) {
                if (now >= li->second) g_later.erase(li);      // 到期：自动恢复，重新催
                else row.snoozed = true;
            }
            row.hue = HueFor(ag.name);
            row.initial = InitialOf(ag.name);
            row.turns = se.turns;
            row.spark = se.spark;
            row.queue_pos = se.queue_pos;
            row.question = se.question;
            row.options = se.options;
            row.tool = se.tool;
            if (se.state == ap::ST_FAILED) row.detail = se.error_text;   // 出错才有原文可引
            pd.rows.push_back(std::move(row));
            shown++;
            pd.live++;
        }
        if (shown > 0) pd.chips.push_back({ag.name, InitialOf(ag.name), shown, HueFor(ag.name)});
        // 有源、能读、但今天没有任何一条动静 —— 才算「今天没活动」
        if (ag.available && ag.scan_error.empty() && !has_today) pd.quiet.push_back(ag.name);
        if (!ag.scan_error.empty()) pd.errors.push_back(ag.name + "：" + ag.scan_error);
        // 设置屏那一行：把上面三条判据原样带过去，不再另算一套（否则同一个采集器在
        // 两块屏上一块说「本机没有」、另一块说「今天没活动」）。
        Tool tl;
        tl.name = ag.name;
        tl.initial = InitialOf(ag.name);
        tl.available = ag.available;
        tl.unreadable = !ag.scan_error.empty();
        tl.quiet = ag.available && ag.scan_error.empty() && !has_today;
        tl.rows = shown;
        tl.hue = HueFor(ag.name);
        pd.tools.push_back(std::move(tl));
    }
    pd.hook_live = s.hook_live;
    // 组内按「多久以前」升序：稿子 done 组就是 4 分钟前排在 14 小时前前面
    std::sort(pd.rows.begin(), pd.rows.end(), [](const UIRow& a, const UIRow& b) {
        int ga = GroupOf(a.state), gb = GroupOf(b.state);
        if (ga != gb) return ga < gb;
        return a.age < b.age;
    });
    // 顶栏两个数按稿子口径：needs you = 需要你介入 + 失败中断（稿子样本里没有失败行，
    // 所以它的公式看不见这一档；把失败算成 0 会让面板比真相慢），running = 运行中 + 排队中。
    for (const auto& r : pd.rows) {
        int g = GroupOf(r.state);
        if (g == G_AWAIT) { pd.needs++; pd.await_n++; }
        else if (g == G_FAIL) { pd.needs++; pd.fail_n++; }
        else if (g == G_RUN) pd.run++;
        else if (g == G_QUEUE) { pd.run++; pd.queue_n++; }
    }
    // 今日用量是整轮口径（scan_all 已汇总），不由逐项拼出来
    pd.credits = s.today_credits;
    pd.yday_credits = s.yesterday_credits;

    g_pd = std::move(pd);
    // 状态项上摆三个数：菜单栏那一行放不下词，数字只能按「介入 · 运行 · 失败」的固定顺序排。
    // 每个数是哪一档、以及「运行」里并进了排队，全部由悬停说明逐条对上——说明里连 0 的那档
    // 也念出来，不然「2 · 5」这种少了位的读法会让人以为第三档没数据而不是没有。
    char label[24], tip[256];
    {
        const int v[3] = {g_pd.await_n, g_pd.run, g_pd.fail_n};
        label[0] = '\0';
        char part[16];
        for (int i = 0; i < 3; i++) {
            if (!v[i]) continue;              // 空档不占字位：菜单栏宽度是全局共享的稀缺资源
            snprintf(part, sizeof part, "%s%d", label[0] ? " · " : "", v[i]);
            strncat(label, part, sizeof label - strlen(label) - 1);
        }
    }
    sb_set_label(label);
    {
        char q[48] = "";
        if (g_pd.queue_n) {
            char qs[40];
            snprintf(qs, sizeof qs, T("sb_queued", g_loc), g_pd.queue_n);
            snprintf(q, sizeof q, " %s", qs);
        }
        snprintf(tip, sizeof tip, "%s %d · %s %d%s · %s %d · %s",
                 T("grp_await", g_loc), g_pd.await_n,
                 T("grp_run", g_loc), g_pd.run, q,
                 T("grp_fail", g_loc), g_pd.fail_n,
                 T("sb_hint", g_loc));
    }
    sb_set_tooltip(tip);
    g_relayout = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 提醒：每轮重扫之后跑一次，与面板可见与否无关。
// 排在可见性判断之前是有原因的：这个 App 的全部卖点就是「人不在屏幕前也能被叫回来」，
// 面板收起时主循环直接 continue —— 挂在那后面就等于最需要它的那一档永远不响。
// ─────────────────────────────────────────────────────────────────────────────
static std::set<std::string> g_ledger;
static long g_alert_seq = -1;
static bool g_alert_first = true;

static void RunAlertRound(const ap::Snapshot& s) {
    // g_cfg 不在这里读盘：设置屏的每次点击都当场 save_alert_cfg 回写 g_cfg，
    // 启动时 main 里 load 一次就够；再在这里 load 会把「刚点的那一下」擦回旧值，
    // 于是关掉的提醒下一轮又响了。
    if (g_alert_first) g_ledger = ap::load_alerted();   // 跨重启留住：昨天响过的今天不许多响
    // 「本轮刚开始静置」＝上一轮还在动、这一轮静置。只有这个跳变才允许响「已完成」那一档。
    // 第一次跑时 g_active 是空的，所以刚启动那一大片静置会话一条都不算跳变 —— 这正是
    // alert.h 里那条门要防的「第一次打开已完成就连吵十几轮」。
    std::set<std::string> cur, just_finished;
    for (const auto& ag : s.agents)
        for (const auto& se : ag.sessions) {
            const std::string k = ap::alert_key(ag.id, se.id);
            if (se.state != ap::ST_RESTING) cur.insert(k);
            else if (g_active.count(k)) just_finished.insert(k);
        }
    std::set<std::string> next;
    ap::AlertRound r = ap::select_alerts(s, g_ledger, g_cfg, &next, &just_finished);

    // 面板里点过「已读」的行不再吵；点过「稍后」的还没到恢复时刻也不吵。
    // 两条都要**从账本里摘掉**，否则恢复时刻一到它就变成「已提醒过」而永远不再响 ——
    // 那等于把「稍后」变成了「永不」。
    std::vector<ap::Pending> keep;
    int held = 0;
    const double now = s.scanned_at > 0 ? s.scanned_at : std::time(nullptr);
    for (const auto& p : r.to_post) {
        const std::string pk = p.agent_id + "/" + p.session_id;
        const bool acked = g_ack.count(pk) > 0;
        auto li = g_later.find(pk);
        const bool snoozed = li != g_later.end() && now < li->second;
        if (acked || snoozed) {
            held++;
            next.erase(ap::alert_key(p.agent_id, p.session_id));
            continue;
        }
        keep.push_back(p);
    }
    for (const auto& p : keep) {
        // 标题里的状态词与面板同一份表：横幅说「等你授权」、面板说「等待确认」
        // 这种两处各念一套的分裂，用户只能靠猜来对齐。
        const char* word = p.state == ap::ST_FAILED ? T("failed", g_loc)
                       : p.state == ap::ST_AWAIT ? T("awaiting", g_loc)
                                                 : T("st_done", g_loc);
        const std::string title = p.agent_name + " · " + word;
        char why[512] = {0};
        int rc = ap_notify_post(title.c_str(), p.title.c_str(), p.agent_id.c_str(),
                                p.session_id.c_str(), g_cfg.sound ? 1 : 0, why, (int)sizeof why);
        // rc==3＝系统没在时限内回执：这条到底发没发出去不知道，所以**从账本里摘掉**，
        // 下轮重扫再试。记进账本就是把它静默丢掉 —— 比多响一次更糟的失效方式。
        if (rc == 3) next.erase(ap::alert_key(p.agent_id, p.session_id));
        fprintf(stderr, "[提醒] %s｜%s｜%s：%s\n", title.c_str(), p.title.c_str(),
                rc == 0 ? "横幅已投" : rc == 1 ? "兜底出声" : rc == 3 ? "没回执，下轮再试"
                                                                     : "两边都不通", why);
        fflush(stderr);
    }
    g_ledger = next;
    std::string err;
    if (!ap::save_alerted(g_ledger, &err)) {
        fprintf(stderr, "[提醒] 账本写失败：%s —— 去重失效，下一轮会重发同一批\n",
                err.empty() ? "原因未回" : err.c_str());
        fflush(stderr);
    }
    if (g_alert_first || r.posted || r.retired || held) {
        printf("[提醒] 命中 %d｜首次 %d｜发出 %zu｜压掉 %d｜摘账 %d｜已读/稍后按住 %d｜账本 %zu 条｜免打扰%s｜刚静置 %zu 条\n",
               r.hits, r.fresh, keep.size(), r.capped, r.retired, held, g_ledger.size(),
               g_cfg.dnd ? "开" : "关", just_finished.size());
        fflush(stdout);
    }
    // 跳变记忆每轮都要换，与「这一轮响没响」无关：挂着上一轮的旧集合，下一轮打开
    // 「已完成」就会拿几分钟前的动静的对家来响。
    g_active = std::move(cur);
    g_alert_first = false;
}

static float g_panelH = 0.0f;   // drawPanel 每帧回填：内容真正用到的高度
// 能摆多高的预算。必须与「窗口现在多高」解耦：窗口是按内容收的，
// 若预算取当前窗口高，收窗→能摆更多行→再收窗会形成正反馈，每轮重扫只走一步，
// 实拍就会拍到「内容 340pt / 窗口 285pt」这种半收敛态（实测踩过）。
static float g_budgetH = 760.0f;

/// 窗口该收到多高：内容量出来的 g_panelH，但不许高过可视预算。
/// 收窗和「摆稳了没」两处必须走同一个式子——实测内容 1034.5pt、预算 1034pt 时，
/// 一边四舍五入成 1035、一边被压成 1034，出图门永远等不到两者相等，进程挂死不出图。
static int WantedWindowH() {
    int want = (int)(g_panelH + 0.5f);
    int cap = (int)(g_budgetH + 0.5f);
    return want > cap ? cap : want;
}

/// 两屏的宽度不同：pin 是 #win{width:340px}，settings 是 #win{width:420px}。
/// 换屏必须连着改窗口宽，否则 420 那一屏的内容会被 340 的窗口框裁掉右沿 80pt。
static int WantedWindowW() { return g_view == 1 ? kSetW : kPanelW; }

static void TogglePanel(void*) {
    if (!g_win) return;
    g_panelVisible = !g_panelVisible;
    // 打这一行是为了让总线点击可被外部核对：宿主调 Activate 只能证明「回包没报错」，
    // 证明不了面板真的动了。取证轮比对 app.log 里这行的出现次数与点击次数。
    printf("[面板] %s\n", g_panelVisible ? "展开" : "收起");
    fflush(stdout);
    if (g_panelVisible) { glfwShowWindow(g_win); glfwFocusWindow(g_win); g_relayout = true; }
    else glfwHideWindow(g_win);
}

int main(int argc, char** argv) {
    bool windowed = false, dump = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--window")) windowed = true;
        else if (!strcmp(argv[i], "--dump")) dump = true;
        else pos.push_back(argv[i]);
    }
    // 无参数＝从访达双击：出窗口。带参数＝离屏探针，按老用法渲一帧存 PPM。
    if (pos.empty() && !dump) windowed = true;

    // 界面语言：先读系统，再让环境变量盖上去。
    // 顺序是有意的——AGENTPING_LANG 是取证口（同一次构建要能拍出六张不同语言的图），
    // 它盖在系统值上，但打印里会同时给出系统原本读到的是哪一语，不然「强制」会被
    // 读成「检测到了」。认不出的强制值不静默丢弃：明说没生效、继续用系统值。
    {
        char src[160] = {0};
        const int sys = ap_lang(src, (int)sizeof src);
        g_loc = sys;
        const char* want = getenv("AGENTPING_LANG");
        if (want && *want) {
            const int forced = ap_lang_from_tag(want);
            if (forced >= 0) {
                g_loc = forced;
                printf("[语言] 强制 AGENTPING_LANG=%s → %s｜系统读到 %s\n", want,
                       kLocTag[g_loc], src);
            } else {
                printf("[语言] AGENTPING_LANG=%s 不在六语里，忽略强制值｜系统读到 %s → %s\n",
                       want, src, kLocTag[g_loc]);
            }
        } else {
            printf("[语言] %s → %s\n", src, kLocTag[g_loc]);
        }
        fflush(stdout);
    }

    if (dump) {
        // 秒级 time() 量不出 0.5s 这一档，冷扫耗时必须能报准
        auto t0 = std::chrono::steady_clock::now();
        ap::Snapshot s = ap::scan_all((double)std::time(nullptr));
        printf("%s", ap::snapshot_dump(s).c_str());
        printf("[耗时] %.2fs（冷扫：进程内缓存为空）\n",
               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        return 0;
    }

    std::string bundled;
    const char* dir = pos.size() > 0 ? pos[0].c_str()
                    : (!(bundled = BundleFontDir()).empty() ? bundled.c_str() : ".");
    const char* out = pos.size() > 1 ? pos[1].c_str() : "pin-imgui.ppm";

    FT_Library ft; FT_Init_FreeType(&ft);
    fprintf(stderr, "[字体] 从「%s」取字形\n", dir);
    fflush(stderr);
    LoadCoverage(ft, dir);
    for (int f = 0; f < FACE_N; f++) printf("[覆盖] %-16s cmap 内 %zu 个码点，行高 %.3f em\n",
                                            kFace[f], g_cover[f].size(), g_line[f]);
    // 一张脸都没开到就没有字形可烘，后面每次量宽都会踩空指针（实测在这里段塞）。
    // 报清搜了哪个目录再退，别让他双击后只看到「没反应」。
    int loaded = 0;
    for (int f = 0; f < FACE_N; f++) if (!g_cover[f].empty()) loaded++;
    if (!loaded) {
        fprintf(stderr, "[字体] 「%s」里 8 张脸全打不开：命令行请把字体目录作为第一个参数传入\n", dir);
        FT_Done_FreeType(ft);
        return 2;
    }

    // 设置盘的八个开关在窗口建起来之前就要落进 g_cfg：常驻置顶是建窗 hint，
    // 先开窗再改会让他在启动瞬间看见一次「本该置顶却没置顶」的闪。
    g_cfg = ap::load_alert_cfg();
    if (const char* v = getenv("AGENTPING_VIEW")) g_view = atoi(v);
    if (!glfwInit()) return 2;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // 窗口先藏，定位好再 show，免得在默认位置闪一下
    glfwWindowHint(GLFW_FLOATING, g_cfg.on_top ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, windowed ? GLFW_FALSE : GLFW_TRUE);
    // 稿子的 #win 是 rounded-16 + 1px hairline 的浮层，圆角外必须真透明，
    // 否则圆角处会露出一方底色，macOS 上就是一张贴在桌面上的矩形。
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, windowed ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    const int H = windowed ? (int)kCanvasH : 1620;
    // 探针改两行：上排 6 张 pin（340＋34 间距＝pitch 374），下排 6 张设置屏（420＋14＝pitch 434）。
    // 宽度取两行里更宽的那条：6×434＋14＝2618。
    const int offW = windowed ? kPanelW : 2620;
    GLFWwindow* win = glfwCreateWindow(offW, H, "Agent Ping", nullptr, nullptr);
    if (!win) return 2;
    g_win = win;
    glfwMakeContextCurrent(win);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // 全部坐标都是手算的绝对位置，ImGui 自己的边距/圆角/底色必须清零，
    // 否则整块面板会被默认的 8px WindowPadding 推移、被默认窗色蒙一层。
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(0, 0);
    st.ItemSpacing = ImVec2(0, 0);
    st.ItemInnerSpacing = ImVec2(0, 0);
    st.WindowRounding = 0.0f;
    st.WindowBorderSize = 0.0f;
    st.ChildRounding = 0.0f;
    st.GrabRounding = 0.0f;
    st.FrameRounding = 0.0f;
    // 卡底由 drawPanel 末尾自己画（要圆角），所以把 ImGui 窗口的默认方底压成全透明。
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);

    // 只预烘「界面固定文案」的码点。会话标题是从盘上读来的、下一轮就换一批，
    // 所以不烘它 —— 这一版 ImGui 的字形库是按需栅格化的（OpenGL3 后端带
    // RendererHasTextures，imgui_draw.cpp:3507 那条全量预烘分支根本不走），
    // 首个用到的码点在当帧就补进图集并上传，不必重启也不必重建字体。
    ImFontGlyphRangesBuilder chrome;
    AddChromeText(chrome);
    ImVector<ImWchar> ranges; chrome.BuildRanges(&ranges);
    char path[1024];
    int baked = 0;
    for (int t = 0; t < TIER_N; t++) {
        for (int f = 0; f < FACE_N; f++) {
            if (!FaceUsed(t, f)) continue;
            snprintf(path, sizeof path, "%s/%s", dir, kFace[f]);
            ImFontConfig fc; fc.SizePixels = Bake(t, f);
            g_font[t][f] = io.Fonts->AddFontFromFileTTF(path, Bake(t, f), &fc, ranges.Data);
            if (!g_font[t][f]) { fprintf(stderr, "字体加载失败 %s @%.1fpx\n", path, Bake(t, f)); return 2; }
            baked++;
        }
    }

    // Retina 上必须告诉 ImGui 按几倍栅格化字形。它只在 g.Font 变化时自己Stamp
    // （imgui.cpp:9300），而我们的混排是直接对各档字体各调 AddText，
    // 不 Stamp 就会拿 1x 的字形画进 2x 帧缓冲 —— 实测整片文字发虚。
    {
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(win, &ww, &wh);
        glfwGetFramebufferSize(win, &fw, &fh);
        float dens = wh > 0 ? (float)fh / (float)wh : 1.0f;
        for (int t = 0; t < TIER_N; t++)
            for (int f = 0; f < FACE_N; f++)
                if (g_font[t][f]) g_font[t][f]->CurrentRasterizerDensity = dens;
        printf("[字形] %d 档字号 × %d 字体＝烘了 %d 张，栅格密度 %.1fx（窗口 %dx%d / 帧缓冲 %dx%d）\n",
               TIER_N, FACE_N, baked, dens, ww, wh, fw, fh);
    }

    ImGui_ImplGlfw_InitForOpenGL(win, true);
#ifdef __APPLE__
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    std::thread scanner(ScanLoop);

    auto drawPanel = [&](float x, float y, float w, int loc, int view) {
        ImGui::SetNextWindowPos(ImVec2(x, y));
        // ImGui 的裁剪矩形就是窗口尺寸。稿子的 760 是画布高，但 maxBot 允许摆到屏幕预算
        // 高（本机实测 919pt）；只按 760 裁的话，760 之后的行会算进 g_panelH、把窗口撑大，
        // 自己却被裁掉——屏幕上留一段没有任何解释的死白。
        ImGui::SetNextWindowSize(ImVec2(w, g_budgetH > kCanvasH ? g_budgetH : kCanvasH));
        // 名字必须带上屏别：探针一帧里同一语会 Begin 两次（pin + 设置），
        // 撞了同一个窗口 ID 就是两次 Begin 一个 End，ImGui 会把两屏内容叠进同一块裁剪矩形。
        char wname[32]; snprintf(wname, sizeof wname, "%s##%s", view == 1 ? "set" : "pin",
                                 kLocTag[loc % kLocales]);
        ImGui::Begin(wname, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // AGENTPING_TN=<秒>：把动画时钟钉死在指定位相。pb-halo 1.9s 一轮，随机相位下
        // 单帧量出来的辉光宽度分不清是「画窄了」还是「正好赶上暗的那一下」，取证要能钉住。
        static const char* kTnLock = getenv("AGENTPING_TN");
        const double tn = kTnLock ? atof(kTnLock) : glfwGetTime();
        // #win 是 border-box：1px 边在 340pt 之内，正文离外沿是 1+12 而不是 12。
        const float kB = 1.0f;
        const float L = x + kB + kPadX, R = x + w - kB - kPadX;
        const float track = kTierPx[T_MICRO] * 0.16f;         // draft 的 letter-spacing:.16em
        const float maxBot = y + g_budgetH - kB - kPadBottom;   // 超出这块就不摆，改用「还有 N 条」
        float cy = y + kB + kPadTop;

        // ── 设置屏（r14/settings.html，420 宽）：整屏自己收口，不走下面 pin 列表那套 ──
        if (view == 1) {
            std::vector<SecBox> secs;
            g_panelH = drawSettings(dl, L, R, maxBot, loc, cy, &secs) - y + kB + kPadBottom;
            CardFrame(x, y, w, g_panelH, &secs);
            ImGui::Dummy(ImVec2(w, g_panelH));
            ImGui::End();
            return;
        }

        // 稿子 render.mjs 的 PIN_GROUPS：需要你介入 / 运行中 / 排队中 / 失败中断 / 今日已结束。
        int cnt[G_N] = {};
        for (const auto& r : g_pd.rows) cnt[GroupOf(r.state)]++;
        int live_total = 0;
        for (int i = 0; i < G_N; i++) live_total += cnt[i];

        // ── 切到设置屏的入口 ──────────────────────────────────────────────────
        // 稿子那屏靠页面外层 nav（pin.html 顶上一排链接）切屏，这块面板没有外层，
        // 所以入口自己占卡顶一行、右沿对齐，与设置屏那侧的「返回」落在同一个角。
        // 代价：pin 首屏比稿子多一行 34.5pt（22.5 胶囊 + 12 间距），整屏往下挪这么多。
        {
            const float pw = GhostPillW("set_title", loc);
            const long z = Zid(loc, kNavRow, HIT_SETTINGS, 0);
            GhostPill(dl, R - pw, cy, pw, loc, "set_title", z);
            AddHit(g_hits, R - pw, cy, R, cy + kSegH, HIT_SETTINGS, kNavRow, 0, loc,
                   T("set_title", loc), 0);
            cy += kSegH + kSectionGap;
        }

        // ── 表头（draft: grid gap-2.5 + num-xl 26pt + micro-label + 可选的环比脚行）──
        // 第三格按 render.mjs 的条件式：今天没有任何消耗记录就整格不摆（不是摆个 0）。
        // 脚行「↑ +59.9% 较昨日」只在昨天同窗也有记录时才摆 —— 单边有数算不出环比。
        char ns[12], rs[12], cost[32];
        snprintf(ns, sizeof ns, "%d", g_pd.needs);
        snprintf(rs, sizeof rs, "%d", g_pd.run);
        // fmtCost（stubs/agent-types.js）：≥100 取整、<100 留一位小数，单位是 cr 不是 $
        snprintf(cost, sizeof cost, g_pd.credits >= 100.0 ? "%.0f\xc2\xa0""cr" : "%.1f\xc2\xa0""cr",
                 g_pd.credits);
        const bool has_cost = g_pd.credits > 0;
        int nc = has_cost ? 3 : 2;
        const char* nums[3] = {ns, rs, cost};
        const char* hkeys[3] = {"grp_await", "grp_run", "cost"};
        ImU32 hcols[3] = {kStAwait, kStRun, kFg};
        const float numH = kTierPx[T_NUM] * 0.92f;
        const float cellW = (R - L - 10.0f * (nc - 1)) / (float)nc;
        const double delta = (has_cost && g_pd.yday_credits > 0)
                             ? (g_pd.credits - g_pd.yday_credits) / g_pd.yday_credits * 100.0 : 0.0;
        const bool has_delta = has_cost && g_pd.yday_credits > 0;
        const bool up = delta >= 0;
        char foot[64] = "";
        if (has_delta) snprintf(foot, sizeof foot, "%s %+.1f%% %s", up ? "\xe2\x86\x91" : "\xe2\x86\x93",
                                delta, T("vs_yesterday", loc));
        for (int i = 0; i < nc; i++) {
            float cx = L + i * (cellW + 10.0f);
            // 第三格（成本）右对齐到内容边 R，前两格照稿子左对齐在 cx。
            // 为什么只有这一格改：稿子是中文，"75.5 cr" 与 "↓ -75.3% 较昨日" 都落在格内；
            // 换成英/德后同一格要念 "Today cost" / "Heutige Kosten" 和 "vs yesterday" /
            // "vs gestern"。修前口径可复算：把下面 right 临时改成 false 重出图，
            // 同一把尺子量到墨迹右缘 x=679px（R=654px，越出 12.5pt），产物留在
            // evidence/2026-10-08-locale/prefix-mac-*.ppm ＋ measure-mac-prefix.txt。
            // 右对齐把数字与脚行都收进 R 以内，且不靠裁字／不靠缩写保住原词。
            const bool right = (i == 2);
            const float anchor_x = right ? R : cx;
            if (right)
                DrawTextRight(dl, anchor_x, TextTop(cy, numH, T_NUM, loc), nums[i], loc, hcols[i],
                              T_NUM, -kTierPx[T_NUM] * 0.03f);
            else
                DrawTextRun(dl, cx, TextTop(cy, numH, T_NUM, loc), nums[i], loc, hcols[i], T_NUM,
                            -kTierPx[T_NUM] * 0.03f);
            // micro-label 是 leading-none（行框＝字号），脚行是 body 的 1.5 倍行框。
            DrawTextRun(dl, cx, TextTop(cy + numH + 4.0f, kTierPx[T_MICRO], T_MICRO, loc),
                        UpAscii(T(hkeys[i], loc)), loc, kMicro, T_MICRO, track);
            // 脚行颜色：稿子的 pin 屏写死琥珀，StatBand 屏按涨跌分琥珀/绿。
            // 我们的真值是跌（今日 53.5 cr / 昨日 2086.6 cr），跌的行配琥珀箭头自相矛盾，
            // 所以按 StatBand 那条条件式走。
            if (i == 2 && has_delta)
                DrawTextRight(dl, R, TextTop(cy + numH + 4.0f + kTierPx[T_MICRO] + 4.0f,
                                             kTierPx[T_SM] * 1.5f, T_SM, loc),
                              foot, loc, up ? kStAwait : kStDone, T_SM, -kTierPx[T_SM] * 0.01f);
        }
        cy += numH + 4.0f + kTierPx[T_MICRO] + (has_delta ? 4.0f + kTierPx[T_SM] * 1.5f : 0.0f)
              + kSectionGap;

        // ── 状态构成条 + 计数胶囊：本轮真有的状态才画，一条都没有就整块不摆 ──
        if (live_total > 0) {
            const float pillGap = 10.0f;
            float pwid[G_N], pillsW = 0; int pn = 0;
            for (int i = 0; i < G_N; i++) if (cnt[i]) {
                char nb[8]; snprintf(nb, sizeof nb, "%d", cnt[i]);
                pwid[pn] = 7 + 6 + 4 + TextWidth(nb, loc, T_PILL) + 7;
                pillsW += pwid[pn]; pn++;
            }
            pillsW += pillGap * (pn - 1);
            // 稿子是一行里「条 flex-1 + 胶囊 shrink-0」；六语的胶囊文案更长，放不下就让条独占一行
            bool wrap = (R - L - pillGap - pillsW) < 60.0f;
            float barW = wrap ? (R - L) : (R - L - pillGap - pillsW);
            // 稿子这行是 flex items-center：胶囊 15.5 最高、定义了行框，6px 的条在其内居中。
            float barY = wrap ? cy : cy + (kPillH - kBarH) * 0.5f;
            dl->AddRectFilled(ImVec2(L, barY), ImVec2(L + barW, barY + kBarH), W(23), kBarH * 0.5f);
            // 稿子是「容器 rounded-full + overflow-hidden，段本身 h-full rounded-full」：
            // 段与段之间不留缝，只有整条的两端是圆的。逐段各自取整圆角会在接缝处露出底色。
            float sx = L;
            int drawn = 0;
            for (int i = 0; i < G_N; i++) if (cnt[i]) {
                ImU32 c = GrpColor(i);
                float segW = barW * (float)cnt[i] / (float)live_total;
                int left = (drawn == 0), right = (drawn == pn - 1);
                ImDrawFlags f = left && right ? ImDrawFlags_RoundCornersAll
                              : left ? ImDrawFlags_RoundCornersLeft
                              : right ? ImDrawFlags_RoundCornersRight
                              : ImDrawFlags_RoundCornersNone;
                dl->AddRectFilled(ImVec2(sx, barY), ImVec2(sx + segW, barY + kBarH), c, kBarH * 0.5f, f);
                sx += segW; drawn++;
            }
            float py = wrap ? cy + kBarH + 8.0f : cy;
            float cxx = wrap ? L : L + barW + pillGap;
            for (int k = 0, i = 0; i < G_N; i++) if (cnt[i]) {
                char nb[8]; snprintf(nb, sizeof nb, "%d", cnt[i]);
                ImU32 c = GrpColor(i);
                GradRound(dl, cxx, py, pwid[k], kPillH, kPillH * 0.5f,
                          WithA(c, 56), WithA(c, 18), WithA(c, 102));
                dl->AddCircleFilled(ImVec2(cxx + 7 + 3, py + kPillH * 0.5f), 3.0f, c);
                DrawTextRun(dl, cxx + 7 + 6 + 4, TextTop(py + 3.0f, kTierPx[T_PILL], T_PILL, loc),
                            nb, loc, c, T_PILL);
                cxx += pwid[k] + pillGap; k++;
            }
            cy = (wrap ? py + kPillH : cy + kPillH) + kSectionGap;
        }

        // ── 采集器分档（draft 的 filter chips）：数字＝本轮该采集器真摆了几行。
        //    跳转还没从 Swift 搬过来，所以这里是读数条，不是能点的筛选器。
        if (!g_pd.chips.empty()) {
            // 外层高度不是拍的：border 1 + py-[3px] + 行框（正文 11px × body 行高 1.5＝16.5）+ 3 + 1 ＝ 24.5。
            const float chipH = kTierPx[T_CHIP] * 1.5f + 2 * 3.0f + 2 * 1.0f, chipGap = 6.0f;
            const float badgeH = kTierPx[T_MICRO] * 1.5f;   // 9 × 1.5，px-1 只加宽不加高
            float cx = L, lineY = cy;
            auto chip = [&](const std::string& name, const std::string& initial, int n, ImU32 hue, bool active) {
                char nb[8]; snprintf(nb, sizeof nb, "%d", n);
                float nameW = TextWidth(name, loc, T_CHIP); if (nameW > 92.0f) nameW = 92.0f;
                float badgeW = 4 + TextWidth(nb, loc, T_MICRO) + 4;
                float wch = 9 + (active ? 0.0f : 13.0f + 6.0f) + nameW + 6.0f + badgeW + 9.0f;
                if (cx + wch > R && cx > L) { cx = L; lineY += chipH + 4.0f; }
                ImU32 fg = active ? kInk : hue;
                // 稿子：选中格＝白底 .92 + 反色字 + hairline 边；未选中＝**透明底** + 色相 55% 边。
                if (active) dl->AddRectFilled(ImVec2(cx, lineY), ImVec2(cx + wch, lineY + chipH), W(235), chipH * 0.5f);
                dl->AddRect(ImVec2(cx + 0.5f, lineY + 0.5f), ImVec2(cx + wch - 0.5f, lineY + chipH - 0.5f),
                            active ? W(235) : WithA(hue, 140), chipH * 0.5f, 0, 1.0f);
                float acy = lineY + chipH * 0.5f, nx = cx + 9.0f;
                if (!active) {
                    float acx = nx + 6.5f;
                    GradCircle(dl, ImVec2(acx, acy), 6.5f, WithA(hue, 66), WithA(hue, 20), WithA(hue, 140));
                    float iw = TextWidth(initial, loc, T_GLYPH_SM);
                    DrawTextRun(dl, acx - iw * 0.5f, TextMid(acy, T_GLYPH_SM, loc), initial, loc, hue, T_GLYPH_SM);
                    nx += 13.0f + 6.0f;
                }
                DrawTextRun(dl, nx, TextTop(lineY + 4.0f, kTierPx[T_CHIP] * 1.5f, T_CHIP, loc),
                            ClipToWidth(name, nameW, loc, T_CHIP), loc, fg, T_CHIP);
                float bx = cx + wch - 9.0f - badgeW;
                dl->AddRectFilled(ImVec2(bx, acy - badgeH * 0.5f), ImVec2(bx + badgeW, acy + badgeH * 0.5f),
                                  active ? WithA(kInk, 26) : W(20), badgeH * 0.5f);
                DrawTextRun(dl, bx + 4.0f, TextTop(acy - badgeH * 0.5f, badgeH, T_MICRO, loc),
                            nb, loc, fg, T_MICRO);
                cx += wch + chipGap;
            };
            chip(T("all", loc), std::string(), live_total, kFg, true);
            for (const auto& c : g_pd.chips) chip(c.name, c.initial, c.n, c.hue, false);
            cy = lineY + chipH + kSectionGap;
        }

        // ── 预算先扣干净，分组只能填剩下的。三笔账都要在账上：卡脚那三块（今天没活动／
        //    读不通／口径脚注）、每一组的组头、每一条「还有 N 条」。修前只扣了第一块和一组
        //    间距（grpBot = maxBot - tail - kGroupGap），组头和入口都不算：容器灌 13 条假会话
        //    实测 g_panelH=1098.8pt 而窗口被 WantedWindowH() 按屏高压到 1054pt ⇒ 底部整条
        //    口径脚注画在窗口外、被 ImGui 的裁剪矩形切掉，超预算的会话行既不摆也不报。
        //    窗口挂的是 NoScrollbar（浮层没有第二条路），所以「摆不下的必须报，报了的必须有出路」。
        const float kFootLine = kTierPx[T_SM] * 1.5f;
        float tail = 0.0f;
        if (!g_pd.quiet.empty()) tail += kFootLine + 8.0f;
        tail += (float)g_pd.errors.size() * (kFootLine + 8.0f);
        if (g_pd.credits > 0) tail += kTierPx[T_MICRO] + 8.0f;
        // 入口那一行＝正文 1.5 倍行框 + 4pt 落点 + 前面那 6pt 行距。
        const float kMoreH = kFootLine + 4.0f + kRowGap;
        const float kGrpHdrH = kTierPx[T_MICRO] + kLabelRowGap;   // 组头是 leading-none 的一行 + 6
        // 「一组跑不掉的最低开销」kMoreH＝一条带组名的行（19）＋它下面那档行距（6）。每组都按这一份
        // 预留，轮到自己就不再预留自己那份；余额线量的是「从已画到的位置 cy 往下还留给后面几组」，
        // 所以组头、入口、组间距这些真实开销不用另记一笔——cy 走到哪儿，余额线就在哪儿起算。
        // 为什么不能按最大开销（组头 15＋间距 16＋入口 25＝56.5）预留：10:1x 那轮 mac40 点名帧实测
        // 运行组被后面三组那 169.5pt 的预扣挤成 '!'（并进一条没组名的折行入口），而它后面那几组轮到
        // 自己时又把额度拿了回去，屏幕底部还空着 59pt——预扣按组序退，中间那组替后面所有组受罚。
        // 为什么「每组一条」这份必须含那 6pt 分隔：10:3x 复算同一帧，被点名那组摆到余额线后加的是
        // 16pt 组间距，下一组只剩 9.5pt 摆它那 19pt 的名字行 ⇒ 又走成 '!'。分隔算进份内之后，
        // 每组轮到时恰好拿到 25pt，紧凑一行（19＋6）刚好装得下，'!' 只在屏面连一行都放不下时才走。
        int grp_n = 0, last_grp = -1;
        for (int i = 0; i < G_N; i++) if (cnt[i]) { last_grp = i; grp_n++; }
        // 另有两笔扣死、不参与退还：① 点名那屏顶部那条「返回总览」是这块版面唯一那条回路，不能
        // 是「预算不够就不画」的装饰；② 尾部那条折行入口画在循环之外，可能替两组以上收账。
        const int extra_lines = (g_focus >= 0 ? 1 : 0) + (grp_n > 1 ? 1 : 0);
        float grpBot = 0.0f;      // 余额线，每轮重算，见循环开头

        //    一条可点的「还有 N 条」：整行是命中区，悬停给与会话行同一层过渡底色（稿子的
        //    hover:bg-menu-accent）。row 位＝kFoldRow＋组号；点下去把版面换给那一组，在同一组上
        //    连着点＝往后翻一页（改的是 g_focus/g_page，重画由下一帧的 g_relayout 走）。
        auto MoreLine = [&](int grp, int n) {
            const long z = Zid(loc, kFoldRow + grp, HIT_MORE, 0);
            const float k = TxPeek(z);
            if (k > 0.001f)
                dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + kFootLine),
                                  MixA(kBg, kHoverRow, k), 7.0f);
            char buf[48]; snprintf(buf, sizeof buf, T("more", loc), n);
            DrawTextRun(dl, L + kRowPl + kAvatarD + kColGap, TextTop(cy, kFootLine, T_SM, loc),
                        buf, loc, MixA(kTime, kFg, k), T_SM);
            AddHit(g_hits, L, cy, R, cy + kFootLine, HIT_MORE, kFoldRow + grp, 0, loc, buf, 0);
            cy += kFootLine + 4.0f;
        };

        // ── 预算紧到连组头都摆不下时的紧凑一行：「组名 · 还有 N 条」合成一行、同一个命中区。
        //    为什么非要带组名：03:25 的 mac40 平铺帧实拍里，尾部三组（排队·失败·已结束）被并成
        //    一条没有名字的「还有 5 条」，点下去只换到排队那 2 条——屏面上没有任何地方写着
        //    「这 5 条其实来自三组」，等于把截断从「看不见」改成了「看得见但对不上账」。
        auto CompactLine = [&](int grp, int n) {
            const long z = Zid(loc, kFoldRow + grp, HIT_MORE, 0);
            const float k = TxPeek(z);
            if (k > 0.001f)
                dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + kFootLine),
                                  MixA(kBg, kHoverRow, k), 7.0f);
            float ex = DrawTextRun(dl, L, TextTop(cy, kFootLine, T_MICRO, loc),
                                   UpAscii(T(kGrpKey[grp], loc)), loc, kMicro, T_MICRO, track) + 6.0f;
            ex = DrawTextRun(dl, ex, TextTop(cy, kFootLine, T_MICRO, loc), "·", loc, W(44),
                             T_MICRO, track) + 6.0f;
            char buf[48]; snprintf(buf, sizeof buf, T("more", loc), n);
            DrawTextRun(dl, ex, TextTop(cy, kFootLine, T_SM, loc), buf, loc, MixA(kTime, kFg, k), T_SM);
            AddHit(g_hits, L, cy, R, cy + kFootLine, HIT_MORE, kFoldRow + grp, 0, loc, buf, 0);
            cy += kFootLine + 4.0f;
        };

        // ── 一组在这一帧摆不出组头时的出路，两条分支只此一处（两条都要在诊断账上留痕）：
        //    摆得下紧凑一行就自己带组名，连一行都摆不下才并进尾部那条折行入口（'!'，无名）。
        //    无名那条是最后手段：条数还在账上、点下去只换到宿主组，其它组在屏面上没有名字。
        int fold_grp = -1, fold_n = 0, fold_mask = 0, agg_mask = 0;
        auto FallbackLine = [&](int i) {
            if (cy + kFootLine + 4.0f <= grpBot) {
                fold_mask |= 1 << i;
                CompactLine(i, cnt[i]);
                if (i != last_grp || fold_n > 0) cy += kRowGap;
                return;
            }
            if (fold_grp < 0) fold_grp = i;
            agg_mask |= 1 << i;
            fold_n += cnt[i];
        };

        // ── 按状态分组；空组不渲染（他的空态门：没有真实输出就不摆这个表面）
        //    平铺时每组装到「每组上限」或「预算」为止，剩下的收进本组那条入口；一旦某组被
        //    点名，版面整块让给它——其它组只留一行「组名 · 还有 N 条」，点它自己的那行就换回去。
        //    这样每一颗点下去都有动静：屏幕是硬上限时，出路只能是「换一组看」，不是滚动。
        if (g_focus >= 0 && (g_focus >= G_N || !cnt[g_focus])) { g_focus = -1; g_page = 0; }
        if (g_focus >= 0 && g_page >= cnt[g_focus]) g_page = 0;   // 翻到头回到这组的头一页
        const int focus = g_focus;
        // 点名＝版面整块换给一组，那就必须留一条明写在屏面上的回路：这块浮层挂 NoScrollbar，
        // 既没有滚动条也没有返回手势。修前 focus 一旦 ≥0，唯一的复位路是「那一组自己清空」——
        // 点进 12 条的运行组之后，平铺版面再也回不来（探针 s13 那轮就是这么撞出这个形状的）。
        if (focus >= 0) {
            const char* back = T("back_overview", loc);
            const long z = Zid(loc, kFoldRow + G_N, HIT_BACKOV, 0);
            const float k = TxPeek(z);
            if (k > 0.001f)
                dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + kFootLine),
                                  MixA(kBg, kHoverRow, k), 7.0f);
            DrawTextRun(dl, L, TextTop(cy, kFootLine, T_SM, loc), back, loc, MixA(kTime, kFg, k), T_SM);
            AddHit(g_hits, L, cy, R, cy + kFootLine, HIT_BACKOV, kFoldRow + G_N, 0, loc, back, 0);
            // 03:25 那轮实拍读图：只留 4pt 时这行与下面那行组头在图上几乎连成一块，而它是面板级
            // 控件、不属于这一组 ⇒ 再加一档行距（额度本来就按整条 kMoreH 预留的，不会顶掉预算）。
            cy += kFootLine + 4.0f + kRowGap;
        }
        for (int i = 0; i < G_N; i++) g_shown[i] = 0;
        for (int i = 0; i < G_N; i++) {
            if (!cnt[i]) continue;
            // 余额线每轮重算：这条线以下才许摆下一组的名字行，量的是「从线到屏幕底还留着后面
            // 每组一条（25pt）加那两条扣死的线」。后面每走过一组就放回一份，所以这条线只会下移，
            // 不会把已经摆下去的行重新变成被裁掉。
            int later = 0;
            for (int j = i + 1; j < G_N; j++) if (cnt[j]) later++;
            grpBot = maxBot - tail - (float)(extra_lines + later) * kMoreH;
            // 点名那一屏上其它组的唯一职责是「点我换到这组」，不需要组头（版面整块已让给被点名那组）
            // ⇒ 一律走紧凑一行。修前它们走「组头 15 ＋ 入口 19 ＋ 间距 16」那 50pt：mac40 的 click1
            // 帧实测 运行＋排队 两组挤成一条没有组名的「还有 7 条」（点下去只换到运行，另外 2 条在账上
            // 看得见、在屏上对不上是谁），而窗口底部还空着 44pt。按每组一行算只要 4×25＝100pt。
            if (focus >= 0 && i != focus) { FallbackLine(i); continue; }
            // 这组画完后面还要不要留分隔：后面还有组就必须留（16pt 组间距）；最后一组且没有
            // 折行入口要收账就不留（那 16pt 谁都不挨着，是纯空额，单组平铺时 extra_lines＝0、
            // 没有余额冲抵，它正好把脚注整块挤出窗口）。分隔放不放，额度就按放不放来算。
            const float tail_sep = (i != last_grp || fold_n > 0) ? kGroupGap : 0.0f;
            // 连「组头＋一条带组名的入口」都摆不下的组不摆半截标题：退成紧凑一行（组名＋条数
            // 同一个命中区，19pt），再塞不下才走 FallbackLine 那条无名出路。
            if (cy + kGrpHdrH + kFootLine + 4.0f + tail_sep > grpBot) { FallbackLine(i); continue; }
            // 上一道闸只保证「组头＋入口」摆得下，不保证中间真能塞进一行会话。10-08 复算平铺帧
            // 读到过这种形状：mac13 的排队／失败／已结束三组各画了一条组头，下面一行会话都没有，
            // 紧跟一条「还有 N 条」——组头报的数与入口报的数是同一个数，等于用 40pt 说了紧凑一行
            // 19pt 就能说完的话。所以先按行循环里同一道闸量一次第一颗候选行（DrawSessionRow 末参
            // false 是干跑，只量高不落笔），量出摆不下就直接退紧凑一行。
            const bool paged = (focus == i);      // 被点名那组不受「每组上限」管：整屏都是它的额度
            float first_need = 0.0f;
            {
                int sk = paged ? g_page : 0;
                for (size_t ri = 0; ri < g_pd.rows.size(); ri++) {
                    const UIRow& r = g_pd.rows[ri];
                    if (GroupOf(r.state) != i) continue;
                    if (sk > 0) { sk--; continue; }
                    first_need = DrawSessionRow(dl, L, R, cy, r, loc, tn, false, (int)ri);
                    break;
                }
            }
            if (first_need <= 0.0f ||
                cy + kGrpHdrH + first_need + (kRowGap + kFootLine + 4.0f + tail_sep) > grpBot) {
                FallbackLine(i); continue;
            }
            // 组头 = .micro-label 一行：标签 ·（opacity .45 叠在 white/38 上＝alpha 44） 计数，
            // 三段共用 9px mono + .16em 字距（计数也在同一个 div 里，不是更亮的独立样式）。
            float mh = kTierPx[T_MICRO];   // .micro-label 是 leading-none，行框＝字号
            float endX = DrawTextRun(dl, L, TextTop(cy, mh, T_MICRO, loc),
                                     UpAscii(T(kGrpKey[i], loc)), loc, kMicro, T_MICRO, track) + 6.0f;
            char nb[8]; snprintf(nb, sizeof nb, "%d", cnt[i]);
            endX = DrawTextRun(dl, endX, TextTop(cy, mh, T_MICRO, loc), "·", loc, W(44), T_MICRO, track) + 6.0f;
            DrawTextRun(dl, endX, TextTop(cy, mh, T_MICRO, loc), nb, loc, kMicro, T_MICRO, track);
            cy += kGrpHdrH;
            int sk = g_page, shown = 0, rest = 0;
            for (size_t ri = 0; ri < g_pd.rows.size(); ri++) {
                const UIRow& r = g_pd.rows[ri];
                if (GroupOf(r.state) != i) continue;
                if (sk > 0) { sk--; rest++; continue; }
                float need = DrawSessionRow(dl, L, R, cy, r, loc, tn, false, (int)ri);
                // 放行之前要扣三笔：这行自己前面的行距、这行的高、以及它之后那条入口加尾部分隔
                // （6＋19＋tail_sep）。第三笔在「已经拒过一行」之后照样要扣——行高是不等的
                // （带待回答块的比别的高），拒掉高的之后还会放进矮的，不扣这几行就把本组那条入口
                // 的位置吃干净：10:5x 复算 mac40 的 click1，介入组 r10 被拒之后 r11 仍摆到 y 794..846、
                // 入口 852..867，画完组间距后 cy 已在余额线下方，于是运行和排队两组一起掉进 '!'，
                // 而屏幕底部还空着 44pt。
                if ((!paged && shown >= g_cfg.max_rows) ||
                    cy + (shown ? kRowGap : 0.0f) + need +
                        (kRowGap + kFootLine + 4.0f + tail_sep) > grpBot) { rest++; continue; }
                if (shown) cy += kRowGap;               // 稿子里每组是 space-y-1.5
                DrawSessionRow(dl, L, R, cy, r, loc, tn, true, (int)ri);
                cy += need; shown++;
            }
            g_shown[i] = shown;
            if (rest > 0) { if (shown) cy += kRowGap; MoreLine(i, rest); }
            if (tail_sep > 0.0f) cy += tail_sep;
        }
        if (fold_n > 0) MoreLine(fold_grp, fold_n);

        // 每组一行「总数/摆出/收起」，供外部把三条判据算成数字而不是看图猜。尾巴上的标记
        // 说的是这组这一帧是怎么摆的：'~'＝退成了紧凑一行（组名＋条数同一行），'!'＝连那一行
        // 都塞不下、条数并进了最后那条折行入口。两种都是「没摆组头」，命中区表里看不出区别，
        // 所以必须在账上留痕，否则外部只能靠数行数猜版面走的是哪条路。
        // 每一组被并进来都要打点，不只打那条入口的宿主组：03:25 那轮尾部三组里只有第一组有标记，
        // 另外两组在账上跟「静默丢掉」长得一模一样——数得出总和少了、数不出是谁少了。
        // 收在这一帧而不是点击那帧：入口点下去之后版面整块换人，账要能对上新旧两帧。
        {
            int off = 0;
            g_fold_diag[0] = 0;
            for (int i = 0; i < G_N; i++) if (cnt[i]) {
                char mk = 0;
                if (fold_mask & (1 << i)) mk = '~';
                else if (agg_mask & (1 << i)) mk = '!';
                off += snprintf(g_fold_diag + off, sizeof g_fold_diag - off, " %s=%d/%d/%d%c",
                                kGrpKey[i], cnt[i], g_shown[i], cnt[i] - g_shown[i], mk ? mk : ' ');
            }
        }

        // 今天没活动的采集器收成一行；读不通的源单独一条 —— 两者长得不一样，混报就是骗他
        // 这两行稿子里没有对应物（我们的数据比他的样本多），排版按同一套行框模型走：
        // 行框＝正文 10px×1.5，9px 的 micro-label 在其内居中。高度已在 grpBot 之前预留。
        const float kFootMicroTop = (kFootLine - kTierPx[T_MICRO]) * 0.5f;
        if (!g_pd.quiet.empty()) {
            std::string q;
            for (size_t i = 0; i < g_pd.quiet.size(); i++) { if (i) q += " · "; q += g_pd.quiet[i]; }
            float ex = DrawTextRun(dl, L, TextTop(cy + kFootMicroTop, kTierPx[T_MICRO], T_MICRO, loc),
                                   UpAscii(T("quiet_agents", loc)), loc, kMicro, T_MICRO, track) + 8.0f;
            DrawTextRun(dl, ex, TextTop(cy, kFootLine, T_SM, loc),
                        ClipToWidth(q, R - ex, loc, T_SM), loc, W(60), T_SM);
            cy += kFootLine + 8.0f;
        }
        for (const auto& err : g_pd.errors) {
            float ex = DrawTextRun(dl, L, TextTop(cy + kFootMicroTop, kTierPx[T_MICRO], T_MICRO, loc),
                                   UpAscii(T("unreadable", loc)), loc, WithA(kStFail, 210),
                                   T_MICRO, track) + 8.0f;
            DrawTextRun(dl, ex, TextTop(cy, kFootLine, T_SM, loc),
                        ClipToWidth(err, R - ex, loc, T_SM), loc, WithA(kStFail, 150), T_SM);
            cy += kFootLine + 8.0f;
        }

        // 口径脚注：只在 cr 那一格真画了的时候跟一条，否则是凭空多一行废话。
        if (g_pd.credits > 0) {
            float lh = kTierPx[T_MICRO];
            DrawTextRun(dl, L, TextTop(cy, lh, T_MICRO, loc),
                        ClipToWidth(T("cost_note", loc), R - L, loc, T_MICRO),
                        loc, W(52), T_MICRO, track);
            cy += lh + 8.0f;
        }

        g_panelH = cy - y + kB + kPadBottom;
        // 卡框：#win{border-radius:16px; border:1px solid var(--hairline)}。
        // 画进 background 层——它在所有窗口之前合成，所以末尾补画也不会盖住行内容。
        // --hairline = oklch(.95 .02 250 / 9%) → sRGB(229,240,252) 带 9% 透明度。
        CardFrame(x, y, w, g_panelH);
        ImGui::Dummy(ImVec2(w, g_panelH));
        ImGui::End();
    };

    if (windowed) {
        // 主显示器拿不到时不能拿窗口尺寸当屏幕宽——实测那样会把面板摆到 x=-14，
        // 整块内容跑到屏幕外，只留一条边，看起来像"双击没反应"。
        int mn = 0;
        GLFWmonitor** mons = glfwGetMonitors(&mn);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (!mon && mn > 0) mon = mons[0];
        int mx = 0, my = 0, mw = 0, mh = 0;
        if (mon) {
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            glfwGetMonitorPos(mon, &mx, &my);
            if (vm) { mw = vm->width; mh = vm->height; }
        }
#if defined(__APPLE__)
        // 本机实测：命令行/nohup 起的进程里 CGGetActiveDisplayList 返回 0 块活动屏，
        // 于是 glfwGetMonitors 也是空——但 CGDisplayBounds(CGMainDisplayID()) 照样给得出
        // 1920x1080@(0,0)。所以 GLFW 这条路走不通时退回 CoreGraphics，而不是猜一个尺寸。
        if (!mw || !mh) {
            CGRect b = CGDisplayBounds(CGMainDisplayID());
            if (b.size.width > 1 && b.size.height > 1) {
                mx = (int)b.origin.x; my = (int)b.origin.y;
                mw = (int)b.size.width; mh = (int)b.size.height;
                printf("[窗口] GLFW 认不出显示器，改用 CoreGraphics 主屏\n");
            }
        }
#endif
        if (!mw || !mh) { mw = 1440; mh = 900; }   // 两条路都空（没有窗口服务器）时的最后兜底
        glfwSetWindowPos(win, mx + mw - offW - 14, my + 34);   // 贴着菜单栏右下方
        g_budgetH = (float)(mh - 34 - my - 12);   // 从 y=34 到屏底再留 12pt
        int px, py; glfwGetWindowPos(win, &px, &py);
        printf("[窗口] %d 个显示器，可视 %dx%d@(%d,%d) → 面板落在 (%d,%d)\n",
               mn, mw, mh, mx, my, px, py);
        fflush(stdout);

        std::string icon = BundleResources() + "/menubar-icon@2x.png";
        if (sb_show(icon.c_str(), T("sb_hint", g_loc), T("sb_show_hide", g_loc),
                    T("sb_quit", g_loc), TogglePanel, nullptr) != 0)
            fprintf(stderr, "[状态项] 本平台还没有实现，只有窗口\n");

        glfwSwapInterval(1);
        glfwShowWindow(win);
        sb_float_window();
        // AGENTPING_SHOT=出图路径：把**真窗口**的帧缓冲回读出来存 PPM，然后自己收摊。
        // 为什么需要这一条：从命令行截屏（screencapture -x / -R）在本机只会得到一张纯黑图——
        // 驱动器没有「屏幕录制」授权，实测 3840x2160 全画面只有 1 种颜色 (0,0,0)。
        // 而 glReadPixels 读的是这个窗口自己合成后的像素，Retina 真实栅格，不依赖授权。
        const char* shot_to = getenv("AGENTPING_SHOT");
        // 出图与取证跑不许吵人：整层不响，否则一次截屏顺手发四条横幅。
        const bool alerts_on = windowed && !shot_to &&
                               strcmp(getenv("AGENTPING_ALERT") ? getenv("AGENTPING_ALERT") : "1", "0") != 0;
        if (alerts_on) ap_notify_on_click(OnBannerClick, nullptr);
        else fprintf(stderr, "[提醒] 本轮不响（出图／AGENTPING_ALERT=0／非窗口模式）\n");
        fflush(stderr);
        double lastResize = glfwGetTime();
        int settle = 0;
        // AGENTPING_SNI_HOLD=毫秒：只挂着总线就收摊的取证模式。
        // 状态项的数不在我们这块窗口里（由宿主画），所以这一档不出图，只保证
        // 「宿主来读的时候进程还在」——没有它，自检脚本要么把窗口关掉导致总线名消失，
        // 要么一直跑不退出。
        static const char* kHolding = getenv("AGENTPING_SNI_HOLD");
        const double hold_ms = kHolding ? atof(kHolding) : 0.0;
        const double hold_until = hold_ms > 0.0 ? glfwGetTime() + hold_ms / 1000.0 : 0.0;
        while (!glfwWindowShouldClose(win)) {
            glfwPollEvents();
            sb_pump();
            if (hold_until > 0.0 && glfwGetTime() >= hold_until) {
                printf("[状态项] 挂总线 %.0fms 后收摊（AGENTPING_SNI_HOLD）\n", hold_ms);
                fflush(stdout);
                glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
            // 提醒排在可见性判断之前：面板收着的时候正是它该干活的时候。
            if (alerts_on) {
                long aseq = g_seq.load();
                if (aseq != g_alert_seq) {
                    g_alert_seq = aseq;
                    ap::Snapshot copy;
                    { std::lock_guard<std::mutex> lk(g_mtx); copy = g_snap; }
                    RunAlertRound(copy);
                }
                DrainBannerClick();
            }
            if (!g_panelVisible) {          // 收起时不空转 GPU，但扫描线程照跑，数字仍是活的
                g_hits.clear();             // 命中区跟着画面一起作废，放回来时不许点在旧几何上
                usleep(16000);
                continue;
            }
            int fw, fh;
            glfwGetFramebufferSize(win, &fw, &fh);
            glViewport(0, 0, fw, fh);
            glClearColor(0, 0, 0, 0);   // 全透明清屏：卡底由 drawPanel 自己画成圆角
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            // 输入排在绘制之前：这一帧的悬停/按下用的是上一帧登记的命中区，
            // 算完过渡值才画，命中那一帧就亮（滞后的 16ms 比 150ms 的过渡短一个数量级）。
            HandleInput(ImGui::GetIO(), glfwGetTime());
            g_hits.clear();
            // 重扫描落地就重摆行。必须放在 NewFrame 之后：RebuildPanel 里要量字宽，
            // 而量字宽会在图集还没建起来时就把字形烘进去（图集此时 RendererHasTextures
            // 还是 false，走的是旧的全量预烘路），实测整屏文字会掉到一成亮度。
            long seq = g_seq.load();
            if (seq != g_pd_seq) {
                g_pd_seq = seq;
                std::lock_guard<std::mutex> lk(g_mtx);
                RebuildPanel(g_snap);
            }
            drawPanel(0, 0, (float)WantedWindowW(), g_loc, g_view);
            // AGENTPING_HITS=1：把这一帧真登记出来的命中区打到 stderr。
            // 取证要按坐标钉悬停/点击，而坐标只有绘制那一趟算得准——手按着稿子估一份，
            // 估错就会「钉在按钮旁边的空白上」然后判成命中失效。
            // 触发条件是「内容变了就打」而不是「只打第一帧」：点击发生在第 2~3 帧，
            // 只报首帧的话日志里根本没有点击后的版面，「点一下到底换了什么」就只能靠脑补。
            // 3 秒一轮的重扫数据相同 ⇒ 文本相同 ⇒ 不重复刷屏。
            if (getenv("AGENTPING_HITS") && !g_hits.empty()) {
                static const char* prev = nullptr;
                std::string dump;
                char line[256];
                // row 位对会话行是行号，对入口是 kFoldRow＋组号（80=介入…84=今日已结束，85=返回总览）：
                // 光看文案分不出两组都收 3 条时点的是哪一条，坐标钉法要靠这个数。
                for (const auto& h : g_hits) {
                    snprintf(line, sizeof line, "[命中区] %s r%d %s %s x %.0f..%.0f y %.0f..%.0f 「%s」\n",
                             ActName(h.act), h.row, h.agent_id.c_str(), h.native_id.c_str(),
                             h.x0, h.x1, h.y0, h.y1, h.label.c_str());
                    dump += line;
                }
                snprintf(line, sizeof line, "[版面] focus=%d page=%d 预算=%.1fpt 折叠账:%s\n",
                         g_focus, g_page, (double)g_budgetH, g_fold_diag);
                dump += line;
                if (prev == nullptr || strcmp(prev, dump.c_str()) != 0) {
                    static std::string keep;
                    keep = dump;
                    prev = keep.c_str();
                    fputs(dump.c_str(), stderr);
                    fflush(stderr);
                }
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(win);
            if (g_relayout && g_panelH > 0) {   // 内容行数变了就把窗口收到刚好包住
                int cw, ch; glfwGetWindowSize(win, &cw, &ch);
                int px0, py0; glfwGetWindowPos(win, &px0, &py0);
                int want = WantedWindowH(), wantW = WantedWindowW();
                if (ch != want || cw != wantW) {
                    glfwSetWindowSize(win, wantW, want);
                    // 换屏把窗口从 340 撑到 420：右沿必须钉住。Cocoa 的 setContentSize 钉的是
                    // 左上角，于是整块面板会往右挪 80pt 并越过屏幕右沿——日志里那条内容框坐标
                    // 和真实指针的落点就全对不上了。所以改完尺寸立刻按「右沿不动」回写位置。
                    if (cw != wantW) glfwSetWindowPos(win, px0 + cw - wantW, py0);
                    lastResize = glfwGetTime();
                    // 每次真收/放都报一次屏幕坐标，供外部按矩形实拍（无边框，内容框＝窗口框）。
                    // 只在尺寸变了时报，不然 3 秒一轮的重扫会把日志刷满。
                    int px, py; glfwGetWindowPos(win, &px, &py);
                    printf("[窗口] 内容 %dx%d @ (%d,%d)｜%s屏\n", wantW, want, px, py,
                           g_view == 1 ? "设置" : "pin");
                    fflush(stdout);
                    // 高度已经与预算解耦，正常一帧就收敛；留 6 帧余量给异步生效，超了就放弃本轮，
                    // 免得系统真不给高（屏太小）时逐帧硬设尺寸刷日志。
                    if (++settle > 6) { g_relayout = false; }
                } else {
                    g_relayout = false;
                    settle = 0;
                }
            }
            // 行高稳住 2 秒（＝连续两轮重扫没改行数）才回读，不然拍到的是半截列表。
            int cw_now, ch_now; glfwGetWindowSize(win, &cw_now, &ch_now);
            if (shot_to && g_seq.load() >= 2 && glfwGetTime() - lastResize > 2.0 &&
                ch_now == WantedWindowH() && cw_now == WantedWindowW()) {   // 窗口收到＝「该摆的高度」才算摆稳；被屏高截断也算稳，否则拍到被裁的一截
                int sw, sh; glfwGetFramebufferSize(win, &sw, &sh);
                std::vector<unsigned char> pix((size_t)sw * sh * 4);
                glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
                std::ofstream of(shot_to, std::ios::binary);
                const bool opened = of.is_open();
                if (opened) of << "P6\n" << sw << "\n" << sh << "\n255\n";
                for (int y = sh - 1; opened && y >= 0; y--)
                    for (int x = 0; x < sw; x++) {
                        unsigned char* p = &pix[((size_t)y * sw + x) * 4];
                        of << p[0] << p[1] << p[2];
                    }
                of.close();
                // 落不了盘就不许报成功：曾经出现过日志写着「实拍 680x1678」、磁盘上什么
                // 都没有的一轮，相对路径在 .app 里跑时按不存在的 cwd 解析。取证只认文件。
                struct stat sb_st;
                const bool wrote = opened && ::stat(shot_to, &sb_st) == 0 && sb_st.st_size > 0;
                if (!wrote) {
                    char cwd[1024] = "?";
                    if (getcwd(cwd, sizeof cwd)) { /* keep */ }
                    fprintf(stderr, "[实拍] 没写成：%s（打不开或写完仍不存在；进程 cwd=%s）——本轮不算出图\n",
                            shot_to, cwd);
                    fflush(stderr);
                }
                time_t when = (time_t)g_snap.scanned_at;
                char stamp[32];
                strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&when));
                // 左上/左下角的 alpha：圆角外应当是 0；圆角内（顶行、横向 20pt 处）应当是 255。
                int tl = pix[(sh - 1) * sw * 4 + 3], tlin = pix[(sh - 1) * sw * 4 + 40 * 4 + 3];
                int bl = pix[3];
                printf("[实拍] %s (%dx%d，窗口帧缓冲原样) 数据扫描于 %s｜内容高 g_panelH=%.1fpt，"
                       "窗口内容框 %dx%dpt｜alpha 左上=%d 左下=%d 圆角内=%d（左上 0 才说明透明浮层真的生效）\n",
                       wrote ? shot_to : "（未落盘）", sw, sh, stamp, g_panelH, cw_now, ch_now, tl, bl, tlin);
                fflush(stdout);
                sb_float_window();   // 收摊前再报一次：resize 之后浮层属性有没有被 GLFW 改回去
                // 菜单栏那颗不在我们这块窗口里，而外部截屏在这台机器上只会得到纯黑
                // （驱动器没有屏幕录制授权），所以「数字到底挂上去没有」只能让按钮自绘一张。
                if (const char* sb = getenv("AGENTPING_SB_SHOT")) {
                    const int rc = sb_capture(sb);
                    if (rc) fprintf(stderr, "[状态项] 实拍没成：这一档没有可拍的按钮（码 %d）\n", rc);
                    fflush(stderr);
                }
                glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
        }
        g_quit.store(true);
        scanner.join();
        FT_Done_FreeType(ft);
        return 0;
    }

    // 离屏探针：先等第一轮真扫描落地，再六语并排渲一帧存 PPM。
    // 两行各自的预算必须按「内容自然高」算，不能拍脑袋给大：
    // ImGui 的裁剪矩形＝ max(预算, kCanvasH 760)，而 pin 屏末尾那段（「还有 N 条」+ 卡脚，实测 56pt）
    // 不受 maxBot 约束、总会多摆一截 —— 预算给到 720 时内容就涨到 776，底部 16pt 被裁剪矩形吃掉，
    // 实拍会拍到卡脚凭空缺一截（不是没画，是被裁了）。所以两行预算都压在 760 之内。
    const float kPinY = 60.0f, kSetY = 846.0f;
    const float kPinBudget = 700.0f, kSetBudget = 740.0f;
    g_budgetH = kPinBudget;
    for (int i = 0; i < 400 && g_seq.load() == 0; i++) usleep(50000);
    for (int pass = 0; pass < 3; pass++) {
        glClearColor(0, 0, 0, 1);   // 探针窗口不透明：底色清成纯黑，卡框边缘才量得准
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        if (pass == 0) {   // 图集建起来之后才允许量字宽，见窗口循环里那条注释
            long seq = g_seq.load();
            std::lock_guard<std::mutex> lk(g_mtx);
            g_pd_seq = seq;
            RebuildPanel(g_snap);
        }
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(offW, H));
        ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(g_font[T_TITLE][SC]);
        char stamp[96];
        time_t when;
        { std::lock_guard<std::mutex> lk(g_mtx); when = (time_t)g_snap.scanned_at; }
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&when));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
            "Agent Ping · 单壳 Dear ImGui 版 · 活数据扫描于 %s · 上排 pin／下排设置，各六语并排", stamp);
        ImGui::PopFont();
        ImGui::End();
        for (int loc = 0; loc < kLocales; loc++) {
            g_budgetH = kPinBudget;
            drawPanel(14 + loc * 374.0f, kPinY, (float)kPanelW, loc, 0);
            const float ph = g_panelH;
            g_budgetH = kSetBudget;
            drawPanel(14 + loc * 434.0f, kSetY, (float)kSetW, loc, 1);
            // 两行各自量一次：超过 760 会被 ImGui 裁剪矩形吃掉，超过行距会叠进下一行。
            if (pass == 0) printf("[探针] 语%d pin %.1fpt｜设置 %.1fpt（裁剪矩形 760，行距 %.0f）%s\n",
                                  loc, ph, g_panelH, kSetY - kPinY,
                                  (ph > 760.0f || ph > kSetY - kPinY || g_panelH > 760.0f)
                                      ? "＝溢出，本行不可信" : "");
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    int w, h; glfwGetFramebufferSize(win, &w, &h);
    std::vector<unsigned char> buf((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    { std::ofstream of(out, std::ios::binary);
      const bool opened = of.is_open();
      if (opened) of << "P6\n" << w << "\n" << h << "\n255\n";
      for (int y = h - 1; opened && y >= 0; y--) for (int x = 0; x < w; x++) {
          unsigned char* p = &buf[((size_t)y * w + x) * 4]; of << p[0] << p[1] << p[2]; }
      of.close();
      struct stat st_out;
      if (!opened || ::stat(out, &st_out) != 0 || st_out.st_size <= 0) {
          fprintf(stderr, "[输出] 没写成：%s——本轮不算出图\n", out);
          g_quit.store(true); scanner.join(); FT_Done_FreeType(ft);
          return 1;
      } }
    printf("[输出] %s (%dx%d)\n", out, w, h);
    g_quit.store(true);
    scanner.join();
    FT_Done_FreeType(ft);
    return 0;
}

// 固定文案参与预烘；会话标题不参与（见 main 里那段注释）。
static void AddChromeText(ImFontGlyphRangesBuilder& rb) {
    for (const auto& s : kStr) for (int loc = 0; loc < kLocales; loc++) rb.AddText(s.t[loc]);
    rb.AddText("·—…$0123456789.,:/ABCDEFGHIJKLMNOPQRSTUVWXYZ ");
}

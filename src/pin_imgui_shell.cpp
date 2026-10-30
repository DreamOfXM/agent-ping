#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "misc/freetype/imgui_freetype.h"
#include "platform/statusbar.h"
#include "platform/open.h"
#include "platform/notify.h"
#include "platform/lang.h"
#include "platform/autostart.h"
#include "alert.h"
#include "claims.h"
#include "scan.h"
#include "hook_install.h"

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
    // 菜单栏那三个数只放要人动手的；「跑完你还没看过」是另一档，只在悬停说明里念。
    {"sb_unseen",      {"待查看 %d", "%d to review", "%d zu prüfen", "%d da vedere",
                        "未確認 %d", "확인 전 %d"}},
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
    {"ask_answer",     {"需要你回答", "needs your answer", "braucht deine Antwort", "richiede una risposta", "回答が必要です", "답변이 필요"}},
    {"in_wait",        {"在等你", "Waiting on you", "Wartet auf dich", "In attesa di te", "待っています", "기다리는 중"}},
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
    // ── 面板补全（r20 的接线／问答详情／授权详情／历史）────────────────────────
    {"nav_history",    {"历史", "History", "Verlauf", "Cronologia", "履歴", "기록"}},
    {"nav_setup",      {"接线", "Connect", "Verbinden", "Collega", "接続", "연결"}},
    {"setup_lead",     {"让 CLI 停在问你的时候，把原话和选项送到这块面板。",
                        "When a CLI stops to ask you something, its question and options land here.",
                        "Wenn eine CLI dich etwas fragt, landen Frage und Optionen hier.",
                        "Quando una CLI si ferma a chiederti qualcosa, domanda e opzioni arrivano qui.",
                        "CLI が質問で止まったとき、原文と選択肢がここに届きます。",
                        "CLI가 질문하며 멈추면 원문과 선택지가 여기로 옵니다."}},
    {"setup_ok",       {"已接线", "Connected", "Verbunden", "Collegato", "接続済み", "연결됨"}},
    {"setup_stale",    {"路径失效", "Script missing", "Skript fehlt", "Script mancante",
                        "参照切れ", "경로 끊김"}},
    {"setup_none",     {"未接线", "Not connected", "Nicht verbunden", "Non collegato",
                        "未接続", "미연결"}},
    {"setup_manual",   {"需手动", "Manual", "Manuell", "Manuale", "手動", "수동"}},
    {"setup_watch",    {"只看状态", "State only", "Nur Status", "Solo stato", "状態のみ", "상태만"}},
    {"setup_install",  {"写入", "Install", "Installieren", "Installa", "設定", "설치"}},
    {"setup_rewrite",  {"重写", "Rewrite", "Neu schreiben", "Riscrivi", "再設定", "다시 쓰기"}},
    {"setup_writer",   {"写入器", "Writer script", "Writer-Skript", "Script writer",
                        "書き込みスクリプト", "기록 스크립트"}},
    {"setup_inbox",    {"收件箱", "Inbox", "Eingang", "Casella", "受信箱", "수신함"}},
    {"setup_cfg",      {"配置", "Config", "Konfiguration", "Configurazione", "設定ファイル", "설정 파일"}},
    {"setup_note",     {"只改本机文件；写配置前先备份。",
                        "Local files only; the existing config is backed up first.",
                        "Nur lokale Dateien; die bestehende Konfiguration wird vorher gesichert.",
                        "Solo file locali; la configurazione esistente viene salvata prima.",
                        "ローカルのファイルだけを変更します。設定は書き換え前にバックアップします。",
                        "로컬 파일만 바꾸며, 쓰기 전에 기존 설정을 백업합니다."}},
    {"setup_wait",     {"接好后让一个 CLI 停下来问你，这里会亮。",
                        "Once connected, the next time a CLI stops to ask, this panel lights up.",
                        "Wenn eine CLI danach fragt, leuchtet dieses Panel auf.",
                        "Dopo il collegamento, quando una CLI chiede, questo pannello si accende.",
                        "接続後、CLI が質問で止まるとここが光ります。",
                        "연결 후 CLI가 질문하며 멈추면 여기가 켜집니다."}},
    {"setup_fail",     {"写入失败", "Install failed", "Installieren fehlgeschlagen",
                        "Installazione fallita", "設定に失敗", "설치 실패"}},
    {"setup_done",     {"%s 已接线", "%s connected", "%s verbunden", "%s collegato",
                        "%s を接続しました", "%s 연결됨"}},
    {"setup_backup",   {"备份 %s", "Backup %s", "Sicherung %s", "Backup %s",
                        "バックアップ %s", "백업 %s"}},
    {"setup_restart",  {"若 CLI 正开着，重新打开一次再试。",
                        "If the CLI is running, reopen it before testing.",
                        "Läuft die CLI noch, öffne sie neu.",
                        "Se la CLI è aperta, riaprila prima di provare.",
                        "CLI を開いたままなら、開き直してから試してください。",
                        "CLI가 열려 있다면 다시 연 뒤 시험하세요."}},
    {"setup_occupied", {"被占用", "Occupied", "Belegt", "Occupato", "使用中", "사용 중"}},
    {"setup_hero",     {"先让它看见你的 agent", "Let it see your agents", "Lass es deine Agents sehen",
                        "Fagli vedere i tuoi agent", "まず agent を見つけさせます", "먼저 agent를 보게 하세요"}},
    {"setup_step1",    {"检测本机 agent", "Detect local agents", "Lokale Agents erkennen",
                        "Rileva gli agent locali", "ローカル agent を検出", "로컬 agent 감지"}},
    {"setup_step2",    {"接入提问信号", "Wire up questions", "Fragesignale verbinden",
                        "Collega i segnali di domanda", "質問シグナルを接続", "질문 신호 연결"}},
    {"setup_avail",    {"%d 个可用", "%d available", "%d verfügbar", "%d disponibili",
                        "%d 個利用可", "%d개 사용 가능"}},
    {"setup_hooknote", {"日志只看得出「停住了」，看不出它在问你什么。把 CLI 的 Notification 钩子接到本机脚本，问题原文就会写进收件箱；面板只读它。",
                        "Logs show that a CLI stopped, not what it asks. Point the CLI's Notification hook at the local script below and the exact question is written to the inbox; the panel only reads it.",
                        "Logs zeigen nur, dass eine CLI angehalten hat, nicht was sie fragt. Richte den Notification-Hook der CLI auf das lokale Skript unten, dann landet die Frage im Postfach; das Panel liest nur mit.",
                        "I log dicono solo che una CLI si è fermata, non cosa chiede. Collega l'hook Notification della CLI allo script locale qui sotto: la domanda finisce nella casella, il pannello la legge soltanto.",
                        "ログでは「止まった」ことしか分からず、何を聞いているかは分かりません。CLI の Notification フックを下のローカルスクリプトに向けると、質問原文が受信箱に書き込まれます。パネルは読むだけです。",
                        "로그로는 CLI가 멈춘 것만 알 수 있고 무엇을 묻는지는 알 수 없습니다. CLI의 Notification 훅을 아래 로컬 스크립트로 연결하면 질문 원문이 수신함에 기록됩니다. 패널은 읽기만 합니다."}},
    {"setup_nohook",   {"ZCode、WorkBuddy、OpenCode 没有可写的通知钩子，只读日志。",
                        "ZCode, WorkBuddy and OpenCode expose no writable notification hook — their logs are read only.",
                        "ZCode, WorkBuddy und OpenCode haben keinen schreibbaren Notification-Hook — nur Log-Lesen.",
                        "ZCode, WorkBuddy e OpenCode non hanno un hook Notification scrivibile: si leggono solo i log.",
                        "ZCode・WorkBuddy・OpenCode には書き込める通知フックがなく、ログを読むだけです。",
                        "ZCode, WorkBuddy, OpenCode는 쓸 수 있는 알림 훅이 없어 로그만 읽습니다."}},
    // 第三步：Qoder / ZCode 没有会话级链接，唯一能落到「那一条对话」的入口是它们自己的
    // 托盘/Command Center，而驱动那个入口要辅助功能授权。这一档现在有没有，屏上必须写，
    // 不然用户只会看到「点了半天只唤起 App」，不知道差在哪一格（2026-10-29 就是这么撞上的）。
    {"setup_step3",    {"授权辅助功能", "Allow Accessibility", "Bedienungshilfen erlauben",
                        "Consenti Accessibilità", "アクセシビリティを許可", "보조 기능 허용"}},
    {"setup_ax_on",    {"已授权", "Granted", "Erlaubt", "Concesso", "許可済み", "허용됨"}},
    {"setup_ax_off",   {"未授权", "Not granted", "Nicht erlaubt", "Non concesso",
                        "未許可", "미허용"}},
    // 「托盘里的条目」10-29 放宽：ZCode 那一路点的是它侧栏的任务行，不是托盘。
    // 只写托盘会让看 ZCode 的人以为这一档跟他无关。
    {"setup_axnote",   {"面板要点中客户端界面里的会话条目（Qoder 的托盘、ZCode 的侧栏）才能跳到那一条对话，这一步要辅助功能授权。不授权也能用，只是那一跳只把 App 带到前台。",
                        "To open one exact conversation the panel has to click that session inside the client (Qoder's tray, ZCode's sidebar), which needs Accessibility access. Without it the jump still brings the app forward — nothing more.",
                        "Für den Sprung auf eine einzelne Sitzung muss das Panel den Eintrag in der App anklicken (AblageMenü bei Qoder, Seitenleiste bei ZCode) — dafür sind die Bedienungshilfen nötig. Ohne sie kommt nur die App nach vorn.",
                        "Per aprire una conversazione precisa il pannello deve cliccare quella voce dentro il client (vassoi di Qoder, barra laterale di ZCode): serve Accessibilità. Senza, il salto porta avanti solo l’app.",
                        "特定の会話を開くには、パネルがクライアント画面内のその項目（Qoder のトレイ、ZCode のサイドバー）をクリックする必要があるためアクセシビリティの許可が要ります。未許可ではアプリを前面にするだけになります。",
                        "특정 대화로 가려면 패널이 클라이언트 화면 속 항목(Qoder 트레이, ZCode 사이드바)을 클릭해야 하므로 보조 기능 권한이 필요합니다. 없으면 앱만 앞으로 나옵니다."}},
    {"setup_ax_go",    {"去系统设置", "Open Settings", "Systemeinstellungen öffnen",
                        "Apri Impostazioni", "システム設定を開く", "시스템 설정 열기"}},
    {"setup_ax_done",  {"已开到辅助功能那一格", "The Accessibility pane is open",
                        "Das Bedienungshilfen-Fenster ist offen", "Il pannello Accessibilità è aperto",
                        "アクセシビリティ欄を開きました", "보조 기능 설정 창을 열었습니다"}},
    {"setup_ax_sub",   {"勾上 Agent Ping 后回到面板再点一次，不用重启",
                        "Tick Agent Ping, then click the row again — no restart needed",
                        "Agent Ping anhaken und die Zeile erneut klicken — kein Neustart",
                        "Spunta Agent Ping e clicca di nuovo la riga: non serve riavviare",
                        "Agent Ping をオンにしたら行をもう一度クリック。再起動は不要",
                        "Agent Ping을 켜고 행을 다시 누르면 됩니다. 재시작 불필요"}},
    {"setup_ax_fail",  {"系统设置没能自动打开，手动进：隐私与安全性 → 辅助功能",
                        "Settings didn't open — go there yourself: Privacy & Security → Accessibility",
                        "Die Einstellungen öffneten sich nicht — selbst gehen: Datenschutz & Sicherheit → Bedienungshilfen",
                        "Impostazioni non aperte — vai tu: Privacy e sicurezza → Accessibilità",
                        "設定が開けませんでした。手動で：プライバシーとセキュリティ → アクセシビリティ",
                        "설정이 열리지 않았어요. 직접 가세요: 개인정보 보호 및 보안 → 보조 기능"}},
    {"det_title",      {"提问", "Question", "Frage", "Domanda", "質問", "질문"}},
    {"det_session",    {"会话", "Session", "Sitzung", "Sessione", "セッション", "세션"}},
    {"det_source_val", {"hook 收件箱 · 本机", "hook inbox · local", "Hook-Postfach · lokal",
                        "casella hook · locale", "hook 受信箱 · ローカル", "훅 수신함 · 로컬"}},
    {"det_sess_n",     {"%d 会话", "%d sessions", "%d Sitzungen", "%d sessioni",
                        "%d セッション", "%d 세션"}},
    {"det_opt_count",  {"%d 个选项", "%d options", "%d Optionen", "%d opzioni",
                        "%d 個の選択肢", "%d개 선택지"}},
    {"det_empty",      {"现在没有待回答的问题。", "No questions are waiting right now.",
                        "Derzeit warten keine Fragen.", "Nessuna domanda in attesa.",
                        "今は答え待ちの質問がありません。", "지금 대기 중인 질문이 없습니다."}},
    {"det_toast",      {"已复制「%s」", "Copied “%s”", "„%s“ kopiert", "Copiato «%s»",
                        "「%s」をコピーしました", "「%s」 복사됨"}},
    {"det_toast_sub",  {"切到 %s 会话，粘贴后回车。", "Switch to the %s session, paste, press Enter.",
                        "Zur %s-Sitzung wechseln, einfügen, Enter.", "Vai alla sessione %s, incolla e premi Invio.",
                        "%s のセッションに移り、貼り付けて Enter。", "%s 세션으로 이동해 붙여넣고 Enter."}},
    {"det_options",    {"选项", "Options", "Optionen", "Opzioni", "選択肢", "선택지"}},
    {"det_copy",       {"复制", "Copy", "Kopieren", "Copia", "コピー", "복사"}},
    {"det_copied",     {"已复制", "Copied", "Kopiert", "Copiato", "コピー済み", "복사됨"}},
    {"det_no_submit",  {"这里不代你提交答案；复制后到终端粘贴。",
                        "Nothing is submitted for you — copy, then paste in the terminal.",
                        "Hier wird nichts gesendet — kopieren und im Terminal einfügen.",
                        "Qui non viene inviato nulla: copia e incolla nel terminale.",
                        "ここからは送信しません。コピーしてターミナルに貼り付けてください。",
                        "여기서 대신 제출하지 않습니다. 복사해 터미널에 붙여넣으세요."}},
    {"det_source",     {"来源", "Source", "Quelle", "Fonte", "取得元", "출처"}},
    {"det_wait",       {"已等", "Waiting", "Wartet", "In attesa", "待機", "대기"}},
    {"perm_title",     {"授权", "Permission", "Berechtigung", "Autorizzazione", "許可", "권한"}},
    {"perm_tool",      {"工具", "Tool", "Werkzeug", "Strumento", "ツール", "도구"}},
    {"perm_message",   {"CLI 原文", "CLI message", "CLI-Meldung", "Messaggio CLI",
                        "CLI の原文", "CLI 원문"}},
    {"perm_hint",      {"授权只能在终端完成。面板不会代你点允许或拒绝，这里只告诉你是哪一条、等了多久。",
                        "Permission is granted in the terminal. The panel never clicks allow or deny for you — it only shows which request it is and how long it has waited.",
                        "Berechtigungen werden im Terminal erteilt. Das Panel klickt nie Erlauben oder Ablehnen für dich — es zeigt nur, welcher Eintrag es ist und wie lange er wartet.",
                        "L'autorizzazione si dà nel terminale. Il pannello non clicca mai consenti o nega al posto tuo: dice solo quale richiesta è e da quanto attende.",
                        "許可はターミナルで行います。パネルが許可／拒否を代わりに押すことはありません。どれが、どれだけ待っているかだけを示します。",
                        "권한은 터미널에서 부여합니다. 패널이 허용/거부를 대신 누르지 않으며, 어떤 요청인지와 얼마나 기다렸는지만 보여 줍니다."}},
    {"perm_status",    {"停在授权", "Waiting for permission", "Wartet auf Freigabe",
                        "In attesa di permesso", "許可待ち", "권한 대기"}},
    {"perm_project",   {"项目", "Project", "Projekt", "Progetto", "プロジェクト", "프로젝트"}},
    {"perm_empty",     {"现在没有停在授权的会话。", "No session is waiting for permission.",
                        "Keine Sitzung wartet auf Freigabe.", "Nessuna sessione attende un permesso.",
                        "許可待ちのセッションはありません。", "권한을 기다리는 세션이 없습니다."}},
    // 那颗「去」按钮的两种真话：有客户端的才敢说「在客户端里打开这条会话」，
    // 纯 CLI 的这一路本机根本没有客户端，按钮只能承诺它真做得到的那件事。
    // （旧文案「切到 %s 的终端」是假的：C++ 版从来没实现过终端跳转。）
    {"land_go_sess",   {"打开客户端里的这条会话", "Open this session in the client",
                        "Diese Sitzung in der App öffnen", "Apri questa sessione nel client",
                        "クライアントでこのセッションを開く", "클라이언트에서 이 세션 열기"}},
    {"land_go_log",    {"打开这条会话的记录目录", "Open this session's log folder",
                        "Protokollordner dieser Sitzung öffnen", "Apri la cartella log di questa sessione",
                        "このセッションのログフォルダを開く", "이 세션의 로그 폴더 열기"}},
    {"hist_done",      {"今日完成", "Finished today", "Heute beendet", "Conclusi oggi",
                        "本日完了", "오늘 완료"}},
    {"hist_latest",    {"回到最新", "Back to latest", "Zu den neuesten", "Torna ai recenti",
                        "最新に戻る", "최신으로"}},
    {"hist_sum",       {"%d 个会话完成", "%d sessions finished", "%d Sitzungen beendet",
                        "%d sessioni concluse", "%d セッション完了", "%d개 세션 완료"}},
    {"hist_empty",     {"今天还没有结束的会话。", "No sessions finished today yet.",
                        "Heute noch keine beendeten Sitzungen.", "Nessuna sessione conclusa oggi.",
                        "今日はまだ終了したセッションがありません。", "오늘 아직 끝난 세션이 없습니다."}},
    // ── r20 版式（340 宽、底部标签、排队式总览）。这组键只服务 r20 那一套结构：
    //    节头带计数与右注、等你回答的琥珀胶囊、等你授权行、44px 标签栏、详情页的 kv 表。
    {"sl_q",           {"等你回答", "Waiting on you", "Wartet auf dich", "In attesa di te",
                        "回答待ち", "답변 대기"}},
    {"sl_p",           {"等你授权", "Waiting for permission", "Wartet auf Freigabe",
                        "In attesa di permesso", "許可待ち", "권한 대기"}},
    {"sl_run",         {"其它在跑", "Also running", "Außerdem läuft", "Altri in corso",
                        "その他実行中", "기타 실행 중"}},
    {"longest",        {"最久 %s", "Oldest %s", "Ältestes %s", "Più vecchio %s",
                        "最長 %s", "최장 %s"}},
    {"confirm_term",   {"在终端确认", "Confirm in the terminal", "Im Terminal bestätigen",
                        "Conferma nel terminale", "ターミナルで確認", "터미널에서 확인"}},
    {"running_now",    {"在跑", "Running", "Läuft", "In corso", "実行中", "실행 중"}},
    {"opt_n",          {"%d 个选项", "%d options", "%d Optionen", "%d opzioni",
                        "%d 個の選択肢", "%d개 옵션"}},
    {"step_ago",       {"上一步 %s 前", "last step %s ago", "letzter Schritt vor %s",
                        "ultimo passo %s fa", "前回の操作 %s前", "마지막 단계 %s 전"}},
    {"turns_n",        {"%d 轮", "%d turns", "%d Runden", "%d turni",
                        "%d ターン", "%d 턴"}},
    {"skip",           {"跳过", "Skip", "Überspringen", "Salta", "スキップ", "건너뛰기"}},
    {"tab_overview",   {"总览", "Overview", "Übersicht", "Panoramica", "概要", "개요"}},
    {"empty_none",     {"没有等你处理的事", "Nothing needs you", "Nichts wartet auf dich",
                        "Niente richiede te", "対応待ちはありません", "처리할 일이 없습니다"}},
    {"empty_live",     {"「%s」还在跑。它一停下来问你，这里会亮起来。",
                        "“%s” is still running. When it stops to ask you, this lights up.",
                        "„%s“ läuft noch. Sobald es dich fragt, leuchtet es hier.",
                        "«%s» è ancora in corso. Quando si ferma a chiederti, qui si accende.",
                        "「%s」は実行中です。質問で止まるとここが光ります。",
                        "「%s」은 아직 실행 중입니다. 질문하며 멈추면 여기가 켜집니다."}},
    {"empty_idle",     {"今天还没有会话需要介入。有新的提问或授权请求时，菜单栏会亮起并提醒。",
                        "No session needs you today. When a question or permission request arrives, the menu bar lights up.",
                        "Heute braucht keine Sitzung dich. Bei einer neuen Frage oder Freigabe leuchtet die Menüleiste.",
                        "Oggi nessuna sessione richiede te. A una nuova domanda o richiesta di permesso si accende la barra dei menu.",
                        "今日は対応が必要なセッションがありません。新しい質問や許可要求があるとメニューバーが光ります。",
                        "오늘은 대응할 세션이 없습니다. 새 질문이나 권한 요청이 오면 메뉴 막대가 켜집니다."}},
    {"det_optlab",     {"%d 个选项 · 点哪个都只复制，不代提交",
                        "%d options · any click only copies, never submits",
                        "%d Optionen · Klick kopiert nur, sendet nie",
                        "%d opzioni · il clic copia soltanto, non invia",
                        "%d 個の選択肢 · どれもコピーのみ、送信はしません",
                        "%d개 옵션 · 클릭은 복사만, 제출하지 않음"}},
    {"det_rec",        {"推荐", "Suggested", "Empfohlen", "Consigliato",
                        "おすすめ", "추천"}},
    {"det_hint_q",     {"复制后切到 %s 的该会话，粘贴、回车。面板不替你把答案发出去。",
                        "Copy, switch to that %s session, paste, press Enter. The panel never sends the answer.",
                        "Kopieren, zur %s-Sitzung wechseln, einfügen, Enter. Das Panel sendet nie.",
                        "Copia, vai a quella sessione %s, incolla, Invio. Il pannello non invia mai.",
                        "コピーして %s の該当セッションで貼り付け、Enter。パネルは送信しません。",
                        "%s 세션으로 이동해 붙여넣고 Enter. 패널은 답을 보내지 않습니다."}},
    {"hist_rail",      {"轮 · 今日 %d 个会话完成", "turns · %d sessions finished today",
                        "Runden · %d Sitzungen heute beendet", "turni · %d sessioni concluse oggi",
                        "ターン · 本日 %d セッション完了", "턴 · 오늘 %d개 세션 완료"}},
    {"hist_span",      {"08:00 – 24:00", "08:00 – 24:00", "08:00 – 24:00",
                        "08:00 – 24:00", "08:00 – 24:00", "08:00 – 24:00"}},
    {"copy",           {"复制", "Copy", "Kopieren", "Copia", "コピー", "복사"}},
    {"set_alert",      {"提醒", "Alerts", "Hinweise", "Avvisi", "通知", "알림"}},
    {"set_access",     {"接入", "Connection", "Anbindung", "Collegamento",
                        "接続", "연결"}},
    {"set_ui",         {"界面", "Interface", "Oberfläche", "Interfaccia",
                        "表示", "인터페이스"}},
    {"set_dnd",        {"免打扰", "Do not disturb", "Nicht stören", "Non disturbare",
                        "おやすみモード", "방해 금지"}},
    {"set_dnd_sub",    {"暂停新提醒，不清账本", "Pause new alerts, keep the ledger",
                        "Neue Hinweise pausieren, Zähler bleiben",
                        "Sospendi i nuovi avvisi, il conteggio resta",
                        "新しい通知を止めます。記録は残ります",
                        "새 알림을 멈추고 기록은 유지"}},
    {"set_claim",      {"权益到账提醒", "Reward alerts", "Gutschein-Hinweise",
                        "Avvisi premi", "特典のお知らせ", "쿠폰 도착 알림"}},
    {"set_claim_sub",  {"有券时弹横幅，不点不收；只读不代领",
                        "Stays until dismissed — read-only, no auto-claim",
                        "Bleibt bis zum Schließen — nur lesend",
                        "Resta finché non lo chiudi — sola lettura",
                        "閉じるまで表示・読み取り専用",
                        "닫을 때까지 유지 — 읽기 전용"}},
    {"set_launch",     {"登录时启动", "Launch at login", "Beim Anmelden starten",
                        "Avvia all'accesso", "ログイン時に起動", "로그인 시 시작"}},
    {"set_launch_sub", {"macOS LaunchAgent · Linux autostart",
                        "macOS LaunchAgent · Linux autostart",
                        "macOS LaunchAgent · Linux autostart",
                        "macOS LaunchAgent · Linux autostart",
                        "macOS LaunchAgent · Linux autostart",
                        "macOS LaunchAgent · Linux autostart"}},
    {"set_recheck",    {"重新运行接入检测", "Re-run detection", "Erkennung erneut starten",
                        "Ripeti il rilevamento", "検出をやり直す", "감지 다시 실행"}},
    {"set_recheck_sub",{"检测本机 agent 与 hook 收件箱", "Check local agents and the hook inbox",
                        "Lokale Agents und Hook-Postfach prüfen",
                        "Controlla agent locali e casella hook",
                        "ローカル agent と hook 受信箱を確認",
                        "로컬 agent와 훅 수신함 확인"}},
    {"set_agents",     {"监控的 agent", "Monitored agents", "Überwachte Agents",
                        "Agent monitorati", "監視する agent", "모니터링 agent"}},
    {"set_agents_sub", {"来自内置采集器", "From the built-in collectors",
                        "Aus den eingebauten Sammlern", "Dai raccoglitori integrati",
                        "内蔵収集器から", "내장 수집기에서"}},
    {"set_lang",       {"语言", "Language", "Sprache", "Lingua", "言語", "언어"}},
    {"set_lang_sub",   {"六语", "Six languages", "Sechs Sprachen", "Sei lingue",
                        "6 言語", "6개 언어"}},
    {"set_data",       {"数据", "Data", "Daten", "Dati", "データ", "데이터"}},
    {"set_data_sub",   {"只读本机六个 agent 的日志与 hook 收件箱；二进制内无网络调用。",
                        "Reads local logs of six agents and the hook inbox; the binary makes no network calls.",
                        "Liest lokale Logs von sechs Agents und das Hook-Postfach; keine Netzwerkaufrufe.",
                        "Legge i log locali di sei agent e la casella hook; nessuna chiamata di rete.",
                        "本機 6 agent のログと hook 受信箱のみ読みます。ネットワーク呼び出しはありません。",
                        "로컬 6개 agent 로그와 훅 수신함만 읽습니다. 네트워크 호출은 없습니다."}},
    {"set_license",    {"许可", "License", "Lizenz", "Licenza", "ライセンス", "라이선스"}},
    {"set_license_sub",{"Apache-2.0 · 字体 SIL OFL · Dear ImGui MIT",
                        "Apache-2.0 · fonts SIL OFL · Dear ImGui MIT",
                        "Apache-2.0 · Schriften SIL OFL · Dear ImGui MIT",
                        "Apache-2.0 · font SIL OFL · Dear ImGui MIT",
                        "Apache-2.0 · フォント SIL OFL · Dear ImGui MIT",
                        "Apache-2.0 · 폰트 SIL OFL · Dear ImGui MIT"}},
    {"set_proto",      {"快照", "Snapshot", "Momentaufnahme", "Istantanea",
                        "スナップショット", "스냅샷"}},
    {"set_proto_sub",  {"列表与数字为最近一次本机扫描结果；接线页的检测与命令均在本机真实执行。",
                        "Lists and numbers come from the latest local scan; the checks and commands on the setup screen run for real, locally.",
                        "Listen und Zahlen stammen vom letzten lokalen Scan; Prüfungen und Befehle der Einrichtung laufen wirklich lokal.",
                        "Elenchi e numeri dall'ultima scansione locale; i controlli e i comandi della schermata di configurazione vengono eseguiti davvero in locale.",
                        "一覧と数字は直近のローカル走査結果です。接続画面の検出とコマンドは本機で実際に実行します。",
                        "목록과 숫자는 최근 로컬 스캔 결과이며, 연결 화면의 감지와 명령은 본 기기에서 실제로 실행됩니다."}},
    {"open",           {"打开", "Open", "Öffnen", "Apri", "開く", "열기"}},
    {"manage",         {"管理", "Manage", "Verwalten", "Gestisci", "管理", "관리"}},
    {"setup_hero_sub", {"Agent Ping 不连服务器：它读本机日志判断谁在跑、谁在等你，再在菜单栏提醒你。两步接完。",
                        "Agent Ping has no server: it reads local logs to see who is running and who waits for you, then alerts from the menu bar. Two steps.",
                        "Agent Ping hat keinen Server: Es liest lokale Logs und meldet sich aus der Menüleiste. Zwei Schritte.",
                        "Agent Ping non ha server: legge i log locali e avvisa dalla barra dei menu. Due passi.",
                        "Agent Ping はサーバーに接続しません。ローカルログを読み、メニューバーで知らせます。2 ステップです。",
                        "Agent Ping은 서버에 연결하지 않습니다. 로컬 로그를 읽고 메뉴 막대에서 알립니다. 두 단계입니다."}},
    {"setup_n_agents", {"%d 个可用", "%d available", "%d verfügbar", "%d disponibili",
                        "%d 個利用可", "%d개 사용 가능"}},
    // ── r20 版式收尾：稿子里几处带占位符的短句。%s 那一段由外面填，所以要六语各留一个洞。
    {"wait_for",       {"等 %s", "%s waiting", "%s wartet", "%s in attesa", "%s 待ち", "%s 대기"}},
    {"src_signal",     {"信号来源", "Signal source", "Signalquelle", "Origine segnale",
                        "シグナル元", "신호 출처"}},
    {"hist_win",       {"%s – %s", "%s – %s", "%s – %s", "%s – %s", "%s – %s", "%s – %s"}},
    // 接线屏两条脚注：粗体那截（收件箱路径 / 文件名形状）在稿子里是 <b>，
    // 这里用 %s 占位，绘制时按「%s 前 / %s 本身 / %s 后」三段分别上色。
    {"setup_inbox_note",
        {"日志只看得出「停住了」，看不出「在问你什么」。把 CLI 的 Notification 钩子接到下面的脚本，"
         "问题原文就会写进收件箱 %s。Agent Ping 只读它，从不写它。",
         "Logs show that a CLI stopped, not what it asks. Point the CLI's Notification hook at the script "
         "below and the exact question lands in the inbox %s. Agent Ping only reads it, never writes it.",
         "Logs zeigen nur, dass eine CLI angehalten hat, nicht was sie fragt. Richte den Notification-Hook "
         "der CLI auf das Skript unten, dann landet die Frage im Postfach %s. Agent Ping liest nur, schreibt nie.",
         "I log dicono solo che una CLI si è fermata, non cosa chiede. Collega l'hook Notification della CLI "
         "allo script qui sotto: la domanda finisce nella casella %s. Agent Ping la legge soltanto, non ci scrive.",
         "ログでは「止まった」ことしか分からず、何を聞いているかは分かりません。CLI の Notification フックを"
         "下のスクリプトに向けると、質問原文が受信箱 %s に書き込まれます。Agent Ping は読むだけで、書きません。",
         "로그로는 CLI가 멈춘 것만 알 수 있고 무엇을 묻는지는 알 수 없습니다. CLI의 Notification 훅을 아래 "
         "스크립트로 연결하면 질문 원문이 수신함 %s 에 기록됩니다. Agent Ping은 읽기만 하고 쓰지 않습니다."}},
    // 跨平台脚注：本机收件箱后面括住另一套系统的默认位置（稿子同款）。
    {"setup_os_hint",  {"（%s：%s）", " (%s: %s)", " (%s: %s)", " (%s: %s)",
                        "（%s：%s）", "(%s: %s)"}},
    {"setup_files_note",
        {"Qoder CLI 挂同一条命令即可。文件名形状 %s。",
         "Qoder CLI uses the same command. File name shape %s.",
         "Qoder CLI nutzt denselben Befehl. Dateinamensmuster %s.",
         "Qoder CLI usa lo stesso comando. Formato del nome file %s.",
         "Qoder CLI も同じコマンドです。ファイル名は %s 形式。",
         "Qoder CLI도 같은 명령을 씁니다. 파일 이름 형태는 %s."}},
    {"setup_cfg_note", {"%s · 追加 hooks 段", "%s · append the hooks block", "%s · hooks-Block anhängen",
                        "%s · aggiungi il blocco hooks", "%s · hooks を追記", "%s · hooks 블록 추가"}},
    {"setup_copied",   {"已复制接入脚本", "Setup snippet copied", "Einrichtungsschnipsel kopiert",
                        "Frammento di configurazione copiato", "設定スニペットをコピーしました",
                        "설정 스니펫 복사됨"}},
    {"setup_copied_sub",{"粘到终端回车即可落盘", "Paste into a terminal and press Enter to write it",
                        "In ein Terminal einfügen und Enter drücken", "Incolla nel terminale e premi Invio",
                        "ターミナルに貼り付けて Enter", "터미널에 붙여넣고 Enter"}},
    {"setup_waitline", {"等待第一个信号……收到后总览会出现「等你回答」。",
                        "Waiting for the first signal… once it arrives, “Waiting on you” shows up in Overview.",
                        "Warte auf das erste Signal… danach erscheint „Wartet auf dich“ in der Übersicht.",
                        "In attesa del primo segnale… poi compare «In attesa di te» nella panoramica.",
                        "最初のシグナルを待っています…届くと概要に「回答待ち」が出ます。",
                        "첫 신호를 기다리는 중… 도착하면 개요에 「답변 대기」가 나타납니다."}},
    {"set_dnd_note",   {"settings.txt · dnd", "settings.txt · dnd", "settings.txt · dnd",
                        "settings.txt · dnd", "settings.txt · dnd", "settings.txt · dnd"}},
    {"set_sound_note", {"settings.txt · sound", "settings.txt · sound", "settings.txt · sound",
                        "settings.txt · sound", "settings.txt · sound", "settings.txt · sound"}},
    {"set_top_note",   {"settings.txt · on_top", "settings.txt · on_top", "settings.txt · on_top",
                        "settings.txt · on_top", "settings.txt · on_top", "settings.txt · on_top"}},
    {"set_state_note", {"settings.txt · %s", "settings.txt · %s", "settings.txt · %s",
                        "settings.txt · %s", "settings.txt · %s", "settings.txt · %s"}},
    {"set_lang_note",  {"AGENTPING_LANG · 六语", "AGENTPING_LANG · six languages",
                        "AGENTPING_LANG · sechs Sprachen", "AGENTPING_LANG · sei lingue",
                        "AGENTPING_LANG · 6 言語", "AGENTPING_LANG · 6개 언어"}},
    {"set_agents_ids", {"%s", "%s", "%s", "%s", "%s", "%s"}},
    {"set_on_top",     {"常驻置顶", "Keep on top", "Immer oben halten", "Sempre in primo piano",
                        "常に手前に表示", "항상 최상단"}},
    {"span_contig",    {"连续 %s", "%s straight", "%s am Stück", "%s di fila",
                        "連続 %s", "%s 연속"}},
    // ── 落点回执（2026-10-29）───────────────────────────────────────────────
    // 用户原话：「怎么点击 qoder 的任务打开的是文件夹，不是应该定位到对应会话吗」。
    // 所以那一跳落到哪一层要写在屏上：主行说清落到哪、副行说下一步由他做的那件事；
    // 平台侧那句人话只进 [落点] 日志（六语表里没有它，上屏会把中文漏进德/日界面）。
    {"land_sess",      {"已定位到这条会话", "That session is now in front",
                        "Diese Sitzung wurde geöffnet", "Sessione aperta",
                        "このセッションを開きました", "해당 세션을 열었습니다"}},
    {"land_tray",      {"这条没在客户端托盘列出的会话里", "That session isn't in the client's tray list",
                        "Diese Sitzung steht nicht in der Ablageliste der App",
                        "Questa sessione non è nell'elenco del tray del client",
                        "このセッションはクライアントのトレイ一覧に出ていません",
                        "이 세션은 클라이언트 트레이 목록에 없습니다"}},
    // 10-29 改字：这一档原来写「在客户端里搜不到这条会话」，那是 ⌘K 那一路的说法。
    // 命令中心撤掉后没有任何检索发生，再说「搜不到」就是编一个没跑过的动作。
    // 现在这句只陈述结果：客户端里没能点到这一条（侧栏没列它／窗口没建起来／点了没换都算）。
    {"land_miss",      {"客户端里没能点到这一条会话", "Couldn't open that session inside the client",
                        "In der App ließ sich diese Sitzung nicht öffnen",
                        "La sessione non è stata aperta nel client",
                        "クライアント内でこのセッションを開けませんでした",
                        "클라이언트에서 해당 세션을 열지 못했습니다"}},
    // 10-29 新增：这一条建得比 ZCode 本次启动晚（实测：本次启动后新建的 15 条零命中）。
    // 不复用 land_copy 那句「对着标题在列表里找」——列表里根本没有它，那是给人指一条死路。
    {"land_newrun",    {"这条比 ZCode 本次启动还新", "This session is newer than ZCode's current run",
                        "Diese Sitzung ist neuer als ZCodes aktueller Lauf",
                        "Questa sessione è più recente dell'avvio corrente di ZCode",
                        "このセッションは ZCode の今回の起動より新しい",
                        "이 세션은 ZCode의 현재 실행보다 최근에 만들어졌습니다"}},
    {"land_newrun_sub",{"它建于本次启动之后，ZCode 的列表里还没有它 — 标题已复制",
                        "Created after that start, so it isn't in ZCode's list yet — title copied",
                        "Nach diesem Start erstellt — deshalb fehlt es in ZCodes Liste; Titel kopiert",
                        "Creata dopo l'avvio: non è ancora nell'elenco di ZCode — titolo copiato",
                        "今回の起動の後に作られたため ZCode の一覧にありません — タイトルをコピーしました",
                        "현재 실행 이후에 만들어져 ZCode 목록에 없습니다 — 제목을 복사했습니다"}},
    {"land_app",       {"只把 %s 带到了前台", "Only brought %s to the front",
                        "%s wurde nur in den Vordergrund geholt",
                        "%s è stata portata solo in primo piano",
                        "%s を前面に出しただけです", "%s 앱만 앞으로 불러왔습니다"}},
    // 10-29 新增：ZCode 一个窗口都没有时，这一跳会先替它把窗口要回来。实测 reopen 3 秒内出窗，
    // 但无障碍树还要长一会儿；面板一跳只等 2.4 秒（4 轮 ×600ms），没长出来就返回 5＝这一档。
    // 主行只说「正在打开」，不说「已经开好」——数到窗口不等于那棵树已经能点。
    {"land_wake",      {"ZCode 的窗口正在打开", "ZCode is opening its window",
                        "ZCode öffnet gerade ein Fenster", "ZCode sta aprendendo una finestra",
                        "ZCode がウィンドウを開いています", "ZCode 창을 여는 중입니다"}},
    {"land_wake_sub",  {"界面还要几秒才长出来 — 稍等一下再点这一行就能落到那条会话",
                        "Its UI needs a few more seconds — click that row again shortly",
                        "Die Oberfläche braucht ein paar Sekunden — gleich erneut auf die Zeile",
                        "L'interfaccia ha bisogno di qualche secondo: clicca di nuovo la riga",
                        "画面の立ち上がりに数秒かかります —少し待ってからもう一度この行をクリック",
                        "화면이 뜨는 데 몇 초 걸립니다 — 잠시 후 이 줄을 다시 누르세요"}},
    // 「贴一下就能到」10-29 撤掉：Qoder 托盘和 ZCode 侧栏都没有能粘贴的检索框（前者只有最近
    // 12 条列表，后者只有任务列表），那句话承诺了一个客户端里不存在的动作。改成只说怎么找。
    {"land_copy",      {"标题已复制 — 在它的会话列表里对着这个标题找",
                        "Title copied — match it against the client's session list",
                        "Titel kopiert — in der Sitzungsliste der App danach suchen",
                        "Titolo copiato — cercalo nell'elenco delle sessioni",
                        "タイトルをコピーしました — クライアントの一覧でこのタイトルを探してください",
                        "제목을 복사했습니다 — 클라이언트 목록에서 이 제목으로 찾으세요"}},
    {"land_dir",       {"只打开了记录目录，没到那条会话", "Only the log folder opened — not that session",
                        "Nur der Protokollordner wurde geöffnet — nicht die Sitzung",
                        "Si è aperta solo la cartella log, non la sessione",
                        "ログフォルダを開いただけで、そのセッションではありません",
                        "로그 폴더만 열었고 해당 세션은 아닙니다"}},
    // 记录目录这一档的第二行：这一路根本没有客户端，所以不许借用 land_copy
    // 那句「在它的会话列表里对着这个标题找」——那列表并不存在（2026-10-29 实拍读出来的）。
    {"land_dir_sub",   {"这一路只有命令行，没有客户端可跳；标题已复制",
                        "This one is CLI-only — no client to jump into; title copied",
                        "Diese Sitzung gibt es nur in der CLI — keine App; Titel kopiert",
                        "Questa è solo CLI: nessun client da aprire; titolo copiato",
                        "この経路はCLIのみでクライアントはありません。タイトルはコピーしました",
                        "이 경로는 CLI뿐이라 클라이언트가 없습니다. 제목은 복사했습니다"}},
    {"land_ax",        {"需要辅助功能授权，才能点中那条会话",
                        "Accessibility access is needed to open that exact session",
                        "Für diesen Sprung fehlt der Zugriff auf die Bedienungshilfen",
                        "Serve l'accesso a Accessibilità per aprire quella sessione",
                        "そのセッションを開くにはアクセシビリティの許可が必要です",
                        "해당 세션을 열려면 보조 기능 권한이 필요합니다"}},
    {"land_ax_sub",    {"系统设置里那一格已经打开；拨上后再点一次即可，不用重启",
                        "The settings pane just opened — tick it and click again, no restart",
                        "Das Einstellfenster ist offen — anhaken und erneut klicken, kein Neustart",
                        "Il pannello delle impostazioni è aperto: spuntalo e clicca di nuovo, senza riavviare",
                        "設定パネルを開きました。オンにしてもう一度クリックすれば再起動は不要です",
                        "설정 창을 열었습니다. 켜고 다시 누르면 됩니다. 재시작이 필요 없습니다"}},
    {"land_none",      {"这一跳没落到实处", "The jump landed nowhere",
                        "Der Sprung ging ins Leere", "Il salto non è arrivato a nulla",
                        "この移動はどこにも着地しませんでした", "이동할 곳을 찾지 못했습니다"}},
    {"toast_back_ov",  {"返回总览", "Back to overview", "Zur Übersicht", "Torna alla panoramica",
                        "概要に戻る", "개요로 돌아가기"}},
    // 稿子授权详情里「触发说明」那一行的键，以及设置屏第四节（四档提醒内容）的节头。
    {"perm_trigger",   {"触发说明", "Trigger note", "Auslöser", "Innesco", "トリガー", "트리거"}},
    {"set_states",     {"提醒内容", "Alert types", "Hinweisarten", "Tipi di avviso",
                        "通知の種類", "알림 종류"}},
    {"det_sess_today", {"%d 会话 · 今日", "%d sessions · today", "%d Sitzungen · heute",
                        "%d sessioni · oggi", "%d セッション · 本日", "%d 세션 · 오늘"}},
    {"setup_absent",   {"未安装", "Not installed", "Nicht installiert", "Non installato",
                        "未インストール", "미설치"}},
    // ── r31 版式的固定文案 ────────────────────────────────────────────────
    {"r_all",          {"全部", "All", "Alle", "Tutti", "すべて", "전체"}},
    {"r_sessions",     {"会话", "Sessions", "Sitzungen", "Sessioni", "セッション", "세션"}},
    {"r_usage_tab",    {"用量与额度", "Usage & limits", "Nutzung & Limits", "Uso e limiti",
                        "使用量と上限", "사용량 및 한도"}},
    // ── 权益领取（view 9）：全部字段来自各家写在自己磁盘上的读数 ──────────────
    {"r_claim_tab",    {"权益", "Rewards", "Vorteile", "Vantaggi", "特典", "혜택"}},
    {"r_claim_head",   {"本机读数能确认的领取", "Claims the local logs can confirm",
                        "Anforderungen laut lokalen Protokollen", "Riscossioni verificabili dai log locali",
                        "ローカルログで確認できる受取", "로컬 로그로 확인할 수 있는 수령"}},
    {"r_claim_ready",  {"现在可领", "Ready to claim", "Jetzt verfügbar", "Disponibile ora",
                        "今受け取れる", "지금 수령 가능"}},
    {"r_claim_done",   {"本期已领", "Claimed", "Abgeholt", "Già riscosso",
                        "受け取り済み", "수령 완료"}},
    {"r_claim_none",   {"当前没有可领", "Nothing to claim", "Nichts verfügbar",
                        "Nulla da riscuotere", "受け取れるものなし", "받을 항목 없음"}},
    {"r_claim_unknown",{"状态未知", "Unknown", "Unbekannt", "Sconosciuto", "状態不明", "상태 모름"}},
    // 数字拆出来单独放大之后，这一格只剩标签：六语都得是「光秃秃一个名词短语」，
    // 不能带「连续…天」那种把数字包在中间的写法，否则拆完读不通。
    {"r_claim_streak_lab", {"连续签到", "day streak", "Tage in Folge", "giorni di fila",
                            "連続日数", "연속 일수"}},
    {"r_claim_opens",  {"本期 %s 起可见", "Opens %s", "Ab %s sichtbar", "Disponibile dalle %s",
                        "%sから利用可", "%s부터 열림"}},
    {"r_claim_sincenone",{"%s 起报没有可领", "None since %s", "Keine seit %s",
                          "Nessuno dalle %s", "%sから受取不可", "%s부터 수령 불가"}},
    {"r_claim_expires",{"%s 后作废", "Expires in %s", "Läuft in %s ab", "Scade tra %s",
                        "%s後に失効", "%s 후 만료"}},
    {"r_claim_legend", {"实线＝知道它什么时候作废；虚线＝只知道它什么时候出现",
                        "Solid = an expiry is known · dashed = only when it appeared",
                        "Durchgehend = Ablauf bekannt · gestrichelt = nur das Erscheinen",
                        "Pieno = scadenza nota · tratteggiato = solo la comparsa",
                        "実線＝失効時刻まで判明／破線＝出現時刻のみ",
                        "실선 = 만료 시각 확인 · 점선 = 등장 시각만"}},
    {"r_claim_last",   {"上次读到可领 %s", "Last ready %s", "Zuletzt bereit %s",
                        "Ultimo pronto %s", "最後に受取可 %s", "마지막 수령 가능 %s"}},
    {"r_claim_seen",   {"读数 %s", "Reading %s", "Messwert %s", "Lettura %s", "読取 %s", "읽은 값 %s"}},
    // 这一行说的是「那家自己什么时候写的盘」，与上一行（它读数里的时刻）和页脚（我们什么时候
    // 读的）是三件事。分开的理由：只有这一行能回答「我们没刷，还是那家没写」。
    {"r_claim_src",    {"本机文件 %s", "File %s", "Datei %s", "File %s", "ファイル %s", "파일 %s"}},
    {"r_claim_alerted", {"上次提醒 %s", "Last alert %s", "Letzte Meldung %s",
                         "Ultimo avviso %s", "前回の通知 %s", "마지막 알림 %s"}},
    {"r_claim_never",  {"还没有提醒过", "No alert yet", "Noch keine Meldung", "Nessun avviso",
                        "通知はまだありません", "알림 없음"}},
    {"r_claim_refresh", {"刷新", "Refresh", "Aktualisieren", "Aggiorna", "更新", "새로고침"}},
    {"r_claim_refreshed", {"已重读本机读数", "Local readings re-read", "Lokale Werte neu gelesen",
                           "Lettura locale aggiornata", "ローカル値を再読み込み", "로컬 값 재확인"}},
    {"r_claim_foot",   {"只读：不代领，不碰登录态", "Read-only: never claims, never reads your login",
                        "Nur lesend: fordert nichts an, liest kein Login",
                        "Sola lettura: non riscuote nulla, non legge il login",
                        "読み取り専用：代行なし・ログイン情報なし",
                        "읽기 전용: 대리 수령 없음, 로그인 정보 접근 없음"}},
    // 2026-10-30 用户：「双击为啥没反应呢，只是提示吗，需要去 agent 领，是吗」。
    // 权益卡原来一个命中区都没有，这三条是接上之后屏上要说的话：
    // open_zone 是那颗命中区的名字（日志与取证序列按它寻址），后两条是点完的回执主行＋副行。
    // 主行复用落点层现成的 land_app／land_none（「只把 X 带到了前台」／「这一跳没落到实处」），
    // 副行必须说清「领」这个动作不在我们手里 —— 与页脚那句只读是同一件事的两种说法。
    {"r_claim_open_zone", {"打开 %s 领", "Open %s to claim", "%s öffnen und abholen",
                           "Apri %s e riscuoti", "%s を開けて受け取る", "%s 열어 수령"}},
    {"r_claim_land_sub", {"领取仍要你自己在那家 App 里点 — 这一层不代领",
                          "Claim it inside that app yourself — this layer never claims for you",
                          "Prämie selbst in der App abholen — diese Ebene fordert nichts an",
                          "Riscuoti tu dentro l'app — questo livello non riscuote nulla",
                          "受け取りはアプリ内であなた自身で — この層は代行しません",
                          "수령은 해당 앱에서 직접 하세요 — 이 계층은 대리 수령하지 않습니다"}},
    {"r_claim_land_none_sub", {"本机没有它的 App，或它没应答 — 这一屏只读磁盘上的读数",
                               "Its app isn't installed here, or it didn't answer — this screen "
                               "only reads what is on disk",
                               "Seine App fehlt hier oder antwortete nicht — diese Ansicht liest "
                               "nur, was auf der Platte steht",
                               "L'app non è installata qui o non risponde — questa schermata legge "
                               "solo ciò che è su disco",
                               "このマシンにその App が無い、または応答しませんでした — この画面はディスク上の値だけ読みます",
                               "이 기기에 해당 앱이 없거나 응답하지 않았습니다 — 이 화면은 디스크 값만 읽습니다"}},
    // 权益卡上那句「差在哪一档」的文案（#132：原来是采集层写死的中文，五语界面上也跟着显示
    // 中文）。采集层只给下面这些键，翻译在这里做。Qoder 那条是 head＋缺口两段式，
    // 模板里的 %s 由 note_arg_key 填。
    {"cl_note_wb_nolog", {"本机没有 ~/.workbuddy/logs/main.log",
                          "No ~/.workbuddy/logs/main.log on this machine",
                          "~/.workbuddy/logs/main.log fehlt auf diesem Rechner",
                          "Nessun ~/.workbuddy/logs/main.log su questa macchina",
                          "このマシンに ~/.workbuddy/logs/main.log がありません",
                          "이 기기에 ~/.workbuddy/logs/main.log 가 없습니다"}},
    {"cl_note_wb_noline", {"main.log 里没有 [Checkin] 这一行（它只在窗口聚焦／页面可见时刷新，开了客户端不等于刷过）",
                           "main.log has no [Checkin] line (it only refreshes while its window is focused or visible — having the client open is not a check-in)",
                           "main.log enthält keine [Checkin]-Zeile (sie wird nur bei fokussiertem oder sichtbarem Fenster aktualisiert — offen heißt nicht eingecheckt)",
                           "main.log non ha righe [Checkin] (si aggiorna solo con la finestra a fuoco o visibile: averlo aperto non è un check-in)",
                           "main.log に [Checkin] の行がありません（ウインドウが前面か表示中のときだけ更新されます。起動している＝チェックイン済みではありません）",
                           "main.log 에 [Checkin] 줄이 없습니다(창이 활성 상태이거나 보일 때만 갱신됩니다. 앱을 켠 것과 체크인은 다릅니다)"}},
    {"cl_note_wb_badjson", {"[Checkin] 那行解析不了", "The [Checkin] line doesn't parse",
                            "Die [Checkin]-Zeile lässt sich nicht lesen",
                            "La riga [Checkin] non è leggibile",
                            "[Checkin] の行を解釈できません", "[Checkin] 줄을 해석할 수 없습니다"}},
    {"cl_note_wb_nofield", {"[Checkin] 那行没有 today_checked_in",
                            "The [Checkin] line has no today_checked_in",
                            "Der [Checkin]-Zeile fehlt today_checked_in",
                            "Nella riga [Checkin] manca today_checked_in",
                            "[Checkin] の行に today_checked_in がありません",
                            "[Checkin] 줄에 today_checked_in 이 없습니다"}},
    {"cl_note_wb_done", {"今天已领 · 下一期 00:00 重新出现（连看三晚都是 00:00 后翻回未领，但没观察到它作废）",
                         "Claimed today · the next round appears at 00:00 (three nights watched: it flips back just after 00:00, and it was never seen expiring)",
                         "Heute abgeholt · die nächste Runde erscheint um 00:00 (drei Nächte beobachtet: kurz nach 00:00 springt es zurück; ein Ablauf war nie zu sehen)",
                         "Riscosso oggi · il prossimo giro appare alle 00:00 (tre notti osservate: torna disponibile subito dopo le 00:00, senza scadenza visibile)",
                         "本日受取済み · 次の回は 00:00 に再表示（3 晩観測：00:00 直後に未受取へ戻る。失効は未観測）",
                         "오늘 수령 완료 · 다음 회차는 00:00 에 다시 나타납니다(3일 밤 관찰: 00:00 직후 미수령으로 돌아가며, 만료는 관측되지 않음)"}},
    {"cl_note_wb_ready", {"今天还没领", "Not claimed yet today", "Heute noch nicht abgeholt",
                          "Non ancora riscosso oggi", "本日はまだ未受取", "오늘 아직 수령하지 않음"}},
    {"cl_note_wb_inactive", {"签到活动当前不是 active", "The check-in campaign is not active right now",
                             "Die Check-in-Aktion ist derzeit nicht aktiv",
                             "La campagna di check-in non è attiva ora",
                             "チェックイン企画は現在 active ではありません",
                             "체크인 이벤트가 현재 active 상태가 아닙니다"}},
    {"cl_note_qd_nodir", {"本机没有 Qoder 的启动日志目录",
                          "No Qoder launch-log directory on this machine",
                          "Kein Qoder-Startlog-Verzeichnis auf diesem Rechner",
                          "Nessuna cartella dei log di avvio di Qoder su questa macchina",
                          "このマシンに Qoder の起動ログフォルダがありません",
                          "이 기기에 Qoder 시작 로그 폴더가 없습니다"}},
    {"cl_note_qd_noline", {"本机 Qoder 日志这一轮没有 [Campaign] 任何一行",
                           "No [Campaign] line in Qoder's logs this round",
                           "In Qoders Logs fand sich diesmal keine [Campaign]-Zeile",
                           "Nessuna riga [Campaign] nei log di Qoder in questo giro",
                           "今回の Qoder ログに [Campaign] の行がありません",
                           "이번 Qoder 로그에 [Campaign] 줄이 없습니다"}},
    {"cl_note_qd_ready", {"它说有得领", "It reports a claim is available",
                          "Es meldet eine verfügbare Prämie", "Segnala un premio disponibile",
                          "受取可能だと報告", "수령 가능하다고 보고"}},
    {"cl_note_qd_done", {"服务端清单说这一期的卡都已领完",
                         "Its server-side list says every card this period is claimed",
                         "Die Serverliste sagt: alle Karten dieser Runde sind abgeholt",
                         "L'elenco lato server dice che tutte le carte di questo periodo sono riscosse",
                         "サーバー側の一覧では今期のカードはすべて受取済み",
                         "서버 목록은 이번 기간 카드를 모두 수령했다고 말합니다"}},
    {"cl_note_qd_none_seen", {"它这一轮说没有可领的，不是本机没读到",
                              "It says there is nothing to claim this round — this machine did read its state",
                              "Es meldet diesmal nichts Verfügbares — der Rechner hat seinen Zustand gelesen",
                              "Dice che non c'è nulla da riscuotere in questo giro: la macchina ha letto il suo stato",
                              "今回は受取可能なしと報告（本機が読めていないわけではありません）",
                              "이번에는 수령 가능한 것이 없다고 보고합니다(이 기기가 못 읽은 것이 아님)"}},
    {"cl_note_qd_none_never", {"这一轮日志里它一直报「没有可领的」",
                               "Every reading in this round says there is nothing to claim",
                               "Jede Meldung dieser Runde sagt: nichts verfügbar",
                               "Ogni lettura di questo giro dice che non c'è nulla da riscuotere",
                               "今回のログでは一貫して「受取可能なし」",
                               "이번 로그는 계속 '수령 가능한 것 없음'을 보고합니다"}},
    {"cl_note_qd_gap", {"%s；卡面文案与面值它本机不写，拿不到",
                        "%s; card copy and face value aren't written locally, so neither can be shown",
                        "%s; Karten-Text und Nennwert schreibt es nicht lokal, daher unbekannt",
                        "%s; testo e valore della carta non sono scritti in locale, quindi non si vedono",
                        "%s。カードの文言と額面は本機に書かれず、取得できません",
                        "%s. 카드 문구와 액면가는 로컬에 기록되지 않아 알 수 없습니다"}},
    {"cl_note_qd_gap_open", {"%s；卡面文案与面值它本机不写，拿不到。你点开过它的活动面板，不再催",
                             "%s; card copy and face value aren't written locally, so neither can be shown. You opened its panel, so it won't be reminded again",
                             "%s; Karten-Text und Nennwert schreibt es nicht lokal, daher unbekannt. Du hast sein Panel geöffnet — keine erneute Meldung",
                             "%s; testo e valore della carta non sono scritti in locale, quindi non si vedono. Hai aperto il suo pannello: nessun altro avviso",
                             "%s。カードの文言と額面は本機に書かれず、取得できません。企画パネルを開いたため、再通知はしません",
                             "%s. 카드 문구와 액면가는 로컬에 기록되지 않아 알 수 없습니다. 활동 패널을 열었으므로 다시 알리지 않습니다"}},
    {"cl_note_zc_nolog", {"本机没有 ZCode 当天的 CLI 日志",
                          "No ZCode CLI log for today on this machine",
                          "Kein Tages-CLI-Log von ZCode auf diesem Rechner",
                          "Nessun log CLI di ZCode per oggi su questa macchina",
                          "このマシンに ZCode の当日 CLI ログがありません",
                          "이 기기에 ZCode 오늘 CLI 로그가 없습니다"}},
    {"cl_note_zc_none", {"本机日志零领取信号：状态只在它自己的服务端响应里，要它的登录态才拿得到",
                         "No claim signal locally: its server holds the state, behind its login",
                         "Kein Signal in lokalen Logs: der Zustand liegt nur in seiner Server-Antwort und braucht sein Login",
                         "Nessun segnale nei log locali: lo stato sta solo nella risposta del server e richiede il suo login",
                         "ローカルログに受取の手掛かりなし。状態はサーバー応答のみにあり、ログイン状態が必要です",
                         "로컬 로그에 수령 신호가 없습니다. 상태는 서버 응답에만 있어 해당 로그인이 필요합니다"}},
    // 券到账横幅的三段：标题的状态词、正文（有数字／没数字）、落点说明。
    // 与会话横幅同一做法：alert 层不给文案，六种语言都在这张表里现做。
    {"claim_word",     {"有券可领", "Reward ready", "Gutschein verfügbar", "Premio disponibile",
                        "受け取れる特典", "쿠폰 수령 가능"}},
    {"claim_credit",   {"%lld credits 待领", "%lld credits to claim", "%lld Credits abzuholen",
                        "%lld credit da riscuotere", "%lld credits を受取可", "%lld 크레딧 수령 대기"}},
    {"claim_open",     {"本期有可领的权益", "A reward is available", "Ein Gutschein ist bereit",
                        "Un premio è disponibile", "受け取れる特典があります", "받을 수 있는 혜택이 있습니다"}},
    // 落点这句必须用各家屏上真有的那个词（r_claim_tab：权益 / Rewards / Vorteile /
    // Vantaggi / 特典 / 혜택）——德语那版原来写 "Vorteile-Übersicht"，屏幕上没有这个词，
    // 而且 29 字符把整条正文挤到省略号（2026-10-30 实拍 banner_de.png 实测）。
    {"claim_goto",     {"点一下进权益屏", "Tap to open Rewards", "Antippen → Vorteile",
                        "Tocca → Vantaggi", "タップして特典画面へ", "누르면 혜택 화면"}},
    // 点完之后的回执，跟上面那句不是一件事：那句是横幅上的引导，这一句是「已经带你到了」。
    // 拿引导语当回执会在屏上留下一句读不通的话（2026-10-30 实拍 banner_after_click.png）。
    {"claim_opened",   {"已进权益屏", "Rewards opened", "Vorteile geöffnet",
                        "Vantaggi aperti", "特典画面を開きました", "혜택 화면을 열었습니다"}},
    // 自绘横幅那张卡的两个词：表头与右侧入口。文案不进 alert 层（那里只给身份）。
    {"banner_head",    {"权益到账", "Rewards ready", "Gutscheine bereit", "Premi disponibili",
                        "特典が届きました", "쿠폰 도착"}},
    {"banner_view",    {"查看", "View", "Ansehen", "Vedi", "見る", "보기"}},
    {"r_claim_hint",   {"只画各家写在自己磁盘上的读数；拿不到的字段整格不画，也不从别处推算。",
                        "Only fields a vendor writes to its own disk are drawn. Anything unavailable stays blank — never inferred.",
                        "Gezeichnet wird nur, was ein Anbieter selbst auf die Platte schreibt. Fehlendes bleibt leer — nie erschlossen.",
                        "Si mostra solo ciò che ogni fornitore scrive sul proprio disco. Ciò che manca resta vuoto, mai dedotto.",
                        "各ベンダーが自身のディスクに書いた値だけを描きます。取れない項目は空欄のままで、推定しません。",
                        "각 벤더가 자기 디스크에 쓴 값만 그립니다. 없는 항목은 비워 두며 추정하지 않습니다."}},
    {"r_today",        {"今日", "Today", "Heute", "Oggi", "本日", "오늘"}},
    {"r_today_usage",  {"今日用量", "Today’s usage", "Heutige Nutzung", "Uso di oggi",
                        "本日の使用量", "오늘 사용량"}},
    {"r_account_limits",{"账户额度", "Account limits", "Konto-Limits", "Limiti account",
                        "アカウント上限", "계정 한도"}},
    {"r_5h",           {"5 小时剩余", "5h remaining", "5 Std. übrig", "5h rimanenti",
                        "5時間残り", "5시간 남음"}},
    {"r_week",         {"周余额", "Weekly remaining", "Wochenrest", "Settimana rimanente",
                        "週間残り", "주간 남음"}},
    {"r_after_reset",  {"%s 后重置", "%s to reset", "Reset in %s", "Reset tra %s",
                        "リセットまで %s", "%s 후 재설정"}},
    {"r_last_reading", {"上次读数 · 尚未刷新", "Last reading · not refreshed",
                        "Letzter Stand · nicht aktualisiert", "Ultima lettura · non aggiornata",
                        "前回の読み値 · 未更新", "마지막 판독 · 새로고침 안 됨"}},
    {"r_stale_note",   {"额度更新失败；以上为上次成功读数，不代表当前可用量。",
                        "Refresh failed. Last known readings may not reflect available quota.",
                        "Aktualisierung fehlgeschlagen. Der letzte Stand kann von der aktuellen Quote abweichen.",
                        "Aggiornamento non riuscito. L’ultima lettura può non riflettere la quota attuale.",
                        "更新に失敗しました。前回の読み値は現在の残量とは限りません。",
                        "업데이트 실패. 마지막 판독값이 현재 잔량과 다를 수 있습니다."}},
    {"r_stale",        {"待更新", "Stale", "Veraltet", "Da aggiornare", "未更新", "이전 값"}},
    {"r_beyond",       {"已超过今日最长已完成会话", "Beyond today’s longest completed session",
                        "Länger als die längste heutige Sitzung", "Oltre la sessione più lunga di oggi",
                        "本日最長の完了セッションを超過", "오늘 가장 긴 완료 세션 초과"}},
    {"r_turn",         {"第 %d 轮", "Turn %d", "Runde %d", "Turn %d", "%d ターン", "%d번째 턴"}},
    {"r_longest",      {"今日最长 %d 轮", "Longest today %d", "Längste heute %d",
                        "Più lunga oggi %d", "本日最長 %d ターン", "오늘 최장 %d턴"}},
    {"r_session_total",{"会话累计", "Session total", "Sitzung gesamt", "Totale sessione",
                        "セッション合計", "세션 합계"}},
    {"r_turn_explain", {"轮次对照，不代表完成百分比；右端是今天已完成会话的最大轮次。",
                        "A turn comparison, not completion progress. The reference is today’s longest completed session.",
                        "Ein Rundenvergleich, kein Fortschritt. Vergleichswert ist die längste heutige abgeschlossene Sitzung.",
                        "Confronto di turni, non avanzamento. Il riferimento è la sessione completata più lunga di oggi.",
                        "ターン数の比較であり、進捗率ではありません。右端は本日完了セッションの最大ターン数です。",
                        "턴 수 비교일 뿐 완료율이 아닙니다. 오른쪽은 오늘 완료된 세션의 최대 턴 수입니다."}},
    {"r_needs_you",    {"待处理", "Needs you", "Wartet auf dich", "In attesa di te",
                        "対応待ち", "처리 대기"}},
    {"r_longest_first",{"等待最久优先", "Longest waiting first", "Längste Wartezeit zuerst",
                        "Attesa più lunga prima", "待ち時間の長い順", "오래 기다린 순"}},
    {"r_nothing",      {"这边没有待处理事项", "Nothing waiting here", "Hier wartet nichts",
                        "Niente in attesa", "ここには何もありません", "대기 중인 항목 없음"}},
    {"r_try_other",    {"切换工具，或查看今天的完成记录。", "Try another tool or review today’s completed sessions.",
                        "Wechsle das Tool oder sieh dir die heutigen Abschlüsse an.",
                        "Prova un altro strumento o rivedi le sessioni completate oggi.",
                        "別のツールに切り替えるか、本日の完了記録を見てください。",
                        "다른 도구로 전환하거나 오늘 완료 기록을 확인하세요."}},
    {"r_no_match",     {"没有匹配的待处理会话", "No matching requests", "Keine passenden Anfragen",
                        "Nessuna richiesta corrispondente", "一致する待ちはありません",
                        "일치하는 대기 항목 없음"}},
    {"r_search_none",  {"没有匹配的会话", "No matching sessions", "Keine passenden Sitzungen",
                        "Nessuna sessione corrispondente", "一致するセッションはありません",
                        "일치하는 세션 없음"}},
    {"r_search_none_hint",{"换个关键词，或查看全部会话。", "Try another keyword or show all sessions.",
                        "Versuch ein anderes Suchwort oder zeig alle Sitzungen.",
                        "Prova un’altra parola chiave o mostra tutte le sessioni.",
                        "別のキーワードを試すか、すべてのセッションを表示してください。",
                        "다른 검색어를 입력하거나 모든 세션을 표시하세요."}},
    {"r_no_sessions",  {"目前没有会话", "No sessions yet", "Noch keine Sitzungen",
                        "Nessuna sessione", "まだセッションがありません", "아직 세션이 없습니다"}},
    {"r_no_sessions_hint",{"开始使用编码工具后，会话会出现在这里。",
                        "Start a coding session and it will appear here.",
                        "Starte eine Coding-Sitzung, dann erscheint sie hier.",
                        "Avvia una sessione di coding e apparirà qui.",
                        "コーディングを始めると、ここにセッションが表示されます。",
                        "코딩 세션을 시작하면 여기에 표시됩니다."}},
    {"r_done_today",   {"今日完成", "Done today", "Heute erledigt", "Completate oggi",
                        "本日完了", "오늘 완료"}},
    // 队列里那一行不是「在等你回答」，而是「跑完了你还没看过」——状态字必须说清是后者，
    // 否则它和上面那几条真正卡住的请求读起来一模一样。
    {"r_unseen_done",  {"待查看", "Unseen", "Ungesehen", "Da vedere", "未確認", "확인 전"}},
    {"r_view_done",    {"查看今天的完成记录 %d", "Review today’s %d completed sessions",
                        "Heute %d abgeschlossene Sitzungen ansehen",
                        "Rivedi le %d sessioni completate oggi",
                        "本日の完了記録 %d 件を見る", "오늘 완료 기록 %d개 보기"}},
    {"r_show_all",     {"查看全部会话", "Show all sessions", "Alle Sitzungen zeigen",
                        "Mostra tutte le sessioni", "すべてのセッションを表示", "모든 세션 보기"}},
    {"r_search",       {"搜索会话", "Search sessions", "Sitzungen suchen", "Cerca sessioni",
                        "セッションを検索", "세션 검색"}},
    {"r_search_ph",    {"搜索项目、工具或问题", "Search project, tool or request",
                        "Projekt, Tool oder Anfrage suchen", "Cerca progetto, strumento o richiesta",
                        "プロジェクト・ツール・リクエストを検索", "프로젝트, 도구 또는 요청 검색"}},
    {"r_clear",        {"清除搜索", "Clear search", "Suche löschen", "Cancella ricerca",
                        "検索をクリア", "검색 지우기"}},
    {"r_filter_tool",  {"按工具筛选", "Filter by tool", "Nach Tool filtern", "Filtra per strumento",
                        "ツールで絞り込む", "도구로 필터"}},
    {"r_all_sessions", {"全部会话", "All sessions", "Alle Sitzungen", "Tutte le sessioni",
                        "すべてのセッション", "모든 세션"}},
    {"r_scroll_more",  {"滚动到更多工具", "Scroll to more tools", "Zu weiteren Tools scrollen",
                        "Scorri ad altri strumenti", "他のツールへスクロール", "더 많은 도구로 스크롤"}},
    {"r_all_tools_list",{"全部工具列表", "All tools list", "Alle Tools", "Elenco strumenti",
                        "ツール一覧", "도구 목록"}},
    {"r_all_tools",    {"全部工具", "All tools", "Alle Tools", "Tutti gli strumenti",
                        "すべてのツール", "모든 도구"}},
    {"r_tools_n",      {"%d 个工具", "%d tools", "%d Tools", "%d strumenti",
                        "%d 個のツール", "%d개 도구"}},
    {"r_search_tools", {"搜索工具名称", "Search tools", "Tools suchen", "Cerca strumenti",
                        "ツールを検索", "도구 검색"}},
    {"r_tools_word",   {"工具", "tools", "Tools", "strumenti", "ツール", "도구"}},
    {"r_sessions_n",   {"%d 条会话", "%d sessions", "%d Sitzungen", "%d sessioni",
                        "%d セッション", "%d개 세션"}},
    {"r_waiting_n",    {"%d 条待处理", "%d waiting", "%d wartend", "%d in attesa",
                        "%d 件待ち", "%d개 대기"}},
    {"r_no_tools",     {"没有找到工具", "No matching tools", "Keine passenden Tools",
                        "Nessuno strumento corrispondente", "一致するツールがありません",
                        "일치하는 도구 없음"}},
    {"r_try_tool",     {"试试工具名或 CLI、IDE。", "Try a tool name, CLI or IDE.",
                        "Versuche einen Toolnamen, CLI oder IDE.", "Prova un nome, CLI o IDE.",
                        "ツール名や CLI・IDE で試してください。", "도구 이름이나 CLI, IDE로 검색해 보세요."}},
    {"r_total_today",  {"今日总用量", "Total tokens today", "Token gesamt heute",
                        "Token totali oggi", "本日の総使用量", "오늘 총 사용량"}},
    {"r_tools_log",    {"%d 个工具 · 本地记录统计", "%d tools · local log statistics",
                        "%d Tools · lokale Log-Statistik", "%d strumenti · statistiche log locali",
                        "%d ツール · ローカルログ集計", "%d개 도구 · 로컬 로그 집계"}},
    {"r_usage_by",     {"按工具查看", "Usage by tool", "Nach Tool", "Per strumento",
                        "ツール別", "도구별"}},
    {"r_data_note",    {"余额属于工具账户，多条会话共用；不同工具的余额不相加。未取得的额度不显示为 0。",
                        "Limits are shared across an account’s sessions, not added across tools. Missing quota is never shown as zero.",
                        "Limits gelten pro Konto und werden nicht über Tools addiert. Fehlende Quoten werden nie als 0 angezeigt.",
                        "I limiti sono per account e non si sommano tra strumenti. Una quota mancante non viene mostrata come 0.",
                        "上限はアカウント単位で、ツール間で合算しません。取得できない上限は 0 と表示しません。",
                        "한도는 계정 단위이며 도구 간에 합산하지 않습니다. 없는 할당량은 0으로 표시하지 않습니다."}},
    {"r_no_usage",     {"这份快照没有用量明细", "No usage breakdown in this snapshot",
                        "Keine Nutzungsdetails in diesem Stand", "Nessun dettaglio d’uso in questo snapshot",
                        "このスナップショットに使用明細はありません", "이 스냅샷에는 사용 내역이 없습니다"}},
    {"r_no_usage_hint",{"只展示来源记录已有的字段，不从 tokens 推算订阅余额。",
                        "Only source fields are shown. Token counts do not imply subscription quota.",
                        "Nur vorhandene Quellenfelder werden gezeigt. Token-Zahlen sind keine Abo-Quote.",
                        "Solo i campi della fonte. I token non indicano la quota dell’abbonamento.",
                        "記録にある項目だけを表示します。tokens から購読残量は推定しません。",
                        "기록에 있는 항목만 표시합니다. 토큰 수로 구독 잔량을 추정하지 않습니다."}},
    {"r_personal",     {"个人账户", "Personal account", "Privates Konto", "Account personale",
                        "個人アカウント", "개인 계정"}},
    {"r_local_usage",  {"本地会话用量", "Local session usage", "Lokale Sitzungsnutzung",
                        "Uso sessioni locali", "ローカルセッション使用量", "로컬 세션 사용량"}},
    {"r_total_usage",  {"总用量", "Total usage", "Gesamtnutzung", "Uso totale",
                        "総使用量", "총 사용량"}},
    {"r_consumed",     {"消耗 %s Credits", "Consumed %s Credits", "Verbraucht %s Credits",
                        "Consumati %s Credits", "消費 %s Credits", "소비 %s Credits"}},
    {"r_tool_sessions",{"查看此工具的会话", "View this tool’s sessions", "Sitzungen dieses Tools",
                        "Sessioni di questo strumento", "このツールのセッション", "이 도구의 세션"}},
    {"r_activity",     {"今日用量分布", "Today’s activity", "Heutige Aktivität",
                        "Attività di oggi", "本日の推移", "오늘 활동"}},
    {"r_hourly",       {"按小时", "Hourly", "Stündlich", "Ogni ora", "時間別", "시간별"}},
    {"r_input",        {"输入", "Input", "Eingabe", "Input", "入力", "입력"}},
    {"r_output",       {"输出", "Output", "Ausgabe", "Output", "出力", "출력"}},
    {"r_cache_read",   {"缓存读取", "Cache read", "Cache-Lesen", "Lettura cache",
                        "キャッシュ読取", "캐시 읽기"}},
    {"r_cache_write",  {"缓存写入", "Cache write", "Cache-Schreiben", "Scrittura cache",
                        "キャッシュ書込", "캐시 쓰기"}},
    {"r_by_model",     {"按模型", "By model", "Nach Modell", "Per modello", "モデル別", "모델별"}},
    {"r_models_n",     {"%d 个", "%d", "%d", "%d", "%d 件", "%d개"}},
    {"r_model_note",   {"总用量 = 输入 + 输出 + 缓存读取 + 缓存写入；不同工具接入时须先统一口径。",
                        "Total = input + output + cache read + cache write. Tool-specific counters must be normalized.",
                        "Gesamt = Eingabe + Ausgabe + Cache-Lesen + Cache-Schreiben. Zähler müssen normalisiert sein.",
                        "Totale = input + output + lettura cache + scrittura cache. I contatori vanno normalizzati.",
                        "総使用量 = 入力 + 出力 + キャッシュ読取 + キャッシュ書込。ツール間で口径を揃える必要があります。",
                        "총 사용량 = 입력 + 출력 + 캐시 읽기 + 캐시 쓰기. 도구별 기준을 통일해야 합니다."}},
    {"r_no_account",   {"没有账户用量数据", "No account usage data", "Keine Kontonutzungsdaten",
                        "Nessun dato d’uso dell’account", "アカウント使用量データなし",
                        "계정 사용량 데이터 없음"}},
    {"r_no_account_hint",{"当前快照未包含这些字段。", "These fields are not included in the current snapshot.",
                        "Diese Felder sind im aktuellen Stand nicht enthalten.",
                        "Questi campi non sono nello snapshot attuale.",
                        "現在のスナップショットにこれらの項目はありません。",
                        "현재 스냅샷에 이 항목이 없습니다."}},
    {"r_original",     {"原始请求", "Original request", "Ursprüngliche Anfrage",
                        "Richiesta originale", "元のリクエスト", "원래 요청"}},
    {"r_record",       {"会话记录", "Session record", "Sitzungsprotokoll", "Registro sessione",
                        "セッション記録", "세션 기록"}},
    {"r_options",      {"待回答选项", "Response options", "Antwortoptionen",
                        "Opzioni di risposta", "回答候補", "응답 옵션"}},
    {"r_boundary",     {"请在原工具中处理请求；这里不执行授权或提交答案。",
                        "Handle the request in its original tool. This panel does not grant permissions or submit answers.",
                        "Bearbeite die Anfrage im Original-Tool. Dieses Panel erteilt keine Rechte und sendet keine Antworten.",
                        "Gestisci la richiesta nello strumento originale. Questo pannello non concede permessi né invia risposte.",
                        "元のツールで処理してください。ここでは許可や回答の送信は行いません。",
                        "원래 도구에서 처리하세요. 이 패널은 권한을 부여하거나 답변을 제출하지 않습니다."}},
    {"r_truncated",    {"来源记录本身已截断，无法在此恢复完整内容。",
                        "The source record is truncated; the full text is not available.",
                        "Der Quelleneintrag ist gekürzt; der vollständige Text ist nicht verfügbar.",
                        "Il record di origine è troncato; il testo completo non è disponibile.",
                        "記録自体が切り詰められており、全文は復元できません。",
                        "원본 기록이 잘려 있어 전체 내용을 복원할 수 없습니다."}},
    {"r_copy_request", {"复制请求原文", "Copy original request", "Anfrage kopieren",
                        "Copia richiesta", "リクエストをコピー", "요청 복사"}},
    {"r_open_session", {"前往会话", "Open session", "Sitzung öffnen", "Apri sessione",
                        "セッションを開く", "세션 열기"}},
    {"r_open_zcode",   {"打开 ZCode", "Open ZCode", "ZCode öffnen", "Apri ZCode",
                        "ZCode を開く", "ZCode 열기"}},
    {"r_completed_at", {"这条会话于 %s 完成，共 %d 轮。", "Completed at %s, after %d turns.",
                        "Abgeschlossen um %s nach %d Runden.", "Completata alle %s dopo %d turni.",
                        "%s に完了、全 %d ターン。", "%s에 완료, 총 %d턴."}},
    {"r_running_none", {"会话正在运行；没有待处理的请求。",
                        "The session is running, with no pending request.",
                        "Die Sitzung läuft; keine offene Anfrage.", "La sessione è in esecuzione; nessuna richiesta in sospeso.",
                        "セッションは実行中で、待機中のリクエストはありません。",
                        "세션이 실행 중이며 대기 중인 요청이 없습니다."}},
    {"r_completed_today",{"今日完成", "Completed today", "Heute abgeschlossen", "Completate oggi",
                        "本日完了", "오늘 완료"}},
    {"r_no_completed", {"没有完成记录", "No completed sessions", "Keine abgeschlossenen Sitzungen",
                        "Nessuna sessione completata", "完了記録なし", "완료 기록 없음"}},
    {"r_no_completed_hint",{"完成的会话会显示在这里。", "Completed sessions will appear here.",
                        "Abgeschlossene Sitzungen erscheinen hier.", "Le sessioni completate appariranno qui.",
                        "完了したセッションがここに表示されます。", "완료된 세션이 여기에 표시됩니다."}},
    {"r_running",      {"运行中", "Running", "Läuft", "In esecuzione", "実行中", "실행 중"}},
    {"r_details",      {"明细", "Details", "Details", "Dettagli", "詳細", "상세"}},
    {"r_back_sessions",{"会话总览", "Sessions", "Sitzungen", "Sessioni", "セッション概要", "세션 개요"}},
    {"r_back_usage",   {"用量与额度", "Usage & limits", "Nutzung & Limits", "Uso e limiti",
                        "使用量と上限", "사용량 및 한도"}},
    {"r_back_account", {"工具用量", "Tool usage", "Tool-Nutzung", "Uso strumenti",
                        "ツール使用量", "도구 사용량"}},
    {"r_back_detail",  {"会话详情", "Session details", "Sitzungsdetails", "Dettagli sessione",
                        "セッション詳細", "세션 상세"}},
    {"r_mode_live",    {"实时", "Live", "Live", "Live", "ライブ", "실시간"}},
    {"r_updated",      {"%s 更新", "updated %s", "%s aktualisiert", "aggiornato %s",
                        "%s 更新", "%s 업데이트"}},
    {"r_next_expiry",  {"最近 %s 后到期", "Next expiry in %s", "Nächster Ablauf in %s",
                        "Prossima scadenza tra %s", "次の期限まで %s", "%s 후 만료"}},
    {"r_last_refresh", {"上次读数 %s", "Last reading %s", "Letzter Stand %s",
                        "Ultima lettura %s", "前回の読み値 %s", "마지막 판독 %s"}},
    {"r_local_data",   {"本机数据", "Local data", "Lokale Daten", "Dati locali",
                        "ローカルデータ", "로컬 데이터"}},
    {"r_sample_data",  {"样例数据", "Sample data", "Beispieldaten", "Dati di esempio",
                        "サンプルデータ", "샘플 데이터"}},
    {"r_fail_short",   {"失败", "Failed", "Fehler", "Errore", "失敗", "실패"}},
    {"r_ask_short",    {"回答", "Question", "Frage", "Domanda", "質問", "질문"}},
    {"r_perm_short",   {"授权", "Permission", "Freigabe", "Permesso", "許可", "권한"}},
    {"r_queue_short",  {"排队", "Queued", "Wartend", "In coda", "待機", "대기"}},
    {"r_back_history", {"完成记录", "History", "Verlauf", "Cronologia",
                        "完了記録", "완료 기록"}},
    {"r_local_log",    {"本机记录", "Local logs", "Lokale Logs", "Log locali",
                        "ローカル記録", "로컬 기록"}},
    {"r_not_billing",  {"不代表订阅实付", "Not subscription charges",
                        "Keine Abo-Abrechnung", "Non è l’addebito dell’abbonamento",
                        "サブスクの実費ではありません", "구독 실결제가 아닙니다"}},
    {"r_no_trunc",     {"来源记录未标截断", "Source record not marked truncated",
                        "Quelle nicht als gekürzt markiert", "Fonte non indicata come troncata",
                        "記録に切り詰めの印なし", "원본에 잘림 표시 없음"}},
    {"r_children",     {"%d 条子对话", "%d sub-agents", "%d Unteragenten", "%d sotto-agenti",
                        "%d 件のサブ会話", "%d개 하위 대화"}},
    {"set_quit",       {"退出", "Quit", "Beenden", "Esci", "終了", "종료"}},
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

/// 同一段时间，只念时长，句子里的「等」「最久」「…前」由调用方自己写。
/// 与 Ago 分开是有原因的：Ago 的六语都自带「前 / ago」，接在「等」后面会念成「等 5 分钟前」。
/// 单位用 r20 的紧凑写法（49m / 15h / 1h 07m）：`.wt`/`.rt`/`.l3` 都是等宽 9.5–10.5，
/// 「4 小时 12 分钟」那种长句会把整行挤爆；六语共用同一套 ASCII 单位，不必各翻一遍。
static std::string Dur(double age_sec, int loc) {
    (void)loc;
    char buf[32];
    if (age_sec < 0) age_sec = 0;
    if (age_sec < 60) { snprintf(buf, sizeof buf, "%ds", (int)age_sec); return buf; }
    if (age_sec < 3600) { snprintf(buf, sizeof buf, "%dm", (int)(age_sec / 60)); return buf; }
    if (age_sec < 86400) {
        const int h = (int)(age_sec / 3600), m = (int)((age_sec - h * 3600) / 60);
        if (m > 0) snprintf(buf, sizeof buf, "%dh %02dm", h, m);
        else snprintf(buf, sizeof buf, "%dh", h);
        return buf;
    }
    snprintf(buf, sizeof buf, "%dd", (int)(age_sec / 86400));
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
            T_MLBL, T_SEG, T_LABEL, T_TOOL, T_GLYPH9,
            // 详情屏的提问原文（r20 `.qbig` 15px）：正文档最大的一号，单独一档，
            // 这样它跟着 340pt 的尺度走，而不是借用 26pt 的数字档。
            T_HERO,
            // r20 新增四档：13.5＝总览里的一条提问（.qt）；17＝历史导轨那个大数（.rh .n）；
            // 10＝节头文字与关于卡（.slabel .tx / .about）；8.5＝历史刻度的时标（.track .lb）。
            T_QT, T_NUM17, T_10S, T_TICK,
            // T_H3＝空态那句标题（r20 `.empty h3` 14px/1.4，font-weight:600）。
            // 与 T_HERO(15) 差 1pt，两处都是稿子里写死的声明，不能共用一档。
            T_H3,
            // r20 的正文里 10.5/9.5 各有两个 family：10.5 既有 mono（已由 T_FAIL 承担）
            // 也有 sans（.live .st / .opt .ds / .hint），9.5 既有 mono（T_PILL）也有
            // sans（.codebox .cp）。同尺寸不同 family 必须拆档，否则 opt 说明会被画成等宽。
            T_S105, T_S95,
            // 同尺寸再按字重拆：`.dhead .tt`/`.src .a` 是 600 12，而 `.prow .pj`/`.gobtn`
            // 是 500 12；`.opt .lb` 是 500 12.5，`.head .word`/`.live .proj` 是 600 12.5。
            // 10 这一档同样是三种：节头 600 sans、导轨单位 400 sans、`.opt .k` 500 mono。
            // T_S115＝接线屏 hero 那句 11.5/1.6 sans 400（同尺寸的 T_LABEL 是 500 行文案）。
            T_S12B, T_S125, T_M10B, T_S10, T_S115,
            // .codebox .cb-h .f 是 mono 500 9.5（与同尺寸的 T_TIME 400 不同档）。
            T_FHEAD,
            // r31 的八处新声明：8.5/9/10/11 已有的档不够用，因为 R31 在同一尺寸上用了
            // 更多字重（500/600/650）。逐条对着 app.css 的真实 font-size/weight 抄：
            //   T_S9   9/400（行用量、脚注）
            //   T_S11B 11/600（节头、详情标签）
            //   T_S12  12/400（标签页、正文）
            //   T_S13B 13/600（行标题、工具名）
            //   T_S14  14/400（提问原文全文）
            //   T_S14B 14/650（产品名、详情提问）
            //   T_S17  17/500（详情身份名、会话累计大数）
            //   T_S20  20/600（账户页标题）
            //   T_S22  22/600（详情页项目标题）
            //   T_N23  23/500（账户 peek 的大数）
            //   T_N28  28/500（额度数字）
            //   T_N35  35/550（今日总用量）
            //   T_N39  39/500（账户总用量）
            T_S9, T_S11B, T_S12, T_S13B, T_S14, T_S14B, T_S17, T_S20, T_S22,
            T_N23, T_N28, T_N35, T_N39,
            // r31 第二轮补齐四处同尺寸异字重：T_S10B 10/650（run-labels、tabs 计数）、
            // T_S11 11/500（状态行）、T_S15B 15/620（run-project）、T_S23B 23/600（历史页标题）。
            // r31 第三轮（2026-10-28 geom_1440.json 实测缺口，check_tiers.py 判 MISSING 的五条）：
            //   T_S8   8/400（.daytimes 起止时刻、账户小时图刻度）
            //   T_S9B  9/600（来源导轨「全部」那颗的文字与图标标签）
            //   T_S16B 16/600（空态标题 .empty h2；app.css 覆盖 base 的 17px，脚本看不到）
            //   T_S17B 17/600（详情屏工具名 .detail-identity .agent-name，与 T_S17=17/500 不同档）
            //   T_N20  20/500（账户页拆解大数 .breakdown strong，与 T_S20=20/600 字重不同）
            T_S10B, T_S11, T_S15B, T_S23B, T_S8, T_S9B, T_S16B, T_S17B, T_N20,
            // T_N40 40/600：权益卡的面值。2026-10-30 用户「权益数字要醒目一点，不然都看不出来」，
            // 原 T_N28 与卡上品牌名只差 2.3 倍、又同色同墨，一整块读起来就是一行正文。
            T_N40, TIER_N };
static const float kTierPx[TIER_N]   = {26.0f, 12.5f, 11.0f, 11.0f, 10.5f, 10.5f, 10.0f, 6.0f,
                                        10.0f, 9.5f, 9.5f, 9.0f,
                                        10.0f, 11.0f, 11.5f, 12.0f, 9.0f, 15.0f,
                                        13.5f, 17.0f, 10.0f, 8.5f, 14.0f, 10.5f, 9.5f,
                                        12.0f, 12.5f, 10.0f, 10.0f, 11.5f, 9.5f,
                                        9.0f, 11.0f, 12.0f, 13.0f, 14.0f, 14.0f, 17.0f,
                                        20.0f, 22.0f, 23.0f, 28.0f, 35.0f, 39.0f,
                                        10.0f, 11.0f, 15.0f, 23.0f,
                                        8.0f, 9.0f, 16.0f, 17.0f, 20.0f, 40.0f};
static const bool  kTierMono[TIER_N] = {true,  false, false, true,  true,  false, false, false,
                                        true,  false, true,  true,
                                        true,  true,  false, false, false, false,
                                        false, true,  false, true,  false, false, false,
                                        false, false, true,  false, false, true,
                                        false, false, false, false, false, false, false,
                                        false, false, false, false, false, false,
                                        false, false, false, false,
                                        false, false, false, false, false, false};
// 稿子里的粗体只有两处落在 mono 上（num-xl 600、「N 轮」semibold），本机有
// JetBrainsMono-SemiBold 可以直接对应。sans 侧的 500/600/650 没有第二份文件，
// 靠 g_fontB 那张「同一文件 + 轮廓加粗」的烘焙出来（见下面的 FaceFont）。
static const bool  kTierBold[TIER_N] = {true,  false, false, true,  false, false, false, false,
                                        false, false, false, false,
                                        false, false, false, false, false, false,
                                        false, true,  false, false, false, false, false,
                                        false, false, false, false, false, false,
                                        false, false, false, false, false, false, false,
                                        false, false, false, false, false, false,
                                        false, false, false, false,
                                        false, false, false, false, false, false};
// 0＝400 / 1＝500 / 2＝600。字体包里的 sans 只有 Regular 一档，>0 的档在烘焙时多烘一张
// 轮廓加粗的脸（g_fontB，见 FaceFont）；mono 档有真 SemiBold，不再烘这一张。
static const unsigned char kTierW[TIER_N] = {0, 2, 0, 0, 0, 1, 1, 1,
                                             0, 1, 0, 0,
                                             1, 0, 1, 1, 1, 2,
                                             1, 0, 2, 0, 2, 0, 1,
                                             2, 1, 1, 0, 0, 1,
                                             0, 2, 0, 2, 0, 2, 1, 2, 2, 1, 1, 1, 1,
                                             2, 1, 2, 2,
                                             0, 2, 2, 2, 1, 2};
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
// 粗体另烘一份：stb 没法给轮廓加粗，只能画两遍错开一格（会留下双线/灰边）。
// 这一份走 FreeType 加载器 + FT_GlyphSlot_Embolden —— 在栅格化之前把轮廓向外撑
// em/24 像素，边缘只 AA 一次。只有非等宽的真粗档才有；等宽档用自带的 SemiBold 文件。
static ImFont* g_fontB[TIER_N][FACE_N] = {};
static ImFont* FaceFont(int tier, int face) {
    ImFont* b = g_fontB[tier][face];
    return b ? b : g_font[tier][face];
}
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
        w += FaceFont(tier, r.face)->CalcTextSizeA(px, 1e9f, 0.0f, r.text.c_str()).x + track;
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
        ImFont* f = FaceFont(tier, r.face);
        float px = Bake(tier, r.face);
        ImVec2 sz = f->CalcTextSizeA(px, 1e9f, 0.0f, r.text.c_str());
        // 行框顶对齐到整点：字形的横向零头随 advance 走，起笔落在整点上才不会被图集重采样一次。
        const float bx = (float)(int)cx, by = (float)(int)(y + ref_asc - AscentPx(tier, r.face));
        dl->AddText(f, px, ImVec2(bx, by), col, r.text.c_str());
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

// ── 配色：逐条抄 r20/interaction.src.html 的 :root token（面板 340×640 的那一版）。
//    旧的 r14 那套 oklch 值全部退休：琥珀/青/红三支换了色相，底与墨也整体提了一档。
//    改颜色请先改稿子，这里只跟。────────────────────────────────────────────────
static const ImU32 kBg       = IM_COL32(13, 16, 21, 255);    // --bg #0D1015
static const ImU32 kSurf     = IM_COL32(18, 22, 28, 255);    // --surf #12161C（live/src/rail/aglist 的底）
static const ImU32 kSurf2    = IM_COL32(22, 27, 34, 255);    // --surf2 #161B22（opt 悬停 / gobtn / sel）
static const ImU32 kRecess   = IM_COL32(10, 13, 17, 255);    // --recess #0A0D11（term / codebox）
static const ImU32 kLine     = IM_COL32(255, 255, 255, 19);  // --line  white 7.5%
static const ImU32 kLine2    = IM_COL32(255, 255, 255, 31);  // --line2 white 12%
static const ImU32 kT1       = IM_COL32(232, 236, 241, 255); // --t1 #E8ECF1
static const ImU32 kT2       = IM_COL32(183, 191, 200, 255); // --t2 #B7BFC8
static const ImU32 kT3       = IM_COL32(138, 147, 157, 255); // --t3 #8A939D
static const ImU32 kT4       = IM_COL32(97, 106, 116, 255);  // --t4 #616A74
static const ImU32 kT5       = IM_COL32(69, 77, 86, 255);    // --t5 #454D56
static const ImU32 kAmber    = IM_COL32(232, 163, 61, 255);  // --amber #E8A33D
static const ImU32 kCyan     = IM_COL32(95, 198, 218, 255);  // --cyan #5FC6DA
static const ImU32 kGreen    = IM_COL32(87, 179, 104, 255);  // --green #57B368
static const ImU32 kRed      = IM_COL32(217, 105, 90, 255);  // --red #D9695A
static const ImU32 kInk      = IM_COL32(10, 13, 17, 255);    // 反色字落在浅底上
// 旧名字保留为别名：几十处调用还在用它们，语义与 r20 那支一一对应。
static const ImU32 kFg        = kT1;
static const ImU32 kMutedFg   = kT3;
static const ImU32 kStAwait   = kAmber;
static const ImU32 kStRun     = kCyan;
static const ImU32 kStQueue   = kT3;
static const ImU32 kStFail    = kRed;
static const ImU32 kStDone    = kGreen;
static const ImU32 kHair      = kLine;
static const ImU32 kSurface1  = kSurf;
static const ImU32 kSurface2  = kSurf2;
static const ImU32 kPrimary   = kCyan;
static const ImU32 kPrimaryFg = kInk;
static const ImU32 kStIdle    = kT4;
static const ImU32 kBorder    = kLine2;
static const ImU32 kInput     = kLine2;
// 白墨分档（draft 用 white/xx 表达层级）
static ImU32 W(int a255) { return IM_COL32(255, 255, 255, a255); }
static const ImU32 kTitle      = kT1;
static const ImU32 kMicro      = kT4;
static const ImU32 kProject    = kT3;
static const ImU32 kTime       = kT4;
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
    const std::string out = TakeCP(s, best) + ell;
    // 覆盖语言轮次里最怕的事不是版面丑，而是「德文那一行被悄悄截成半句」。
    // AGENTPING_OVERFLOW=1 时把每次真截断的原文与可用宽度打到 stderr，配着图一起读。
    static const bool log_clip = getenv("AGENTPING_OVERFLOW") != nullptr;
    if (log_clip) fprintf(stderr, "[裁切] %.0fpt｜%s\n", (double)maxw, s.c_str());
    return out;
}

/// 路径太长时保住文件名：显示成 ~/…/hook.sh，而不是 ~/Library/Applica…。
/// 六种语言的「写入器」标签宽度不同，全路径在德/日/韩会被整段裁掉尾巴，
/// 而尾巴恰好是唯一能认出这是哪份脚本的部分。
static std::string MiddleClipPath(const std::string& s, float maxw, int locale, int tier) {
    if (TextWidth(s, locale, tier) <= maxw) return s;
    if (s.find('/') == std::string::npos) return ClipToWidth(s, maxw, locale, tier);
    std::vector<std::string> comps;
    std::size_t p0 = 0;
    for (std::size_t i = 0; i <= s.size(); i++)
        if (i == s.size() || s[i] == '/') {
            if (i > p0) comps.push_back(s.substr(p0, i - p0));
            p0 = i + 1;
        }
    if (comps.empty()) return ClipToWidth(s, maxw, locale, tier);
    const std::string head = s[0] == '/' ? "/" : "~/";
    const char* ell = g_cover[LATIN].count(0x2026) ? "\xE2\x80\xA6" : "...";
    // 从文件名往回整段地加，塞不下就停：~/Application Support/… → ~/…/AgentPing/hooks/hook.sh。
    // 只留文件名（旧写法）会把唯一有信息量的上级目录也扔掉。
    std::string fit = comps.back();
    bool took = false;
    for (int i = (int)comps.size() - 2; i >= 0; i--) {
        const std::string cand = comps[(std::size_t)i] + "/" + fit;
        if (TextWidth(head + ell + "/" + cand, locale, tier) <= maxw) { fit = cand; took = true; }
        else break;
    }
    if (!took) return ClipToWidth(comps.back(), maxw, locale, tier);
    return head + ell + "/" + fit;
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
static const int kPanelW = 416;                 // r31 #win{width:416px}
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
static const float kCanvasH = 850.0f;   // r31 .panel{height:850px}

// ── r20 的公共骨架（interaction.src.html 里 .head/.tabs/.dhead 三块）──────────
// 面板固定 640 高：内容不足 640 时底部标签栏挂在 640 的下沿，与稿子逐像素同构；
// 内容超过 640（接线屏那种长页）就让面板长高，标签栏跟着下移——浮层没有滚动条，
// 硬裁会把摆不下的行变成看不见也点不到，比「比稿子高一点」更坏。
static const float kHeadH = 42.0f;            // .head{height:42px}
static const float kTabsH = 44.0f;            // .tabs{height:44px}
static const float kDHeadH = 40.0f;           // .dhead{height:40px}
static const float kBodyPadTop = 4.0f;        // .inner{padding:4px 0 14px}
static const float kBodyPadBot = 14.0f;
static const float kMarginX = 14.0f;          // 稿子的左右 14px
static const float kSlTop = 12.0f, kSlBottom = 7.0f;   // .slabel padding:12px 14px 7px
static const float kSlLine = 10.0f;           // .tx font-size:10px / line-height 1
static const float kLogoD = 18.0f;

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
    std::vector<std::string> option_desc;   // 与 options 并行；缺项＝这条选项没有说明
    int turns = 0, queue_pos = 0;
    double age;
    bool inferred;
    bool is_new = false;                 // 稿子 collect.py:413：age_s < 900 → 标题前挂辉光点
    std::vector<int> spark;              // 24 格 × 15 分钟的活跃度，稿子 run 行的 Spark 输入
    ImU32 hue;
    /// 一行的身份：agent id + 该 agent 里的会话 id。已读/稍后/落点都按这个键记，
    /// 单独用会话 id 会跨采集器撞名（两家都用 uuid，但不同家同名 uuid 不是没可能）。
    std::string key, agent_id, native_id;
    /// 这一行的会话形态签原文（IDE／CLI）。空＝采集层没给出正证，那一格整格不画。
    /// 值只由 ClientTagOf 给；不再按 agent 名字硬编码，否则 App 里开的会话会被写成 CLI。
    std::string client;
    /// 今天这一段的跨度（秒）：`.live` 的「连续 4h 12m」。起点不在今天就是 0，界面不画。
    double span_sec = 0;
    /// `.live` 的 `$ cmd`：尾部最后一条 tool_use 的真命令；空＝整行不画。
    std::string last_cmd;
    /// 授权详情「触发说明」：hook 原文；空＝退回「工具」那一行。
    std::string trigger;
    /// R31 每行那一小截用量：只摆来源真给了的字段。Qoder 的 JSONL 没有 token
    /// 明细（全 0），所以 has_tokens 为假时整行不画——不拿 0 冒充读数。
    long tokens = 0;
    double credits = 0;
    bool has_tokens = false, has_credits = false;
    std::string model;
    std::string cwd;                       // 会话的工作区绝对路径：ZCode 落点要开到工作区窗口
    /// 这一条建出来的时刻（采集层给的 time_created，0＝这一路没给）。
    /// ZCode 落点用它分清「侧栏藏在显示更多后面」和「它这次运行压根没列」两种没点中。
    double created = 0;
    std::vector<ap::Session::Child> children;   // 子代理对话：主任务下可展开的那层
    /// 完成了但你还没看过（完成时刻 > 已读水位线）。只有完成态才有点；
    /// 双击跳过去或 ⓘ 看过详情就推进水位线，点消失。
    bool unseen = false;

    double at = 0;                       // 这条会话最后有动静的时刻（日轨要按它落点）
    /// 「已读」＝本进程内不再催；「稍后」＝20 分钟后自动恢复（settings.html:「20 分钟后恢复」）。
    bool acked = false, snoozed = false;
    bool muted() const { return acked || snoozed; }
};

/// 「停下来问你」的两种形态：带候选选项/AskUserQuestion 的是提问，其余是授权确认。
/// 详情路由、自动选行、状态胶囊都走这一个判据，三处不许各判一套。
static bool RowAsksQuestion(const UIRow& r) {
    return !r.options.empty() || r.tool == "AskUserQuestion";
}
struct Chip { std::string name, initial; int n; ImU32 hue; };
/// 设置屏「监听工具」一行的数据源：本轮 scan 真返回的采集器，一个不多一个不少。
/// 三个布尔互斥（本机没有／读不通／今天没动静），全 false 才是「正常在听」。
struct Tool {
    std::string id, name, initial;
    bool available = false, unreadable = false, quiet = false;
    int rows = 0;
    ImU32 hue;
    // R31：来源自己写下的用量。没有 token 明细的采集器（Qoder）has_tokens 为假，
    // 面板就不摆 token 那一列；余额也不跨工具相加（每格是它自己账户的口径）。
    ap::UsageParts usage;
    double credits = 0;
    bool has_credits = false;
    std::vector<ap::ModelUse> models;
    std::array<long, 24> hourly{};
    ap::Quota quota;
    /// 这一路的会话形态（IDE／CLI）。空＝没有正证，那一格整格不画。
    /// 值一律由 ClientTagOf 给，不在这里按名字猜。
    std::string client;
    int wait_n = 0;
    bool present() const {
        return rows > 0 || usage.total() > 0 || has_credits || quota.known || available;
    }
};
struct PanelData {
    std::vector<UIRow> rows;
    std::vector<Chip> chips;             // 每个采集器一格（数字＝本轮真摆出来的行）
    std::vector<Tool> tools;             // 全部采集器，含本机没有的那几路
    std::vector<ap::Claim> claims;       // 权益领取：各家写在自己磁盘上的读数（只读层）
    std::vector<std::string> quiet;      // 今天没活动的采集器名
    std::vector<std::string> errors;     // 「某采集器：读失败原因」
    int hook_live = 0;                   // Hook 收件箱里这轮真数得到的信号文件数（数据源那一格）
    int needs = 0, run = 0;              // 顶栏两个数（needs＝介入+失败，run＝运行+排队）
    int await_n = 0, fail_n = 0;         // 状态项那三个数：介入与失败各算一档，不并成 needs
    int unseen_n = 0;                    // 今天完成但还没看过的条数（待处理清单的第二段）
    int queue_n = 0;                     // run 里有多少条其实在排队——悬停说明要摊开这一层
    double credits = 0;
    double yday_credits = 0;             // 昨天同窗用量，只为第三格的「较昨日」环比
    long today_tokens = 0;               // 今日 token 汇总（scan_all 已按今天窗口算过）
    bool has_today_tokens = false;
    int live = 0;                        // 状态项上的数字：非静置会话数
    int hero = -1;                       // 总览顶部「在跑」卡指的那一行（最近有动静的运行/排队会话）
    double now = 0, window_start = 0;    // 历史屏要按「今天零点→现在」算时刻，只此一处带过来
};
static PanelData g_pd;
static long g_pd_seq = -1;
/// 详情屏看的是哪一行。按 key 从本轮的 g_pd.rows 取最新一版；那一行被重扫挤掉时退回
/// 点下去时的那一份（屏幕上不能因为 3 秒重扫就突然空白）。
static std::string g_sel_key;
static int g_sel_kind = 0;               // 1＝问答详情，2＝授权详情
static UIRow g_sel_copy;
/// 接线状态缓存：探一次要读两份配置，2 秒一探足够（点「写入」之后立刻重探）。
static std::vector<ap::HookAgent> g_hooks;
static double g_hooks_at = -1;
/// 最近一次界面动作的回执（复制／接线／失败），2.5 秒后自己消失。
static std::string g_toast, g_toast_sub;
static double g_toast_at = 0;
/// r20 的 toast 在「详情屏里复制」这一路会多挂一颗「返回总览」（稿子 showToast 的 act 参数）。
/// 这是它唯一带动作的回执，所以不做成通用机制，只留一个开关：那颗按钮走 HIT_BACK。
static bool g_toast_go = false;
/// toast 左边那颗图标：3＝复制（RI_COPY），4＝跳转（RI_JUMP）。稿子按回执类型给图。
static int g_toast_icon = 3;
/// 历史屏翻到第几页（按从最新往旧的顺序）。只在一屏里有效，切走不重置也行。
static int g_hist_page = 0;
/// 上一帧历史屏真摆了几行：翻页入口的步长，与 pin 那套 g_shown 同一口径。
static int g_hist_shown = 0;

// ── R31 的界面状态 ─────────────────────────────────────────────────────────
// 与稿子的 fresh() 一一对应，只是把「返回栈」压成一层：稿子最多同时叠两屏
// （总览 → 账户 → 会话），返回一次就够，所以存「来时的屏」而不是完整历史。
static std::string g_r31_src = "all";      // 来源筛选：all 或 tool id
static bool g_r31_search = false;          // 总览里那条搜索框是否展开
static std::string g_r31_query;            // 会话搜索（项目 / 工具 / 客户端 / 问题）
static std::string g_r31_toolq;            // 工具列表搜索
static int g_r31_focus = 0;                // 0＝没有输入框在收字，1＝会话搜索，2＝工具搜索
static int g_r31_rail = 0;                 // 来源导轨从第几颗开始摆（横向滚动）
static std::string g_r31_account;          // 账户页看的是哪一路（空＝该路没有账户数据）
static bool g_r31_models = false;          // 账户页「按模型」是否展开
static bool g_r31_explain = false;         // 运行卡「今日最长」的说明是否展开
static std::vector<int> g_r31_trail;       // 返回栈：来时的 view
static int g_r31_detail_kind = 0;          // 1＝问答 2＝授权 3＝已完成
static std::string g_r31_detail_key;       // 详情屏那一行的 key（活数据按 key 重取）
static std::set<std::string> g_r31_subopen; // 展开着子对话列表的行（key），跨屏保留
static bool g_lang_menu = false;            // 设置屏语言下拉是否展开
static int g_r31_hist_n = 12;               // 完成记录已加载到第几条（懒加载页大小 12）
static bool g_r31_queue_all = false;        // 首页队列是否展开全部（默认只摆前 8 行）

static const UIRow* SelectedRow(const std::string& key) {
    // 空键不能落到 g_sel_copy 上：那个默认行的 key 也是空串，会把一张「空头像＋等待确认」
    // 的鬼卡画出来（空收件箱时详情屏实测就是这样，而不是空态文案）。
    if (key.empty()) return nullptr;
    for (const auto& r : g_pd.rows) if (r.key == key) return &r;
    return g_sel_copy.key == key ? &g_sel_copy : nullptr;
}
/// 按「采集器 id + 该采集器里的会话 id」找行（横幅点击只带得到这两个，没有 key 可用）。
/// key 的两种拼法（采集器用 /、hook 收件箱用 :）不在这里兜——两处 key 各自稳定，
/// 而横幅带来的这对值就是行上的那两列，直接比它们最省事。
static const UIRow* RowOf(const std::string& agent_id, const std::string& native_id) {
    if (agent_id.empty() || native_id.empty()) return nullptr;
    for (const auto& r : g_pd.rows)
        if (r.agent_id == agent_id && r.native_id == native_id) return &r;
    return nullptr;
}
static bool g_relayout = false;
static bool g_panelVisible = true;
static GLFWwindow* g_win = nullptr;      // 点击要跳走时得先把这块面板收起来
/// 出图模式：这一轮是给取证的，点完不许真把窗口藏了（那样拍不到点击后的状态），也不许跳走。
static const char* g_shot_to = getenv("AGENTPING_SHOT");
/// 离屏探针轮：AGENTPING_CLICK 在这一轮是合成的，落点必须走假档（AGENTPING_LAND_SHOT 指定），
/// 否则「给六语并排图取证」会把他的桌面一家家点开。g_shot_to 在这一轮是空的，所以它拦不住。
static bool g_probe_round = false;

// ── 设置屏接的三个活口子 ────────────────────────────────────────────────────
// g_cfg 是「屏幕上那一屏」与 alert.cpp 的持久化之间唯一的桥：启动时 load、每次点击 save。
// 面板不再每轮 load（原来 RunAlertRound 每 3s 读一次盘）——那样设置屏上刚点下去的档
// 会被下一轮读盘盖掉，屏幕上亮着、账上却没改，是最难查的一种分叉。
static ap::AlertCfg g_cfg;
/// 0＝pin 列表，1＝设置屏。同一块窗口换内容，不另开窗口（另开就得多块卡框、多处收窗）。
static int g_view = 0;
/// 底部标签栏当前选中的那一格，用 g_view 的同一套编号（0 总览 / 5 历史 / 1 设置）。
/// 稿子 `data-act="back"` 是 `state.view = state.tab`：从详情返回要回到「你刚才所在的那一屏」，
/// 不是一律回总览。接线屏不是标签（它由设置屏的「打开」进去），所以它不改 g_tab。
static int g_tab = 0;
/// r20 的面板是固定 640 高、`.body{overflow-y:auto}` 且滚动条隐藏，所以长页（接线屏）
/// 在 C++ 这边只能自己算滚动：每屏一格偏移、一格上限、一格外框。
/// 上限由绘制那一趟量出来给下一帧用（单趟绘制），外框供 HandleInput 判「滚轮落不落在正文里」。
/// 这三样必须声明在 HandleInput 之前，而 r20 的绘制层是在文件后半段 include 进来的。
static float g_scroll[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static float g_scrollTarget[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // 滚轮写目标，帧间缓动过去（丝滑）
static float g_scrollMax[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static ImVec2 g_bodyTop[10], g_bodyBot[10];
static bool g_bodyValid[10] = {false, false, false, false, false, false, false, false, false, false};
static float g_contentH[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
/// App 图标（assets/icon-1024.png 重采样成 RGBA 存进 brand_icon.h）上传后的纹理 id。
/// 稿子的 .mark 是同一张图，这里不许重画星芒，只能贴真像素。
static ImU64 g_brandTex36 = 0, g_brandTex68 = 0;
/// 稿子里 `.ag img` / `.m1 img` 用的是各家 logo；两张 64px 位图存在 agent_icons.h。
static ImU64 g_agentTexQoder = 0, g_agentTexClaude = 0;
/// r31 来源导轨要摆全部六路：后三路的官方像素存在 agent_icons31.h。
static ImU64 g_agent31TexZCode = 0, g_agent31TexWorkBuddy = 0, g_agent31TexOpenCode = 0;
// ── 自绘横幅：券到账时在桌面弹一块小卡（2026-10-30 用户拍板「我们自己画一块横幅窗」）──
// 为什么不走系统横幅：那条通道先要过「系统设置→通知→Agent Ping」这道闸，本机实测档位是
// 「被关着」，ap_notify_post 于是只返回 1（兜底出声）——人眼前什么都没有。自己画不欠授权。
// 它吃同一颗开关（AlertCfg::claims）与同一颗免打扰：关掉＝真不弹，不是弹了不响。
struct BannerItem {
    std::string agent;   // 厂商 id：Brand31 认它，落点也按它认
    std::string key;     // 整条券键 claim:<vendor>:<日期>
    std::string title;   // 「WorkBuddy · 有券可领」
    std::string body;    // 「100 credits 待领 · 点横幅进权益屏」
};
static std::vector<BannerItem> g_banners;
static bool g_bannerActive = false;   // 本帧画的是横幅而不是面板
static bool g_bannerShown = false;    // 窗口因横幅被叫醒：收起时要还它睡回去
static float g_bannerH = 0.0f;        // 绘制那一趟量出来的卡高，供 WantedWindowH 下一帧用
// 没有倒计时：2026-10-30 用户拍板「弹的话不点就常驻，点了才没」。收起只有三条路——
// 点那一行（进权益屏）、表头 ✕（整块收掉）、以及那张券自己不再可领（见 RunAlertRound 的撤账）。
/// 一批横幅的卡高：绘制层和「叫醒窗口那一帧的尺寸」用同一条式子。
/// 第一帧还没量过时若按 0 开窗，窗口会先是一条空白再被 settle 拉长。
static float BannerHeight(int n) { return 38.0f + 62.0f * (float)n + 10.0f; }

/// 换屏。r20 每屏各有自己的滚动位置，切过去必须从头顶开始——稿子每次 render() 重建 DOM，
/// `.body` 的 scrollTop 自然归零；这边不清就会把上一屏滚到 halfway 的偏移带过去。
/// 同一屏内的位置要留着（滚下去点一行再返回，不该被弹回头顶），所以只清目标屏那一格。
/// 屏号不连续：权益屏占的是 9（4 号在 r20 就空出来了），只按「最大号」写死会把新屏的
/// 一次切换静默吃掉——实测点「权益」标签，命中区、DoAction 全走通了，屏却没换。
static void GotoView(int v) {
    if (v < 0 || v > 9) return;
    g_view = v;
    g_scroll[v] = 0.0f;
    g_scrollTarget[v] = 0.0f;
    g_relayout = true;
}
/// 这一轮界面用哪一语（kStr 六列的下标）。优先级从低到高：平台层探测 → settings.txt
/// 的 lang（设置屏那颗 .sel 写的）→ AGENTPING_LANG（取证口，必须盖得过他存的偏好，
/// 否则同一份设置只能拍出一种语言）。取值口径见 [语言] 那行打印。六语的界面固定文案
/// 在启动时就全烘进图集（AddChromeText 遍历 kLocales），会话标题那类活文本由这一版 ImGui
/// 按需栅格化，所以运行时改口不会出现半屏旧字半屏新字。
/// 改完必须 g_relayout：RefFace 决定行框高，换语等于换度量。
/// 初值只在「main 还没跑到检测」时生效。
static int g_loc = AP_LANG_ZH;
/// 上一轮「还在动」（非静置）的会话身份键。设置屏打开「已完成」那一档后，只有
/// 从这条集合里掉出来的会话才允许响 —— 没有它，第一次打开就会把几十条几天前静置的
/// 会话当成跳变，连吵十几轮（alert.h 里同一条门）。
static std::set<std::string> g_active;
static const int kSetW = 340;            // r20 里设置屏与总览同宽（稿子不再有 420 那版）

// ─────────────────────────────────────────────────────────────────────────────
// 画原子：全部是 r14/pin.html 里那一套（胶囊 / 头像环 / 左侧状态条 / 引用块）
// ─────────────────────────────────────────────────────────────────────────────
static const float kPillH = kTierPx[T_PILL] + 2 * 2.0f + 2 * 1.0f;   // 15.5：leading-none 的 9.5 + py-[2px] + 上下 1px 边
static const float kCardR = 14.0f;   // r20 #win{border-radius:14px}

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

// 圆角矩形 + 竖向渐变。
// 旧版用 20 条横带近似：带宽 h/20 只要大于圆角半径，带心就落在圆角区之外，整条第一带
// 会被当成直边填满 —— 850 高的面板是 42pt/带、23pt 圆角，四角直接成了直角（实测窗
// 左上像素＝面板色，光边弧露在方角里侧）。横带在几何上就写不准：圆顶处切线与 x 轴
// 平行，带高再小，那条带的横向偏差仍是 √(2r·带高)，20 带缩到 200 带也还有 3pt 方肩。
// 这里按角度铺圆角、整条轮廓一次填：顶点色由 y 插值（线性函数在三角形内插值仍是同一
// 函数，渐变是精确的），抗锯齿沿用 ImGui 自己的做法 —— 内圈顶点沿内法线缩半个 fringe、
// 外圈顶点外扩半个 fringe 且 alpha=0。
static void GradRound(ImDrawList* dl, float x, float y, float w, float h, float r,
                      ImU32 rgbTop, ImU32 rgbBot, ImU32 border) {
    if (w <= 0.0f || h <= 0.0f) return;
    float rr = r;
    if (rr > h * 0.5f) rr = h * 0.5f;
    if (rr > w * 0.5f) rr = w * 0.5f;
    if (rr < 0.0f) rr = 0.0f;
    auto colAt = [&](float yy) {
        float t = (yy - y) / h;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return MixA(rgbTop, rgbBot, t);
    };
    if (rr < 0.75f) {
        dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h),
                                    rgbTop, rgbTop, rgbBot, rgbBot);
    } else {
        const float kPi = 3.14159265358979f;
        int seg = (int)(rr * 1.6f) + 2;               // 每 90°：r=23 → 38 段，弦高 < 0.01pt
        if (seg < 6) seg = 6;
        if (seg > 48) seg = 48;
        enum { kMaxPts = 4 + 4 * 49 };
        ImVec2 pts[kMaxPts];
        int n = 0;
        auto arc = [&](float cx, float cy, float a0, float a1) {
            for (int i = 0; i <= seg && n < kMaxPts; i++) {
                const float a = a0 + (a1 - a0) * (float)i / (float)seg;
                pts[n++] = ImVec2(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
            }
        };
        arc(x + rr, y + rr, kPi, kPi * 1.5f);             // 左上 → 上沿起点
        arc(x + w - rr, y + rr, kPi * 1.5f, kPi * 2.0f);  // 右上 → 右沿起点
        arc(x + w - rr, y + h - rr, 0.0f, kPi * 0.5f);    // 右下 → 下沿起点
        arc(x + rr, y + h - rr, kPi * 0.5f, kPi);         // 左下 → 回到起点
        const float aa = dl->_FringeScale * 0.5f;
        dl->PrimReserve((n - 2) * 3 + n * 6, n * 2);
        const unsigned int base = dl->_VtxCurrentIdx;
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        for (int i = 0; i < n; i++) {
            const ImVec2& p0 = pts[(i + n - 1) % n];
            const ImVec2& p1 = pts[i];
            const ImVec2& p2 = pts[(i + 1) % n];
            // 顶点法线取前后两条边的外法线均值（顺时针路径里 (dy,-dx) 朝外）。
            float nx = 0.0f, ny = 0.0f;
            for (int k = 0; k < 2; k++) {
                const ImVec2& a = k == 0 ? p0 : p1;
                const ImVec2& b = k == 0 ? p1 : p2;
                float ex = b.y - a.y, ey = -(b.x - a.x);
                const float d2 = ex * ex + ey * ey;
                const float inv = d2 > 1e-6f ? 1.0f / std::sqrt(d2) : 0.0f;
                nx += ex * inv;
                ny += ey * inv;
            }
            const float d2 = nx * nx + ny * ny;
            const float inv = d2 > 1e-6f ? aa / std::sqrt(d2) : 0.0f;
            nx *= inv;
            ny *= inv;
            const ImU32 ci = colAt(p1.y);
            dl->PrimWriteVtx(ImVec2(p1.x - nx, p1.y - ny), uv, ci);                 // 内圈
            dl->PrimWriteVtx(ImVec2(p1.x + nx, p1.y + ny), uv, ci & 0x00FFFFFFu);   // 外圈 alpha=0
        }
        for (int i = 2; i < n; i++) {
            dl->PrimWriteIdx((ImDrawIdx)base);
            dl->PrimWriteIdx((ImDrawIdx)(base + ((i - 1) << 1)));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i << 1)));
        }
        for (int i = 0; i < n; i++) {
            const int i0 = i, i1 = (i + 1) % n;
            dl->PrimWriteIdx((ImDrawIdx)(base + (i1 << 1)));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i0 << 1)));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i0 << 1) + 1));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i0 << 1) + 1));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i1 << 1) + 1));
            dl->PrimWriteIdx((ImDrawIdx)(base + (i1 << 1)));
        }
    }
    if (border)
        dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), border, rr, 0, 1.0f);
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
              HIT_BACKOV = 8, HIT_NAVHIST = 9, HIT_NAVSETUP = 10,
              HIT_HOOKINST = 11, HIT_JUMP = 12, HIT_COPY = 13, HIT_HISTMORE = 14,
              // r20 版式新加的四个落点：
              // HIT_LANG    设置屏「语言」那颗 .sel（没有下拉控件，点一下轮换到下一语）
              // HIT_SKIP    接线屏表头的「跳过」（data-act="skip" → 回总览）
              // HIT_TABOV   底部标签栏的「总览」
              // HIT_COPYTEXT 接线屏两个代码框的「复制」：复制 h.title 里装的那段原文
              HIT_LANG = 15, HIT_SKIP = 16, HIT_TABOV = 17, HIT_COPYTEXT = 18,
              // R31 版式新接的落点：
              // HIT_R31BACK 子屏左上那颗「← 来处」；HIT_SRC 来源导轨（key＝tool id / all）；
              // HIT_TAB31 会话/用量两个标签（opt＝0 / 6）；HIT_INFO31 行上的 ⓘ 进详情；
              // HIT_RAILNEXT 导轨箭头；HIT_TOOLS31 全部工具；HIT_ACCOUNT31 账户页；
              // HIT_MODELSTOG 按模型展开；HIT_SEARCHTOG 搜索框；HIT_CLEARQ 清搜索；
              // HIT_GEAR 设置；HIT_EXPLAIN 轮次说明；HIT_ACCTSESS 账户页看会话；
              // HIT_PICKTOOL 工具列表选一路；HIT_SEARCHFIELD 输入框收字。
              // HIT_SHOWALL31 空态「查看全部会话」；HIT_HISTORY31 总览页脚进完成记录。
              HIT_R31BACK = 19, HIT_SRC = 20, HIT_TAB31 = 21, HIT_INFO31 = 22,
              HIT_RAILNEXT = 23, HIT_TOOLS31 = 24, HIT_ACCOUNT31 = 25,
              HIT_MODELSTOG = 26, HIT_SEARCHTOG = 27, HIT_CLEARQ = 28,
              HIT_GEAR = 29, HIT_EXPLAIN = 30, HIT_ACCTSESS = 31,
              HIT_PICKTOOL = 32, HIT_SEARCHFIELD = 33,
              HIT_SHOWALL31 = 34, HIT_HISTORY31 = 35,
              // HIT_SUBTOG 队列行/运行卡上那颗「N 条子对话」的展开开关
              HIT_SUBTOG = 36,
              HIT_H31MORE = 38,      // 完成记录「还有 N 条」补位行（自动续页的兜底入口）
              HIT_QUEUEALL = 39,     // 首页队列「还有 N 条」：展开全部待处理行
              // HIT_AXGO 接线屏第三步那颗「去系统设置」：开到「隐私与安全性 → 辅助功能」
              // HIT_BANNER 自绘横幅上的一行券（key＝券键，opt＝第几条）；HIT_BANNERCLOSE 是右上角✕
              // HIT_CLAIMREFRESH 权益屏表头那颗「刷新」：立刻重读三家落盘读数
              // HIT_CLAIMCARD 权益卡整张：把这家的 App 唤到前台（领取仍归他自己点）
              HIT_AXGO = 40, HIT_BANNER = 41, HIT_BANNERCLOSE = 42, HIT_CLAIMREFRESH = 43,
              HIT_CLAIMCARD = 44 };
/// 设置屏上每一颗控件的身份。`Hit::row` 装这个，`Hit::opt` 装分段控件的取值。
/// 顺序与设置屏从上到下一致，改了要把 alert.cpp 的 key 表一起看一遍。
/// SC_DND 是 r20 那行「免打扰」：屏上叫免打扰、账上也叫 dnd，同向，所以不像
/// SC_NOTIFY（屏上「系统通知」对账上 dnd 要取反）那样绕一道。
static void Toast(const std::string& a, const std::string& b, bool go = false);   // 定义在 r20 段
static const Tool* AgentTool(const char* id);   // 定义在绘制段；落点回执要先认得出这一路的工具名
/// 面板上念的那个产品名（providers 表）。定义在文件末尾 include 的 r31_views.inc。
/// 权益卡的回执必须与卡头同名。实测点击那一帧 g_pd.tools 还是空的（日志 tools=0），
/// 走 AgentTool 就退回采集器 id，屏幕上成了「只把 workbuddy 带到了前台」。
static std::string R31Agent(const std::string& id, int loc);
enum SetCtl { SC_NEEDS = 0, SC_AWAIT, SC_FAIL, SC_DONE, SC_SOUND, SC_NOTIFY,
              SC_TOP, SC_ROWS, SC_DND, SC_CLAIMS, SC_LAUNCH, SC_QUIT };
struct Hit {
    float x0, y0, x1, y1;
    int act = 0, row = 0, opt = 0;
    /// 这一颗当前是不是「开着」：开关/胶囊/分段都靠它驱动 150ms 的状态过渡（稿子的
    /// transition-colors / transition-all）。命中区是绘制那一趟登记的，状态也在那一趟给出。
    int state = 0;
    long zid = 0;
    std::string key, agent_id, native_id, label, title;
    std::string cwd;                       // 行的工作区路径：ZCode 那级落点用
    double created = 0;                    // 这条会话建出来的时刻：ZCode 落点据此说清为什么点不到
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
/// 位宽按「act 最多 99、row 最多 9999、opt 最多 999」留：原来 act*100 与 row*1000
/// 是重叠的（act≥10 时 row=0,act=14 与 row=1,act=4 同号），只影响悬停过渡的归属，
/// 但 r20 把落点种类加到 19 个，重叠面变大，这里一次拉宽。
static long Zid(int loc, int row, int act, int opt) {
    return (long)loc * 1000000000L + (long)row * 100000L + (long)act * 1000L + (long)opt;
}
/// 反解 locale：DoAction 里那两处要用它挑文案，位宽跟着 Zid 一起改，别再各写一遍除数。
static int LocOfZid(long zid) { return (int)(zid / 1000000000L) % kLocales; }
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
    h.cwd = r.cwd;
    h.created = r.created;
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
    if (ctl == SC_LAUNCH) {
        // 登录时启动：真源是登录项文件，不在 settings.txt 里。点一下＝写/删那个文件，
        // 亮灭按写完之后的文件存在性重新读，不按我们「以为成功了」的返回值。
        const bool was = ap_autostart_get(nullptr, 0) == 1;
        char why[512] = {0};
        const int rc = ap_autostart_set(was ? 0 : 1, why, (int)sizeof why);
        const bool now = ap_autostart_get(nullptr, 0) == 1;
        printf("[设置] %s ＝ %s｜%s\n", T("set_launch", 0), now ? "开" : "关",
               (rc == 0 ? std::string("已生效：") + why
                        : std::string("没改成：") + (why[0] ? why : "原因未回")).c_str());
        fflush(stdout);
        return;
    }
    switch (ctl) {
        case SC_NEEDS:  g_cfg.needs_user = !g_cfg.needs_user; b = g_cfg.needs_user; key = "needs_you"; break;
        case SC_AWAIT:  g_cfg.await_perm = !g_cfg.await_perm; b = g_cfg.await_perm; key = "await_confirm"; break;
        case SC_FAIL:   g_cfg.failed     = !g_cfg.failed;     b = g_cfg.failed;     key = "failed"; break;
        case SC_DONE:   g_cfg.done       = !g_cfg.done;       b = g_cfg.done;       key = "st_done"; break;
        case SC_SOUND:  g_cfg.sound      = !g_cfg.sound;      b = g_cfg.sound;      key = "sound"; break;
        // 屏上那颗叫「系统通知」，账上那项叫 dnd（免打扰）：同一件事的两个方向，所以这里必须取反。
        // 日志把两边都念出来，对账时才不会把「一个开一个关」误读成 bug。
        case SC_NOTIFY: g_cfg.dnd        = !g_cfg.dnd;        b = !g_cfg.dnd;       key = "sys_notify"; break;
        case SC_DND:    g_cfg.dnd        = !g_cfg.dnd;        b = g_cfg.dnd;        key = "set_dnd"; break;
        // 「权益到账」不看会话，只看各家磁盘上的领取读数；关掉期间不记账（见 select_claim_alerts）。
        case SC_CLAIMS: g_cfg.claims     = !g_cfg.claims;     b = g_cfg.claims;     key = "set_claim"; break;
        case SC_TOP:    g_cfg.on_top     = !g_cfg.on_top;      b = g_cfg.on_top;    key = "on_top"; break;
        case SC_ROWS:   g_cfg.max_rows   = opt;               b = true;             key = "max_rows"; break;
        case SC_QUIT:   // 设置屏底部「退出」：走窗口正常关闭（扫描线程在 join 里收）
            if (g_win) glfwSetWindowShouldClose(g_win, GLFW_TRUE);
            return;
        default: return;
    }
    if (ctl == SC_SOUND && b) {   // 翻到开：当场播一声测试音，不用等下一次提醒才知道生效
        char why[160];
        ap_sound_test(why, (int)sizeof why);
        printf("[设置] %s\n", why);
        fflush(stdout);
    }
    {   // 每次翻转都给一行明确回执：开关两态在暗主题下不够分（2026-10-28 用户实测「点不动」）
        Toast(std::string(T(key, g_loc)) + (b ? "  已开启" : "  已关闭"), "");
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
        case HIT_OPTION:   return "选项";
        case HIT_NAVHIST:  return "历史";
        case HIT_NAVSETUP: return "接线";
        case HIT_HOOKINST: return "写入接线";
        case HIT_JUMP:     return "跳转会话";
        case HIT_AXGO:     return "去系统设置";
        case HIT_BANNER:   return "横幅·券";
        case HIT_BANNERCLOSE: return "横幅·关掉";
        case HIT_CLAIMREFRESH: return "权益·刷新";
        case HIT_CLAIMCARD:    return "权益·卡";
        case HIT_COPY:     return "复制选项";
        case HIT_HISTMORE: return "历史翻页";
        case HIT_LANG:     return "语言";
        case HIT_SKIP:     return "跳过接线";
        case HIT_TABOV:    return "标签·总览";
        case HIT_COPYTEXT: return "复制脚本";
        case HIT_R31BACK:  return "返回";
        case HIT_SRC:      return "来源";
        case HIT_TAB31:    return "标签";
        case HIT_INFO31:   return "详情";
        case HIT_RAILNEXT: return "导轨箭头";
        case HIT_TOOLS31:  return "全部工具";
        case HIT_ACCOUNT31:return "账户";
        case HIT_MODELSTOG:return "按模型";
        case HIT_SEARCHTOG:return "搜索";
        case HIT_CLEARQ:   return "清除搜索";
        case HIT_GEAR:     return "设置";
        case HIT_EXPLAIN:  return "轮次说明";
        case HIT_ACCTSESS: return "账户会话";
        case HIT_PICKTOOL: return "选工具";
        case HIT_SEARCHFIELD: return "输入框";
        case HIT_SHOWALL31: return "查看全部会话";
        case HIT_HISTORY31: return "今日完成";
        case HIT_SUBTOG:   return "子对话";
        case HIT_H31MORE: return "加载更多";
        case HIT_QUEUEALL: return "展开队列";
        default:           return "未命名";
    }
}

/// 那一跳真落到了哪一层。面板据此写回执，也据此决定收不收面板：
/// 只有落到「那一条对话」才把面板藏起来——藏面板会把要解释的话一起藏掉，
/// 而 2026-10-29 那次就是因为没解释：用户只看见 Finder 开在 ~/.qoder/projects，
/// 以为这个产品压根不会跳会话。
enum LandLevel { L_SESS = 0,   // 精确到那一条对话（托盘点中 / 侧栏点中 / deep link 被接住）
                 L_TRAY,       // 托盘这一轮只露最近那 12 条，没列它（Qoder 实测）
                 L_MISS,       // 客户端里试过了但没落到这一条：侧栏这一列没它、窗口没建起来、
                               // 或点下去主区没换（ZCode 实测，10-29 逐条读过）
                 L_NEWRUN,     // 这条比 ZCode 本次启动还晚：它这次的侧栏列表里根本没有它（10-29 实测）。
                               // 单独立一档，因为 land_miss 那句「对着标题在列表里找」在这里是死路——
                               // 列表里不会有它，翻页也翻不出来。
                 L_WAKE,       // ZCode 原本一扇窗口都没有，这一跳只替它把窗口要了回来：
                               // 界面还要几秒才长出来（实测窗口 3 秒、树更晚），这一档不算落到会话，
                               // 也不许走阶梯凑数
                 L_APP,        // 只把客户端唤到前台，没到具体会话
                 L_DIR,        // 落到记录目录
                 L_AX,         // 挡在辅助功能授权前面——这一格只能他自己拨
                 L_NONE };     // 一层都没落上
struct Landing { int level = L_NONE; std::string why; };   // why＝平台原话，只进日志

static const char* LandName(int lv) {
    switch (lv) {
        case L_SESS: return "会话级";
        case L_TRAY: return "托盘没列";
        case L_MISS: return "客户端没点中";
        case L_NEWRUN: return "比本次启动晚";
        case L_WAKE: return "窗口在开";
        case L_APP:  return "只到 App";
        case L_DIR:  return "记录目录";
        case L_AX:   return "缺辅助功能";
        default:     return "落空";
    }
}

/// 收面板：这块浮层是 topmost，跳过去的窗口会被我们自己盖住（Swift 那边 dismiss() 同一件事）。
static void HidePanel() {
    if (g_panelVisible) {
        g_panelVisible = false;
        if (g_win) glfwHideWindow(g_win);
    }
}

/// 展面板，与 HidePanel 对称。点横幅那条路进来时面板本来就是藏着的，
/// 而没落到具体会话时要在屏上说一句话——面板还藏着，回执等于没写。
static void ShowPanel() {
    if (!g_panelVisible) {
        g_panelVisible = true;
        if (g_win) glfwShowWindow(g_win);
        g_relayout = true;
    }
}

/// 跳到那会话。先用会话级那把钥匙（Qoder＝按名字点它自己托盘里的条目，ZCode＝按名字点侧栏
/// 任务列表里的行），没开成才走 ap_open3 的三层阶梯；两条 reason 各用各的缓冲区。
/// 上一版复用同一个 why，ap_open 一把盖掉了「未授权辅助功能」那句，屏幕上就只剩「打开了文件夹」。
static Landing JumpTo(const Hit& h) {
    Landing out;
    char key[512], lad[512];
    key[0] = lad[0] = 0;
    const bool titled = !h.title.empty();
    // tried 必须单独记：会话级那两把钥匙只有 Qoder/ZCode 有，其余那几路压根没试。
    // 早期版本用「rc 初值 1」当没试的哨兵，结果 Claude 的行也被判成「托盘没列它」。
    bool tried = false;
    int rc = 1;
    if (h.agent_id == "zcode" && titled) {
        // 点侧栏那一行是 AXPress 别的 App 的界面，面板留不留都不挡路；
        // 「窗口都没开」那一档由平台侧自己发一次 reopen 要回窗口，不需要我们传路径
        //（10-29 撤掉了 workspace/open 那条 URL：它会弹文件夹信任确认，见 open.h）。
        rc = ap_zcode_navigate(h.title.c_str(), h.cwd.c_str(), h.created, key, (int)sizeof key);
        tried = rc != 4;
    } else if (h.agent_id == "qoder" && titled) {
        // 托盘那一菜单是 AXPress 点中的，不占键盘，面板留不留都不挡路。
        rc = ap_qoder_tray(h.title.c_str(), key, (int)sizeof key);
        tried = rc != 4;
    }
    if (tried && rc == 0) {
        out.level = L_SESS;
        out.why = key;
        HidePanel();
        fprintf(stderr, "[落点] %s/%s → 会话级 ok ｜ %s\n", h.agent_id.c_str(),
                h.native_id.c_str(), key[0] ? key : "无说明");
        fflush(stderr);
        return out;
    }
    int lvl = -1;
    if (tried && rc == 2) lvl = L_AX;                    // 未授权辅助功能：这一格只能他自己拨
    // 5＝ZCode 原本一扇窗口都没有，这一跳替它把主窗口要回来了，但界面树还没长出来
    // （面板这一跳最多等 2.4 秒，实测窗口 3 秒、树更晚）。这一趟就此收手：
    // 再走阶梯只会多唤一层（activate／开目录），把「稍等再点一次」这句冲掉。
    else if (tried && rc == 5) {
        out.level = L_WAKE;
        out.why = key;
        ShowPanel();
        fprintf(stderr, "[落点] %s/%s → %s ｜ 会话级：%s ｜ 阶梯：未走（窗口正在开）\n",
                h.agent_id.c_str(), h.native_id.c_str(), LandName(out.level),
                key[0] ? key : "无说明");
        fflush(stderr);
        return out;
    }
    else if (tried && rc == 6) lvl = L_NEWRUN;   // 这条比 ZCode 本次启动晚，它这次的列表里没它
    else if (tried && rc == 1)
        lvl = h.agent_id == "zcode" ? L_MISS : L_TRAY;   // 试过了，那一条不在客户端露出来的名单里

    // 缺授权时顺手发起系统那次问询（弹窗里带「打开系统设置」），并把那一格真开到屏上：
    // 回执第二行写的是「系统设置里那一格已经打开」，不真开就是谎话（10-29 逐张读实拍读出来的）。
    if (lvl == L_AX) {
        char ax[256], pane[256];
        ap_ax_trusted(1, ax, (int)sizeof ax);
        if (ap_open_privacy_pane(pane, (int)sizeof pane) == 0)
            fprintf(stderr, "[落点] 已开系统设置那一格：%s\n", pane);
        else
            fprintf(stderr, "[落点] 系统设置那一格没开成：%s\n", pane);
        fflush(stderr);
    }

    int layer = 0;
    const int rc2 = ap_open3(h.agent_id.c_str(), h.native_id.c_str(), h.cwd.c_str(),
                             lad, (int)sizeof lad, &layer);
    if (lvl < 0) {
        if (rc2 != 0) lvl = L_NONE;
        else lvl = layer == 1 ? L_SESS : layer == 2 ? L_APP : layer == 3 ? L_DIR : L_NONE;
    }
    out.level = lvl;
    out.why = (key[0] ? std::string(key) + "；" : std::string()) + (lad[0] ? lad : "未走阶梯");
    if (out.level == L_SESS) HidePanel();
    else ShowPanel();   // 点横幅进来时面板是藏着的；这一层要他看见回执，得先摆回来
    fprintf(stderr, "[落点] %s/%s → %s ｜ 会话级：%s ｜ 阶梯：%s（ap_open %s，layer=%d）\n",
            h.agent_id.c_str(), h.native_id.c_str(), LandName(out.level),
            key[0] ? key : "不适用", lad[0] ? lad : "未走", rc2 == 0 ? "ok" : "落空", layer);
    fflush(stderr);
    return out;
}

/// 把落点评语写屏：只有真落到那一条对话才算成，其余每一层都在屏上留一句话，
/// 并把标题放进剪贴板——客户端的列表里没有可粘贴的检索框，这一份是给他对着标题找的（10-29 核过）。
/// copy_title=false：那一趟剪贴板上已经装着更要紧的东西（点选项时先把答案复制了），不许覆盖。
/// 文案里带 %s 的只有 land_app（要念工具名），所以用一个填空函数而不是 snprintf：
/// 六语表里少一个 %s，snprintf 就报一次 -Wformat-extra-args。
static void LandingToast(const Landing& ld, const Hit& h, bool copy_title = true) {
    const int loc = LocOfZid(h.zid);
    if (ld.level == L_SESS) {
        Toast(T("land_sess", loc), "");
        g_toast_icon = 4;   // 这一句也是「跳到了哪儿」，不是复制
        return;
    }
    const Tool* t = AgentTool(h.agent_id.c_str());
    const std::string name = t && !t->name.empty() ? t->name : h.agent_id;
    // L_WAKE 不动剪贴板：那一行的下文是「过几秒再点一次这一行」，我们这边还等着自己点，
    // 复制标题反而把上一趟真正要贴的东西顶掉。
    if (copy_title && ld.level != L_WAKE && !h.title.empty() && !g_shot_to)
        ImGui::SetClipboardText(h.title.c_str());
    auto fill = [&name](const char* f) {
        std::string s = f;
        const std::size_t p = s.find("%s");
        return p == std::string::npos ? s : s.replace(p, 2, name);
    };
    const char* key = ld.level == L_AX ? "land_ax" : ld.level == L_TRAY ? "land_tray"
                    : ld.level == L_MISS ? "land_miss"
                    : ld.level == L_NEWRUN ? "land_newrun"
                    : ld.level == L_WAKE ? "land_wake"
                    : ld.level == L_APP ? "land_app" : ld.level == L_DIR ? "land_dir" : "land_none";
    Toast(fill(T(key, loc)),
          T(ld.level == L_AX ? "land_ax_sub" : ld.level == L_DIR ? "land_dir_sub"
            : ld.level == L_WAKE ? "land_wake_sub" : ld.level == L_NEWRUN ? "land_newrun_sub"
            : "land_copy",
            loc));
    g_toast_icon = 4;   // 回执是「跳到了哪儿」，图标该用箭头而不是剪贴板
}

/// 出图轮不许真跳（会把他的桌面点开），但回执那句话必须拍得到。
/// AGENTPING_LAND_SHOT=sess|tray|miss|newrun|wake|app|dir|ax|none 造一个该档位的落点，
/// 走的是与真跳同一个 LandingToast——拍到的字就是屏幕上会出的字，不是另写的示意。
static void JumpAndReport(const Hit& h, bool copy_title = true) {
    if (!g_shot_to) { LandingToast(JumpTo(h), h, copy_title); return; }
    const char* e = getenv("AGENTPING_LAND_SHOT");
    if (!e || !*e) return;
    Landing ld;
    ld.why = "出图轮预览，未发起真跳转";
    ld.level = !strcmp(e, "sess") ? L_SESS  : !strcmp(e, "tray") ? L_TRAY
             : !strcmp(e, "miss") ? L_MISS  : !strcmp(e, "newrun") ? L_NEWRUN
             : !strcmp(e, "wake") ? L_WAKE
             : !strcmp(e, "app") ? L_APP
             : !strcmp(e, "dir") ? L_DIR    : !strcmp(e, "ax") ? L_AX : L_NONE;
    LandingToast(ld, h, copy_title);
}

/// 那颗按钮的字按「这一路到底有没有客户端可唤」出：Claude / Codex 只有 JSONL，
/// 那一趟真打开的是记录目录，按钮写「打开客户端里的这条会话」就是骗人。
/// 上一版写的是「切到 … 的终端」，而 C++ 这版从没实现过切终端。
static const char* LandGoKey(const std::string& agent_id) {
    return ap_has_client(agent_id.c_str()) ? "land_go_sess" : "land_go_log";
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

/// 券那条横幅的落点：收横幅 → 唤窗口 → 切权益屏。系统横幅和自绘横幅共用这一条，
/// 两条路点下去必须是同一个地方，不然「点横幅」这件事就有两种答案。
/// 不跳那家 App：券要在它自己的界面里领，而那几家的读数我们本来就摆在同一屏上了
/// （Ready 那张卡已被 R31Claims 排到最前，所以不动来源导轨：导轨一改，会话屏跟着被筛到
/// 同一家，那是横幅点不该带的副作用）。
static void OpenClaimScreen(const std::string& agent, const std::string& key) {
    // 「点了就没了」只对他点的那一条成立：同块卡上并着的另一家还欠他一次决定，
    // 顺手清掉等于替他按了 ✕。系统横幅那条回传的 key 可能只是厂商 id，所以两个都认。
    const size_t before = g_banners.size();
    g_banners.erase(std::remove_if(g_banners.begin(), g_banners.end(),
                                   [&](const BannerItem& b) {
                                       return b.key == key || (!agent.empty() && b.agent == agent);
                                   }),
                    g_banners.end());
    ShowPanel();
    if (g_win) glfwFocusWindow(g_win);
    GotoView(9);
    printf("[横幅点击] 券 %s/%s → 权益屏｜撤下 %zu 条，屏上还剩 %zu 条\n", agent.c_str(),
           key.c_str(), before - g_banners.size(), g_banners.size());
    fflush(stdout);
    Toast(std::string(T("claim_opened", g_loc)), "");
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
    // 券那张横幅没有「会话」可定位：它的 session_id 是整条 claim 键。
    if (ap::is_claim_key(ses)) {
        OpenClaimScreen(agent, ses);
        return;
    }
    h.native_id = NativeId(agent, ses);
    // 标题必须一起带上：会话级落点认的就是标题（Qoder 托盘里没有 URL 可认）。
    // 上一版这里只填了 agent/id，于是点横幅永远走不到托盘点名那一级，
    // 只会「唤起 App／打开记录目录」——和点行跳到文件夹是同一个病。
    if (const UIRow* r = RowOf(agent, h.native_id)) {
        h.key = r->key;
        h.title = r->title;
        h.cwd = r->cwd;
        h.created = r->created;
    }
    LandingToast(JumpTo(h), h);
}

/// 就地翻这一行的状态：下一轮重扫（3s）才会走 RebuildPanel，点下去必须马上看见反应。
static int GroupOf(int st);    // 定义在绘制段；这里前置声明给 MarkSeen 用
/// 看过一条完成会话（双击跳走或 ⓘ 进详情）：把已读水位线推进到它的完成时刻并落盘。
/// 不落盘的话重启就把今天全部点回未读，那比没有点还吵。
static void MarkSeen(const UIRow& r) {
    if (GroupOf(r.state) != G_DONE) return;
    const long long t = (long long)r.at;
    if (t <= g_cfg.seen_until) return;
    g_cfg.seen_until = t;
    std::string err;
    if (!ap::save_alert_cfg(g_cfg, &err))
        fprintf(stderr, "[已读] 水位线没写下去：%s\n", err.c_str());
}

static void MarkRow(const Hit& h, bool ack, bool later) {
    for (auto& r : g_pd.rows) {
        if (r.key != h.key) continue;
        if (ack) r.acked = true;
        if (later) r.snoozed = true;
    }
    g_relayout = true;
}

// DoAction 在绘制层之前：接线／回执三个私有件先声明，定义留在 r20 四屏那一块。
static void RefreshHooks(bool force);
static std::string TildePath(const std::string& p);

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
            JumpAndReport(h, false);
            return;
        }
        case HIT_SETTINGS:                    // 换屏：同一块窗口换内容，不另开窗口
            g_tab = 1;
            GotoView(1);
            return;
        case HIT_TABOV: {                     // 底部标签栏：opt 装目标屏（0 总览 / 5 历史 / 1 设置）
            int v = (h.opt == 5 || h.opt == 1) ? h.opt : 0;
            g_tab = v;
            if (v == 5) g_hist_page = 0;
            GotoView(v);
            return;
        }
        case HIT_SKIP:                        // 接线屏表头那颗「跳过」＝稿子 data-act="skip"
            g_tab = 0;
            GotoView(0);
            return;
        case HIT_NAVSETUP:
            RefreshHooks(true);
            GotoView(2);
            return;
        case HIT_NAVHIST:
            g_tab = 5;
            g_hist_page = 0;
            GotoView(5);
            return;
        case HIT_LANG: {
            if (h.opt < 0) {                  // 点在下拉胶囊上：开/关菜单
                g_lang_menu = !g_lang_menu;
                g_relayout = true;
                return;
            }
            const int next = h.opt % kLocales;   // 点在菜单某一行：选定该语
            g_lang_menu = false;
            g_loc = next;
            g_cfg.lang = next;
            std::string err;
            const bool wrote = ap::save_alert_cfg(g_cfg, &err);
            // 菜单栏那串计数与悬停说明是 RebuildPanel 按 g_loc 现做的，把 seq 账清掉
            // 就等于要求下一帧重摆一次——不然托盘还会挂着上一语的说法，最多拖 3 秒。
            g_pd_seq = -1;
            printf("[语言] %s｜%s\n", kLocTag[next],
                   wrote ? "已落盘 settings.txt" : (std::string("落盘失败：") + err).c_str());
            fflush(stdout);
            g_relayout = true;
            return;
        }
        case HIT_COPYTEXT: {                  // 接线屏两个代码框的「复制」：原文装在 h.title
            if (!h.title.empty()) ImGui::SetClipboardText(h.title.c_str());
            const int loc = LocOfZid(h.zid);
            Toast(T("setup_copied", loc), T("setup_copied_sub", loc));
            return;
        }
        case HIT_HOOKINST: {                    // 接线屏那颗「写入 / 重写」：真改本机配置
            std::string backup, err;
            const bool ok = ap::install_hook(h.agent_id, &backup, &err);
            RefreshHooks(true);
            const int loc = LocOfZid(h.zid);
            if (ok) {
                char tb[96];
                snprintf(tb, sizeof tb, T("setup_done", loc), h.title.empty() ? h.agent_id.c_str() : h.title.c_str());
                if (backup.empty()) Toast(tb, TildePath(h.label));
                else {
                    // 备份名比行宽长，而尾部的日期正是还原时要看的那截。按最后一刀拆成
                    // 「目录 / 文件名」两行，比裁掉尾部诚实。
                    const std::string tilde = TildePath(backup);
                    const std::size_t cut = tilde.find_last_of('/');
                    const std::string wrapped =
                        cut == std::string::npos ? tilde : tilde.substr(0, cut + 1) + "\n" + tilde.substr(cut + 1);
                    char sb[256]; snprintf(sb, sizeof sb, T("setup_backup", loc), wrapped.c_str());
                    Toast(tb, sb);
                }
            } else {
                Toast(T("setup_fail", loc), err.empty() ? T("setup_restart", loc) : err);
            }
            g_relayout = true;
            return;
        }
        case HIT_AXGO: {                        // 第三步那颗「去系统设置」：开到辅助功能那一格
            const int loc = LocOfZid(h.zid);
            char why[192];
            // 平台那句 reason 是中文，不进屏幕（六语界面漏一句中文比不解释更难看），只进日志。
            const int rc = ap_open_privacy_pane(why, (int)sizeof why);
            fprintf(stderr, "[辅助功能] 开系统设置 %s → %s\n", rc == 0 ? "ok" : "落空", why);
            fflush(stderr);
            if (rc == 0) Toast(T("setup_ax_done", loc), T("setup_ax_sub", loc));
            else Toast(T("setup_fail", loc), T("setup_ax_fail", loc));
            return;
        }
        case HIT_JUMP: {                        // 详情那颗「打开会话」：真跳，并回执落到了哪一层
            JumpAndReport(h);
            return;
        }
        case HIT_BANNER:                        // 自绘横幅那一行券：落点与系统横幅同一条出口
            OpenClaimScreen(h.agent_id, h.key);
            return;
        case HIT_BANNERCLOSE: {                 // 横幅右上角✕：只收横幅，不叫醒面板
            // 表头这一颗管整块卡（它是卡级的关闭，不是某一行的「稍后」）；
            // 常驻之后它是唯一一条「我现在不想处理、但也不看权益屏」的出口。
            const size_t n = g_banners.size();
            g_banners.clear();
            printf("[横幅] 手动关掉 %zu 条（账本已记，本期不再弹）\n", n);
            fflush(stdout);
            return;
        }
        case HIT_CLAIMREFRESH: {          // 权益屏表头那颗「刷新」：立刻重读三家落盘的读数
            // 只重读，不触发提醒轮：面板开着的时候提醒那一档本来就不记账（见 RunAlertRound
            // 里那条门），在这里顺手跑一轮会把当天的券静默吃掉。
            const auto t0 = std::chrono::steady_clock::now();
            const double t = (double)std::time(nullptr);
            std::vector<ap::Claim> cs = ap::claims_scan(t);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count();
            int ready = 0;
            for (const auto& c : cs) if (c.ready) ready++;
            g_pd.claims = std::move(cs);
            g_pd.now = t;               // 页脚那个「读数于」跟着推到当下——这是按钮唯一的可见回执
            g_relayout = true;
            printf("[权益刷新] 重读 %zu 家｜可领 %d｜%.1fms\n", g_pd.claims.size(), ready, ms);
            fflush(stdout);
            Toast(T("r_claim_refreshed", g_loc), "");
            return;
        }
        case HIT_CLAIMCARD: {   // 权益卡整张：把那家 App 唤到前台，领取仍由他自己点
            // 2026-10-30 用户：「双击为啥没反应呢，只是提示吗，需要去 agent 领，是吗」。
            // 这一跳刻意不走会话级那两把钥匙：权益面板不是会话，没有「那一条对话」可落。
            // 会话 id 传空串 → 平台层的 DeepURL 对空 id 直接返回 nil，所以设计上就只可能
            // 停在 layer 2（把 App 带到前台）；真跳不到（没装／唤不起）就报落空，不猜。
            const int loc = LocOfZid(h.zid);
            char why[256]; why[0] = 0;
            int layer = 0;
            int rc = 1;
            if (!g_shot_to && !g_probe_round) {
                rc = ap_open3(h.agent_id.c_str(), "", nullptr, why, (int)sizeof why, &layer);
            } else {
                // 出图轮与离屏探针：合成点击是给取证的，真跳会把他的桌面点开（与
                // JumpAndReport 同一层含义）。这一档落在哪由 AGENTPING_LAND_SHOT 指定，
                // 拍到的字与真跳走的是同一条 Toast 通路。
                const char* e = getenv("AGENTPING_LAND_SHOT");
                rc = (e && !strcmp(e, "app")) ? 0 : 1;
                layer = rc == 0 ? 2 : 0;
                snprintf(why, sizeof why, "出图轮预览，未发起真跳转");
            }
            const std::string name = R31Agent(h.agent_id, loc);
            printf("[权益落点] %s → rc=%d layer=%d｜%s\n", h.agent_id.c_str(), rc, layer, why);
            fflush(stdout);
            char tb[256];
            snprintf(tb, sizeof tb, T(rc == 0 ? "land_app" : "land_none", loc), name.c_str());
            Toast(tb, T(rc == 0 ? "r_claim_land_sub" : "r_claim_land_none_sub", loc));
            g_toast_icon = 4;   // 回执是「跳到了哪儿」，图标该用箭头而不是剪贴板
            return;
        }
        case HIT_COPY: {                        // 详情屏的选项：只进剪贴板，不跳走
            if (!h.label.empty()) ImGui::SetClipboardText(h.label.c_str());
            const int loc = LocOfZid(h.zid);
            char tb[128];
            snprintf(tb, sizeof tb, T("det_toast", loc), h.label.c_str());
            // 在详情屏里复制时，稿子会给这声回执挂一颗「返回总览」；总览里复制没有这颗。
            const bool in_detail = (g_view == 3 || g_view == 4);
            if (h.title.empty()) Toast(tb, T("det_copy", loc), in_detail);
            else {
                char sb[160]; snprintf(sb, sizeof sb, T("det_toast_sub", loc), h.title.c_str());
                Toast(tb, sb, in_detail);
            }
            return;
        }
        case HIT_HISTMORE:
            if (h.opt == 1) g_hist_page = 0;
            else g_hist_page += g_hist_shown > 0 ? g_hist_shown : 1;
            g_relayout = true;
            return;
        case HIT_R31BACK:                       // 子屏左上「← 来处」：按来时的屏返回
            if (!g_r31_trail.empty()) {
                const int v = g_r31_trail.back();
                g_r31_trail.pop_back();
                GotoView(v);
            } else {
                GotoView(g_tab);
            }
            return;
        case HIT_SRC:                           // 来源导轨：key 装 tool id，空＝全部
            g_r31_src = h.key.empty() ? "all" : h.key;
            g_r31_rail = 0;
            g_scroll[g_view] = 0.0f;
            g_relayout = true;
            return;
        case HIT_TAB31:                         // 会话 / 用量 / 权益三个标签（opt＝目标屏）
            g_tab = (h.opt == 6 || h.opt == 9) ? h.opt : 0;
            GotoView(g_tab);
            return;
        case HIT_INFO31: {                      // 行上的 ⓘ：进详情，不动跳转
            const UIRow* r = SelectedRow(h.key);
            if (!r) return;
            MarkSeen(*r);
            g_sel_key = r->key;
            g_sel_copy = *r;
            g_sel_kind = (r->state == ap::ST_RESTING) ? 3 : (RowAsksQuestion(*r) ? 1 : 2);
            g_r31_detail_kind = g_sel_kind;
            g_r31_detail_key = r->key;
            if (g_view != 3) g_r31_trail.push_back(g_view);
            GotoView(3);
            return;
        }
        case HIT_RAILNEXT:                      // 导轨箭头：到头就回到开头，不藏任何一路
            g_r31_rail = g_r31_rail > 0 ? 0 : 999;
            g_relayout = true;
            return;
        case HIT_TOOLS31:
            if (g_view != 8) g_r31_trail.push_back(g_view);
            GotoView(8);
            return;
        case HIT_ACCOUNT31:                     // key＝要看哪一路账户
            g_r31_account = h.key;
            if (g_view != 7) g_r31_trail.push_back(g_view);
            GotoView(7);
            return;
        case HIT_MODELSTOG:
            g_r31_models = !g_r31_models;
            g_relayout = true;
            return;
        case HIT_SEARCHTOG:                     // 稿子 app.js:165：按下先回总览；关掉时清掉搜索词
            g_r31_search = !g_r31_search;
            g_r31_focus = g_r31_search ? 1 : 0;
            if (g_r31_search && g_view != 0) { g_r31_trail.clear(); GotoView(0); }
            if (!g_r31_search) g_r31_query.clear();
            g_relayout = true;
            return;
        case HIT_CLEARQ:                        // opt=2 是工具列表那颗：语义走 g_r31_toolq
            if (h.opt == 2) {
                g_r31_toolq.clear();
                g_r31_focus = 2;
            } else {
                g_r31_query.clear();
                g_r31_focus = 1;
            }
            g_relayout = true;
            return;
        case HIT_GEAR:
            if (g_view != 1) g_r31_trail.push_back(g_view);
            g_tab = 1;    // 设置屏底部标签栏要跟着亮「设置」（只切屏不切 tab 会亮着总览）
            GotoView(1);
            return;
        case HIT_EXPLAIN:
            g_r31_explain = !g_r31_explain;
            g_relayout = true;
            return;
        case HIT_ACCTSESS:                      // 账户页脚：把这个工具的会话筛出来
            g_r31_src = g_r31_account.empty() ? "all" : g_r31_account;
            g_r31_focus = 0;
            g_r31_trail.clear();
            GotoView(0);
            return;
        case HIT_PICKTOOL:                      // 工具列表选一路 → 总览按它筛
            g_r31_src = h.key;
            g_r31_focus = 0;
            g_r31_trail.clear();
            GotoView(0);
            return;
        case HIT_SEARCHFIELD:                   // opt＝1 会话搜索 / 2 工具搜索
            g_r31_focus = h.opt;
            return;
        case HIT_SHOWALL31:                     // 空态那颗：清掉来源与搜索，回到全部会话
            g_r31_src = "all";
            g_r31_query.clear();
            g_r31_toolq.clear();
            g_r31_focus = 0;
            g_r31_rail = 0;
            GotoView(0);
            return;
        case HIT_H31MORE:                       // 完成记录补位行：再翻一页
            g_r31_hist_n += 12;
            g_relayout = true;
            return;
        case HIT_QUEUEALL:                      // 首页队列：展开收起的待处理行
            g_r31_queue_all = true;
            g_relayout = true;
            return;
        case HIT_SUBTOG: {                      // 「N 条子对话」展开/收起（key＝父行身份）
            if (g_r31_subopen.count(h.key)) g_r31_subopen.erase(h.key);
            else g_r31_subopen.insert(h.key);
            g_relayout = true;
            return;
        }
        case HIT_HISTORY31:                     // 总览页脚「今日完成」
            if (g_view != 5) g_r31_trail.push_back(g_view);
            GotoView(5);
            return;
        case HIT_BACK:                          // 稿子 data-act="back"：回到你刚才所在的标签，不是一律回总览
            GotoView(g_tab);
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
        case HIT_SET: {                        // 设置屏的一颗：改档 + 落盘 + 该生效的立刻生效
            // 防抖：现在人人都会下意识双击，开关是单击语义——两连击等于翻过去又翻回来，
            // 屏上看起来就是「点不动」。同一颗 0.4s 内的第二击忽略。
            static long lastZid = -1;
            static double lastAt = 0;
            const double now2 = glfwGetTime();
            if (lastZid == h.zid && now2 - lastAt < 0.4) { lastAt = now2; return; }
            lastZid = h.zid;
            lastAt = now2;
            FlipSet(h.row, h.opt);
            return;
        }
        default: {                      // HIT_OPEN：R31 的主点击＝直接去那会话；ⓘ 才是详情
            // 出图轮不许推进已读水位线（那是会落盘的状态），但回执那句话仍要能拍下来。
            if (!g_shot_to) {
                if (const UIRow* r = SelectedRow(h.key)) MarkSeen(*r);
            }
            JumpAndReport(h);
            return;
        }
    }
}

static long g_press = -1;               // 按下时命中的那颗区，抬起时要在同一颗里才算点击
static bool g_was_down = false;         // 上一帧的按下态：抬起那一帧才判一次点击
// 行的「打开」改双击触发（2026-10-28 产品拍板：单击误触的代价是面板收起+跳走，太重）。
// 记上一击的 zid 与时刻：同一颗 0.5s 内再击一次才算打开；详情页那颗「前往会话」按钮
// 是明确意图（opt=1 标记），保持单击。ⓘ/芯片等小件本就是单击。
static long g_open_last_zid = -1;
static double g_open_last_at = 0;
/// 标题栏拖动发生过：松手的那一击不算点击（拖完不该触发行/按钮）。
static bool g_drag_moved = false;

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
// AGENTPING_CLICK 里的键盘步：text:<串> 打一串字、key:<名> 一按一放、hold:<名>:<帧> 按住若干帧。
// 有了它们，「点开搜索框→打字→清空／退格／Esc」这条链能在出图轮里整条跑完，
// 不必再去真的动他的桌面（2026-10-29 实测：合成按键会落进他正在用的 App）。
static bool IsKeyStep(const std::string& s) {
    return s.rfind("text:", 0) == 0 || s.rfind("key:", 0) == 0 || s.rfind("hold:", 0) == 0;
}
static ImGuiKey SeqKeyByName(const std::string& n) {
    if (n == "esc") return ImGuiKey_Escape;
    if (n == "enter") return ImGuiKey_Enter;
    if (n == "backspace") return ImGuiKey_Backspace;
    if (n == "tab") return ImGuiKey_Tab;
    return ImGuiKey_None;
}
// AGENTPING_CLICK_DELAY=毫秒：推迟序列的第一次按下。出图要等第二轮扫描（≥3s）才回读，
// 而回执只活 2.5s——不推迟就永远拍不到「复制成功」那条浮层（实测截图里只有选项没有回执）。
static double g_click_delay = []() {
    const char* e = getenv("AGENTPING_CLICK_DELAY");
    return e ? atof(e) / 1000.0 : 0.0;
}();

/// 真实指针为什么不取 io.MousePos：这块面板是系统浮层，指针在它上面时窗口往往不是 key，
/// GLFW 的 motion 回调只在窗口收到事件时才更新，浮层上实测会漏。Apple 侧直接问系统要全局
/// 指针位置再换算进窗口内容框——ImGui 的坐标系就是窗口的「点」尺寸（DisplaySize 取
/// glfwGetWindowSize，Retina 倍率走 DisplayFramebufferScale），而 CG 给的也是点，所以直接减。
/// 非 Apple 分支退回 io.MousePos（Linux 侧本机还没实机跑过，这条只是不阻断编译）。
/// 全局屏幕坐标的光标（y 向下，与 glfwGet/SetWindowPos 同一坐标系）。
/// 拖动窗口必须用它：窗口相对坐标在「窗口跟着动」时会自我反馈，一帧滞后就来回震荡。
static ImVec2 GlobalCursorNow() {
    ImVec2 p(-1.0e6f, -1.0e6f);
#ifdef __APPLE__
    CGEventRef ev = CGEventCreate(nullptr);
    if (ev) {
        const CGPoint g = CGEventGetLocation(ev);
        p = ImVec2((float)g.x, (float)g.y);
        CFRelease(ev);
    }
#endif
    return p;
}

static ImVec2 MousePos(const ImGuiIO& io) {
    ImVec2 p;
    // 序列还没走完就钉在当前这颗上；走完了钉在最后一颗（与单颗点击原本的行为一致：
    // 点完之后合成指针就留在那儿，出图轮的真指针另外被 g_shot_to 关掉）。
    if (!g_click_seq.empty()) {
        size_t i = g_click_at < (int)g_click_seq.size() ? (size_t)g_click_at : g_click_seq.size() - 1;
        while (i > 0 && IsKeyStep(g_click_seq[i])) i--;   // 键盘步不钉指针，停在上一颗鼠标上
        if (!IsKeyStep(g_click_seq[i]) && ResolveZone(g_click_seq[i].c_str(), p)) return p;
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
static void Utf8Append(std::string& s, unsigned int cp) {
    if (cp < 0x80) s.push_back((char)cp);
    else if (cp < 0x800) { s.push_back((char)(0xC0 | (cp >> 6))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        s.push_back((char)(0xE0 | (cp >> 12)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        s.push_back((char)(0xF0 | (cp >> 18)));
        s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    }
}
static void Utf8Pop(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    s.erase(i);
}

/// 逐码点过一段 UTF-8：丢掉控制字符与 Apple 的功能键私用区，遇换行就停（搜索框只收一行）。
template <typename F>
static void ForEachTextChar(const char* u8, F&& f) {
    for (const unsigned char* p = (const unsigned char*)u8; p && *p;) {
        unsigned int cp = 0;
        int n = 0;
        if (*p < 0x80) { cp = *p; n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { ++p; continue; }
        for (int i = 1; i < n && p[i]; ++i) cp = (cp << 6) | (p[i] & 0x3F);
        p += n;
        if (cp == '\n' || cp == '\r') return;
        if (cp < 32 || cp == 127) continue;
        if (cp >= 0xF700 && cp <= 0xF8FF) continue;
        f(cp);
    }
}

/// ImGui 的按下态只靠 GLFW 回调翻转。面板没拿到 key 状态时，抬起事件会被派给前台那个 App，
/// 这一侧就永远停在「按住」——之后每帧都当按住看，点击整条链路再也不发火（2026-10-28 实拍：
/// 面板收得到按下、收不到抬起，放大镜点不开第二次）。所以每帧向系统问一次真实按键态。
/// 非 Apple 分支不补：X11/Wayland 的抬起事件派给按下那颗窗口（隐式抓取），这条漏不了。
static bool RealLeftDown() {
#ifdef __APPLE__
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
#else
    return true;
#endif
}

#ifdef __APPLE__
// GLFW 把按键交给 AppKit 的输入法（interpretKeyEvents），字符只从 insertText 回来。
// 本机搜狗在这台机器上第一下按键后就连不上（IMKCFRunLoopWakeUpReliable 报错）：
// 组词态挂住、永远不 commit，之后每个键都是「按下有、字符无」——搜索框点得进去、
// 却一个字也打不出来（2026-10-28 实拍：z 之后 k 不进，marked=1 卡死）。
//
// 这块面板没有 AppKit 文本控件，输入全由自己从字符队列取，所以干脆不给这个视图
// 输入法上下文：interpretKeyEvents 变成空操作，键上的 characters 直接进队列。
// 代价是没法用拼音组词（组合窗本来也没地方画），中文搜索走 ⌘V 粘贴。
static IMP g_origKeyDown = nullptr;
static id NoInputContext(id, SEL) { return nil; }

// 常驻浮层在别的 App 前台时，macOS 会把第一下点击只当「激活进程」，不给这个视图送 mouseDown。
// 表现就是「图标点了没反应，得点第二次」。浮层本来就该一点即动，所以这颗视图收第一下点击。
static BOOL AcceptsFirstMouseOn(id, SEL, id) { return YES; }

static void KeyDownHook(id self, SEL _cmd, id ev) {
    ImGuiIO& io = ImGui::GetIO();
    const int before = io.InputQueueCharacters.Size;
    ((void (*)(id, SEL, id))g_origKeyDown)(self, _cmd, ev);
    if (io.InputQueueCharacters.Size != before) return;
    const unsigned long flags =
        ((unsigned long (*)(id, SEL))objc_msgSend)(ev, sel_registerName("modifierFlags"));
    if (flags & ((1UL << 20) | (1UL << 19) | (1UL << 18))) return;
    id s = ((id (*)(id, SEL))objc_msgSend)(ev, sel_registerName("characters"));
    const char* u8 =
        s ? ((const char* (*)(id, SEL))objc_msgSend)(s, sel_registerName("UTF8String")) : nullptr;
    if (!u8) return;
    ForEachTextChar(u8, [&](unsigned int cp) { io.AddInputCharacter(cp); });
}
#endif

static void HandleInput(const ImGuiIO& io, double now) {
    ImVec2 mp = MousePos(io);
    // R31 的两个搜索框自己收字：面板不给控件焦点，字符从 ImGui 的输入队列取。
    // 这个视图关掉了输入法上下文（见启动处 KeyDownHook）：直接上屏的字符才进队列，
    // 不做组词；中文走 ⌘V 粘贴（剪贴板是整段文字，不经过输入法）。
    bool escTaken = false;
    if (g_r31_focus) {
        std::string& target = (g_r31_focus == 2) ? g_r31_toolq : g_r31_query;
        for (ImWchar c : io.InputQueueCharacters)
            if (c >= 32 && c != 127) Utf8Append(target, (unsigned int)c);
        // macOS 的 Cmd 在 ImGui 里落在 KeyCtrl 上（ImGuiMod_Ctrl 即 Cmd），两个都收。
        if ((io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_V)) {
            if (const char* clip = glfwGetClipboardString(g_win))
                ForEachTextChar(clip, [&](unsigned int cp) { Utf8Append(target, cp); });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) Utf8Pop(target);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            g_r31_focus = 0;
            escTaken = true;   // 这一下归输入框：同帧不许再被下面「收搜索」那条吃掉
        }
        g_relayout = true;
    }
    // 稿子 app.js:177 的两条键盘路（「能补就补」档）：⌘/Ctrl+F 唤出搜索并聚焦；
    // Esc 在根屏收搜索、在子屏等于点「← 来处」。输入框收字时 Esc 仍归输入框。
    if (!g_r31_focus && !escTaken && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if ((g_view == 0 || g_view == 6 || g_view == 9) && g_r31_search) {
            g_r31_search = false;
            g_r31_query.clear();
            g_r31_focus = 0;
            g_relayout = true;
        } else if (g_view != 0 && g_view != 6 && g_view != 9) {
            Hit back_hit;
            back_hit.act = HIT_R31BACK;
            DoAction(back_hit, mp);
        }
    }
    if ((io.KeyCtrl || ImGui::IsKeyDown(ImGuiMod_Super)) && ImGui::IsKeyPressed(ImGuiKey_F)) {
        if (!g_r31_search) {
            g_r31_search = true;
            g_r31_trail.clear();
            GotoView(0);
        }
        g_r31_focus = 1;
        g_relayout = true;
    }
    // r20 的 .body 是个真滚动容器（overflow-y:auto 且滚动条隐藏），面板高度锁死 640。
    // 稿子那边由浏览器滚，这边只能自己算：滚轮一格 40pt（约三行正文），上限由绘制那一趟
    // 量出来（g_scrollMax，单趟绘制所以滞后一帧）。只有指针落在正文框里才响应——落在表头
    // 或标签栏上滚动会把内容推着走，那不是稿子的行为。
    if (io.MouseWheel != 0.0f && g_bodyValid[g_view] &&
        mp.x >= g_bodyTop[g_view].x && mp.x < g_bodyBot[g_view].x &&
        mp.y >= g_bodyTop[g_view].y && mp.y < g_bodyBot[g_view].y) {
        g_scrollTarget[g_view] = std::fmin(g_scrollMax[g_view],
                                           std::fmax(0.0f, g_scrollTarget[g_view] - io.MouseWheel * 40.0f));
    }
    // 取证轮要把长页（接线屏）的下半截拍下来，而合成指针不会滚滚轮：
    // 给一个「这一屏从多少偏移起画」的入口，只在 AGENTPING_SHOT 那轮生效。
    static const char* kScroll = getenv("AGENTPING_SCROLL");
    if (g_shot_to && kScroll) g_scroll[g_view] = g_scrollTarget[g_view] = std::atof(kScroll);
    static const char* kDown = getenv("AGENTPING_DOWN");
    bool down = g_shot_to ? false : io.MouseDown[0];
    // 出图轮只认合成输入：实测他人在桌面上的真点击会落在面板上（这块是系统浮层，340×993 占着
    // 右上角），一张「悬停」证据就这么被串成了「已读之后」。真指针照常驱动，只在拍图那一轮关掉。
    //
    // 一颗点击占 8 帧：第 1 帧按下，第 2 帧抬起，DoAction 在抬起之后的第 3 帧真发生
    // （见下面 g_was_down 那段），第 4~8 帧空转，让重摆行和 150ms 过渡都走完，
    // 下一颗才钉得到新版面而不是旧几何。
    if (!g_click_seq.empty() && g_click_at < (int)g_click_seq.size()) {
        // 等目标真的出现在命中区里再按：扫描落地前版面是空的，这时按下会落在空白上，
        // 八帧一过整条序列就被消耗光（实测「复制选项」的 toast 一直不来，日志里 0 次点击）。
        // 但只有按下 / 抬起 / 发火这三帧（gap 0–2）需要目标在场；之后五帧是冷却，
        // 点的若是「今日完成」这类一点就切屏的入口，目标在冷却期就没了，再要求它在场
        // 就会把 gap 冻在 3 上，整条序列永远走不完（实测连点三颗只发出第一颗）。
        // 另留一条兜底：目标 6 秒不出现就作废余下的颗，取证轮至少拍得出「走到哪算哪」那一屏。
        static double lastProgress = 0.0;
        static ImGuiKey seqHeld = ImGuiKey_None;
        const std::string& step = g_click_seq[(size_t)g_click_at];
        if (IsKeyStep(step) && now >= g_click_delay) {
            lastProgress = now;
            const size_t colon = step.find(':');
            const std::string kind = step.substr(0, colon);
            std::string arg = step.substr(colon + 1);
            int releaseAt = 1, frames = 8;
            if (kind == "hold") {
                const size_t c2 = arg.find(':');
                if (c2 != std::string::npos) { releaseAt = atoi(arg.c_str() + c2 + 1); arg.resize(c2); }
                frames = (std::max)(8, releaseAt + 2);
            }
            ImGuiIO& kio = ImGui::GetIO();
            if (g_click_gap == 0) {
                if (kind == "text")
                    ForEachTextChar(arg.c_str(), [&](unsigned int cp) { kio.AddInputCharacter(cp); });
                else if ((seqHeld = SeqKeyByName(arg)) != ImGuiKey_None)
                    kio.AddKeyEvent(seqHeld, true);
            } else if (seqHeld != ImGuiKey_None && g_click_gap >= releaseAt) {
                kio.AddKeyEvent(seqHeld, false);
                seqHeld = ImGuiKey_None;
            }
            if (g_click_gap >= frames - 1) {
                if (seqHeld != ImGuiKey_None) { kio.AddKeyEvent(seqHeld, false); seqHeld = ImGuiKey_None; }
                g_click_at++;
                g_click_gap = 0;
            } else {
                g_click_gap++;
            }
        } else {
        if (now < g_click_delay) lastProgress = now;   // 还在等 CLICK_DELAY 的那段不算「无进展」
        ImVec2 tp;
        const bool cooling = g_click_gap >= 3;
        const bool resolved = now >= g_click_delay &&
                              (cooling || (!g_hits.empty() &&
                                           ResolveZone(g_click_seq[g_click_at].c_str(), tp)));
        if (getenv("AGENTPING_CLICK_TRACE"))
            fprintf(stderr, "[序列] at=%d gap=%d resolved=%d mp=(%.0f,%.0f) press=%ld hits=%zu\n",
                    g_click_at, g_click_gap, resolved ? 1 : 0, mp.x, mp.y, g_press, g_hits.size());
        if (resolved) {
            lastProgress = now;
            down = (g_click_gap == 0);
            if (g_click_gap == 7) { g_click_at++; g_click_gap = 0; }
            else g_click_gap++;
        } else if (lastProgress > 0.0 && now - lastProgress > 6.0) {
            fprintf(stderr, "[点击] 「%s」6 秒未出现在命中区，序列作废\n",
                    g_click_seq[(size_t)g_click_at].c_str());
            fflush(stderr);
            g_click_at = (int)g_click_seq.size();
        }
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

    static bool poked = false;                  // 这一按已经叫过窗口，松开再复位
    if (!down) poked = false;
    if (down) {
        // 这块面板是常驻浮层（LSUIElement＋无边框窗口）。别的 App 在前台时点进来，
        // macOS 只把这一下当「激活进程」，GLFW 的窗口拿不到 key 状态——鼠标事件照收，
        // 键盘一个字节都不来：搜索框看着能点，其实打不出字（2026-10-28 实拍：连打 5 键，
        // 收字 0 次）。按下那一帧自己把窗口叫成 key，之后的按键与抬起才归这里。
        if (!poked && !g_shot_to) {
            poked = true;
            if (!glfwGetWindowAttrib(g_win, GLFW_FOCUSED)) glfwFocusWindow(g_win);
        }
        g_press = -1;
        for (size_t i = g_hits.size(); i-- > 0;)      // 从后往前：按钮登记在行之后，优先级天然高于行
            if (Inside(g_hits[i], mp)) {
                g_press = g_hits[i].zid;
                if (g_hits[i].act != HIT_SEARCHFIELD) g_r31_focus = 0;
                break;
            }
        if (getenv("AGENTPING_INPUT_TRACE")) {
            int wx = 0, wy = 0;
            glfwGetWindowPos(g_win, &wx, &wy);
            fprintf(stderr, "[输入] 按下 mp=(%.0f,%.0f) win=(%d,%d) press=%ld hits=%zu\n", mp.x,
                    mp.y, wx, wy, g_press, g_hits.size());
        }
    } else if (!g_was_down && !g_drag_moved) {
        for (size_t i = g_hits.size(); i-- > 0;)      // 抬起也得落在同一颗上才算一次点击
            if (g_hits[i].zid == g_press && Inside(g_hits[i], mp)) {
                if (g_hits[i].act == HIT_OPEN && g_hits[i].opt == 0 &&
                    !(g_open_last_zid == g_hits[i].zid && now - g_open_last_at < 0.5)) {
                    g_open_last_zid = g_hits[i].zid;   // 第一击：只记账，不动作（悬停高亮已给反馈）
                    g_open_last_at = now;
                } else {
                    g_open_last_zid = -1;
                    DoAction(g_hits[i], mp);
                }
                break;
            }
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
/// 日韩文与全角标点按字断，拉丁词整体挪行——不然 "Notification" 会在中间被切两半。
static bool IsCjkCp(unsigned int cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0x20000 && cp <= 0x2FA1F);
}

static std::vector<std::string> WrapLines(const std::string& s, float maxw, int locale, int tier, int maxlines) {
    auto cps = DecodeUTF8(s.c_str());
    struct Tok { std::string s; float w; bool space; };
    std::vector<Tok> toks;
    bool pending_space = false;
    std::string run;
    auto flush_run = [&]() {
        if (run.empty()) return;
        toks.push_back({run, TextWidth(run, locale, tier), pending_space});
        pending_space = false;
        run.clear();
    };
    auto cp_at = [&](size_t i) {
        unsigned int cp = cps[i];
        char buf[5] = {}; size_t n = 0;
        if (cp < 0x80) buf[n++] = (char)cp;
        else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                 buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
               buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        return std::string(buf, n);
    };
    for (size_t i = 0; i < cps.size(); i++) {
        const unsigned int cp = cps[i];
        if (cp == ' ') { flush_run(); pending_space = true; continue; }
        if (IsCjkCp(cp)) {
            flush_run();
            const std::string ch = cp_at(i);
            toks.push_back({ch, TextWidth(ch, locale, tier), pending_space});
            pending_space = false;
            continue;
        }
        run += cp_at(i);
    }
    flush_run();

    std::vector<std::string> out;
    std::string line; float lw = 0;
    const float spaceW = TextWidth(" ", locale, tier);
    auto push = [&]() {
        if (!line.empty()) out.push_back(line);
        line.clear(); lw = 0;
    };
    for (size_t i = 0; i < toks.size(); i++) {
        const Tok& t = toks[i];
        const float need = (line.empty() ? 0.0f : (t.space ? spaceW : 0.0f)) + t.w;
        if (!line.empty() && lw + need > maxw && (int)out.size() < maxlines - 1) {
            push();
            if ((int)out.size() == maxlines - 1) {   // 已到最后一行的位置：剩下的按行宽裁
                std::string rest = t.s;
                for (size_t j = i + 1; j < toks.size(); j++) rest += (toks[j].space ? " " : "") + toks[j].s;
                out.push_back(ClipToWidth(rest, maxw, locale, tier));
                return out;
            }
        }
        if (line.empty() && t.w > maxw) {            // 单个词就比行宽还长：裁掉并另起一行
            out.push_back(ClipToWidth(t.s, maxw, locale, tier));
            if ((int)out.size() >= maxlines) return out;
            continue;
        }
        if (line.empty()) { line = t.s; lw = t.w; }
        else {
            if (t.space) { line += ' '; lw += spaceW; }
            line += t.s; lw += t.w;
        }
    }
    push();
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

// ─────────────────────────────────────────────────────────────────────────────
// r20 的接线／提问详情／授权详情／历史四屏。
// 版式逐条对着 interaction.src.html：屏头 → 来源卡 → 正文 → 键值行 → 脚注 → 整宽按钮。
// 只摆活数据：没有提问原文就整块不画、今天没有完成会话就不摆轨卡，不放「演示」占位。
// ─────────────────────────────────────────────────────────────────────────────
static const int kHistRow = 95;        // 历史翻页那条入口占 95：避开 90..94 的导航位
static const int kHookRowBase = 300;   // 接线行占 300..305，与 SetCtl 的 0..7 不撞

static const char* ViewTag(int v) {
    switch (v) {
        case 1: return "set";
        case 2: return "setup";
        case 3: return "det";
        case 4: return "perm";
        case 5: return "hist";
        case 6: return "usage";
        case 7: return "account";
        case 8: return "tools";
        case 9: return "claims";
        default: return "pin";
    }
}
static const char* ViewName(int v) {
    switch (v) {
        case 1: return "设置";
        case 2: return "接线";
        case 3: return "会话详情";
        case 4: return "授权详情";
        case 5: return "完成记录";
        case 6: return "用量与额度";
        case 7: return "工具用量";
        case 8: return "全部工具";
        case 9: return "权益领取";
        default: return "会话总览";
    }
}

/// 接线状态每 2 秒重探一次：读两份小配置，够便宜；点「写入」后立刻强制重探。
/// 用 glfwGetTime 而不是 tn —— 取证时 AGENTPING_TN 会把动画钟冻住，冻住的钟不能拿来判缓存过期。
static void RefreshHooks(bool force) {
    const double now = glfwGetTime();
    if (!force && g_hooks_at >= 0 && now - g_hooks_at < 2.0) return;
    g_hooks = ap::probe_hooks();
    g_hooks_at = now;
}
/// 有可修的接线问题才在导航那颗上点灯：脚本路径失效，或某家能写却还没接。
static bool HookAttention() {
    for (const auto& h : g_hooks) {
        if (h.stale) return true;
        if (h.supported && !h.installed && !h.occupied) return true;
    }
    return false;
}
static const ap::HookAgent* HookOf(const char* id) {
    for (const auto& h : g_hooks) if (h.id == id) return &h;
    return nullptr;
}
static const Tool* AgentTool(const char* id) {
    for (const auto& t : g_pd.tools) if (t.id == id) return &t;
    return nullptr;
}
static std::string TildePath(const std::string& p) {
    const char* h = getenv("HOME");
    if (h && *h) {
        std::string home = h;
        if (p.compare(0, home.size(), home) == 0) return "~" + p.substr(home.size());
    }
    return p;
}
static void Toast(const std::string& a, const std::string& b, bool go) {
    g_toast = a;
    g_toast_sub = b;
    g_toast_at = glfwGetTime();
    g_toast_go = go;
    g_toast_icon = 3;
}
static void DrawCheck(ImDrawList* dl, float x, float cy, ImU32 col, float s = 1.0f) {
    dl->AddLine(ImVec2(x - 4.5f * s, cy), ImVec2(x - 1.2f * s, cy + 3.2f * s), col, 1.5f * s);
    dl->AddLine(ImVec2(x - 1.2f * s, cy + 3.2f * s), ImVec2(x + 5.0f * s, cy - 3.6f * s), col, 1.5f * s);
}
static void CopyGlyph(ImDrawList* dl, float x, float cy, ImU32 col, ImU32 bg) {
    dl->AddRect(ImVec2(x, cy - 5.0f), ImVec2(x + 7.0f, cy + 3.0f), col, 1.5f, 0, 1.0f);
    dl->AddRectFilled(ImVec2(x + 3.0f, cy - 3.0f), ImVec2(x + 10.5f, cy + 5.0f), bg, 1.5f);
    dl->AddRect(ImVec2(x + 3.0f, cy - 3.0f), ImVec2(x + 10.5f, cy + 5.0f), col, 1.5f, 0, 1.0f);
}
/// 屏头：左上角微标签写这一屏叫什么，右沿一颗「返回」。四屏共用同一个落点。
static float ScreenHeader(ImDrawList* dl, float L, float R, float cy, int loc, const char* titleKey) {
    const float track = kTierPx[T_MLBL] * 0.16f;
    const float pw = GhostPillW("back", loc);
    DrawTextRun(dl, L, CellTop(cy, kSegH, kHead10Line, T_MLBL, loc),
                UpAscii(T(titleKey, loc)), loc, kStIdle, T_MLBL, track);
    GhostPill(dl, R - pw, cy, pw, loc, "back", Zid(loc, kNavRow, HIT_BACK, 0));
    AddHit(g_hits, R - pw, cy, R, cy + kSegH, HIT_BACK, kNavRow, 0, loc, T("back", loc), 0);
    return cy + kSegH + kSectionGap;
}
static void StepBadge(ImDrawList* dl, float x, float cy, int num, bool done, int loc) {
    const float r = 9.0f;
    if (done) dl->AddCircleFilled(ImVec2(x + r, cy), r, WithA(kStDone, 30));
    dl->AddCircle(ImVec2(x + r, cy), r - 0.5f, WithA(done ? kStDone : kStIdle, done ? 150 : 90), 20, 1.0f);
    if (done) { DrawCheck(dl, x + r, cy, kStDone, 0.8f); return; }
    char b[8]; snprintf(b, sizeof b, "%d", num);
    DrawTextRun(dl, x + r - TextWidth(b, loc, T_MLBL) * 0.5f, TextMid(cy, T_MLBL, loc), b, loc, kStIdle, T_MLBL);
}
static float MiniBtnW(const char* key, int loc) {
    return 2 * 7.0f + 2 * 1.0f + TextWidth(T(key, loc), loc, T_BTN);
}
static void MiniBtn(ImDrawList* dl, float x, float y, const char* key, int loc, long z) {
    const float w = MiniBtnW(key, loc), h = 20.0f;
    const float hv = TxPeek(z);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                      MixA(WithA(kPrimary, 22), WithA(kPrimary, 40), hv), 5.0f);
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f),
                WithA(kPrimary, (int)(90 + 80 * hv)), 5.0f, 0, 1.0f);
    const std::string lab = T(key, loc);
    DrawTextRun(dl, x + (w - TextWidth(lab, loc, T_BTN)) * 0.5f, TextMid(y + h * 0.5f, T_BTN, loc),
                lab, loc, kPrimary, T_BTN);
}
/// 一行「键 74pt ＋ 值折两行」，上沿一条 hairline。值来自磁盘，长度不设防。
static float KvRow(ImDrawList* dl, float L, float R, float y, const char* key, const std::string& value,
                   int loc, bool hairTop) {
    const float keyW = 74.0f, padY = 7.0f, lineH = kTierPx[T_SM] * 1.5f;
    // 空值整行不画：行高只有 14pt，硬画一条空键值会和下一条压在同一行（实测「项目」压在
    // 「来源」上）。没有这个字段就当作没有这一行，不拿空串占位。
    if (value.empty()) return 0.0f;
    if (hairTop) dl->AddLine(ImVec2(L, y), ImVec2(R, y), kHair, 1.0f);
    auto lines = WrapLines(value, R - (L + keyW), loc, T_SM, 2);
    float yy = y + padY;
    DrawTextRun(dl, L, TextTop(yy, lineH, T_SM, loc), T(key, loc), loc, kStIdle, T_SM);
    for (const auto& ln : lines) {
        DrawTextRun(dl, L + keyW, TextTop(yy, lineH, T_SM, loc), ln, loc, kFg, T_SM);
        yy += lineH;
    }
    return (yy - y) + padY;
}
/// 详情两屏顶上的来源卡：头像、名字、项目·会话号，右沿一颗状态点＋状态词。
static float SourceCard(ImDrawList* dl, float L, float R, float y, const UIRow& row,
                        const char* statusKey, ImU32 statusCol, int loc) {
    const float h = 44.0f;
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + h), kSurface1, 10.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + h - 0.5f), kHair, 10.0f, 0, 1.0f);
    const float acx = L + 12.0f + 11.0f, acy = y + h * 0.5f;
    GradCircle(dl, ImVec2(acx, acy), 11.0f, WithA(row.hue, 66), WithA(row.hue, 20), WithA(row.hue, 140));
    DrawTextRun(dl, acx - TextWidth(row.initial, loc, T_GLYPH9) * 0.5f, TextMid(acy, T_GLYPH9, loc),
                row.initial, loc, row.hue, T_GLYPH9);
    const std::string st = T(statusKey, loc);
    const float stw = TextWidth(st, loc, T_MLBL) + 12.0f;
    const float rx = R - 12.0f;
    const float tx = acx + 11.0f + 8.0f;
    const float nameLine = kTierPx[T_TOOL] * 1.5f, subLine = kTierPx[T_MICRO] * 1.5f;
    DrawTextRun(dl, tx, y + 6.0f, ClipToWidth(row.agent, rx - stw - 8.0f - tx, loc, T_TOOL),
                loc, kFg, T_TOOL);
    std::string sub = row.project + (row.native_id.empty() ? "" : " · " + row.native_id);
    DrawTextRun(dl, tx, y + 6.0f + nameLine, ClipToWidth(sub, rx - 8.0f - tx, loc, T_MICRO),
                loc, kMutedFg, T_MICRO);
    dl->AddCircleFilled(ImVec2(rx - stw + 3.0f, acy), 3.0f, statusCol);
    DrawTextRun(dl, rx - stw + 9.0f, TextMid(acy, T_MLBL, loc), st, loc, statusCol, T_MLBL);
    return h;
}
static float GoButton(ImDrawList* dl, float L, float R, float y, const std::string& text, int loc,
                      const UIRow& row) {
    const float h = 32.0f;
    const long z = Zid(loc, 200, HIT_JUMP, 0);
    const float hv = TxPeek(z);
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + h),
                      MixA(WithA(kPrimary, 22), WithA(kPrimary, 40), hv), 8.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + h - 0.5f),
                WithA(kPrimary, (int)(90 + 80 * hv)), 8.0f, 0, 1.0f);
    DrawTextRun(dl, (L + R - TextWidth(text, loc, T_LABEL)) * 0.5f, TextMid(y + h * 0.5f, T_LABEL, loc),
                text, loc, kPrimary, T_LABEL);
    AddHit(g_hits, L, y, R, y + h, HIT_JUMP, 200, 0, row, loc, text);
    return h;
}
/// 历史轨上的时刻用本机时区，与面板其它「多久以前」同一把钟。
static std::string ClockOf(double epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm tv;
    localtime_r(&t, &tv);
    char b[8];
    snprintf(b, sizeof b, "%02d:%02d", tv.tm_hour, tv.tm_min);
    return b;
}

// ── 接线屏 ──────────────────────────────────────────────────────────────────
// 两步照 r20：第一步是只读检测（每条路径与今日会话数都来自这一轮 scan），
// 第二步把 Notification 钩子写进 CLI 配置。写入由 hook_install.cpp 干，写前备份、原子落盘；
// 面板这边的按钮只负责「叫他写」并把成败回执摆出来。
static float drawSetup(ImDrawList* dl, float L, float R, float maxBot, int loc, float cy) {
    const float track = kTierPx[T_MLBL] * 0.16f;
    cy = ScreenHeader(dl, L, R, cy, loc, "nav_setup");

    // 开场：一句它做什么，一句为什么要接两步（稿子的 hero 没有图，这里也不摆假图）
    {
        const float lineH = kTierPx[T_TITLE] * 1.5f;
        DrawTextRun(dl, L, cy, T("setup_hero", loc), loc, kFg, T_TITLE);
        cy += lineH + 4.0f;
        for (const auto& ln : WrapLines(T("setup_lead", loc), R - L, loc, T_SM, 3)) {
            DrawTextRun(dl, L, cy, ln, loc, kMutedFg, T_SM);
            cy += kTierPx[T_SM] * 1.5f;
        }
        cy += 10.0f;
    }

    static const struct { const char* id; const char* path; } kDetect[] = {
        {"qoder", "~/.qoder/projects"},
        {"claude", "~/.claude/projects"},
        {"codex", "~/.codex/sessions"},
        {"zcode", "~/.zcode/cli/db/db.sqlite"},
        {"workbuddy", "~/.workbuddy/workbuddy.db"},
        {"opencode", "~/.local/share/opencode/opencode.db"},
    };
    int avail = 0;
    for (const auto& d : kDetect) {
        const Tool* t = AgentTool(d.id);
        if (t && t->available) avail++;
    }

    auto stepHead = [&](const char* key, int num, const std::string& right, bool done) {
        StepBadge(dl, L, cy + 9.0f, num, done, loc);
        DrawTextRun(dl, L + 26.0f, CellTop(cy, 18.0f, kHead10Line, T_LABEL, loc),
                    T(key, loc), loc, kFg, T_LABEL);
        if (!right.empty())
            DrawTextRight(dl, R, CellTop(cy, 18.0f, kHead10Line, T_MLBL, loc), right, loc, kStIdle, T_MLBL);
        cy += 18.0f + 6.0f;
    };
    char av[32];
    snprintf(av, sizeof av, T("setup_avail", loc), avail);
    stepHead("setup_step1", 1, av, avail > 0);

    for (int i = 0; i < (int)(sizeof kDetect / sizeof kDetect[0]); i++) {
        const Tool* t = AgentTool(kDetect[i].id);
        const std::string name = t ? t->name : kDetect[i].id;
        const std::string initial = t ? t->initial : InitialOf(kDetect[i].id);
        const ImU32 hue = t ? t->hue : kMutedFg;
        const bool ok = t && t->available && !t->unreadable && !t->quiet;
        std::string st;
        ImU32 stc = kStIdle;
        if (!t || !t->available) st = T("absent", loc);
        else if (t->unreadable) { st = T("unreadable", loc); stc = WithA(kStFail, 210); }
        else if (t->quiet) st = T("quiet_agents", loc);
        else {
            char nb[48];
            snprintf(nb, sizeof nb, T("det_sess_n", loc), t->rows);
            st = nb;
            stc = kStDone;
        }
        if (i) dl->AddLine(ImVec2(L, cy), ImVec2(R, cy), kHair, 1.0f);
        const float acx = L + 9.0f, acy = cy + 16.0f;
        GradCircle(dl, ImVec2(acx, acy), 8.0f, WithA(hue, t ? 60 : 24), WithA(hue, t ? 18 : 8),
                   WithA(hue, t ? 130 : 60));
        DrawTextRun(dl, acx - TextWidth(initial, loc, T_GLYPH9) * 0.5f, TextMid(acy, T_GLYPH9, loc),
                    initial, loc, t && t->available ? hue : kStIdle, T_GLYPH9);
        const float stw = TextWidth(st, loc, T_MLBL) + (ok ? 10.0f : 0.0f);
        const float sx = R - 8.0f - stw;
        if (ok) DrawCheck(dl, sx + 2.0f, acy, kStDone, 0.7f);
        DrawTextRun(dl, sx + (ok ? 10.0f : 0.0f), TextMid(acy, T_MLBL, loc), st, loc, stc, T_MLBL);
        DrawTextRun(dl, L + 22.0f, cy + 3.0f, ClipToWidth(name, sx - 8.0f - (L + 22.0f), loc, T_TOOL),
                    loc, kFg, T_TOOL);
        DrawTextRun(dl, L + 22.0f, cy + 17.0f,
                    MiddleClipPath(kDetect[i].path, sx - 8.0f - (L + 22.0f), loc, T_MICRO),
                    loc, kMutedFg, T_MICRO);
        cy += 32.0f;
    }
    cy += 12.0f;

    stepHead("setup_step2", 2, "hook", false);
    for (const auto& ln : WrapLines(T("setup_hooknote", loc), R - L, loc, T_SM, 4)) {
        DrawTextRun(dl, L, cy, ln, loc, kMutedFg, T_SM);
        cy += kTierPx[T_SM] * 1.5f;
    }
    cy += 6.0f;

    static const char* kWrite[] = {"claude", "qoder", "codex"};
    for (int i = 0; i < 3; i++) {
        const ap::HookAgent* h = HookOf(kWrite[i]);
        const Tool* t = AgentTool(kWrite[i]);
        const std::string name = h ? h->name : (t ? t->name : kWrite[i]);
        const std::string initial = t ? t->initial : InitialOf(kWrite[i]);
        const ImU32 hue = t ? t->hue : HueFor(name);
        ImU32 dot = kStIdle;
        const char* stKey = "setup_none";
        if (h && h->occupied) { dot = kStAwait; stKey = "setup_occupied"; }
        else if (h && h->stale) { dot = kStAwait; stKey = "setup_stale"; }
        else if (h && h->installed) { dot = kStDone; stKey = "setup_ok"; }
        else if (h && !h->supported) { dot = kStIdle; stKey = "setup_manual"; }
        const bool btn = h && h->supported;
        const char* btnKey = (h && (h->installed || h->stale)) ? "setup_rewrite" : "setup_install";
        const float bw = btn ? MiniBtnW(btnKey, loc) : 0.0f;
        const float acx = L + 10.0f, acy = cy + 21.0f;
        GradCircle(dl, ImVec2(acx, acy), 9.0f, WithA(hue, 60), WithA(hue, 18), WithA(hue, 130));
        DrawTextRun(dl, acx - TextWidth(initial, loc, T_GLYPH9) * 0.5f, TextMid(acy, T_GLYPH9, loc),
                    initial, loc, hue, T_GLYPH9);
        const float rx = btn ? R - 8.0f - bw : R - 8.0f;   // 按钮左沿
        const std::string st = T(stKey, loc);
        // 状态列宽按「到按钮左边为止」算，不再钉死 80pt：意/德的状态词更长，
        // 钉死会把「Script mancante」裁成半句。名字的右界让到状态左侧，免得两列压字。
        const float stRight = btn ? rx - 6.0f : R - 8.0f;
        const std::string stShown =
            btn ? ClipToWidth(st, stRight - (L + 26.0f) - 8.0f, loc, T_MICRO) : st;
        const float nameRight = btn ? stRight - TextWidth(stShown, loc, T_MICRO) - 10.0f : rx - 8.0f;
        DrawTextRun(dl, L + 26.0f, cy + 5.0f, ClipToWidth(name, nameRight - (L + 26.0f), loc, T_TOOL),
                    loc, kFg, T_TOOL);
        DrawTextRun(dl, L + 26.0f, cy + 22.0f,
                    MiddleClipPath(h ? TildePath(h->config) : std::string(), rx - 6.0f - (L + 26.0f), loc, T_MICRO),
                    loc, kMutedFg, T_MICRO);
        if (btn) {
            const long z = Zid(loc, kHookRowBase + i, HIT_HOOKINST, 0);
            DrawTextRight(dl, stRight, cy + 5.0f, stShown, loc, dot, T_MICRO);
            MiniBtn(dl, R - 8.0f - bw, cy + 11.0f, btnKey, loc, z);
            Hit hh;
            hh.x0 = R - 8.0f - bw; hh.y0 = cy + 11.0f; hh.x1 = R - 8.0f; hh.y1 = cy + 31.0f;
            hh.act = HIT_HOOKINST; hh.row = kHookRowBase + i; hh.zid = z;
            hh.agent_id = kWrite[i]; hh.title = name; hh.label = T(btnKey, loc);
            g_hits.push_back(hh);
        } else {
            dl->AddCircleFilled(ImVec2(R - 8.0f - TextWidth(st, loc, T_MLBL), cy + 5.0f + kHead10Line * 0.5f),
                                3.0f, dot);
            DrawTextRight(dl, R - 8.0f, cy + 5.0f, st, loc, dot, T_MICRO);
        }
        cy += 42.0f;
    }
    cy += 2.0f;
    for (const auto& ln : WrapLines(T("setup_nohook", loc), R - L, loc, T_MICRO, 2)) {
        DrawTextRun(dl, L, cy, ln, loc, kMicro, T_MICRO);
        cy += kTierPx[T_MICRO] * 1.5f;
    }
    cy += 6.0f;

    // 第三步：辅助功能。会话级那一跳（点 Qoder 托盘里的条目、点 ZCode 侧栏里的任务行）
    // 全靠这一档，不授权就只能把 App 带到前台。Linux 上 ap_ax_trusted 返回 4＝不适用，
    // 整块不画：显示成「未授权」是催他去拨一个这儿根本不存在的开关。
    char axr[192];
    const int axrc = ap_ax_trusted(0, axr, (int)sizeof axr);
    if (axrc != 4) {
        const bool axok = axrc == 0;
        stepHead("setup_step3", 3, T(axok ? "setup_ax_on" : "setup_ax_off", loc), axok);
        for (const auto& ln : WrapLines(T("setup_axnote", loc), R - L, loc, T_SM, 3)) {
            DrawTextRun(dl, L, cy, ln, loc, kMutedFg, T_SM);
            cy += kTierPx[T_SM] * 1.5f;
        }
        cy += 6.0f;
        if (!axok) {
            const float bw = MiniBtnW("setup_ax_go", loc);
            const long z = Zid(loc, kHookRowBase + 6, HIT_AXGO, 0);
            MiniBtn(dl, R - 8.0f - bw, cy, "setup_ax_go", loc, z);
            Hit hh;
            hh.x0 = R - 8.0f - bw; hh.y0 = cy; hh.x1 = R - 8.0f; hh.y1 = cy + 20.0f;
            hh.act = HIT_AXGO; hh.row = kHookRowBase + 6; hh.zid = z;
            hh.label = T("setup_ax_go", loc);
            g_hits.push_back(hh);
            cy += 20.0f + 6.0f;
        }
    }

    // 下面三块是解释性的，屏幕真矮时按「路径 → 备份说明 → 等待」的顺序让位，
    // 不挤掉上面的检测与写入按钮（那两块才是这一屏要干的事）。
    const float boxH = 46.0f;
    if (cy + boxH <= maxBot) {
        dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + boxH), kSurface2, 8.0f);
        dl->AddRect(ImVec2(L + 0.5f, cy + 0.5f), ImVec2(R - 0.5f, cy + boxH - 0.5f), Over(kSurface2, kHair),
                    8.0f, 0, 1.0f);
        const float lx = L + 10.0f, ly = cy + 8.0f;
        const char* labels[2] = {"setup_writer", "setup_inbox"};
        const std::string vals[2] = {TildePath(ap::hook_script_path()), TildePath(ap::hook_inbox_dir())};
        for (int i = 0; i < 2; i++) {
            const std::string lab = T(labels[i], loc);
            DrawTextRun(dl, lx, ly + i * 16.0f, lab, loc, kStIdle, T_MICRO);
            const float px = lx + TextWidth(lab, loc, T_MICRO) + 8.0f;
            DrawTextRun(dl, px, ly + i * 16.0f,
                        MiddleClipPath(vals[i], R - 10.0f - px, loc, T_MICRO), loc, kFg, T_MICRO);
        }
        cy += boxH + 8.0f;
    }
    // 这句在德/意/日比中文长一截：一行放不下就折成两行，不裁半句。
    const auto noteLines = WrapLines(T("setup_note", loc), R - L, loc, T_MICRO, 2);
    const float noteH = kTierPx[T_MICRO] * 1.5f * (float)noteLines.size();
    if (cy + noteH <= maxBot) {
        for (const auto& ln : noteLines) {
            DrawTextRun(dl, L, cy, ln, loc, kMicro, T_MICRO);
            cy += kTierPx[T_MICRO] * 1.5f;
        }
        cy += 8.0f;
    }
    const float waitH = kTierPx[T_SM] * 1.5f * 2.0f + 8.0f;
    if (cy + waitH <= maxBot) {
        const double tn = glfwGetTime();
        const float e = PingK(std::fmod(tn, 1.9), 1.9f, kEaseInOut);
        dl->AddCircleFilled(ImVec2(L + 4.0f, cy + 7.0f), 3.0f, WithA(kPrimary, (int)(120 + 120 * e)));
        dl->AddCircleFilled(ImVec2(L + 4.0f, cy + 7.0f), 6.0f, WithA(kPrimary, (int)(10 + 30 * e)));
        for (const auto& ln : WrapLines(T("setup_wait", loc), R - (L + 16.0f), loc, T_SM, 2)) {
            DrawTextRun(dl, L + 16.0f, cy, ln, loc, kMutedFg, T_SM);
            cy += kTierPx[T_SM] * 1.5f;
        }
        cy += 8.0f;
    }
    return cy;
}

// ── 提问详情 ────────────────────────────────────────────────────────────────
static float drawDetail(ImDrawList* dl, float L, float R, float maxBot, int loc, float cy) {
    const float track = kTierPx[T_MLBL] * 0.16f;
    cy = ScreenHeader(dl, L, R, cy, loc, "det_title");
    const UIRow* r = SelectedRow(g_sel_key);
    if (!r) {
        DrawTextRun(dl, L, cy, T("det_empty", loc), loc, kMutedFg, T_SM);
        return cy + kTierPx[T_SM] * 1.5f;
    }
    const bool asks = RowAsksQuestion(*r);
    cy += SourceCard(dl, L, R, cy, *r, asks ? "in_wait" : "await_confirm",
                     asks ? kStRun : kStAwait, loc) + 10.0f;

    const std::string q = r->question.empty() ? r->title : r->question;
    const float qLine = kTierPx[T_HERO] * 1.5f;
    for (const auto& ln : WrapLines(q, R - L, loc, T_HERO, 5)) {
        DrawTextRun(dl, L, cy, ln, loc, kFg, T_HERO);
        cy += qLine;
    }
    cy += 8.0f;

    if (!r->options.empty()) {
        char ol[32];
        snprintf(ol, sizeof ol, T("det_opt_count", loc), (int)r->options.size());
        DrawTextRun(dl, L, cy, ol, loc, kStIdle, T_MLBL, track);
        cy += kHead10Line + 6.0f;
        for (size_t i = 0; i < r->options.size() && i < 4; i++) {
            const long z = Zid(loc, (int)i, HIT_COPY, 0);
            const float hv = TxPeek(z);
            dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + 34.0f),
                              MixA(kSurface1, kSurface2, hv), 8.0f);
            dl->AddRect(ImVec2(L + 0.5f, cy + 0.5f), ImVec2(R - 0.5f, cy + 33.5f), kHair, 8.0f, 0, 1.0f);
            dl->AddRectFilled(ImVec2(L + 8.0f, cy + 9.0f), ImVec2(L + 24.0f, cy + 25.0f), kSurface2, 4.0f);
            char kn[8];
            snprintf(kn, sizeof kn, "%d", (int)i + 1);
            DrawTextRun(dl, L + 16.0f - TextWidth(kn, loc, T_MLBL) * 0.5f, TextMid(cy + 17.0f, T_MLBL, loc),
                        kn, loc, kMutedFg, T_MLBL);
            DrawTextRun(dl, L + 32.0f, TextMid(cy + 17.0f, T_TOOL, loc),
                        ClipToWidth(r->options[i], R - 32.0f - (L + 32.0f) - 20.0f, loc, T_TOOL),
                        loc, kFg, T_TOOL);
            CopyGlyph(dl, R - 20.0f, cy + 17.0f, WithA(kFg, (int)(110 + 100 * hv)), kSurface1);
            AddHit(g_hits, L, cy, R, cy + 34.0f, HIT_COPY, (int)i, 0, *r, loc, r->options[i]);
            g_hits.back().title = r->project;
            cy += 34.0f + 6.0f;
        }
        cy += 2.0f;
    }

    cy += KvRow(dl, L, R, cy, "det_session",
                r->native_id.empty() ? r->key : r->native_id, loc, true);
    cy += KvRow(dl, L, R, cy, "det_wait", Ago(r->age, loc), loc, false);
    cy += KvRow(dl, L, R, cy, "det_source", T("det_source_val", loc), loc, false);
    cy += 8.0f;

    for (const auto& ln : WrapLines(T("det_no_submit", loc), R - L, loc, T_SM, 2)) {
        DrawTextRun(dl, L, cy, ln, loc, kMutedFg, T_SM);
        cy += kTierPx[T_SM] * 1.5f;
    }
    cy += 8.0f;

    const std::string jb = T(LandGoKey(r->agent_id), loc);
    cy += GoButton(dl, L, R, cy, jb, loc, *r);
    return cy;
}

// ── 授权详情 ────────────────────────────────────────────────────────────────
static float drawPermission(ImDrawList* dl, float L, float R, float maxBot, int loc, float cy) {
    cy = ScreenHeader(dl, L, R, cy, loc, "perm_title");
    const UIRow* r = SelectedRow(g_sel_key);
    if (!r) {
        DrawTextRun(dl, L, cy, T("perm_empty", loc), loc, kMutedFg, T_SM);
        return cy + kTierPx[T_SM] * 1.5f;
    }
    cy += SourceCard(dl, L, R, cy, *r, "perm_status", kStFail, loc) + 10.0f;

    DrawTextRun(dl, L, cy, T("perm_message", loc), loc, kStIdle, T_MLBL,
                kTierPx[T_MLBL] * 0.16f);
    cy += kHead10Line + 6.0f;
    const std::string msg = r->question.empty() ? std::string("\xe2\x80\x94") : r->question;
    auto mlines = WrapLines(msg, R - L - 24.0f, loc, T_FAIL, 4);
    const float mlineH = kTierPx[T_FAIL] * 1.5f;
    const float blockH = mlines.size() * mlineH + 2 * 10.0f;
    dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + blockH), kSurface2, 8.0f);
    dl->AddRect(ImVec2(L + 0.5f, cy + 0.5f), ImVec2(R - 0.5f, cy + blockH - 0.5f), Over(kSurface2, kHair),
                8.0f, 0, 1.0f);
    DrawTextRun(dl, L + 10.0f, cy + 10.0f, "$", loc, kStRun, T_FAIL);
    for (size_t i = 0; i < mlines.size(); i++)
        DrawTextRun(dl, L + 22.0f, cy + 10.0f + i * mlineH, mlines[i], loc, kFg, T_FAIL);
    cy += blockH + 10.0f;

    std::string tool = r->tool.empty() ? std::string("\xe2\x80\x94") : r->tool;
    cy += KvRow(dl, L, R, cy, "perm_tool", tool, loc, true);
    cy += KvRow(dl, L, R, cy, "det_wait", Ago(r->age, loc), loc, false);
    cy += KvRow(dl, L, R, cy, "perm_project", r->project, loc, false);
    cy += KvRow(dl, L, R, cy, "det_source", T("det_source_val", loc), loc, false);
    cy += 8.0f;

    for (const auto& ln : WrapLines(T("perm_hint", loc), R - L, loc, T_SM, 3)) {
        DrawTextRun(dl, L, cy, ln, loc, kMutedFg, T_SM);
        cy += kTierPx[T_SM] * 1.5f;
    }
    cy += 8.0f;

    const std::string jb = T(LandGoKey(r->agent_id), loc);
    cy += GoButton(dl, L, R, cy, jb, loc, *r);
    return cy;
}

// ── 历史 ────────────────────────────────────────────────────────────────────
static float drawHistory(ImDrawList* dl, float L, float R, float maxBot, int loc, float cy) {
    const float track = kTierPx[T_MLBL] * 0.16f;
    cy = ScreenHeader(dl, L, R, cy, loc, "nav_history");

    std::vector<const UIRow*> done;
    int turns = 0;
    for (const auto& r : g_pd.rows) {
        if (GroupOf(r.state) != G_DONE) continue;
        done.push_back(&r);
        turns += r.turns;
    }
    if (done.empty()) {
        DrawTextRun(dl, L, cy, T("hist_empty", loc), loc, kMutedFg, T_SM);
        return cy + kTierPx[T_SM] * 1.5f;
    }

    // 轨卡：今天这一段的完成量与时刻分布。数字是这一轮真数出来的行，不是估的。
    // 没有轮次可报时这张卡不长到 90pt——上面只留一行会话数，下面直接接时间轴。
    const float railH = turns > 0 ? 90.0f : 64.0f;
    dl->AddRectFilled(ImVec2(L, cy), ImVec2(R, cy + railH), kSurface1, 12.0f);
    dl->AddRect(ImVec2(L + 0.5f, cy + 0.5f), ImVec2(R - 0.5f, cy + railH - 0.5f), kHair, 12.0f, 0, 1.0f);
    char tb[24];
    char sb[64];
    snprintf(sb, sizeof sb, T("hist_sum", loc), (int)done.size());
    if (turns > 0) {
        snprintf(tb, sizeof tb, "%d", turns);
        DrawTextRun(dl, L + 14.0f, cy + 10.0f, tb, loc, kFg, T_NUM, -kTierPx[T_NUM] * 0.03f);
        DrawTextRun(dl, L + 14.0f + TextWidth(tb, loc, T_NUM, -kTierPx[T_NUM] * 0.03f) + 6.0f,
                    TextMid(cy + 26.0f, T_SM, loc), T("turns", loc), loc, kMutedFg, T_SM);
        DrawTextRun(dl, L + 14.0f, cy + 42.0f, sb, loc, kMutedFg, T_SM);
    } else {
        // 一行轮次都没数出来时（WorkBuddy / ZCode 的日志没有 step-finish），
        // 大数字位不摆 0：那是「没这个字段」，不是「零轮」。只报真有的会话数。
        DrawTextRun(dl, L + 14.0f, cy + 10.0f, sb, loc, kMutedFg, T_SM);
    }

    const float ax0 = L + 14.0f, ax1 = R - 14.0f, aby = cy + (turns > 0 ? 68.0f : 42.0f);
    dl->AddLine(ImVec2(ax0, aby), ImVec2(ax1, aby), kHair, 1.0f);
    const double ws = g_pd.window_start, now = g_pd.now > 0 ? g_pd.now : std::time(nullptr);
    const double span = now > ws ? now - ws : 1.0;
    for (int k = 0; k < 3; k++) {
        const float f = k * 0.5f;
        const float x = ax0 + (ax1 - ax0) * f;
        dl->AddLine(ImVec2(x, aby), ImVec2(x, aby + 4.0f), kHair, 1.0f);
        const std::string clk = ClockOf(ws + span * f);
        if (k == 0) DrawTextRun(dl, x, aby + 5.0f, clk, loc, kMicro, T_MICRO);
        else if (k == 2) DrawTextRight(dl, x, aby + 5.0f, clk, loc, kMicro, T_MICRO);
        else DrawTextRun(dl, x - TextWidth(clk, loc, T_MICRO) * 0.5f, aby + 5.0f, clk, loc, kMicro, T_MICRO);
    }
    for (size_t i = 0; i < done.size(); i++) {
        const double at = now - done[i]->age;
        float f = (float)((at - ws) / span);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        const float x = ax0 + (ax1 - ax0) * f;
        const float y = aby - 3.0f - (float)(i % 3) * 2.5f;
        dl->AddCircleFilled(ImVec2(x, y), 5.0f, WithA(kStDone, 40));
        dl->AddCircleFilled(ImVec2(x, y), 2.5f, kStDone);
    }
    cy += railH + 12.0f;

    // 节头：「今日完成 · N」。翻过页时右沿常驻一条「回到最新」，翻页永远有出口。
    {
        std::string lab = UpAscii(T("hist_done", loc));
        DrawTextRun(dl, L, cy, lab, loc, kStIdle, T_MLBL, track);
        char nb[16];
        snprintf(nb, sizeof nb, "%d", (int)done.size());
        DrawTextRun(dl, L + TextWidth(lab, loc, T_MLBL, track) + 8.0f, cy, nb, loc, kMutedFg, T_MLBL);
        if (g_hist_page > 0) {
            const float pw = GhostPillW("hist_latest", loc);
            const long z = Zid(loc, kHistRow, HIT_HISTMORE, 1);
            GhostPill(dl, R - pw, cy - 4.0f, pw, loc, "hist_latest", z);
            AddHit(g_hits, R - pw, cy - 4.0f, R, cy - 4.0f + kSegH, HIT_HISTMORE, kHistRow, 1, loc,
                   T("hist_latest", loc), 0);
        }
        cy += kHead10Line + 6.0f;
    }

    // 页宽按预算算：先留翻页那一行，再尽量多摆；一行都摆不下就摆一行。
    const float rowH = 34.0f, pagerH = 24.0f;
    if (g_hist_page >= (int)done.size()) g_hist_page = 0;
    int shown = (int)((maxBot - cy) / rowH);
    if (shown < 1) shown = 1;
    if (shown > (int)done.size() - g_hist_page) shown = (int)done.size() - g_hist_page;
    bool more = g_hist_page + shown < (int)done.size();
    if (more && cy + shown * rowH + pagerH > maxBot) shown = std::max(1, shown - 1);
    more = g_hist_page + shown < (int)done.size();
    g_hist_shown = shown;

    for (int i = 0; i < shown; i++) {
        const UIRow& r = *done[g_hist_page + i];
        if (i) dl->AddLine(ImVec2(L, cy), ImVec2(R, cy), kHair, 1.0f);
        const float ccx = L + 8.0f, ccy = cy + 17.0f;
        dl->AddCircleFilled(ImVec2(ccx, ccy), 7.0f, WithA(kStDone, 34));
        DrawCheck(dl, ccx, ccy, kStDone, 0.72f);
        const float tx = L + 22.0f, rx = R - 8.0f;
        DrawTextRun(dl, tx, cy + 4.0f, ClipToWidth(r.project, rx - 46.0f - tx, loc, T_TOOL),
                    loc, kFg, T_TOOL);
        DrawTextRun(dl, tx, cy + 20.0f, ClipToWidth(r.agent, rx - 46.0f - tx, loc, T_MICRO),
                    loc, kMutedFg, T_MICRO);
        DrawTextRight(dl, rx, cy + 4.0f, ClockOf(g_pd.now - r.age), loc, kTime, T_MICRO);
        // 轮次是采集层真数出来的事件数；WorkBuddy / 部分旧 Qoder 会话没有这一步，
        // 0 不是「零轮」而是「没这个字段」——空格子比一个会被读成零的 0 诚实。
        if (r.turns > 0) {
            char rb[32];
            snprintf(rb, sizeof rb, "%d %s", r.turns, T("turns", loc));
            DrawTextRight(dl, rx, cy + 20.0f, rb, loc, kMutedFg, T_MICRO);
        }
        AddHit(g_hits, L, cy, R, cy + rowH, HIT_OPEN, (int)(g_hist_page + i), 0, r, loc, std::string());
        cy += rowH;
    }
    if (more) {
        char mb[48];
        snprintf(mb, sizeof mb, T("more", loc), (int)(done.size() - g_hist_page - shown));
        const long z = Zid(loc, kHistRow, HIT_HISTMORE, 0);
        dl->AddRect(ImVec2(L + 0.5f, cy + 0.5f), ImVec2(R - 0.5f, cy + pagerH - 0.5f),
                    MixA(kHair, WithA(kFg, 90), TxPeek(z)), 6.0f, 0, 1.0f);
        DrawTextRun(dl, (L + R - TextWidth(mb, loc, T_BTN)) * 0.5f, TextMid(cy + pagerH * 0.5f, T_BTN, loc),
                    mb, loc, kMutedFg, T_BTN);
        AddHit(g_hits, L, cy, R, cy + pagerH, HIT_HISTMORE, kHistRow, 0, loc, mb, 0);
        cy += pagerH;
    }
    return cy;
}

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
    const bool asks_q = RowAsksQuestion(row);
    std::string plab = T(is_await ? (asks_q ? "needs_you" : "await_confirm")
                                  : StateKey(row.state), loc);
    float pillW = 7 + 5 + 4 + TextWidth(plab, loc, T_PILL) + 7;
    std::string readlab = (is_await && !mute) ? T("mark_read", loc) : "";
    float readW = readlab.empty() ? 0 : 7 + TextWidth(readlab, loc, T_BTN) + 7;
    float reserved = pillW + (readW ? 6.0f + readW : 0);
    // 稿子里 await 行的标题就是「AskUserQuestion 需要你确认」——工具名 + 这句话，
    // 不是转录本的最后一句人话（那句在引用块里才是它的位子）。hook 没给工具名就退回原标题。
    // 带候选选项的那类不是「确认」而是「回答」，两句话不能混用。
    std::string ttl = row.title;
    if (is_await && !row.tool.empty()) {
        ttl = row.tool; ttl += ' ';
        ttl += T(asks_q ? "ask_answer" : "ask_confirm", loc);
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

// r20 版式：六屏都由这一个文件里的 R20Draw 出图，旧的一套 draw* 已不再接线。
#include "r20_views.inc"
static float g_panelH = 850.0f;   // 内容真正用到的高度：窗口收放跟着它走
// r31 版式（R31 依赖 kRPi/RCullHits/RToast，必须在 r20 之后 include）：
// 会话与信息架构六屏（0 总览 / 3 详情 / 5 历史 / 6 用量 / 7 账户 / 8 工具）。
#include "r31_views.inc"

static void RebuildPanel(const ap::Snapshot& s) {
    PanelData pd;
    const double now = s.scanned_at > 0 ? s.scanned_at : std::time(nullptr);

    for (const auto& ag : s.agents) {
        bool has_today = false;
        bool app_seen = false;
        int shown = 0;
        for (const auto& se : ag.sessions) {
            // 「今日已结束」＝今天有过动静、但已经不跑的会话（稿子 done 组）。
            // 昨天及更早的静置会话不进面板 —— 稿子的组名就写着「今日」。
            if (se.state == ap::ST_RESTING && se.last_activity < s.window_start) continue;
            has_today = has_today || (se.last_activity >= s.window_start);
            app_seen = app_seen || se.app_indexed;
            UIRow row;
            row.state = se.state;
            // 「等你多久」问的是这次停下来的时长。hook 说得出就按信号时刻算；
            // 否则退回日志最后一条的时间。
            const double since = (se.state == ap::ST_AWAIT && se.await_since > 0)
                                     ? se.await_since : se.last_activity;
            row.age = now - since;
            row.is_new = row.age < 900;            // 15 分钟内有过动静＝稿子的 isNew
            row.inferred = se.inferred_wait;
            row.title = se.title;
            row.project = se.project;
            row.agent = ag.name;
            row.agent_id = ag.id;
            row.client = ClientTagOf(ag.id, se.app_indexed);
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
            row.option_desc = se.option_desc;
            row.tool = se.tool;
            // 采集层已经保证 first_activity 落在今天之内；这里再扣一次「最后有动静」，
            // 时长不为负。没有命令原文就留空，`.live` 那一行整行不画。
            row.span_sec = (se.first_activity > 0 && se.last_activity > se.first_activity)
                               ? se.last_activity - se.first_activity : 0.0;
            row.last_cmd = se.last_cmd;
            row.trigger = se.trigger;
            row.tokens = se.tokens;
            row.credits = se.credits;
            row.has_tokens = se.usage.total() > 0;
            row.has_credits = se.credits > 0;
            row.model = se.model;
            row.cwd = se.cwd;
            row.created = se.created;
            row.children = se.children;
            row.at = se.last_activity;
            row.unseen = GroupOf(row.state) == G_DONE &&
                         (long long)row.at > g_cfg.seen_until;
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
        tl.id = ag.id;
        tl.name = ag.name;
        tl.initial = InitialOf(ag.name);
        tl.available = ag.available;
        tl.unreadable = !ag.scan_error.empty();
        tl.quiet = ag.available && ag.scan_error.empty() && !has_today;
        tl.rows = shown;
        tl.hue = HueFor(ag.name);
        tl.usage = ag.usage;
        tl.credits = 0;
        for (const auto& se : ag.sessions) tl.credits += se.credits;
        tl.has_credits = tl.credits > 0;
        tl.models = ag.models;
        tl.hourly = ag.hourly;
        tl.quota = ag.quota;
        // 形态签按这一路今天真摆出来的会话给：Qoder 只要有一条被 App 的会话库索引到就写
        // IDE，一条都没有就留空（留空＝不知道，不等于「它是 CLI」）。屏上那颗小签只念这一件事。
        tl.client = ClientTagOf(ag.id, app_seen);
        pd.tools.push_back(std::move(tl));
    }
    pd.hook_live = s.hook_live;
    pd.today_tokens = s.today_tokens;
    pd.has_today_tokens = s.today_tokens > 0;
    // 组内按「多久以前」升序：稿子 done 组就是 4 分钟前排在 14 小时前前面
    std::sort(pd.rows.begin(), pd.rows.end(), [](const UIRow& a, const UIRow& b) {
        int ga = GroupOf(a.state), gb = GroupOf(b.state);
        if (ga != gb) return ga < gb;
        return a.age < b.age;
    });
    // 总览顶部那张「在跑」卡：稿子只摆一个正在跑的会话。真机上可能同时有几路在跑，
    // 取「最近有动静」的那一条当主角，其余在「其它在跑」里逐行列出——不合并、不丢行。
    for (size_t i = 0; i < pd.rows.size(); i++) {
        const int g = GroupOf(pd.rows[i].state);
        if (g == G_RUN || g == G_QUEUE) { pd.hero = (int)i; break; }   // 已按 age 升序
    }
    // 顶栏两个数按稿子口径：needs you = 需要你介入 + 失败中断（稿子样本里没有失败行，
    // 所以它的公式看不见这一档；把失败算成 0 会让面板比真相慢），running = 运行中 + 排队中。
    for (const auto& r : pd.rows) {
        int g = GroupOf(r.state);
        if (g == G_AWAIT) { pd.needs++; pd.await_n++; }
        else if (g == G_FAIL) { pd.needs++; pd.fail_n++; }
        else if (g == G_RUN) pd.run++;
        else if (g == G_QUEUE) { pd.run++; pd.queue_n++; }
        else if (g == G_DONE && r.unseen) pd.unseen_n++;   // 跑完但没人看过：待处理清单的第二段
    }
    // 今日用量是整轮口径（scan_all 已汇总），不由逐项拼出来
    pd.credits = s.today_credits;
    pd.yday_credits = s.yesterday_credits;
    // 权益层与快照分开取：它读的是各家另外的日志，一轮 <10ms（实测），不必进快照缓存。
    pd.claims = ap::claims_scan(now);
    pd.now = now;
    pd.window_start = s.window_start;
    // 每个工具「在等」几条：R31 的工具列表要摆这一格，判据与总览队列同源。
    for (const auto& r : pd.rows) {
        if (GroupOf(r.state) != G_AWAIT && GroupOf(r.state) != G_FAIL) continue;
        for (auto& t : pd.tools)
            if (t.id == r.agent_id) { t.wait_n++; break; }
    }

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
        // 「待查看」不进菜单栏那串数字（它不催人），但悬停说明得把它念出来，
        // 否则面板里待处理 8 与这里 2+1 对不上，读的人只会以为哪边算错了。
        char us[48] = "";
        if (g_pd.unseen_n) {
            char one[40];
            snprintf(one, sizeof one, T("sb_unseen", g_loc), g_pd.unseen_n);
            snprintf(us, sizeof us, " · %s", one);
        }
        snprintf(tip, sizeof tip, "%s %d · %s %d%s · %s %d%s · %s",
                 T("grp_await", g_loc), g_pd.await_n,
                 T("grp_run", g_loc), g_pd.run, q,
                 T("grp_fail", g_loc), g_pd.fail_n, us,
                 T("sb_hint", g_loc));
    }
    sb_set_tooltip(tip);
    // 悬停说明只在变的时候记一行：面板条数和菜单栏那串数字对不上时，先看这里到底发了什么，
    // 不必去悬停菜单栏（那块屏的状态不许为了取证去动）。
    {
        static char last_tip[256] = "";
        if (strcmp(last_tip, tip) != 0) {
            snprintf(last_tip, sizeof last_tip, "%s", tip);
            printf("[状态项] 悬停＝%s\n", tip);
            fflush(stdout);
        }
    }
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
    // ── 券到账这一轮：接在同一本账之后，靠 claim: 前缀各扫各的 ────────────────
    // 这里必须自己 scan，不能读 g_pd.claims：那一份是「画那一趟」的产物，
    // 面板收起时主循环根本不去画，而提醒恰恰要在收起的时候干活。
    // 面板开着的时候整段跳过，而且**不记账**：本机实测踩过——App 一启动面板就是开着的，
    // 当轮就把当天那张券判成「已提醒」写进账本，于是横幅永远弹不出来（只有一句两秒的
    // toast）。跳过而不记账，等他收起面板的那一轮再真弹，才是「有通知」这句话的意思。
    int claim_posted = 0, claim_retired = 0, claim_hits = 0, claim_fresh = 0;
    if (!g_panelVisible) {
        const std::vector<ap::Claim> cs = ap::claims_scan(now);
        std::set<std::string> cnext;
        const ap::AlertRound cr = ap::select_claim_alerts(cs, next, g_cfg, &cnext);
        claim_hits = cr.hits; claim_fresh = cr.fresh;
        claim_retired = cr.retired;
        for (const auto& p : cr.to_post) {
            const ap::Claim* c = nullptr;
            for (const auto& x : cs) if (x.vendor == p.agent_id) { c = &x; break; }
            // 正文优先报数字：WorkBuddy 那条每天 100 credits，读数里有就说读数里的。
            char line[192];
            if (c && c->credit_known)
                snprintf(line, sizeof line, T("claim_credit", g_loc), (long long)c->credit);
            else
                snprintf(line, sizeof line, "%s", T("claim_open", g_loc));
            const std::string body = std::string(line) + " · " + T("claim_goto", g_loc);
            const std::string title = p.agent_name + " · " + T("claim_word", g_loc);
            // 自己画，不发系统横幅（2026-10-30 用户拍板）。两条理由：
            // ① 系统那条通道先要过「系统设置→通知」这道闸，本机实测档位是被关着，
            //    发出去只返回 1（出声），人眼前什么都没有，而账本照记＝这一期再也不弹；
            // ② 一旦他哪天开了权限，系统横幅和我们自己的卡会同时出现，同一件事响两遍。
            BannerItem bi;
            bi.agent = p.agent_id; bi.key = p.session_id; bi.title = title; bi.body = body;
            // 常驻之后同一家可能连着来两张（昨天的他没点，今天这张又出了）：换字，不叠第二行。
            // 叠上去那块卡会长高，而两行写着同一家「有券可领」的卡看不出是两件事。
            bool rewrote = false;
            for (auto& b : g_banners)
                if (b.agent == p.agent_id) { b = bi; rewrote = true; break; }
            if (!rewrote) g_banners.push_back(bi);
            claim_posted++;
            fprintf(stderr, "[权益提醒] %s｜%s｜自绘横幅在屏 %d 条%s\n", title.c_str(),
                    body.c_str(), (int)g_banners.size(),
                    rewrote ? "（同厂商换期，没叠第二行）" : "");
            fflush(stderr);
        }
        if (claim_posted && g_cfg.sound) {
            // 只在「这一轮真推了新横幅」时响一次。挂在 g_banners 非空上会每 3 秒重扫
            // 就补一声——横幅改成常驻之后，那张卡会一路响个没完（2026-10-30 活机
            // 日志在 8 秒版上就实测到一张卡补了三声）。
            char why[256] = {0};
            const int sr = ap_sound_test(why, (int)sizeof why);
            fprintf(stderr, "[权益提醒] 提示音%s：%s\n", sr == 0 ? "已出" : "没出成", why);
            fflush(stderr);
        }
        // 「上次提醒」的时刻得活过重启，所以落 settings.txt；只在真推了横幅那一轮写盘——
        // 这一轮每 3 秒跑一次，无条件写就把设置文件当心跳文件刷了。
        if (claim_posted) {
            g_cfg.last_claim_at = (long long)std::time(nullptr);
            std::string serr;
            if (!ap::save_alert_cfg(g_cfg, &serr))
                fprintf(stderr, "[权益提醒] 上次提醒的时刻没写进去：%s\n",
                        serr.empty() ? "原因未回" : serr.c_str());
            fflush(stderr);
        }
        next = std::move(cnext);
        if (cr.posted || cr.retired) {
            printf("[权益提醒] 命中 %d｜首次 %d｜发出 %d｜压掉 %d｜摘账 %d｜开关%s｜免打扰%s\n",
                   cr.hits, cr.fresh, cr.posted, cr.capped, cr.retired,
                   g_cfg.claims ? "开" : "关", g_cfg.dnd ? "开" : "关");
            fflush(stdout);
        }
    }
    // 撤掉倒计时之后，「这张券其实已经不用提醒了」只能靠每轮拿当前读数对一次键来收。
    // 这一小段故意放在 !g_panelVisible 那道门外面：面板开着的时候横幅虽然不画，但卡还挂在
    // g_banners 里，等他领完券把面板一收，屏上就剩下几张永远点不完的旧卡。
    if (!g_banners.empty()) {
        const std::vector<ap::Claim> live = ap::claims_scan(now);
        std::set<std::string> still;
        for (const auto& c : live) if (c.ready) still.insert(c.vendor);
        const size_t before = g_banners.size();
        g_banners.erase(std::remove_if(g_banners.begin(), g_banners.end(),
                                       [&](const BannerItem& b) { return !still.count(b.agent); }),
                        g_banners.end());
        if (g_banners.size() != before) {
            printf("[横幅] 券已不在可领状态，撤下 %zu 条（屏上剩 %zu 条）\n",
                   before - g_banners.size(), g_banners.size());
            fflush(stdout);
        }
    }
    g_ledger = next;
    std::string err;
    if (!ap::save_alerted(g_ledger, &err)) {
        fprintf(stderr, "[提醒] 账本写失败：%s —— 去重失效，下一轮会重发同一批\n",
                err.empty() ? "原因未回" : err.c_str());
        fflush(stderr);
    }
    if (g_alert_first || r.posted || r.retired || held || claim_posted || claim_retired) {
        printf("[提醒] 命中 %d｜首次 %d｜发出 %zu｜压掉 %d｜摘账 %d｜已读/稍后按住 %d｜"
               "券 %d/%d/%d｜账本 %zu 条｜免打扰%s｜刚静置 %zu 条\n",
               r.hits, r.fresh, keep.size(), r.capped, r.retired, held,
               claim_hits, claim_fresh, claim_posted, g_ledger.size(),
               g_cfg.dnd ? "开" : "关", just_finished.size());
        fflush(stdout);
    }
    // 跳变记忆每轮都要换，与「这一轮响没响」无关：挂着上一轮的旧集合，下一轮打开
    // 「已完成」就会拿几分钟前的动静的对家来响。
    g_active = std::move(cur);
    g_alert_first = false;
}

// drawPanel 每帧回填：内容真正用到的高度（R31 自适应收放；定义在 include 前供 r31 用）
// 能摆多高的预算。必须与「窗口现在多高」解耦：窗口是按内容收的，
// 若预算取当前窗口高，收窗→能摆更多行→再收窗会形成正反馈，每轮重扫只走一步，
// 实拍就会拍到「内容 340pt / 窗口 285pt」这种半收敛态（实测踩过）。
static float g_budgetH = 850.0f;

/// 窗口该收到多高：r20 是固定 640 高的浮层，内容超出走屏内滚动；只有屏幕放不下时才压到预算。
static int WantedWindowH() {
    // 横幅那一档不套 400 的下限：通知卡就该只有通知卡那么大，撑到 400 会变成一块空玻璃。
    if (g_bannerActive) return (int)(g_bannerH + 0.5f);
    // R31 的面板高跟内容走：几行会话就收成几行高（下不再挂一大截空白），
    // 上限仍是屏预算与 850。r20 三屏画不满 640，由各自 Draw 返回值兜住。
    float h = g_panelH;
    if (h < 400.0f) h = 400.0f;
    if (h > g_budgetH) h = g_budgetH;
    if (h > kCanvasH) h = (float)kCanvasH;
    return (int)(h + 0.5f);
}

/// r20 六屏同宽：都是 340pt 的浮层（设置也是同一张卡，不再单独 420）。
static int WantedWindowW() { return kPanelW; }

static void TogglePanel(void*) {
    if (!g_win) return;
    // 出图轮不许收面板：收了主循环就跳过 HandleInput，AGENTPING_CLICK 序列冻在半路，
    // 窗口永远等不到那张 PPM。真点菜单栏那颗只打一行账，面板留在屏上直到拍完自收。
    if (g_shot_to) {
        printf("[面板] 出图轮忽略状态项切换\n");
        fflush(stdout);
        return;
    }
    g_panelVisible = !g_panelVisible;
    // 打这一行是为了让总线点击可被外部核对：宿主调 Activate 只能证明「回包没报错」，
    // 证明不了面板真的动了。取证轮比对 app.log 里这行的出现次数与点击次数。
    printf("[面板] %s\n", g_panelVisible ? "展开" : "收起");
    fflush(stdout);
    if (g_panelVisible) { glfwShowWindow(g_win); glfwFocusWindow(g_win); g_relayout = true; }
    else glfwHideWindow(g_win);
}

/// 离屏探针专用的样例快照（AGENTPING_FIXTURE=1）。
/// 只为一件事：本机今天恰好没有待回答/待授权时，详情两屏仍能对版式。
/// 窗口路径不读它；探针标题会写明「样例数据」，产物不会被当成实拍。
static void LoadProbeFixture() {
    PanelData pd;
    struct tm tm0;
    time_t seed = std::time(nullptr);
    localtime_r(&seed, &tm0);
    tm0.tm_hour = 0; tm0.tm_min = 0; tm0.tm_sec = 0;
    const double midnight = (double)mktime(&tm0);
    // 稿子的快照时刻是 23:35；钉住它，「多久以前」与历史刻度才能逐屏对得上。
    const double now = midnight + 23 * 3600.0 + 35 * 60.0;
    auto at = [&](int hh, int mm) { return now - (midnight + hh * 3600.0 + mm * 60.0); };

    auto row = [&](ap::State st, const char* proj, const char* agent, const char* aid,
                   const char* sess, double age, int turns) {
        UIRow r;
        r.state = st;
        r.project = proj;
        r.agent = agent;
        r.agent_id = aid;
        // 样例数据：这一条按「桌面 App 的会话库里有它」摆，好让形态签那一格仍能对版式。
        r.client = ClientTagOf(aid, true);
        r.native_id = sess;
        r.key = std::string(aid) + ":" + sess;
        r.initial = InitialOf(agent);
        r.hue = HueFor(agent);
        r.age = age;
        r.turns = turns;
        r.inferred = false;
        pd.rows.push_back(std::move(r));
        return &pd.rows.back();
    };

    UIRow* live = row(ap::ST_WORKING, "image-tools", "Qoder CLI", "qoder", "a1b2c3d4", 12.0, 77);
    live->title = "【样例】多 Agent 监控工具调研";   // R31RowName 标题优先
    live->span_sec = 4 * 3600.0 + 12 * 60.0;      // .live「连续 4h 12m」
    live->last_cmd = "rg -n \"safe-delete\" src/"; // .live 的 `$ cmd`
    live->children = {                             // 任务下挂着的子代理对话（展开用样例）
        {"c-a1", "你是调研员。收集三款同类产品的定价页与收费方式。", true, now - 300},
        {"c-a2", "你是 UI 调研员。只读它们的设置屏，列出可借鉴的三处。", false, now - 4320},
        {"c-a3", "你是数据整理员。把上面的结论合成一张对比表。", false, now - 7800},
    };
    (void)live;

    UIRow* q1 = row(ap::ST_AWAIT, "budget-app", "Qoder CLI", "qoder", "a1", at(22, 46), 0);
    q1->children = {
        {"c-b1", "你是产品调研员。读这三款产品的公开文档，找出各自的限制。", false, now - 900},
        {"c-b2", "你是技术调研员。对比三种本地存储方案的迁移成本。", false, now - 1500},
    };
    q1->question = "这批改动要一起提交吗？还是先拆成两笔？";
    q1->options = {"一起提交", "拆成两笔", "先不提交"};
    q1->option_desc = {"一次进主干", "按功能与测试分开", "留在工作区等明天"};

    UIRow* q2 = row(ap::ST_AWAIT, "image-tools", "Qoder CLI", "qoder", "b2", at(8, 35), 0);
    q2->question = "这个模块的旧版本还挂在别处，怎么处理？";
    q2->options = {"先搬再删", "只留新的", "两个都停"};
    q2->option_desc = {"补齐后再移除", "旧的继续挂着", "等下周再说"};

    UIRow* p1 = row(ap::ST_AWAIT, "image-tools", "Qoder CLI", "qoder", "c3", at(22, 28), 0);
    p1->tool = "Bash";
    p1->question = "rm '/tmp_cache.txt'";
    p1->trigger = "Dangerous rm operation detected";
    UIRow* p2 = row(ap::ST_AWAIT, "budget-app", "Qoder CLI", "qoder", "d4", at(14, 35), 0);
    p2->tool = "Bash";
    p2->question = "cd /Users/you/projects/mobile-app";
    p2->trigger = "Allow Bash to run";
    UIRow* p3 = row(ap::ST_AWAIT, "photo-sorter", "Claude Code", "claude", "e5", at(10, 35), 0);
    p3->tool = "Write";
    p3->question = "write ~/…/app-icon.svg";
    p3->trigger = "Writing to paths outside the workspace";
    (void)p1; (void)p2; (void)p3;

    const struct { const char* proj; const char* agent; const char* aid; const char* sess;
                   int hh, mm; int turns; } kDone[] = {
        {"budget-app", "Qoder CLI", "qoder", "h1", 22, 57, 331},
        {"kitchen-timer", "Claude Code", "claude", "h2", 21, 17, 16},
        {"image-tools", "Qoder CLI", "qoder", "h3", 19, 3, 28},
        {"poetry-site", "Claude Code", "claude", "h4", 16, 48, 18},
        {"photo-sorter", "Claude Code", "claude", "h5", 14, 22, 196},
        {"budget-app", "Qoder CLI", "qoder", "h6", 12, 5, 31},
        {"habit-box", "Claude Code", "claude", "h7", 10, 40, 130},
        {"image-tools", "Qoder CLI", "qoder", "h8", 8, 12, 17},
    };
    for (const auto& d : kDone) {
        UIRow* r2 = row(ap::ST_RESTING, d.proj, d.agent, d.aid, d.sess, at(d.hh, d.mm), d.turns);
        r2->at = midnight + (d.hh * 3600.0 + d.mm * 60.0);   // 完成时刻：未读点按它对水位线
        r2->unseen = (d.hh >= 19);                           // 演示：19 点后的算「还没看过」
    }

    // 行级用量也各留缺口：Qoder 只有 Credits（JSONL 没有 token 明细 → has_tokens 假），
    // Claude 只有 tokens —— 与本机真实数据同形状，「缺哪格整格不画」才有得验。
    for (auto& r : pd.rows) {
        if (r.agent_id == "claude") {
            r.has_tokens = true;
            r.tokens = 18000 + (r.turns * 137) % 220000;
        } else if (r.agent_id == "qoder") {
            r.has_credits = true;
            r.credits = 12.5 + (double)(r.turns % 60) * 1.7;
        }
    }
    // 六路采集器全摆出来（导轨横向滚动、全部工具列表才摆得满），并按第 7 节的
    // 「缺数据」规矩故意留缺口：
    //   qoder     只有 Credits，没有 token 明细 → 拆解/按模型/peek tokens 整列不画
    //   zcode     只有 5 小时额度，没有周额度 → 额度卡单列占满
    //   workbuddy 额度读数是一小时前拍的 → 整卡走「待更新」红字 + r_stale_note
    //   codex     有 token 但缓存两档为 0 → 拆解照画、0 值显示 0（稿子 js 的行为）
    //   opencode  只有缓存写入一档非零
    auto tool = [&](const char* id, const char* name, bool avail, bool quiet, int rows) {
        Tool tl;
        tl.id = id;
        tl.name = name;
        tl.initial = InitialOf(name);
        tl.available = avail;
        tl.quiet = quiet;
        tl.rows = rows;
        tl.hue = HueFor(name);
        tl.client = ClientTagOf(id, true);   // 同上面的样例口径：一律当成有正证那一档，好把格子摆满
        pd.tools.push_back(std::move(tl));
        return &pd.tools.back();
    };
    {
        Tool* t = tool("qoder", "Qoder CLI", true, false, 8);
        t->has_credits = true;
        t->credits = 469.72;
    }
    {
        Tool* t = tool("claude", "Claude Code", true, false, 5);
        t->usage.input = 1234567;
        t->usage.output = 84521;
        t->usage.cache_read = 5678901;
        t->usage.cache_write = 210944;
        t->models = {{"claude-opus-4-6", 4200000}, {"claude-sonnet-4-5", 2600000},
                     {"claude-haiku-4-5", 309933}};
        for (int i = 9; i <= 22; i++)
            t->hourly[(size_t)i] =
                (long)(120000.0 + 900000.0 * std::exp(-std::abs(i - 14) / 4.0));
        t->hourly[14] = 1200000;
        t->quota.known = true;
        t->quota.plan = "Max 20x";
        t->quota.five.known = true;
        t->quota.five.used = 0.34;
        t->quota.five.resets_at = now + 2 * 3600.0 + 16 * 60.0;
        t->quota.five.window_min = 300;
        t->quota.five.seen_at = now - 120.0;
        t->quota.week.known = true;
        t->quota.week.used = 0.12;
        t->quota.week.resets_at = now + 3 * 86400.0;
        t->quota.week.window_min = 10080;
        t->quota.week.seen_at = now - 120.0;
    }
    {
        Tool* t = tool("codex", "Codex", true, false, 0);
        t->usage.input = 89012;
        t->usage.output = 43210;
        t->models = {{"gpt-5.2-codex", 132222}};
        t->hourly[11] = 40000;
        t->hourly[12] = 61000;
        t->hourly[13] = 31222;
    }
    {
        Tool* t = tool("zcode", "ZCode", true, false, 0);
        t->quota.known = true;
        t->quota.plan = "Pro";
        t->quota.five.known = true;
        t->quota.five.used = 0.05;
        t->quota.five.resets_at = now + 3600.0 + 5 * 60.0;
        t->quota.five.window_min = 300;
        t->quota.five.seen_at = now - 45.0;
    }
    {
        Tool* t = tool("workbuddy", "WorkBuddy", true, false, 0);
        t->quota.known = true;
        t->quota.plan = "Team";
        t->quota.five.known = true;
        t->quota.five.used = 0.71;
        t->quota.five.resets_at = now + 47 * 60.0;
        t->quota.five.window_min = 300;
        t->quota.five.seen_at = now - 3600.0;    // 一小时前的读数 → 整卡「待更新」
        t->quota.week.known = true;
        t->quota.week.used = 0.44;
        t->quota.week.resets_at = now + 2 * 86400.0;
        t->quota.week.window_min = 10080;
        t->quota.week.seen_at = now - 3600.0;
    }
    {
        Tool* t = tool("opencode", "OpenCode", true, false, 0);
        t->usage.cache_write = 51200;
        t->models = {{"qwen3-coder-plus", 51200}};
        t->hourly[20] = 51200;
    }
    // Codex 也要一条完成记录：完成记录屏 / 详情屏才有第二个工具可点。
    row(ap::ST_RESTING, "kitchen-timer", "Codex", "codex", "h9", at(11, 8), 9);

    // 权益屏（view 9）的样例读数。原先这一屏在探针里走的是「本机盘上没有读数」的空态，
    // 文档图没东西可拍。三家的形态按采集层真实能产出的形状各写一种：有面值+连续签到+
    // 失效时刻 / 只有面值和一个出现时刻 / 状态未知。最后这家不能写成「本期已领」——
    // ZCode 把权益留在服务端，本机日志零信号，from_zcode 恒为 known=false（见 claims.cpp）。
    // ZCode 的说明行用采集层同一个键：图上每个语言栏看到的是那一栏自己的语言。
    {
        auto ep = [&](int hh, int mm) { return now - at(hh, mm); };   // at() 给的是「多久以前」
        ap::Claim w;
        w.vendor = "workbuddy"; w.name = "WorkBuddy";
        w.known = w.ready = true;
        w.credit = 100; w.credit_known = true;
        w.streak = 6; w.streak_known = true;
        w.appears_at = ep(19, 0);
        w.expires_at = now + 5 * 3600.0 + 20 * 60.0;
        w.last_ready_at = now - 2 * 3600.0;
        w.seen_at = now - 45.0;
        w.log_mtime = now - 8 * 60.0;
        w.source = "~/.workbuddy/logs/main.log";
        pd.claims.push_back(w);

        ap::Claim q;
        q.vendor = "qoder"; q.name = "Qoder";
        q.known = q.ready = true;
        q.credit = 20; q.credit_known = true;
        q.notif = 3; q.notif_known = true;
        q.appears_at = ep(17, 30);
        q.last_ready_at = now - 10 * 60.0;
        q.seen_at = now - 30.0;
        q.log_mtime = now - 3 * 60.0;
        q.source = "~/Library/Application Support/com.qoder.app.stable/logs/<newest>/main.log";
        pd.claims.push_back(q);

        // ZCode：采集层恒 known=false，所以卡上只有状态胶囊和出处那一行，没有面值也没有时间轨。
        ap::Claim z;
        z.vendor = "zcode"; z.name = "ZCode";
        z.log_mtime = now - 20 * 60.0;
        z.source = "~/.zcode/cli/log/zcode-2026-10-30.jsonl";
        z.note_key = "cl_note_zc_none";
        pd.claims.push_back(z);
        g_cfg.last_claim_at = now - 2 * 3600.0;   // 屏上那行「上次提醒 2h 前」
    }

    pd.now = now;
    pd.window_start = midnight;
    pd.has_today_tokens = true;
    pd.today_tokens = 0;
    for (const auto& t : pd.tools) pd.today_tokens += t.usage.total();
    pd.hero = 0;
    pd.needs = 5;
    pd.await_n = 2;
    pd.run = 1;
    pd.live = 6;
    pd.credits = 469.72;    // Qoder 一路自记计数（不是金额），口径说明沿用 cost_note
    // 空态探针：只留完成记录，队列真空才有得验（AGENTPING_EMPTY=1，仅探针路径）。
    if (getenv("AGENTPING_EMPTY")) {
        pd.rows.erase(std::remove_if(pd.rows.begin(), pd.rows.end(),
                                     [](const UIRow& r) { return GroupOf(r.state) != G_DONE; }),
                      pd.rows.end());
        // 完成未看的也进「待处理」，所以这一档要一起清掉：否则探针拍的不是空队列。
        for (auto& r : pd.rows) r.unseen = false;
        pd.needs = 0;
        pd.await_n = 0;
        pd.run = 0;
        pd.live = 0;
    }
    g_pd = std::move(pd);
    g_pd_seq = g_seq.load();
}

int main(int argc, char** argv) {
    bool windowed = false, dump = false, hook_status = false;
    std::string hook_install, hook_uninstall;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--window")) windowed = true;
        else if (!strcmp(argv[i], "--dump")) dump = true;
        else if (!strcmp(argv[i], "--hook-status")) hook_status = true;
        else if (!strcmp(argv[i], "--install-hook") && i + 1 < argc) hook_install = argv[++i];
        else if (!strcmp(argv[i], "--uninstall-hook") && i + 1 < argc) hook_uninstall = argv[++i];
        else pos.push_back(argv[i]);
    }
    // 无参数＝从访达双击：出窗口。带参数＝离屏探针，按老用法渲一帧存 PPM。
    if (pos.empty() && !dump && !hook_status && hook_install.empty() && hook_uninstall.empty())
        windowed = true;

    // 接线走命令行：与面板里那颗按钮调的是同一个 ap::install_hook（同一份字节、同一套备份）。
    // 退出码沿用全仓口径：0 成功 / 1 失败（原因打到 stderr）/ 2 用法错。
    if (hook_status || !hook_install.empty() || !hook_uninstall.empty()) {
        if (!hook_install.empty() && !hook_uninstall.empty()) {
            fprintf(stderr, "--install-hook 与 --uninstall-hook 不能同时给\n");
            return 2;
        }
        if (hook_status) {
            for (const auto& h : ap::probe_hooks()) {
                printf("%-10s %-14s supported=%d installed=%d stale=%d occupied=%d  %s%s%s\n",
                       h.id.c_str(), h.name.c_str(), h.supported ? 1 : 0, h.installed ? 1 : 0,
                       h.stale ? 1 : 0, h.occupied ? 1 : 0,
                       h.config.empty() ? "-" : h.config.c_str(),
                       h.note.empty() ? "" : "  # ", h.note.c_str());
            }
            return 0;
        }
        std::string backup, err;
        const bool ok = !hook_install.empty()
                            ? ap::install_hook(hook_install, &backup, &err)
                            : ap::uninstall_hook(hook_uninstall, &backup, &err);
        const std::string& who = hook_install.empty() ? hook_uninstall : hook_install;
        if (!ok) {
            fprintf(stderr, "%s %s 失败：%s\n", hook_install.empty() ? "撤销" : "接线", who.c_str(),
                    err.empty() ? "未知原因" : err.c_str());
            return 1;
        }
        printf("%s %s 完成", hook_install.empty() ? "撤销" : "接线", who.c_str());
        if (!backup.empty()) printf("；原配置备份在 %s", backup.c_str());
        printf("\n");
        return 0;
    }

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
    // 设置屏改过语言就得活过重启：HIT_LANG 把 lang=0..5 写进 settings.txt，但这一版
    // 原来只有「写」没有「读」——他选德语、重启，界面又回中文（2026-10-30 实拍发现）。
    // 优先级是 探测 < settings.txt < AGENTPING_LANG：环境变量是取证口，必须能盖过他
    // 存下来的偏好，否则同一份 settings.txt 只能拍出一种语言的图。
    if (g_cfg.lang >= 0 && !getenv("AGENTPING_LANG")) {
        g_loc = g_cfg.lang;
        printf("[语言] 用 settings.txt 存的 lang=%d → %s\n", g_cfg.lang, kLocTag[g_loc]);
        fflush(stdout);
    }
    if (const char* v = getenv("AGENTPING_VIEW")) {
        g_view = atoi(v);
        // 探针要拍「停在该屏」的样子：底部标签条的高亮跟 g_tab 走，不同步就每屏都亮总览。
        // R31 的根屏标签是 会话(0)/用量(6)/权益(9)，历史(5) 与设置(1) 也各认各的。
        g_tab = (g_view == 1) ? 1 : (g_view == 5) ? 5 : (g_view == 6 || g_view == 9) ? g_view : 0;
    }
    if (!glfwInit()) return 2;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // 窗口先藏，定位好再 show，免得在默认位置闪一下
    // 出图轮不许抢键盘：取证实例出现在他正在打字的桌面上，一 show 就会成 key 窗口，
    // 他的拼音直接落进这块面板（2026-10-29 实测：没人碰面板，搜索框里却多了「xian」）。
    if (getenv("AGENTPING_SHOT")) glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, g_cfg.on_top ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, windowed ? GLFW_FALSE : GLFW_TRUE);
    // 稿子的 #win 是 rounded-16 + 1px hairline 的浮层，圆角外必须真透明，
    // 否则圆角处会露出一方底色，macOS 上就是一张贴在桌面上的矩形。
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, windowed ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    const int H = windowed ? (int)kCanvasH : 920;
    // 探针改两行：六语一排（416 面板＋24 间距＝pitch 440）。
    // 宽度＝14＋6×440＝2654，向上取 2660；高 ≥850＋tile 顶偏移 50 → 920。
    const int offW = windowed ? kPanelW : 2660;
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
    // 整张图集统一走 FreeType 加载器：1.93 还不支持逐字体换加载器
    // （imgui_draw.cpp:3127 明确断言 LoaderInit 必须为空）。默认的 stb 没有给轮廓
    // 加粗的能力，粗档只能靠画两遍错开一格，收尾处会留下双线/灰边。
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    int baked = 0;
    for (int t = 0; t < TIER_N; t++) {
        for (int f = 0; f < FACE_N; f++) {
            if (!FaceUsed(t, f)) continue;
            snprintf(path, sizeof path, "%s/%s", dir, kFace[f]);
            ImFontConfig fc; fc.SizePixels = Bake(t, f);
            g_font[t][f] = io.Fonts->AddFontFromFileTTF(path, Bake(t, f), &fc, ranges.Data);
            if (!g_font[t][f]) { fprintf(stderr, "字体加载失败 %s @%.1fpx\n", path, Bake(t, f)); return 2; }
            baked++;
            // 粗档的第二张脸：同一份文件、同一个大小，只多一个「给轮廓加粗」的标志。
            // 等宽档不烘 —— 它本来就有 JetBrains Mono SemiBold。
            if (kTierW[t] > 0 && !MonoTier(t)) {
                ImFontConfig fb; fb.SizePixels = Bake(t, f);
                fb.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Bold;
                g_fontB[t][f] = io.Fonts->AddFontFromFileTTF(path, Bake(t, f), &fb, ranges.Data);
                if (!g_fontB[t][f]) {
                    fprintf(stderr, "粗体字形加载失败 %s @%.1fpx\n", path, Bake(t, f));
                    return 2;
                }
                baked++;
            }
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
            for (int f = 0; f < FACE_N; f++) {
                if (g_font[t][f]) g_font[t][f]->CurrentRasterizerDensity = dens;
                if (g_fontB[t][f]) g_fontB[t][f]->CurrentRasterizerDensity = dens;
            }
        printf("[字形] %d 档字号 × %d 字体＝烘了 %d 张，栅格密度 %.1fx（窗口 %dx%d / 帧缓冲 %dx%d）\n",
               TIER_N, FACE_N, baked, dens, ww, wh, fw, fh);
    }

    ImGui_ImplGlfw_InitForOpenGL(win, true);
#ifdef __APPLE__
    {
        Class c = objc_getClass("GLFWContentView");
        Method m = c ? class_getInstanceMethod(c, sel_registerName("keyDown:")) : nullptr;
        if (m) g_origKeyDown = method_setImplementation(m, (IMP)KeyDownHook);
        if (c) class_addMethod(c, sel_registerName("inputContext"), (IMP)NoInputContext, "@@:");
        if (c) class_addMethod(c, sel_registerName("acceptsFirstMouse:"), (IMP)AcceptsFirstMouseOn,
                               "B@:@");
    }
#endif
#ifdef __APPLE__
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif
    // 屏头/菜单栏的品牌图是 GL 纹理，必须在上下文就绪之后建一次。
    BrandUpload();

    // AGENTPING_FIXTURE：用探针样例数据顶掉活扫描（空态、定数版面的实拍验证要走这条）。
    const bool use_fixture = getenv("AGENTPING_FIXTURE") != nullptr;
    std::thread scanner;
    if (use_fixture) {
        LoadProbeFixture();
        g_seq.fetch_add(2);          // 实拍条件要求 g_seq≥2
        g_pd_seq = g_seq.load();
    } else {
        scanner = std::thread(ScanLoop);
    }

    auto drawPanel = [&](float x, float y, float w, int loc, int view) {
        ImGui::SetNextWindowPos(ImVec2(x, y));
        // r20 是固定 640 高的浮层：宽度恒 340，内容超出走屏内滚动，窗口不跟着内容收放。
        ImGui::SetNextWindowSize(ImVec2(w, (float)WantedWindowH()));
        // 名字必须带上屏别与语种：探针一帧里同一语会 Begin 两次，撞了窗口 ID 就会叠裁剪矩形。
        char wname[32]; snprintf(wname, sizeof wname, "%s##%s", ViewTag(view),
                                 kLocTag[loc % kLocales]);
        ImGui::Begin(wname, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // AGENTPING_TN=<秒>：把动画时钟钉死在指定位相，取证才量得准呼吸/转圈的某一相。
        static const char* kTnLock = getenv("AGENTPING_TN");
        const double tn = kTnLock ? atof(kTnLock) : glfwGetTime();

        // 接线状态六屏共用（导航那颗的琥珀点也要它）；读两份小配置，两秒缓存。
        RefreshHooks(false);
        // 详情两屏要指一行具体会话。从别的屏直接切过来（探针按 VIEW 切）时自己挑第一条真有的，
        // 没有就照实画空态——不编一行假会话来填版面。
        if ((view == 3 || view == 4) && (g_sel_key.empty() || !SelectedRow(g_sel_key))) {
            g_sel_key.clear();
            g_sel_kind = view == 3 ? 1 : 2;
            for (const auto& r : g_pd.rows) {
                if (r.state != ap::ST_AWAIT || r.inferred) continue;
                if (view == 3 ? !RowAsksQuestion(r) : RowAsksQuestion(r)) continue;
                if (view == 4 && r.tool.empty() && r.question.empty()) continue;
                g_sel_key = r.key;
                g_sel_copy = r;
                break;
            }
        }

        // 账户屏同理：探针直接 VIEW=7 进来时挑第一路有额度的工具，没有就照实画空态。
        if (view == 7 && (g_r31_account.empty() || !R31ToolOf(g_r31_account))) {
            g_r31_account.clear();
            for (const auto& t : g_pd.tools) {
                if (!t.present()) continue;
                if (t.quota.known && (t.quota.five.known || t.quota.week.known)) {
                    g_r31_account = t.id;
                    break;
                }
            }
            if (g_r31_account.empty())
                for (const auto& t : g_pd.tools)
                    if (t.present() && (t.usage.total() > 0 || t.has_credits)) { g_r31_account = t.id; break; }
        }

        // 分屏路由（§2.3.1）：设置 / 接线 / 授权聚焦仍是 r20 版面（产品未要求重做），
        // 其余六屏走 R31。两者都要回写 g_bodyTop/g_bodyBot/g_contentH/g_scrollMax。
        // 横幅那一档不在这张路由表里：它不是「一屏」，是同一块窗口换了一副尺寸。
        if (g_bannerActive)
            g_panelH = g_bannerH = R31Banner(dl, x, y, w, loc, tn);
        else if (view == 1 || view == 2 || view == 4)
            g_panelH = R20Draw(dl, x, y, w, (float)WantedWindowH(), loc, view, tn);
        else
            g_panelH = R31Draw(dl, x, y, w, (float)WantedWindowH(), loc, view, tn);
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
        // 拖动过的位置优先（在屏内才用）；没拖过＝贴着菜单栏右下方
        if (g_cfg.win_x >= mx - 60 && g_cfg.win_x + offW <= mx + mw + 60 &&
            g_cfg.win_y >= my && g_cfg.win_y <= my + mh - 120)
            glfwSetWindowPos(win, g_cfg.win_x, g_cfg.win_y);
        else
            glfwSetWindowPos(win, mx + mw - offW - 14, my + 34);
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
        // AGENTPING_BANNER=1：把券横幅摆上屏取证。文案与数字都还走 claims_scan 的磁盘真读数，
        // 只是不等「本期第一次」那道判据——出图轮的账本本来就指向临时目录。
        {
            const char* seed = getenv("AGENTPING_BANNER");
            if (seed && *seed && strcmp(seed, "0") != 0) {
                const double nowSec = (double)std::time(nullptr);
                for (const auto& c : ap::claims_scan(nowSec)) {
                    if (!c.ready) continue;
                    char line[192];
                    if (c.credit_known)
                        snprintf(line, sizeof line, T("claim_credit", g_loc), (long long)c.credit);
                    else
                        snprintf(line, sizeof line, "%s", T("claim_open", g_loc));
                    BannerItem bi;
                    bi.agent = c.vendor; bi.key = "claim:" + c.vendor;
                    bi.title = c.name + " · " + T("claim_word", g_loc);
                    bi.body = std::string(line) + " · " + T("claim_goto", g_loc);
                    g_banners.push_back(bi);
                }
                g_panelVisible = false;
                printf("[横幅] 摆样 %d 条（AGENTPING_BANNER，只读磁盘上的真读数）\n",
                       (int)g_banners.size());
                fflush(stdout);
            }
        }
        double lastResize = glfwGetTime();
        int settle = 0;
        // AGENTPING_SNI_HOLD=毫秒：只挂着总线就收摊的取证模式。
        // 状态项的数不在我们这块窗口里（由宿主画），所以这一档不出图，只保证
        // 「宿主来读的时候进程还在」——没有它，自检脚本要么把窗口关掉导致总线名消失，
        // 要么一直跑不退出。
        static const char* kHolding = getenv("AGENTPING_SNI_HOLD");
        const double hold_ms = kHolding ? atof(kHolding) : 0.0;
        const double hold_until = hold_ms > 0.0 ? glfwGetTime() + hold_ms / 1000.0 : 0.0;
        // AGENTPING_PANEL=0：以「面板收着」起跑。真机上收面板是点菜单栏那颗，探针点不到它，
        // 而券横幅恰恰只在收着的时候才该出现——不补这个口，「开关能不能拦住横幅」就没法验。
        if (const char* pv = getenv("AGENTPING_PANEL")) {
            if (!strcmp(pv, "0")) {
                g_panelVisible = false;
                glfwHideWindow(win);
                printf("[面板] 按 AGENTPING_PANEL=0 起跑在收起态\n");
                fflush(stdout);
            }
        }
        while (!glfwWindowShouldClose(win)) {
            glfwPollEvents();
            sb_pump();
#ifdef __APPLE__
            // AGENTPING_REAL_CLICK="x,y"：在 HID 层发一次真鼠标按下/抬起（与用户的物理鼠标
            // 同一条系统路径，区别于 AGENTPING_CLICK 的合成按下）。诊断「真点击不生效」用。
            static const char* kRealClick = getenv("AGENTPING_REAL_CLICK");
            static bool realClickDone = false;
            if (kRealClick && !realClickDone && glfwGetTime() > 2.5 && g_seq.load() >= 1) {
                static int rcPhase = 0;
                float rx2 = 0, ry2 = 0;
                if (sscanf(kRealClick, "%f,%f", &rx2, &ry2) == 2) {
                    const CGPoint pt = CGPointMake(rx2, ry2);
                    if (rcPhase == 0) {
                        CGEventRef dn = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, pt, (CGMouseButton)0);
                        CGEventPost(kCGHIDEventTap, dn);
                        CFRelease(dn);
                        rcPhase = 1;
                        fprintf(stderr, "[真点击] 按下 @ (%.0f,%.0f)\n", rx2, ry2);
                    } else if (rcPhase == 1 && glfwGetTime() > 2.65) {
                        CGEventRef up = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, pt, (CGMouseButton)0);
                        CGEventPost(kCGHIDEventTap, up);
                        CFRelease(up);
                        rcPhase = 2;
                        realClickDone = true;
                        fprintf(stderr, "[真点击] 抬起\n");
                    }
                } else realClickDone = true;
                fflush(stderr);
            }
#endif
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
            }
            // AGENTPING_BANNER_CLICK=<agent>/<session>：自己灌一次「有人点了横幅」。
            // 为什么要这条：系统横幅要先有通知授权才存在，而本机档位是「系统里被关着」，
            // 于是「点横幅落到哪一屏」这条主张在活机上永远拍不到。这里把它接到平台回调
            // 同一个登记口，走的还是 DrainBannerClick 那条真路，不是另搭的假状态。
            {
                static bool injected = false;
                const char* bc = getenv("AGENTPING_BANNER_CLICK");
                if (!injected && bc && *bc) {
                    injected = true;
                    const std::string s = bc;
                    const size_t slash = s.find('/');
                    OnBannerClick(s.substr(0, slash).c_str(),
                                  slash == std::string::npos ? "" : s.c_str() + slash + 1, nullptr);
                }
            }
            DrainBannerClick();
            // ── 自绘横幅的开机与收起 ────────────────────────────────────────────
            // 排在这里（可见性判断之前）：面板收着的时候才是它干活的时候，而窗口本
            // 就是那块玻璃，只是换一副尺寸露一下脸。
            {
                // 面板开着就不换画面；横幅只在「面板没占着窗口」时才有屏可占。
                const bool want = !g_banners.empty() && !g_panelVisible;
                if (want && !g_bannerActive) {
                    g_bannerActive = true;
                    g_bannerH = BannerHeight((int)g_banners.size());
                    if (!g_bannerShown) {
                        g_bannerShown = true;
                        // 不许抢焦点：他正在别的 App 里打字，一块提示不该把输入法带出来。
                        glfwSetWindowAttrib(win, GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
                        glfwShowWindow(win);
                        glfwSetWindowAttrib(win, GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
                        g_relayout = true;
                        printf("[横幅] 弹出 %d 条，不点不收\n", (int)g_banners.size());
                        fflush(stdout);
                    }
                } else if (!want && g_bannerActive) {
                    g_bannerActive = false;
                    g_relayout = true;
                    if (g_panelVisible)
                        g_bannerShown = false;   // 窗口交给面板拿着了，收它归 TogglePanel 那条路
                    else if (g_bannerShown) {
                        g_bannerShown = false;
                        glfwHideWindow(win);
                        printf("[横幅] 画面撤了，窗口睡回去（不留在屏上占地方）\n");
                        fflush(stdout);
                    }
                }
            }
            if (!g_panelVisible && !g_bannerActive) {   // 收起时不空转 GPU，但扫描线程照跑，数字仍是活的
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
            // 系统说已经松开、这一侧还按着：补一次抬起（原因见 RealLeftDown 上那段）。
            // 出图轮不补：那一轮的按下是合成的，真指针本来就没按。
            if (!g_shot_to) {
                ImGuiIO& iio = ImGui::GetIO();
                if (iio.MouseDown[0] && !RealLeftDown()) iio.AddMouseButtonEvent(0, false);
            }
            ImGui::NewFrame();
            // 输入排在绘制之前：这一帧的悬停/按下用的是上一帧登记的命中区，
            // 算完过渡值才画，命中那一帧就亮（滞后的 16ms 比 150ms 的过渡短一个数量级）。
            // ── 标题栏拖动（无边框窗口没有系统拖动条）：按住表头空白拖动＝移动窗口 ──
            // 拖过 4pt 就把这一击作废（g_drag_moved，HandleInput 跳过它的 DoAction）；
            // 松手把位置写进设置盘，下次启动还落在拖到的地方。取证轮（SHOT/点击序列）不参与。
            {
                const ImGuiIO& io = ImGui::GetIO();
                static bool dragArm = false, dragMoved = false;
                static ImVec2 dragWin0(0, 0), dragMouse0(0, 0);
                const bool down = io.MouseDown[0] && !g_shot_to && g_click_seq.empty() &&
                                  g_panelVisible;
                int wx0 = 0, wy0 = 0;
                glfwGetWindowPos(win, &wx0, &wy0);
                const ImVec2 mp = io.MousePos;
                bool onHit = false;
                for (const auto& h2 : g_hits)
                    if (mp.x >= h2.x0 && mp.x < h2.x1 && mp.y >= h2.y0 && mp.y < h2.y1) {
                        onHit = true;
                        break;
                    }
                // 表头右端是图标簇（齿轮／搜索／折叠）：按在那儿是点按，绝不起拖。
                // 只靠命中区判不够——布局刚换帧时命中区还是上一帧的，正好漏掉刚点的那颗；
                // 所以再按几何划一条死线（图标簇宽 90pt 贴右沿）。
                const bool inHead = (mp.y <= 54.0f) && mp.x < io.DisplaySize.x - 90.0f;
                // 这一按若是把窗口叫醒的那一下，只做激活，不当作拖拽起点。
                const bool justActivated = !glfwGetWindowAttrib(win, GLFW_FOCUSED);
                // 全局光标点＝窗口原点＋视口内鼠标，两个数同一帧取，天然同源。
                // 别改用 CGEventGetLocation：它和窗口位置不同步，差一拍就会按「差多少」把窗口弹走
                // （2026-10-28 实拍：单击表头，面板自己挪了 72pt）。窗口一动，视口内鼠标反向补齐，
                // 这个点保持不变，所以没有反馈回路。
                const ImVec2 gCur((float)wx0 + mp.x, (float)wy0 + mp.y);
                if (down && !dragArm && inHead && !onHit && !justActivated) {
                    dragArm = true;
                    dragMoved = false;
                    dragWin0 = ImVec2((float)wx0, (float)wy0);
                    dragMouse0 = gCur;
                }
                if (dragArm && !down) dragArm = false;    // 没拖就松手＝一次普通空白点击
                if (dragArm && down) {
                    const float dx = gCur.x - dragMouse0.x, dy = gCur.y - dragMouse0.y;
                    if (dragMoved || dx * dx + dy * dy > 16.0f) {
                        dragMoved = true;
                        g_drag_moved = true;
                        glfwSetWindowPos(win, (int)std::lround(dragWin0.x + dx),
                                         (int)std::lround(dragWin0.y + dy));
                    }
                }
                if (!down && dragMoved) {                 // 拖完松手：落盘位置，放行下一击
                    dragMoved = false;
                    g_cfg.win_x = wx0;
                    g_cfg.win_y = wy0;
                    std::string err;
                    if (!ap::save_alert_cfg(g_cfg, &err))
                        fprintf(stderr, "[窗口] 位置没存下：%s\n", err.c_str());
                }
            }
            HandleInput(ImGui::GetIO(), glfwGetTime());
            g_drag_moved = false;
            {   // 滚动缓动：每帧向目标推近 24%，差半像素内贴齐——滚轮不再是瞬移
                g_scroll[g_view] += (g_scrollTarget[g_view] - g_scroll[g_view]) * 0.24f;
                if (std::fabs(g_scrollTarget[g_view] - g_scroll[g_view]) < 0.5f)
                    g_scroll[g_view] = g_scrollTarget[g_view];
                g_scroll[g_view] = std::fmin(g_scrollMax[g_view],
                                             std::fmax(0.0f, g_scroll[g_view]));
                g_scrollTarget[g_view] = std::fmin(g_scrollMax[g_view],
                                                   std::fmax(0.0f, g_scrollTarget[g_view]));
            }
            g_hits.clear();
            // 重扫描落地就重摆行。必须放在 NewFrame 之后：RebuildPanel 里要量字宽，
            // 而量字宽会在图集还没建起来时就把字形烘进去（图集此时 RendererHasTextures
            // 还是 false，走的是旧的全量预烘路），实测整屏文字会掉到一成亮度。
            long seq = g_seq.load();
            if (seq != g_pd_seq && !use_fixture) {
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
            static float lastPanelH = 0.0f;
            if (std::fabs(g_panelH - lastPanelH) > 1.0f) {   // 内容高变了：窗口跟着收/放
                lastPanelH = g_panelH;
                g_relayout = true;
            }
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
                           ViewName(g_view));
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
            // AGENTPING_CLICK 序列没走完也不拍：否则「A;B;C」三连点只会拍到 A 之后那一屏。
            int cw_now, ch_now; glfwGetWindowSize(win, &cw_now, &ch_now);
            const bool clicksDone = g_click_seq.empty() ||
                                    (g_click_at >= (int)g_click_seq.size() && g_click_gap == 0);
            if (shot_to && g_seq.load() >= 2 && glfwGetTime() - lastResize > 2.0 && clicksDone &&
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
                // 左上/左下角的 alpha：圆角外应当是 0；圆角内应当是 255。
                // 取样点横向 30pt：面板半径 23pt，顶行在 20pt 处正好压在抗锯齿边上（实测 170）。
                int tl = pix[(sh - 1) * sw * 4 + 3], tlin = pix[(sh - 1) * sw * 4 + 60 * 4 + 3];
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
        if (scanner.joinable()) scanner.join();
        FT_Done_FreeType(ft);
        return 0;
    }

    // 离屏探针：一屏一跑（AGENTPING_VIEW 0–8），六语横向并排渲一帧存 PPM。
    // 面板现在是固定 416×850 的浮层，内容超出走屏内滚动；探针这里把预算给足，
    // 让 WantedWindowH() 落到 850 —— 与真窗口同高，量出来的版面才有可比性。
    // kTileGap 必须 > kPanelW(416)，否则相邻 tile 压叠 62pt，六语并排图整张作废。
    const float kTileY = 50.0f, kTileGap = 440.0f;
    g_budgetH = 850.0f;
    g_probe_round = true;   // 从这一行起，合成点击只许走假落点，见 g_probe_round 的声明
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
            if (getenv("AGENTPING_FIXTURE")) LoadProbeFixture();   // 只影响探针这一段
        }
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)offW, (float)H));
        ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(g_font[T_TITLE][SC]);
        char stamp[96];
        time_t when;
        { std::lock_guard<std::mutex> lk(g_mtx); when = (time_t)g_snap.scanned_at; }
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&when));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
            "Agent Ping · 单壳 Dear ImGui 版 · 屏 %d（%s）· 活数据扫描于 %s · 六语并排 416×850%s",
            g_view, ViewTag(g_view), stamp,
            getenv("AGENTPING_FIXTURE") ? " · 样例数据（AGENTPING_FIXTURE）" : "");
        ImGui::PopFont();
        ImGui::End();
        for (int loc = 0; loc < kLocales; loc++) {
            drawPanel(14.0f + loc * kTileGap, kTileY, (float)kPanelW, loc, g_view);
            if (pass == 0) printf("[探针] 屏%d 语%d 内容 %.1fpt / 可视 %.1fpt / 滚动上限 %.1fpt\n",
                                  g_view, loc, g_contentH[g_view],
                                  g_bodyBot[g_view].y - g_bodyTop[g_view].y, g_scrollMax[g_view]);
        }
        if (pass == 0 && getenv("AGENTPING_HITS"))
            for (const auto& hh : g_hits)
                if (hh.act == HIT_HISTORY31)
                    printf("[命中区] 完成记录入口 x %.0f..%.0f y %.0f..%.0f 「%s」｜正文框 %.0f..%.0f\n",
                           hh.x0, hh.x1, hh.y0, hh.y1, hh.label.c_str(),
                           g_bodyTop[g_view].y, g_bodyBot[g_view].y);
        if (pass == 0 && getenv("AGENTPING_HITS")) fflush(stdout);

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
          g_quit.store(true); if (scanner.joinable()) scanner.join(); FT_Done_FreeType(ft);
          return 1;
      } }
    printf("[输出] %s (%dx%d)\n", out, w, h);
    g_quit.store(true);
    if (scanner.joinable()) scanner.join();
    FT_Done_FreeType(ft);
    return 0;
}

// 固定文案参与预烘；会话标题不参与（见 main 里那段注释）。
static void AddChromeText(ImFontGlyphRangesBuilder& rb) {
    for (const auto& s : kStr) for (int loc = 0; loc < kLocales; loc++) rb.AddText(s.t[loc]);
    rb.AddText("·—…$0123456789.,:/ABCDEFGHIJKLMNOPQRSTUVWXYZ ");
}

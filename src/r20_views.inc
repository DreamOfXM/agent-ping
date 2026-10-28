// ═══════════════════════════════════════════════════════════════════════════
// r20 版式层：表头 / 标签栏 / 总览 / 会话详情 / 授权详情 / 历史 / 设置 / 接线 / 回执。
// 逐条对照 agent-notify/r20/interaction.src.html 的 CSS 与六个 view 函数。
// 面板是固定 340×640：`.body{overflow-y:auto}` 在 ImGui 这边等价成「裁剪框 + 自算偏移」，
// 内容高由绘制这一趟量出来，下一帧给 HandleInput 判滚轮。
// ═══════════════════════════════════════════════════════════════════════════
#include "r20_atoms.inc"
#include <cstdarg>

#ifndef AP_VERSION
#define AP_VERSION "0.1"      // 与 CMakeLists 的 project(VERSION) 同源，见那里的注释
#endif

static std::string RFmt(const char* fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return std::string(b);
}

/// 稿子 `.q` 的 `等 49m`：先由 wait_for 拼句子，时长只念时长（见 Dur 的注释）。
static std::string RWait(const UIRow& r, int loc) {
    return RFmt(T("wait_for", loc), Dur(r.age, loc).c_str());
}

/// 会话短号：稿子 `.sid` 是 8 位。native_id 已经是去掉采集器前缀的那一截。
static std::string RShortId(const UIRow& r) {
    const std::string& s = r.native_id;
    if (s.empty()) return "";
    return s.size() > 8 ? s.substr(0, 8) : s;
}

static void RTimeOfDay(double epoch, int* hh, int* mm) {
    const std::time_t t = (std::time_t)epoch;
    std::tm tv;
    localtime_r(&t, &tv);
    *hh = tv.tm_hour;
    *mm = tv.tm_min;
}
static std::string RClock(double epoch) {
    int hh = 0, mm = 0;
    RTimeOfDay(epoch, &hh, &mm);
    return RFmt("%02d:%02d", hh, mm);
}

/// 行数据的两类：`.qrow` 看的是「停下来问」的会话，`.prow` 看的是卡在授权上的。
/// 判据直接借 RowAsksQuestion，与详情路由同一份，不另判一套。
static std::vector<const UIRow*> RAskRows() {
    std::vector<const UIRow*> v;
    for (const auto& r : g_pd.rows)
        if (r.state == ap::ST_AWAIT && !r.inferred && RowAsksQuestion(r)) v.push_back(&r);
    return v;
}
static std::vector<const UIRow*> RPermRows() {
    std::vector<const UIRow*> v;
    for (const auto& r : g_pd.rows)
        if (r.state == ap::ST_AWAIT && !r.inferred && !RowAsksQuestion(r) &&
            (!r.tool.empty() || !r.question.empty()))
            v.push_back(&r);
    return v;
}
/// 稿子给危险命令的行框点红：判据是 CLI 真递过来的那条命令里有没有这两类前缀。
static bool RDanger(const UIRow& r) {
    const std::string& s = r.question;
    return s.find("rm ") != std::string::npos || s.find("sudo ") != std::string::npos;
}
static bool RIsWriteTool(const std::string& tool) {
    return tool == "Write" || tool == "Edit" || tool == "write_file" || tool == "apply_patch";
}

// ── 面板外框：#win{border:1px solid #262C34;border-radius:14px;inset 0 1px 0 white/6%} ──
static void RPanelFrame(ImDrawList* dl, float x, float y, float w, float h) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), kBg, kCardR);
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f),
                kRPanelLine, kCardR, 0, 1.0f);
    dl->AddLine(ImVec2(x + 14.0f, y + 1.5f), ImVec2(x + w - 14.0f, y + 1.5f), kRInset, 1.0f);
}

// ── .head{height:42px;padding:0 14px;gap:8px} ─────────────────────────────
// 接线屏的表头同一块，右端把时钟换成 `跳过`（data-act="skip"）。
static void RHead(ImDrawList* dl, float x, float y, float w, int loc, bool setup) {
    const float L = x + kRPaneBorder + kRPadX, R = x + w - kRPaneBorder - kRPadX;
    const float cy = y + kRPaneBorder + kRHeadH * 0.5f;
    BrandMark(dl, L, cy - kLogoD * 0.5f, kLogoD);
    float wx = L + kLogoD + 8.0f;
    wx = DrawTextRun(dl, wx, TextMid(cy, T_TITLE, loc), "Agent Ping", loc, kT1, T_TITLE);
    DrawTextRun(dl, wx + 6.0f, TextMid(cy, T_SM, loc), "v" AP_VERSION, loc, kT4, T_SM);
    if (setup) {
        const std::string sk = T("skip", loc);
        const float sw = TextWidth(sk, loc, T_LABEL);
        DrawTextRight(dl, R, TextMid(cy, T_LABEL, loc), sk, loc, kT4, T_LABEL);
        AddHit(g_hits, R - sw - 6.0f, cy - 12.0f, R + 6.0f, cy + 12.0f,
               HIT_SKIP, kNavRow, 0, loc, sk, 0);
    } else {
        DrawTextRight(dl, R, TextMid(cy, T_SEG, loc), RClock(g_pd.now), loc, kT3, T_SEG);
    }
    RHair(dl, x + kRPaneBorder, y + kRPaneBorder + kRHeadH - 1.0f, x + w - kRPaneBorder, kLine);
}

// ── .dhead{height:40px;padding:0 12px}：返回 + 项目 + 已等 ──────────────────
// r（可空）：详情屏选中行被重扫挤掉时仍要摆同一条头，只是没有项目名与等待时长。
static void RDetailHead(ImDrawList* dl, float x, float y, float w, int loc, const UIRow* r) {
    const float L = x + kRPaneBorder + 12.0f, R = x + w - kRPaneBorder - 12.0f;
    const float cy = y + kRPaneBorder + kRDHeadH * 0.5f;
    const std::string bk = T("tab_overview", loc);      // 返回的标签写目的地，不写「返回」
    const float bw = 14.0f + 4.0f + TextWidth(bk, loc, T_LABEL);
    DrawIcon(dl, RI_BACK, L, cy - 7.0f, 14.0f, kT3);
    DrawTextRun(dl, L + 18.0f, TextMid(cy, T_LABEL, loc), bk, loc, kT3, T_LABEL);
    AddHit(g_hits, L - 4.0f, cy - 12.0f, L + bw + 4.0f, cy + 12.0f,
           HIT_BACK, kNavRow, 0, loc, bk, 0);
    const std::string wt = r ? RWait(*r, loc) : std::string();
    const float ww = wt.empty() ? 0.0f : TextWidth(wt, loc, T_FAIL);
    const float tx = L + bw + 8.0f;
    const std::string title = r ? (r->project.empty() ? r->agent : r->project) : std::string();
    if (!title.empty())
        DrawTextRun(dl, tx, TextMid(cy, T_S12B, loc),
                    ClipToWidth(title, R - ww - 8.0f - tx, loc, T_S12B), loc, kT1, T_S12B);
    if (!wt.empty())
        DrawTextRight(dl, R, TextMid(cy, T_FAIL, loc), wt, loc, kAmber, T_FAIL);
    RHair(dl, x + kRPaneBorder, y + kRPaneBorder + kRDHeadH - 1.0f, x + w - kRPaneBorder, kLine);
}

// ── .tabs{height:44px;border-top:1px;background:var(--surf)} ───────────────
static void RTabs(ImDrawList* dl, float x, float y, float w, int loc) {
    const float h = kRTabsH;
    const float x0 = x + kRPaneBorder, x1 = x + w - kRPaneBorder;
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), kSurf, 13.0f, ImDrawFlags_RoundCornersBottom);
    RHair(dl, x0, y, x1, kLine);

    const int badge = (int)(RAskRows().size() + RPermRows().size());
    const int view[3] = {0, 5, 1};
    const int icon[3] = {RI_LIST, RI_CLOCK, RI_SLIDERS};
    const char* key[3] = {"tab_overview", "nav_history", "set_title"};
    const float tw = (x1 - x0) / 3.0f;
    const float cy = y + h * 0.5f;
    for (int i = 0; i < 3; i++) {
        const float tx0 = x0 + tw * (float)i;
        const bool on = (g_tab == view[i]);
        const long z = Zid(loc, kNavRow, HIT_TABOV, view[i]);
        const float hv = TxPeek(z);
        const ImU32 col = on ? kT1 : MixA(kT4, kT2, hv);
        const std::string lb = T(key[i], loc);
        const float lw = TextWidth(lb, loc, T_LABEL);
        char nb[16] = "";
        float bdw = 0.0f;
        if (i == 0 && badge > 0) {
            snprintf(nb, sizeof nb, "%d", badge);
            bdw = TextWidth(nb, loc, T_TIME) + 8.0f;   // .bdg{font:600 9.5/13 mono;padding:0 4px}
        }
        const float total = 14.0f + 6.0f + lw + (bdw > 0 ? 6.0f + bdw : 0.0f);
        float px = tx0 + (tw - total) * 0.5f;
        DrawIcon(dl, icon[i], px, cy - 7.0f, 14.0f, col);
        px += 14.0f + 6.0f;
        DrawTextRun(dl, px, TextMid(cy, T_LABEL, loc), lb, loc, col, T_LABEL);
        px += lw;
        if (bdw > 0) {
            px += 6.0f;
            dl->AddRectFilled(ImVec2(px, cy - 6.5f), ImVec2(px + bdw, cy + 6.5f), WithA(kAmber, 33), 4.0f);
            DrawTextRun(dl, px + (bdw - TextWidth(nb, loc, T_TIME)) * 0.5f,
                        TextMid(cy, T_TIME, loc), nb, loc, kAmber, T_TIME);
        }
        if (on) {
            const float cx = tx0 + tw * 0.5f;
            dl->AddRectFilled(ImVec2(cx - 7.0f, y + h - 9.0f), ImVec2(cx + 7.0f, y + h - 7.0f),
                              kAmber, 2.0f);
        }
        AddHit(g_hits, tx0, y, tx0 + tw, y + h, HIT_TABOV, kNavRow, view[i], loc, lb, on ? 1 : 0);
    }
}

// ── 总览 ───────────────────────────────────────────────────────────────────
/// `.live`：三行＝9 上 + 16 头像行 + 7 + 16.5 命令行 + 6 + 10.5 元信息行 + 10 下。
/// 命令行只在采集层真给出最后一条 tool_use 时才画（没有就整行不画，高 51.5）。
static float RLive(ImDrawList* dl, float L, float R, float y, const UIRow& r, int loc, double tn) {
    const bool has_cmd = !r.last_cmd.empty();
    // .live 的 1px 边框要算进高度：稿子里实测 77 / 53.5。
    const float h = has_cmd ? 77.0f : 53.5f;
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + h), kSurf, 10.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + h - 0.5f), kLine, 10.0f, 0, 1.0f);
    const float lx = L + 11.0f, rx = R - 11.0f;
    const float cy1 = y + 9.0f + 8.0f;
    RAgentTile(dl, lx, cy1 - 8.0f, 16.0f, 4.0f, r.hue, r.initial, loc, r.agent_id);
    const std::string proj = r.project.empty() ? r.agent : r.project;
    const std::string sid = RShortId(r);
    const std::string st = T("running_now", loc);
    const float stw = TextWidth(st, loc, T_BTN);
    const float sidw = sid.empty() ? 0.0f : TextWidth(sid, loc, T_SM) + 7.0f;
    float px = lx + 16.0f + 7.0f;
    const float praw = rx - stw - 6.0f - 5.0f - 6.0f;      // 右侧：圆点 6 + gap 5 + 文字
    DrawTextRun(dl, px, TextMid(cy1, T_TITLE, loc),
                ClipToWidth(proj, praw - sidw - px, loc, T_TITLE), loc, kT1, T_TITLE);
    if (!sid.empty()) {
        const float pw = TextWidth(ClipToWidth(proj, praw - sidw - px, loc, T_TITLE), loc, T_TITLE);
        DrawTextRun(dl, px + pw + 7.0f, TextMid(cy1, T_SM, loc), sid, loc, kT4, T_SM);
    }
    RDot(dl, rx - stw - 5.0f - 3.0f, cy1, kCyan, tn, true);
    DrawTextRight(dl, rx, TextMid(cy1, T_BTN, loc), st, loc, kCyan, T_BTN);

    float y3 = y + 9.0f + 16.0f + 6.0f;
    if (has_cmd) {
        // .cmd{margin-top:7;font:11/1.5 mono t2}：`$` 是 t4 且与命令隔 5，光标 6×11 cyan 75%
        const float cy2 = y + 9.0f + 16.0f + 7.0f + 16.5f * 0.5f;
        float cx = lx;
        cx = DrawTextRun(dl, cx, TextMid(cy2, T_SEG, loc), "$", loc, kT4, T_SEG) + 5.0f;
        const std::string cmd = ClipToWidth(r.last_cmd, rx - cx - 6.0f - 3.0f, loc, T_SEG);
        cx = DrawTextRun(dl, cx, TextMid(cy2, T_SEG, loc), cmd, loc, kT2, T_SEG);
        if (std::fmod(tn, 1.1) < 0.55)
            dl->AddRectFilled(ImVec2(cx + 3.0f, cy2 - 5.5f), ImVec2(cx + 9.0f, cy2 + 5.5f),
                              WithA(kCyan, 191));                 // opacity .75
        y3 = y + 9.0f + 16.0f + 7.0f + 16.5f + 6.0f;
    }
    // .l3：轮数 · 连续跨度 · 上一步多久以前，三项 gap 10；跨度不在今天就不画那一项。
    float x3 = lx;
    x3 = DrawTextRun(dl, x3, TextMid(y3 + 5.25f, T_FAIL, loc),
                     RFmt(T("turns_n", loc), r.turns), loc, kT4, T_FAIL) + 10.0f;
    if (r.span_sec > 0) {
        x3 = DrawTextRun(dl, x3, TextMid(y3 + 5.25f, T_FAIL, loc),
                         RFmt(T("span_contig", loc), Dur(r.span_sec, loc).c_str()),
                         loc, kT4, T_FAIL) + 10.0f;
    }
    DrawTextRun(dl, x3, TextMid(y3 + 5.25f, T_FAIL, loc),
                RFmt(T("step_ago", loc), Dur(r.age, loc).c_str()), loc, kT4, T_FAIL);
    return h;
}

/// `.qrow`：10/11 内边距 + 14 抬头 + 5 + 问题（1–2 行 ×19.575）+ 8 + 26 操作行。
static float RQuestionRow(ImDrawList* dl, float L, float R, float y, const UIRow& r, int idx,
                          int loc, bool hot) {
    const float lx = L + 2.0f, rx = R - 2.0f;
    const float cy1 = y + 10.0f + 7.0f;
    RAgentTile(dl, lx, cy1 - 7.0f, 14.0f, 3.0f, r.hue, r.initial, loc, r.agent_id);
    const std::string proj = r.project.empty() ? r.agent : r.project;
    const std::string wt = RWait(r, loc);
    const float ww = TextWidth(wt, loc, T_FAIL);
    const float px = lx + 14.0f + 6.0f;
    DrawTextRun(dl, px, TextMid(cy1, T_LABEL, loc),
                ClipToWidth(proj, rx - ww - 8.0f - px, loc, T_LABEL), loc, kT2, T_LABEL);
    DrawTextRight(dl, rx, TextMid(cy1, T_FAIL, loc), wt, loc, hot ? kAmber : kT4, T_FAIL);

    const std::string q = r.question.empty() ? r.title : r.question;
    const float lineH = kTierPx[T_QT] * 1.45f;
    const float qtTop = y + 10.0f + 14.0f + 5.0f;
    const float qtH = DrawRuns(dl, lx, rx, qtTop, {{q, kT1}}, loc, T_QT, lineH, 2);

    const float actY = qtTop + qtH + 8.0f;
    const float pillH = 26.0f;
    // 整行是 button.q（data-open）：先登记，后两个小零件盖在它上面。
    AddHit(g_hits, L, y, R, actY + pillH + 11.0f, HIT_OPEN, idx, 0, r, loc, q);
    if (!r.options.empty()) {
        const std::string& opt = r.options[0];
        const std::string shown = ClipToWidth(opt, 212.0f - 20.0f - 14.0f - 6.0f, loc, T_LABEL);
        const float pw = std::fmin(212.0f, 20.0f + 14.0f + 6.0f + TextWidth(shown, loc, T_LABEL));
        dl->AddRectFilled(ImVec2(lx, actY), ImVec2(lx + pw, actY + pillH), WithA(kAmber, 36), 7.0f);
        dl->AddRect(ImVec2(lx + 0.5f, actY + 0.5f), ImVec2(lx + pw - 0.5f, actY + pillH - 0.5f),
                    WithA(kAmber, 97), 7.0f, 0, 1.0f);
        DrawIcon(dl, RI_COPY, lx + 10.0f, actY + (pillH - 14.0f) * 0.5f, 14.0f, kAmber);
        DrawTextRun(dl, lx + 10.0f + 14.0f + 6.0f, TextMid(actY + pillH * 0.5f, T_LABEL, loc),
                    shown, loc, kAmber, T_LABEL);
        Hit hh;
        hh.x0 = lx; hh.y0 = actY; hh.x1 = lx + pw; hh.y1 = actY + pillH;
        hh.act = HIT_COPY; hh.row = idx; hh.opt = 0;
        hh.zid = Zid(loc, idx, HIT_COPY, 0);
        hh.key = r.key; hh.agent_id = r.agent_id; hh.native_id = r.native_id;
        hh.label = opt; hh.title = proj;
        g_hits.push_back(hh);
    }
    // 右侧 `N 个选项 ›`：与 pill 同一行，两处都通详情。
    const std::string mo = RFmt(T("det_opt_count", loc), (int)r.options.size());
    const float mw = TextWidth(mo, loc, T_BTN);
    const float mx = rx - 14.0f - 3.0f - mw;
    DrawTextRun(dl, mx, TextMid(actY + pillH * 0.5f, T_BTN, loc), mo, loc, kT4, T_BTN);
    DrawIcon(dl, RI_CHEV, rx - 14.0f, actY + (pillH - 14.0f) * 0.5f, 14.0f, kT4);
    AddHit(g_hits, mx - 4.0f, actY, rx + 4.0f, actY + pillH, HIT_OPEN, idx, 0, r, loc, mo);
    // 行高 93.56；`.qrow:last-child` 无底边，1px 边框由调用方按「不是最后一行」补。
    return (actY + pillH + 11.0f) - y;
}

/// `.prow`：24 图标 + 两行（项目 + 工具标签 / 命令原文）+ 等 + ›。
static float RPermRow(ImDrawList* dl, float L, float R, float y, const UIRow& r, int idx, int loc) {
    const float lx = L + 2.0f, rx = R - 2.0f;
    // `.mid .a` 是 flex 行：项目 12/1 与标签 9.5/1+上下各 2 取高 → 13.5；
    // `.cm` 的行内框 13.65 套在 12px 正文的 strut 里 → 14.64。两者相加 32.14。
    const float l1 = 13.5f;
    const float l2 = 14.64f;
    const float contentH = l1 + 4.0f + l2;
    const float rowH = 9.0f + contentH + 9.0f;    // 50.14；1px 底边由调用方补
    const float cy = y + 9.0f + contentH * 0.5f;
    const bool danger = RDanger(r);
    const ImU32 tc = danger ? kRed : kT3;
    // .ti{24×24;radius 6;bg white/5%;border line}；.ti.hot＝红 10% 底 + 28% 边
    dl->AddRectFilled(ImVec2(lx, cy - 12.0f), ImVec2(lx + 24.0f, cy + 12.0f),
                      danger ? WithA(kRed, 26) : W(13), 6.0f);
    dl->AddRect(ImVec2(lx + 0.5f, cy - 11.5f), ImVec2(lx + 23.5f, cy + 11.5f),
                danger ? WithA(kRed, 71) : kLine, 6.0f, 0, 1.0f);
    DrawIcon(dl, RIsWriteTool(r.tool) ? RI_FILE : RI_TERM, lx + 5.0f, cy - 7.0f, 14.0f, tc);

    const float tx = lx + 24.0f + 9.0f;
    const std::string wt = Dur(r.age, loc);
    const float ww = TextWidth(wt, loc, T_FAIL);
    const float chw = 14.0f + 9.0f;
    const float praw = rx - ww - 9.0f - chw;
    const std::string proj = r.project.empty() ? r.agent : r.project;
    const std::string tag = r.tool;
    // .tl{9.5/1 mono;padding 2px 4px;radius 3;bg white/6%}：宽按字宽 + 8
    const float tagw = tag.empty() ? 0.0f : TextWidth(tag, loc, T_TIME) + 8.0f;
    const float ay = y + 9.0f + l1 * 0.5f;
    const std::string projShown = ClipToWidth(proj, praw - tx - tagw - 6.0f, loc, T_TOOL);
    DrawTextRun(dl, tx, TextMid(ay, T_TOOL, loc), projShown, loc, kT1, T_TOOL);
    if (!tag.empty()) {
        const float tgx = tx + TextWidth(projShown, loc, T_TOOL) + 6.0f;
        dl->AddRectFilled(ImVec2(tgx, ay - 6.75f), ImVec2(tgx + tagw, ay + 6.75f), W(15), 3.0f);
        DrawTextRun(dl, tgx + 4.0f, TextMid(ay, T_TIME, loc), tag, loc, kT3, T_TIME);
    }
    const std::string cmd = r.question.empty() ? r.title : r.question;
    const float cmTop = y + 9.0f + l1 + 4.0f;
    DrawTextRun(dl, tx, TextMid(cmTop + l2 * 0.5f, T_FAIL, loc),
                ClipToWidth(cmd, praw - tx, loc, T_FAIL), loc, kT3, T_FAIL);
    DrawTextRight(dl, rx - chw, TextMid(cy, T_FAIL, loc), wt, loc, kT4, T_FAIL);
    DrawIcon(dl, RI_CHEV, rx - 14.0f, cy - 7.0f, 14.0f, kT5);
    AddHit(g_hits, L, y, R, y + rowH, HIT_OPEN, idx, 0, r, loc, cmd);
    return rowH;
}

/// 空态：`.empty{flex:1;justify-content:center;padding:0 40px;gap:10px}`
static float REmpty(ImDrawList* dl, float L, float R, float top, float bot, int loc,
                    const UIRow* live) {
    const float cx = (L + R) * 0.5f;
    const std::string h3 = T("empty_none", loc);
    const std::string p = live ? RFmt(T("empty_live", loc),
                                      (live->project.empty() ? live->agent : live->project).c_str())
                               : std::string(T("empty_idle", loc));
    const auto lines = WrapLines(p, R - L - 80.0f, loc, T_CHIP, 4);   // .empty p{11px sans}
    const float h3lh = kTierPx[T_H3] * 1.4f;
    const float plh = kTierPx[T_CHIP] * 1.65f;
    // justify-content:center：44 环 + 10 + h3 + 10 + p 整列居中于 .empty（下边到内边距 14 为止）
    const float colH = 44.0f + 10.0f + h3lh + 10.0f + plh * (float)lines.size();
    const float cy = (top + bot - kRInnerBot) * 0.5f - colH * 0.5f + 22.0f;
    dl->AddCircle(ImVec2(cx, cy), 22.0f, kLine2, 48, 1.0f);
    DrawIcon(dl, RI_CHECK, cx - 9.0f, cy - 9.0f, 18.0f, kGreen);
    const float hw = TextWidth(h3, loc, T_H3);
    DrawTextRun(dl, cx - hw * 0.5f, cy + 32.0f, h3, loc, kT1, T_H3);
    float yy = cy + 32.0f + h3lh + 10.0f;
    for (const auto& ln : lines) {
        DrawTextRun(dl, cx - TextWidth(ln, loc, T_CHIP) * 0.5f, yy, ln, loc, kT4, T_CHIP);
        yy += plh;
    }
    return yy;
}

static float ROverview(ImDrawList* dl, float L, float R, float top, float bot, int loc, double tn) {
    float y = top;
    const UIRow* live = (g_pd.hero >= 0 && g_pd.hero < (int)g_pd.rows.size())
                            ? &g_pd.rows[(size_t)g_pd.hero] : nullptr;
    if (live) { y += 8.0f; y += RLive(dl, L, R, y, *live, loc, tn); }

    const auto qs = RAskRows();
    const auto ps = RPermRows();
    if (qs.empty() && ps.empty()) {
        REmpty(dl, L, R, std::max(y, top), bot, loc, live);
        return y;
    }
    if (!qs.empty()) {
        double maxAge = 0;
        for (auto* r : qs) maxAge = std::fmax(maxAge, r->age);
        const char* txKey = "sl_q";
        y += RSLabel(dl, L, R, y, loc, txKey, (int)qs.size(), true,
                     RFmt(T("longest", loc), Dur(maxAge, loc).c_str()), true,
                     live ? 12.0f : 10.0f, 7.0f);
        RHair(dl, L, y, R, kLine);                       // .rows{border-top}
        y += 1.0f;
        for (size_t i = 0; i < qs.size(); i++) {
            // 「等」只亮最久的那一条：与节头写的「最久 X」是同一个数，不另设阈值。
            y += RQuestionRow(dl, L, R, y, *qs[i], (int)i, loc, qs[i]->age >= maxAge);
            if (i + 1 < qs.size()) {
                RHair(dl, L, y, R, kLine);
                y += 1.0f;
            }
        }
    }
    if (!ps.empty()) {
        y += RSLabel(dl, L, R, y, loc, "sl_p", (int)ps.size(), false,
                     T("confirm_term", loc), false, 12.0f, 7.0f);
        RHair(dl, L, y, R, kLine);
        y += 1.0f;
        for (size_t i = 0; i < ps.size(); i++) {
            y += RPermRow(dl, L, R, y, *ps[i], (int)i, loc);
            if (i + 1 < ps.size()) {
                RHair(dl, L, y, R, kLine);
                y += 1.0f;
            }
        }
    }
    return y;
}

// ── 两屏详情共用的来源卡：.src{50.4 高;padding 9 10;radius 9} ───────────────
// subTool：授权屏的第二行是「项目 · 工具」，提问屏是「项目 · 会话短号」。
static float RSourceCard(ImDrawList* dl, float L, float R, float y, const UIRow& r, int loc,
                         const char* statusKey, ImU32 statusCol, bool pulse, double tn,
                         bool subTool) {
    // 稿子的 `.src` 实测 54.39 高：两行文字各占一个行盒，上下 9 + 1px 边框。
    const float h = 54.39f;
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + h), kSurf, 9.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + h - 0.5f), kLine, 9.0f, 0, 1.0f);
    const float ax = L + 10.0f;
    RAgentTile(dl, ax, y + (h - 18.0f) * 0.5f, 18.0f, 4.0f, r.hue, r.initial, loc, r.agent_id);
    const std::string b = (r.project.empty() ? r.agent : r.project) + " · " +
                          (subTool ? (r.tool.empty() ? std::string("—") : r.tool) : r.native_id);
    const std::string st = T(statusKey, loc);
    const float stw = TextWidth(st, loc, T_BTN) + 11.0f;
    const float tx = ax + 18.0f + 9.0f;
    DrawTextRun(dl, tx, y + 13.0f, ClipToWidth(r.agent, R - 10.0f - stw - tx, loc, T_S12B),
                loc, kT1, T_S12B);
    DrawTextRun(dl, tx, y + 30.4f,
                ClipToWidth(b, R - 10.0f - stw - tx, loc, T_SM), loc, kT4, T_SM);
    const float scy = y + h * 0.5f;
    if (pulse) RDot(dl, R - 10.0f - stw + 5.0f, scy, statusCol, tn, true);
    else dl->AddCircleFilled(ImVec2(R - 10.0f - stw + 5.0f, scy), 3.0f, statusCol, 14);
    DrawTextRight(dl, R - 10.0f, TextMid(scy, T_BTN, loc), st, loc, statusCol, T_BTN);
    return h;
}

/// .kv 的一行：7/2 内边距 + 74 宽键 + 10 间距 + 值（mono 10.5；触发说明那行按稿子走 sans 11）。
static float RKv(ImDrawList* dl, float L, float R, float y, const char* key, const std::string& val,
                 int loc, int valTier = T_FAIL) {
    const float lh = kTierPx[T_CHIP] * 1.4f;
    const float rowH = 7.0f + lh + 7.0f + 1.0f;       // `.kv .r` 的 border-bottom 进高度
    const float cy = y + 7.0f + lh * 0.5f;
    DrawTextRun(dl, L + 2.0f, TextMid(cy, T_CHIP, loc), T(key, loc), loc, kT4, T_CHIP);
    const float vx = L + 2.0f + 74.0f + 10.0f;
    DrawTextRun(dl, vx, TextMid(cy, valTier, loc),
                ClipToWidth(val, R - 2.0f - vx, loc, valTier), loc, kT2, valTier);
    RHair(dl, L, y + rowH - 1.0f, R, kLine);
    return rowH;
}

/// .hint：14 图标 + 7 间距 + 10.5/1.55 说明（`.hint{gap:7px}`），行距按稿子的 1.55。
static float RHint(ImDrawList* dl, float L, float R, float y, int icon, const std::string& text,
                   int loc) {
    DrawIcon(dl, icon, L, y + 1.0f, 14.0f, kT4);
    const float lineH = kTierPx[T_S105] * 1.55f;
    const float h = DrawRuns(dl, L + 14.0f + 7.0f, R, y, {{text, kT4}}, loc, T_S105, lineH, 0);
    return std::fmax(h, 14.0f);
}

// ── 会话详情 ───────────────────────────────────────────────────────────────
static float RSessionView(ImDrawList* dl, float L, float R, float y, int loc, double tn) {
    const UIRow* r = SelectedRow(g_sel_key);
    if (!r) {
        const std::string em = T("det_empty", loc);
        DrawTextRun(dl, L + (R - L - TextWidth(em, loc, T_SM)) * 0.5f, y + 40.0f, em,
                    loc, kT4, T_SM);
        return y + 40.0f + kTierPx[T_SM] * 1.5f;
    }
    y += RSourceCard(dl, L, R, y, *r, loc, "in_wait", kCyan, true, tn, false) + 12.0f;
    const std::string q = r->question.empty() ? r->title : r->question;
    y += DrawRuns(dl, L, R, y, {{q, kT1}}, loc, T_HERO, kTierPx[T_HERO] * 1.5f, 0);

    if (!r->options.empty()) {
        y += 14.0f;
        const std::string lab = RFmt(T("det_optlab", loc), (int)r->options.size());
        DrawTextRun(dl, L, TextTop(y, kTierPx[T_10S], T_10S, loc), lab, loc, kT3, T_10S,
                    kTierPx[T_10S] * 0.14f);
        y += kTierPx[T_10S];   // 下面每个选项的 margin-top 6 与 .optlab 的 6 折叠成 6
        for (size_t i = 0; i < r->options.size() && i < 6; i++) {
            y += 6.0f;
            const std::string ds = i < r->option_desc.size() ? r->option_desc[i] : std::string();
            const float lbH = kTierPx[T_S125] * 1.35f;
            const float dsH = kTierPx[T_S105] * 1.4f;      // .opt .ds{10.5px/1.4}
            const float oh = 9.0f + lbH + (ds.empty() ? 0.0f : 2.0f + dsH) + 9.0f + 2.0f;
            const long z = Zid(loc, (int)i, HIT_COPY, 0);
            const float hv = TxPeek(z);
            // 稿子把第一条标成 `.opt.rec`：琥珀 2pt 左轨 + 琥珀 35% 边 + `推荐` 小签。
            // 它对应总览那颗一键胶囊的同一份文本（options[0]），不是另算的评分。
            const bool rec = i == 0;
            const std::string tag = T("det_rec", loc);
            const float tagw = rec ? TextWidth(tag, loc, T_S95) + 10.0f : 0.0f;
            dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + oh), MixA(kSurf, kSurf2, hv), 9.0f);
            dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + oh - 0.5f),
                        rec ? WithA(kAmber, 89) : MixA(kLine, kLine2, hv), 9.0f, 0, 1.0f);
            if (rec) dl->AddRectFilled(ImVec2(L - 1.0f, y + 8.0f), ImVec2(L + 1.0f, y + oh - 8.0f),
                                       kAmber, 1.0f);
            // 盒子里有 1px 边：内边距 9 从 y+10 起（DOM 实测 opt 53.56 = 1+9+33.56+9+1）。
            // 横向同理：padding 10 从 L+11 起（k x=356=L+11、cp 右边=R−11）。
            // 序号/签/复制图标都是 flex 子项，按整盒垂直居中排，不跟第一行。
            const float ky = y + oh * 0.5f;
            dl->AddRect(ImVec2(L + 11.0f, ky - 8.0f), ImVec2(L + 27.0f, ky + 8.0f), kLine2, 4.0f, 0, 1.0f);
            const std::string kn = RFmt("%d", (int)i + 1);
            DrawTextRun(dl, L + 19.0f - TextWidth(kn, loc, T_M10B) * 0.5f, TextMid(ky, T_M10B, loc),
                        kn, loc, kT4, T_M10B);
            const float lbx = L + 35.0f;
            const float lbw = R - 11.0f - 14.0f - 8.0f - lbx - (rec ? tagw + 8.0f : 0.0f);
            DrawTextRun(dl, lbx, TextTop(y + 10.0f, lbH, T_S125, loc),
                        ClipToWidth(r->options[i], lbw, loc, T_S125), loc, kT1, T_S125);
            if (!ds.empty())
                DrawTextRun(dl, lbx, TextTop(y + 10.0f + lbH + 2.0f, dsH, T_S105, loc),
                            ClipToWidth(ds, lbw, loc, T_S105), loc, kT4, T_S105);
            if (rec) {
                // `.tag{9.5/1;padding 2px 4px;border 1px rgba(amber,.4);radius 3}`，整行居中
                const float tx = R - 11.0f - 14.0f - 8.0f - tagw;
                const float ty = y + oh * 0.5f - 7.75f;
                dl->AddRect(ImVec2(tx + 0.5f, ty + 0.5f), ImVec2(tx + tagw - 0.5f, ty + 15.0f),
                            WithA(kAmber, 102), 3.0f, 0, 1.0f);
                DrawTextRun(dl, tx + 5.0f, ty + 3.0f, tag, loc, kAmber, T_S95);
            }
            DrawIcon(dl, RI_COPY, R - 11.0f - 14.0f, y + oh * 0.5f - 7.0f, 14.0f, kT5);
            AddHit(g_hits, L, y, R, y + oh, HIT_COPY, (int)i, 0, *r, loc, r->options[i]);
            y += oh;
        }
    }

    y += 12.0f;
    RHair(dl, L, y, R, kLine);
    y += 1.0f;
    y += RKv(dl, L, R, y, "det_session", r->native_id.empty() ? r->key : r->native_id, loc);
    y += RKv(dl, L, R, y, "det_wait", Dur(r->age, loc), loc);
    y += RKv(dl, L, R, y, "src_signal", T("det_source_val", loc), loc);
    y += 12.0f;
    y += RHint(dl, L, R, y, RI_INFO, RFmt(T("det_hint_q", loc), r->agent.c_str()), loc);
    return y;
}

// ── 授权详情 ───────────────────────────────────────────────────────────────
static float RPermView(ImDrawList* dl, float L, float R, float y, int loc, double tn) {
    const UIRow* r = SelectedRow(g_sel_key);
    if (!r) {
        const std::string em = T("perm_empty", loc);
        DrawTextRun(dl, L + (R - L - TextWidth(em, loc, T_SM)) * 0.5f, y + 40.0f, em,
                    loc, kT4, T_SM);
        return y + 40.0f + kTierPx[T_SM] * 1.5f;
    }
    y += RSourceCard(dl, L, R, y, *r, loc, "perm_status", kRed, false, tn, true) + 12.0f;

    // .term{background:var(--recess);padding:10px 11px;inset 0 2px 6px black/55%}
    // 正文里 CLI 递来的原文就是这一行；bash 才带 $ 提示符。
    const std::string cmd = r->question.empty() ? r->title : r->question;
    const bool shell = (r->tool == "Bash" || r->tool == "bash" || r->tool == "shell");
    const float lineH = kTierPx[T_SEG] * 1.6f;
    const float lx = L + 11.0f, rx = R - 11.0f;
    const float px = shell ? lx + TextWidth("$", loc, T_SEG) + 6.0f : lx;
    const auto lines = WrapLines(cmd, rx - px, loc, T_SEG, 8);
    const float termH = 10.0f + lineH * (float)lines.size() + 10.0f + 2.0f;   // .term 的 1px 边框
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + termH), kRecess, 9.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + termH - 0.5f), kLine, 9.0f, 0, 1.0f);
    if (shell)
        DrawTextRun(dl, lx, TextTop(y + 10.0f, lineH, T_SEG, loc), "$", loc, kT4, T_SEG);
    const ImU32 cc = RDanger(*r) ? kRed : kT2;
    float ly = y + 10.0f;
    for (const auto& ln : lines) {
        DrawTextRun(dl, px, TextTop(ly, lineH, T_SEG, loc), ln, loc, cc, T_SEG);
        ly += lineH;
    }
    y += termH + 12.0f;

    RHair(dl, L, y, R, kLine);
    y += 1.0f;
    // 触发说明：hook 批准类信号自带的原文。没有这条信号时才退回「工具」行。
    if (!r->trigger.empty())
        y += RKv(dl, L, R, y, "perm_trigger", r->trigger, loc, T_CHIP);
    else
        y += RKv(dl, L, R, y, "perm_tool", r->tool.empty() ? std::string("—") : r->tool, loc);
    y += RKv(dl, L, R, y, "det_wait", Dur(r->age, loc), loc);
    y += RKv(dl, L, R, y, "perm_project", r->project.empty() ? r->agent : r->project, loc);
    y += RKv(dl, L, R, y, "det_source", T("det_source_val", loc), loc);
    y += 12.0f;
    y += RHint(dl, L, R, y, RI_SHIELD, T("perm_hint", loc), loc);

    // .gobtn{height:32;margin-top:14;border:line2;background:surf2}
    y += 14.0f;
    const std::string jb = RFmt(T("jump_project", loc),
                                (r->project.empty() ? r->agent : r->project).c_str());
    const long z = Zid(loc, 200, HIT_JUMP, 0);
    const float hv = TxPeek(z);
    dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + 32.0f), MixA(kSurf2, kRGoHover, hv), 8.0f);
    dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + 31.5f), kLine2, 8.0f, 0, 1.0f);
    const float jw = TextWidth(jb, loc, T_TOOL);
    const float jx = (L + R - jw - 7.0f - 14.0f) * 0.5f;
    DrawIcon(dl, RI_JUMP, jx, y + 9.0f, 14.0f, kT1);
    DrawTextRun(dl, jx + 21.0f, TextMid(y + 16.0f, T_TOOL, loc), jb, loc, kT1, T_TOOL);
    AddHit(g_hits, L, y, R, y + 32.0f, HIT_JUMP, 200, 0, *r, loc, jb);
    y += 32.0f;
    return y;
}

// ── 历史 ───────────────────────────────────────────────────────────────────
/// 今日已结束：今天有过动静、现在静置的会话。时间取 last_activity 的本机钟点。
static std::vector<const UIRow*> RDoneRows() {
    std::vector<const UIRow*> v;
    for (const auto& r : g_pd.rows)
        if (GroupOf(r.state) == G_DONE) v.push_back(&r);
    return v;                              // g_pd.rows 已按 age 升序＝最新的在前
}

static float RHistory(ImDrawList* dl, float L, float R, float y, int loc) {
    const auto rows = RDoneRows();
    int turns = 0;
    for (auto* r : rows) turns += r->turns;

    // 时间窗：起点＝今天最早一条完成会话所在整点（稿子的 08:12 → 08:00），终点固定 24:00。
    // 没有完成会话时退回整天，轴仍在，只是没有点。
    int startMin = 0;
    if (!rows.empty()) {
        int earliest = 1440;
        for (auto* r : rows) {
            int hh = 0, mm = 0;
            RTimeOfDay(g_pd.now - r->age, &hh, &mm);
            earliest = std::min(earliest, hh * 60 + mm);
        }
        startMin = std::min(23, earliest / 60) * 60;
    }
    const int endMin = 1440;
    const int startH = startMin / 60;

    // .rail{margin:10px 14px 2px;padding:10px 11px 8px}
    y += 10.0f;
    const float railX0 = L, railX1 = R, railY = y;
    // 稿子 DOM 实测：内容 10+17+8+26+8=69，加上下两条 1px 边 → 71。
    const float railH = 71.0f;
    dl->AddRectFilled(ImVec2(railX0, railY), ImVec2(railX1, railY + railH), kSurf, 10.0f);
    dl->AddRect(ImVec2(railX0 + 0.5f, railY + 0.5f), ImVec2(railX1 - 0.5f, railY + railH - 0.5f),
                kLine, 10.0f, 0, 1.0f);
    const float inx = railX0 + 12.0f, inr = railX1 - 12.0f;   // 1px 边 + 11 padding
    const float cy1 = railY + 11.0f + 8.5f;
    const std::string n = RFmt("%d", turns);
    const float nw = DrawTextRun(dl, inx, TextMid(cy1, T_NUM17, loc), n, loc, kT1, T_NUM17);
    DrawTextRun(dl, nw + 8.0f, TextMid(cy1 + 4.0f, T_S10, loc),
                RFmt(T("hist_rail", loc), (int)rows.size()), loc, kT4, T_S10);
    char cr[32];
    snprintf(cr, sizeof cr, "%.0f cr", g_pd.credits);
    DrawTextRight(dl, inr, TextMid(cy1 + 4.0f, T_SM, loc), cr, loc, kT4, T_SM);

    // .track{height:26;margin:8px 16px 0}：轴按今日实际窗口铺，每 4 小时一格
    const float trkX0 = railX0 + 1.0f + 11.0f + 16.0f, trkX1 = railX1 - 1.0f - 11.0f - 16.0f;
    const float trkY = railY + 1.0f + 10.0f + 17.0f + 8.0f;
    const float axY = trkY + 12.0f;
    const float span = (float)(endMin - startMin);
    RHair(dl, trkX0, axY, trkX1, kLine2);
    auto tick = [&](int minutes) {
        const float px = trkX0 + (trkX1 - trkX0) * (float)(minutes - startMin) / span;
        dl->AddRectFilled(ImVec2(px, axY - 6.0f), ImVec2(px + 1.0f, axY - 1.0f), kT5);
        // 刻度的最后一格是 24 点，但稿子按 String(h%24) 写成 00:00；右端那个窗口标签
        // （08:00 – 24:00）不动，它写的是窗口，不是这一格。
        const std::string lb = RFmt("%02d:00", (minutes / 60) % 24);
        const float lw = TextWidth(lb, loc, T_TICK);
        float lx = px - lw * 0.5f;
        if (lx < trkX0) lx = trkX0;
        if (lx + lw > trkX1) lx = trkX1 - lw;
        DrawTextRun(dl, lx, trkY + 17.0f, lb, loc, kT5, T_TICK);
    };
    for (int m = startMin; m < endMin; m += 240) tick(m);
    tick(endMin);
    // 点：每个今日完成的会话一颗，按钟点落位。稿子的规则是 `turns>=196 → .pt.hi`
    // （琥珀），这里照抄同一个门槛——之前改成「今天最重的一个」少亮了一颗。
    for (auto* r : rows) {
        const double when = g_pd.now - r->age;
        int hh = 0, mm = 0;
        RTimeOfDay(when, &hh, &mm);
        const float frac = (float)(hh * 60 + mm - startMin) / span;
        const float px = trkX0 + (trkX1 - trkX0) * std::fmax(0.0f, std::fmin(1.0f, frac));
        const ImU32 col = r->turns >= 196 ? kAmber : kGreen;
        dl->AddCircleFilled(ImVec2(px, axY), 5.5f, kSurf, 14);     // box-shadow 0 0 0 2px surf
        dl->AddCircleFilled(ImVec2(px, axY), 3.5f, col, 12);
    }
    y = railY + railH + 2.0f;

    y += RSLabel(dl, L, R, y, loc, "hist_done", (int)rows.size(), false,
                 RFmt(T("hist_win", loc), RFmt("%02d:00", startH).c_str(), "24:00"),
                 false, 12.0f, 7.0f);
    RHair(dl, L, y, R, kLine);
    if (rows.empty()) {
        DrawTextRun(dl, L + 2.0f, y + 10.0f, T("hist_empty", loc), loc, kT4, T_SM);
        return y + 10.0f + kTierPx[T_SM] * 1.5f;
    }
    const float ih = 20.0f;
    for (size_t i = 0; i < rows.size(); i++) {
        const UIRow& r = *rows[i];
        const float l1 = kTierPx[T_LABEL] * 1.3f;
        // `.hrow .mid .b{font:400 9.5px/1 mono;margin-top:3}` —— 9.5 行盒，
        // 不是 10；DOM 实测整行 46.44，差的就是这半格。
        const float l2 = kTierPx[T_TIME];
        const float contentH = l1 + 3.0f + l2;
        const float rowH = 9.0f + contentH + 9.0f + 1.0f;
        const float cy = y + 9.0f + contentH * 0.5f;
        dl->AddRectFilled(ImVec2(L + 2.0f, cy - ih * 0.5f), ImVec2(L + 2.0f + ih, cy + ih * 0.5f),
                          WithA(kGreen, 26), 5.0f);
        DrawIcon(dl, RI_CHECK, L + 5.0f, cy - 7.0f, 14.0f, kGreen);
        const float tx = L + 2.0f + ih + 9.0f;
        const std::string tn = r.turns > 0 ? RFmt(T("turns_n", loc), r.turns) : std::string("—");
        const float tw = TextWidth(tn, loc, T_TURNS);
        const float tmw = 40.0f;
        const std::string proj = r.project.empty() ? r.agent : r.project;
        DrawTextRun(dl, tx, y + 9.0f,
                    ClipToWidth(proj, R - 2.0f - tw - 9.0f - tmw - tx, loc, T_LABEL),
                    loc, kT2, T_LABEL);
        DrawTextRun(dl, tx, y + 9.0f + l1 + 3.0f,
                    ClipToWidth(r.agent, R - 2.0f - tw - 9.0f - tmw - tx, loc, T_TIME),
                    loc, kT5, T_TIME);
        DrawTextRight(dl, R - 2.0f - tmw - 9.0f, TextMid(cy, T_TURNS, loc), tn, loc, kT3, T_TURNS);
        DrawTextRight(dl, R - 2.0f, TextMid(cy, T_SM, loc), RClock(g_pd.now - r.age), loc, kT5, T_SM);
        if (i + 1 < rows.size()) RHair(dl, L, y + rowH, R, kLine);
        y += rowH;
    }
    return y;
}

// ── 设置 ───────────────────────────────────────────────────────────────────
/// .srow：22 图标 + 主/副两行 + 右侧控件（开关 / .sel / 打开）。
static float RSetRow(ImDrawList* dl, float L, float R, float y, int icon, const char* labelKey,
                     const std::string& sub, int loc, int ctl, bool on, bool amb, long zid,
                     const std::string& hitLabel) {
    const float l1 = kTierPx[T_TOOL] * 1.3f;          // .srow .lb .a{12px/1.3}
    const float l2 = kTierPx[T_SM] * 1.4f;            // .srow .lb .b{10px/1.4}
    const float contentH = l1 + 2.0f + l2;
    const float rowH = 9.0f + std::fmax(contentH, 22.0f) + 9.0f + 1.0f;
    const float cy = y + 9.0f + std::fmax(contentH, 22.0f) * 0.5f;
    const float lx = L + 2.0f;
    dl->AddRectFilled(ImVec2(lx, cy - 11.0f), ImVec2(lx + 22.0f, cy + 11.0f), W(13), 6.0f);
    DrawIcon(dl, icon, lx + 4.0f, cy - 7.0f, 14.0f, kT3);
    const float tx = lx + 22.0f + 10.0f;

    const float sw = 32.0f;
    // 退出行不是开关：右侧那颗是「退出 ›」文字按钮，得按它的实际宽度给副行让位，
    // 否则文字压在开关轨道上（2026-10-28 实拍设置屏发现的叠字）。
    const std::string quitLbl =
        ctl == SC_QUIT ? std::string(T("set_quit", loc)) + " ›" : std::string();
    const float quitW = quitLbl.empty() ? 0.0f : TextWidth(quitLbl, loc, T_TOOL) + 10.0f;
    const float morew = (!hitLabel.empty() && ctl < 0)
                            ? TextWidth(hitLabel, loc, T_S105) + 3.0f + 14.0f : 0.0f;
    const float rightW = quitW > 0.0f ? quitW : ((ctl >= 0) ? sw : morew);
    DrawTextRun(dl, tx, y + 9.0f, T(labelKey, loc), loc, kT1, T_TOOL);
    DrawTextRun(dl, tx, y + 9.0f + l1 + 2.0f,
                ClipToWidth(sub, R - 2.0f - rightW - 10.0f - tx, loc, T_SM), loc, kT5, T_SM);
    if (ctl >= 0 && quitW == 0.0f) {
        const float t = TxPeekNew(zid | (1L << 41), on ? 1.0f : 0.0f);
        RSwitch(dl, R - 2.0f - sw, cy - 9.5f, t, on, amb);
    } else if (morew > 0.0f) {
        // .more：透明底，悬停转 t2，右端一枚 14 的 chevron
        const long z = zid != 0 ? zid : Zid(loc, 0, HIT_NAVSETUP, 0);
        const float hv = TxPeek(z);
        DrawTextRun(dl, R - 2.0f - 14.0f - 3.0f - TextWidth(hitLabel, loc, T_S105),
                    TextMid(cy, T_S105, loc), hitLabel, loc, MixA(kT4, kT2, hv), T_S105);
        DrawIcon(dl, RI_CHEV, R - 2.0f - 14.0f, cy - 7.0f, 14.0f, MixA(kT4, kT2, hv));
    }
    if (ctl == SC_QUIT) {   // 退出行：右侧红字「退出 ›」（跟着语言走），整行可点
        DrawTextRight(dl, R - 2.0f, TextMid(y + rowH * 0.5f, T_TOOL, loc), quitLbl, loc,
                      IM_COL32(255, 176, 155, 255), T_TOOL);
        AddHit(g_hits, L, y, R, y + rowH, HIT_SET, ctl, 0, loc, hitLabel, 0);
        return rowH;
    }
    if (ctl >= 0 || morew > 0.0f)
        AddHit(g_hits, L, y, R, y + rowH,
               ctl >= 0 ? HIT_SET : HIT_NAVSETUP, ctl >= 0 ? ctl : 0, 0, loc, hitLabel,
               ctl >= 0 ? (on ? 1 : 0) : 0);   // state 必须带开关真态：输入循环用它推动画目标，
                                                // 传 0 等于每帧把开关压回「关」（探针无输入循环
                                                // 所以画得对，真窗口永远灰——2026-10-28 三轮排查）
    return rowH;
}

/// 监控的 agent 副行：稿子写 `qoder · claude`，这里按本轮扫描到的可用采集器拼同一句话。
static std::string RMonitoredIds(int loc) {
    std::string s;
    for (const auto& t : g_pd.tools) {
        if (!t.available || t.unreadable) continue;
        if (!s.empty()) s += " · ";
        s += t.id;
    }
    return s.empty() ? T("set_agents_sub", loc) : s;
}

static float g_langMenuX = 0, g_langMenuY = 0, g_langMenuW = 0;   // 语言下拉锚点（行绘制时记下）
static const char* kLangNames[6] = {"中文", "English", "日本語", "Deutsch", "Italiano", "한국어"};

static float RSettings(ImDrawList* dl, float L, float R, float y, int loc) {
    const float lx = L + 2.0f, rx = R - 2.0f;      // .setg 的 .slabel/.srow 都是 padding 端点 +2
    // .srow{border-bottom}：稿子每一行脚下都有一条（含每节最后一行），只有全屏最后一行
    // （语言）例外 —— 它下面直接是 14pt 留白与关于卡。
    auto row = [&](int icon, const char* key, const std::string& sub, int ctl,
                   bool on, bool amb, long zid, const std::string& hitLabel) {
        y += RSetRow(dl, lx, rx, y, icon, key, sub, loc, ctl, on, amb, zid, hitLabel);
        RHair(dl, L, y, R, kLine);
    };

    // 三节与稿子同序：提醒 → 接入 → 界面。
    y += RSLabel(dl, lx, rx, y, loc, "set_alert", -1, false, "", false, 14.0f, 7.0f);
    row(RI_BELL, "sound", "AGENTPING_SOUND", SC_SOUND, g_cfg.sound, false,
        Zid(loc, SC_SOUND, HIT_SET, 0), T("sound", loc));
    row(RI_BELLOFF, "set_dnd", RFmt("AGENTPING_DND · %s", T("set_dnd_sub", loc)),
        SC_DND, g_cfg.dnd, true, Zid(loc, SC_DND, HIT_SET, 0), T("set_dnd", loc));
    // 「登录时启动」不是画出来的：开关的真源就是登录项那个文件在不在（macOS LaunchAgent /
    // Linux XDG autostart），点了当场写/删，下次进设置读的还是那个文件。
    char launch_where[512] = {0};
    const bool launch_on = ap_autostart_get(launch_where, (int)sizeof launch_where) == 1;
    row(RI_LAUNCH, "set_launch", T("set_launch_sub", loc), SC_LAUNCH, launch_on, false,
        Zid(loc, SC_LAUNCH, HIT_SET, 0), T("set_launch", loc));

    y += RSLabel(dl, lx, rx, y, loc, "set_access", -1, false, "", false, 14.0f, 7.0f);
    row(RI_PLUG, "set_recheck", T("set_recheck_sub", loc), -1, false, false, 0,
        T("open", loc));
    row(RI_EYE, "set_agents", RMonitoredIds(loc), -1, false, false, 0,
        T("manage", loc));

    y += RSLabel(dl, lx, rx, y, loc, "set_ui", -1, false, "", false, 14.0f, 7.0f);
    // 语言：稿子是一颗原生 <select>（appearance:auto），截出来是 87×28pt、10.5 mono、
    // 粗下折箭头。ImGui 没有下拉，外形照截出来的那颗抄，点一下轮换到下一语。
    {
        const float l1 = kTierPx[T_TOOL] * 1.3f;
        const float l2 = kTierPx[T_SM] * 1.4f;
        const float contentH = l1 + 2.0f + l2;
        const float ctlH = 28.0f;                       // 稿子截图量出来的 select 外框高
        // 这一行是全屏最后一颗 `.srow`，`.srow:last-child` 无底边 —— DOM 实测 49.59。
        const float rowH = 9.0f + std::fmax(contentH, ctlH) + 9.0f;
        const float cy = y + 9.0f + std::fmax(contentH, ctlH) * 0.5f;
        const std::string cur = kLangNames[g_loc % 6];
        // 宽度由「最长语名」定（Chrome 的原生 select 就是这么量的），切语不跳动。
        float nameW = 0.0f;
        for (const char* n : kLangNames) nameW = std::fmax(nameW, TextWidth(n, loc, T_FAIL));
        const float selw = std::fmax(78.0f, nameW + 37.0f);
        const float st = cy - ctlH * 0.5f;              // 稿子：框顶正好落在 9pt 内边距上
        dl->AddRectFilled(ImVec2(lx, cy - 11.0f), ImVec2(lx + 22.0f, cy + 11.0f), W(13), 6.0f);
        DrawIcon(dl, RI_GLOBE, lx + 4.0f, cy - 7.0f, 14.0f, kT3);
        const float tx = lx + 32.0f;
        DrawTextRun(dl, tx, y + 9.0f, T("set_lang", loc), loc, kT1, T_TOOL);
        DrawTextRun(dl, tx, y + 9.0f + l1 + 2.0f, T("set_lang_note", loc), loc, kT5, T_SM);
        dl->AddRectFilled(ImVec2(rx - selw, st), ImVec2(rx, st + ctlH), kSurf2, 6.0f);
        dl->AddRect(ImVec2(rx - selw + 0.5f, st + 0.5f), ImVec2(rx - 0.5f, st + ctlH - 0.5f),
                    kLine2, 6.0f, 0, 1.0f);
        DrawTextRun(dl, rx - selw + 13.0f, TextMid(st + ctlH * 0.5f, T_FAIL, loc), cur, loc, kT2, T_FAIL);
        // 原生下拉那颗箭头比稿子的图标线更粗、更扁：8pt 宽、1.6pt 厚，中心在框右 9.25pt。
        {
            const float ax = rx - 9.25f, ay = st + ctlH * 0.5f - 0.5f;
            const ImVec2 pts[3] = {ImVec2(ax - 4.0f, ay - 1.75f), ImVec2(ax, ay + 1.75f),
                                   ImVec2(ax + 4.0f, ay - 1.75f)};
            dl->AddPolyline(pts, 3, kT1, 0, 1.6f);
        }
        AddHit(g_hits, rx - selw - 4.0f, st - 2.0f, rx + 4.0f, st + ctlH + 2.0f,
               HIT_LANG, 0, -1, loc, T("set_lang", loc), 0);
        // 下拉菜单的锚点（菜单本体在 RSettings 末尾画，保证盖住关于卡）
        g_langMenuX = rx - selw - 4.0f;
        g_langMenuY = st + ctlH + 4.0f;
        g_langMenuW = selw + 8.0f;
        y += rowH;
    }

    // 退出：整行可点，右缘红字「退出 ›」。设置屏就是兜底出口，不必去菜单栏找。
    y += 8.0f;
    row(RI_LAUNCH, "set_quit", "退出 Agent Ping · 停止本机提醒", SC_QUIT, false, false,
        Zid(loc, SC_QUIT, HIT_SET, 0), T("set_quit", loc));

    // .about{margin:14px 14px 0;padding:11px 12px}
    y += 14.0f;
    const float ax0 = L, ax1 = R;
    const float aline = kTierPx[T_S105] * 1.6f;
    const struct { const char* k; const char* sub; } kAbout[3] = {
        {"set_data", "set_data_sub"}, {"set_license", "set_license_sub"}, {"set_proto", "set_proto_sub"}};
    // 先量：三条都可能折两行（德/意更长），高度按真行数算。
    float bodyH = 0.0f;
    std::vector<std::vector<std::string>> wrapped;
    for (const auto& a : kAbout) {
        auto ls = WrapLines(T(a.sub, loc), ax1 - 12.0f - (ax0 + 12.0f + 34.0f + 8.0f), loc, T_S105, 3);
        bodyH += aline * (float)ls.size() + (wrapped.empty() ? 0.0f : 6.0f);
        wrapped.push_back(ls);
    }
    const float aboutH = 11.0f + bodyH + 11.0f + 2.0f;   // 上下内边距 + .about 的 1px 边框
    dl->AddRectFilled(ImVec2(ax0, y), ImVec2(ax1, y + aboutH), kSurf, 10.0f);
    dl->AddRect(ImVec2(ax0 + 0.5f, y + 0.5f), ImVec2(ax1 - 0.5f, y + aboutH - 0.5f), kLine, 10.0f, 0, 1.0f);
    float ay = y + 1.0f + 11.0f;                       // 边框 1 + 内边距 11
    for (size_t i = 0; i < 3; i++) {
        if (i) ay += 6.0f;
        DrawTextRun(dl, ax0 + 12.0f, ay, T(kAbout[i].k, loc), loc, kT2, T_BTN);
        for (const auto& ln : wrapped[i]) {
            DrawTextRun(dl, ax0 + 12.0f + 34.0f + 8.0f, ay, ln, loc, kT4, T_S105);
            ay += aline;
        }
    }
    y += aboutH;
    // 语言下拉菜单本体：在最后画才盖得住关于卡；点行选定（HIT_LANG opt=语种下标）。
    if (g_lang_menu) {
        const float rowH2 = 28.0f;
        const float mh2 = 6.0f + 6.0f * rowH2;
        dl->AddRectFilled(ImVec2(g_langMenuX, g_langMenuY),
                          ImVec2(g_langMenuX + g_langMenuW, g_langMenuY + mh2), kSurf, 8.0f);
        dl->AddRect(ImVec2(g_langMenuX + 0.5f, g_langMenuY + 0.5f),
                    ImVec2(g_langMenuX + g_langMenuW - 0.5f, g_langMenuY + mh2 - 0.5f), kLine2,
                    8.0f, 0, 1.0f);
        for (int i = 0; i < 6; i++) {
            const float ry = g_langMenuY + 6.0f + i * rowH2;
            const bool cur2 = (g_loc % 6) == i;
            const float hov = TxPeekNew(Zid(loc, 2000 + i, HIT_LANG, i), 0.0f);
            if (cur2 || hov > 0.01f)
                dl->AddRectFilled(ImVec2(g_langMenuX + 4.0f, ry),
                                  ImVec2(g_langMenuX + g_langMenuW - 4.0f, ry + rowH2),
                                  cur2 ? kRGoHover : MixA(kSurf, kSurf2, hov), 5.0f);
            DrawTextRun(dl, g_langMenuX + 12.0f, TextMid(ry + rowH2 * 0.5f, T_FAIL, loc),
                        kLangNames[i], loc, cur2 ? kT1 : kT2, T_FAIL);
            if (cur2) DrawTextRun(dl, g_langMenuX + g_langMenuW - 24.0f,
                                  TextMid(ry + rowH2 * 0.5f, T_FAIL, loc), "✓", loc, kT1, T_FAIL);
            AddHit(g_hits, g_langMenuX + 2.0f, ry, g_langMenuX + g_langMenuW - 2.0f, ry + rowH2,
                   HIT_LANG, 2000 + i, i, loc, kLangNames[i], cur2 ? 1 : 0);
        }
    }

    return y;
}

// ── 接线（首跑） ───────────────────────────────────────────────────────────
static const struct { const char* id; const char* path; } kRDetect[] = {
    {"qoder", "~/.qoder/projects"},
    {"claude", "~/.claude/projects"},
    {"codex", "~/.codex/sessions"},
    {"zcode", "~/.zcode/cli/db/db.sqlite"},
    {"workbuddy", "~/.workbuddy/workbuddy.db"},
    {"opencode", "~/.local/share/opencode/opencode.db"},
};

/// 步号圆：`.no{18×18;border:line2}` / `.step.done .no{green;font-size:9px}`
static void RStepNo(ImDrawList* dl, float x, float cy, int num, bool done, int loc) {
    dl->AddCircleFilled(ImVec2(x + 9.0f, cy), 9.0f, done ? WithA(kGreen, 26) : kBg, 24);
    dl->AddCircle(ImVec2(x + 9.0f, cy), 8.5f, done ? WithA(kGreen, 128) : kLine2, 24, 1.0f);
    const int tier = done ? T_TIME : T_M10B;     // 稿子 done 态把字号从 10 压到 9
    const std::string n = RFmt("%d", num);
    DrawTextRun(dl, x + 9.0f - TextWidth(n, loc, tier) * 0.5f,
                TextMid(cy, tier, loc), n, loc, done ? kGreen : kT3, tier);
}

/// 竖排块（代码框正文）：按真换行切段、每段再按宽折行。`.codebox pre` 是 9.5/1.6 mono。
static float RCodeBlock(ImDrawList* dl, float L, float R, float y, const std::string& body,
                        int loc, float lineH) {
    size_t i = 0;
    while (i <= body.size()) {
        size_t j = body.find('\n', i);
        if (j == std::string::npos) j = body.size();
        const std::string ln = body.substr(i, j - i);
        const auto sub = WrapLines(ln.empty() ? std::string(" ") : ln, R - L, loc, T_TIME, 12);
        for (const auto& w : sub) {
            DrawTextRun(dl, L, y, w, loc, kT2, T_TIME);
            y += lineH;
        }
        if (j >= body.size()) break;
        i = j + 1;
    }
    return y;
}

/// 检测行第二行那个路径：只给本机真认识的六路采集器写落点，认不出就留空，不编。
static const char* RDetectPath(const std::string& id) {
    for (const auto& d : kRDetect) if (id == d.id) return d.path;
    return "";
}

static float RSetup(ImDrawList* dl, float L, float R, float y, int loc, double tn) {
    const std::vector<Tool>& tools = g_pd.tools;
    int avail = 0;
    for (const auto& t : tools)
        if (t.available && !t.unreadable) avail++;

    // hero：34 徽标 + 600 15/1.35 标题 + 400 11.5/1.6 两句说明
    {
        BrandMark(dl, L, y, 34.0f);
        const float tx = L + 34.0f + 10.0f;
        const float h2H = kTierPx[T_HERO] * 1.35f;
        DrawTextRun(dl, tx, TextTop(y, h2H, T_HERO, loc), T("setup_hero", loc), loc, kT1, T_HERO);
        float py = y + h2H + 4.0f;
        py += DrawRuns(dl, tx, R, py, {{T("setup_hero_sub", loc), kT3}}, loc, T_S115,
                       kTierPx[T_S115] * 1.6f, 0);
        y = std::fmax(y + 34.0f, py) + 16.0f;
    }

    // 第一步：只读检测（.step.margin-top 16 → .sh 18 高 → .aglist.margin-top 9）
    {
        const float shH = 18.0f;
        RStepNo(dl, L, y + shH * 0.5f, 1, avail > 0, loc);
        DrawTextRun(dl, L + 26.0f, CellTop(y, shH, shH, T_S12B, loc), T("setup_step1", loc),
                    loc, kT1, T_S12B);
        DrawTextRight(dl, R, CellTop(y, shH, kTierPx[T_SM], T_SM, loc),
                      RFmt(T("setup_avail", loc), avail), loc, kT4, T_SM);
        y += shH + 9.0f;
        const float agH = 40.0f;                 // 8 + 11.5 + 3 + 9.5 + 8
        // .aglist 上下各一条 1px 边，行间 n−1 条分隔线（最后一行那条被 overflow 裁掉）。
        const float seps = tools.empty() ? 0.0f : (float)tools.size() - 1.0f;
        const float listH = 2.0f + agH * (float)tools.size() + seps;
        dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + listH), kSurf, 9.0f);
        dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + listH - 0.5f), kLine, 9.0f, 0, 1.0f);
        float ry = y + 1.0f;
        for (size_t i = 0; i < tools.size(); i++) {
            const Tool& t = tools[i];
            const float cy = ry + agH * 0.5f;
            const bool ok = t.available && !t.unreadable && !t.quiet;
            if (t.available && !t.unreadable)
                RAgentTile(dl, L + 10.0f, cy - 8.0f, 16.0f, 4.0f, t.hue, t.initial, loc, t.id);
            else
                DrawIcon(dl, RI_TERM, L + 11.0f, cy - 7.0f, 14.0f, kT3);
            std::string st;
            ImU32 sc = kT5;
            if (!t.available) st = T("setup_absent", loc);
            else if (t.unreadable) { st = T("unreadable", loc); sc = WithA(kRed, 210); }
            else if (t.quiet) st = T("quiet_agents", loc);
            else {
                st = RFmt(T("det_sess_today", loc), t.rows);
                sc = kGreen;
            }
            const float stw = TextWidth(st, loc, T_GLYPH) + (ok ? 18.0f : 0.0f);   // 14 图标 + 4 gap
            const float sx = R - 10.0f - stw;
            if (ok) DrawIcon(dl, RI_CHECK, sx, cy - 7.0f, 14.0f, kGreen);
            DrawTextRun(dl, sx + (ok ? 18.0f : 0.0f), TextMid(cy, T_GLYPH, loc), st, loc, sc, T_GLYPH);
            const float tx = L + 10.0f + 16.0f + 8.0f;
            const char* path = RDetectPath(t.id);
            DrawTextRun(dl, tx, ry + 8.0f, ClipToWidth(t.name, sx - 8.0f - tx, loc, T_LABEL),
                        loc, kT1, T_LABEL);
            DrawTextRun(dl, tx, ry + 8.0f + kTierPx[T_LABEL] + 3.0f,
                        MiddleClipPath(path, sx - 8.0f - tx, loc, T_TIME), loc, kT5, T_TIME);
            ry += agH;
            if (i + 1 < tools.size()) { RHair(dl, L, ry, R, kLine); ry += 1.0f; }
        }
        y += listH + 16.0f;
    }

    // 第二步：把 Notification 钩子接到写盘脚本
    {
        const float shH = 18.0f;
        RStepNo(dl, L, y + shH * 0.5f, 2, false, loc);
        DrawTextRun(dl, L + 26.0f, CellTop(y, shH, shH, T_S12B, loc), T("setup_step2", loc),
                    loc, kT1, T_S12B);
        DrawTextRight(dl, R, CellTop(y, shH, kTierPx[T_SM], T_SM, loc), "hook", loc, kT4, T_SM);
        y += shH + 7.0f;                         // .stepnote{margin-top:7px}

        const std::string inbox = TildePath(ap::hook_inbox_dir());
        const std::string note = T("setup_inbox_note", loc);
        // 稿子在这句后面括了「另一套系统的收件箱在哪」：本机 macOS 就括 Linux 的，
        // 本机 Linux 就括 macOS 的。写的是两边的真实默认位置。
#ifdef __APPLE__
        const std::string osHint = RFmt(T("setup_os_hint", loc), "Linux",
                                        "$XDG_CONFIG_HOME/AgentPing/hooks/");
#else
        const std::string osHint = RFmt(T("setup_os_hint", loc), "macOS",
                                        "~/Library/Application Support/AgentPing/hooks/");
#endif
        const size_t at = note.find("%s");
        std::vector<RRunIn> runs;
        if (at == std::string::npos) runs.push_back({note, kT4});
        else {
            runs.push_back({note.substr(0, at), kT4});
            runs.push_back({inbox, kT2});
            runs.push_back({osHint, kT4});
            runs.push_back({note.substr(at + 2), kT4});
        }
        y += DrawRuns(dl, L, R, y, runs, loc, T_S105, kTierPx[T_S105] * 1.6f, 0) + 9.0f;

        // 两个代码框：写入器脚本 + CLI 配置片段。正文都是真字节（ap::hook_script_body）。
        const std::string script = ap::hook_script_body();
        const std::string cfgPath = TildePath(ap::hook_script_path());
        const std::string json = "{\n  \"hooks\": {\n    \"Notification\": [{\n"
                                 "      \"hooks\": [{ \"type\": \"command\",\n"
                                 "        \"command\": \"" + cfgPath + "\" }]\n"
                                 "    }]\n  }\n}";
        auto codebox = [&](const std::string& file, const std::string& body, int row, bool mt7) {
            if (mt7) y += 7.0f;
            const float lineH = kTierPx[T_TIME] * 1.6f;
            const float innerW = R - L - 20.0f;
            float bodyH = 0.0f;
            size_t i2 = 0;
            while (i2 <= body.size()) {
                size_t j2 = body.find('\n', i2);
                if (j2 == std::string::npos) j2 = body.size();
                const std::string ln = body.substr(i2, j2 - i2);
                bodyH += lineH * (float)WrapLines(ln.empty() ? std::string(" ") : ln, innerW,
                                                  loc, T_TIME, 12).size();
                if (j2 >= body.size()) break;
                i2 = j2 + 1;
            }
            // .cb-h：6 上 + max(9.5 字, 9.5+2×2 按钮) + 6 下 = 25.5
            const float hdrH = 25.5f;
            const float boxH = hdrH + 16.0f + bodyH;
            dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + boxH), kRecess, 9.0f);
            dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + boxH - 0.5f), kLine, 9.0f, 0, 1.0f);
            RHair(dl, L, y + hdrH, R, kLine);
            DrawTextRun(dl, L + 9.0f, TextMid(y + hdrH * 0.5f, T_FHEAD, loc),
                        MiddleClipPath(file, R - L - 90.0f, loc, T_FHEAD), loc, kT4, T_FHEAD);
            const std::string cp = T("copy", loc);
            const float cw = 12.0f + 4.0f + TextWidth(cp, loc, T_S95);
            DrawIcon(dl, RI_COPY, R - 9.0f - cw, y + hdrH * 0.5f - 6.0f, 12.0f, kT3);
            DrawTextRun(dl, R - 9.0f - cw + 16.0f, TextMid(y + hdrH * 0.5f, T_S95, loc), cp,
                        loc, kT3, T_S95);
            Hit hh;
            hh.x0 = R - 9.0f - cw - 2.0f; hh.y0 = y + 2.0f; hh.x1 = R - 7.0f; hh.y1 = y + hdrH - 2.0f;
            hh.act = HIT_COPYTEXT; hh.row = row; hh.zid = Zid(loc, row, HIT_COPYTEXT, 0);
            hh.title = body; hh.label = cp;
            g_hits.push_back(hh);
            RCodeBlock(dl, L + 10.0f, R - 10.0f, y + hdrH + 8.0f, body, loc, lineH);
            y += boxH;
        };
        codebox(cfgPath, script, kHookRowBase + 20, false);
        codebox(RFmt(T("setup_cfg_note", loc), "~/.claude/settings.json"), json, kHookRowBase + 21, true);

        const std::string fname = "<agent>-<epoch>-<pid>.json";
        const std::string filesNote = RFmt(T("setup_files_note", loc), fname.c_str());
        const size_t fat = filesNote.find(fname);
        std::vector<RRunIn> fruns;
        if (fat == std::string::npos) fruns.push_back({filesNote, kT4});
        else {
            fruns.push_back({filesNote.substr(0, fat), kT4});
            fruns.push_back({fname, kT2});
            fruns.push_back({filesNote.substr(fat + fname.size()), kT4});
        }
        y += 7.0f;
        y += DrawRuns(dl, L, R, y, fruns, loc, T_S105, kTierPx[T_S105] * 1.6f, 0);
    }

    // 每家的写入／重写：稿子在这屏只给命令，真写盘由这几行承接（比稿子多的一块，取证要摊开）。
    {
        const int n = (int)g_hooks.size();
        if (n > 0) {
            const float agH = 40.0f;
            const float listH = 2.0f + agH * (float)n;
            dl->AddRectFilled(ImVec2(L, y), ImVec2(R, y + listH), kSurf, 9.0f);
            dl->AddRect(ImVec2(L + 0.5f, y + 0.5f), ImVec2(R - 0.5f, y + listH - 0.5f), kLine, 9.0f, 0, 1.0f);
            float ry = y + 1.0f;
            for (int i = 0; i < n; i++) {
                const ap::HookAgent& h = g_hooks[(size_t)i];
                const float cy = ry + agH * 0.5f;
                const Tool* t = AgentTool(h.id.c_str());
                if (t) RAgentTile(dl, L + 10.0f, cy - 8.0f, 16.0f, 4.0f, t->hue, t->initial, loc, t->id);
                else DrawIcon(dl, RI_PLUG, L + 11.0f, cy - 7.0f, 14.0f, kT3);
                ImU32 dc = kT5;
                const char* stk = "setup_none";
                if (h.occupied) { dc = kAmber; stk = "setup_occupied"; }
                else if (h.stale) { dc = kAmber; stk = "setup_stale"; }
                else if (h.installed) { dc = kGreen; stk = "setup_ok"; }
                else if (!h.supported) { dc = kT5; stk = "setup_manual"; }
                const bool btn = h.supported;
                const char* bk = (h.installed || h.stale) ? "setup_rewrite" : "setup_install";
                const std::string st = T(stk, loc);
                const std::string bl = T(bk, loc);
                const float bw = btn ? TextWidth(bl, loc, T_S95) + 20.0f : 0.0f;
                const float stw = TextWidth(st, loc, T_GLYPH);
                const float stx = R - 10.0f - (btn ? bw + 8.0f : 0.0f) - stw;
                DrawTextRun(dl, stx, TextMid(cy, T_GLYPH, loc), st, loc, dc, T_GLYPH);
                if (btn) {
                    const long z = Zid(loc, kHookRowBase + i, HIT_HOOKINST, 0);
                    const float bxx = R - 10.0f - bw;
                    const float hv = TxPeek(z);
                    dl->AddRectFilled(ImVec2(bxx, cy - 11.0f), ImVec2(bxx + bw, cy + 11.0f),
                                      MixA(kSurf2, kRGoHover, hv), 6.0f);
                    dl->AddRect(ImVec2(bxx + 0.5f, cy - 10.5f), ImVec2(bxx + bw - 0.5f, cy + 10.5f),
                                kLine2, 6.0f, 0, 1.0f);
                    DrawTextRun(dl, bxx + 10.0f, TextMid(cy, T_S95, loc), bl, loc, kT2, T_S95);
                    Hit hh;
                    hh.x0 = bxx; hh.y0 = cy - 11.0f; hh.x1 = bxx + bw; hh.y1 = cy + 11.0f;
                    hh.act = HIT_HOOKINST; hh.row = kHookRowBase + i; hh.zid = z;
                    hh.agent_id = h.id; hh.title = h.name; hh.label = h.config;
                    g_hits.push_back(hh);
                }
                const float tx = L + 34.0f;
                DrawTextRun(dl, tx, ry + 8.0f,
                            ClipToWidth(h.name, stx - 8.0f - tx, loc, T_LABEL), loc, kT1, T_LABEL);
                DrawTextRun(dl, tx, ry + 8.0f + kTierPx[T_LABEL] + 3.0f,
                            MiddleClipPath(TildePath(h.config), stx - 8.0f - tx, loc, T_TIME),
                            loc, kT5, T_TIME);
                ry += agH;
                if (i + 1 < n) RHair(dl, L, ry, R, kLine);
            }
            y += listH + 8.0f;
        }
    }

    // .waitline{border:1px dashed line2;radius 9;padding:10px 11px;font 11/1.5} + 1s 转圈
    {
        y += 14.0f;                                       // .waitline{margin-top:14px}
        const float lineH = kTierPx[T_CHIP] * 1.5f;
        const float textW = R - L - 22.0f - 12.0f - 8.0f;
        const auto lines = WrapLines(T("setup_waitline", loc), textW, loc, T_CHIP, 3);
        const float wlH = 20.0f + lineH * (float)lines.size();
        DashRoundRect(dl, L, y, R - L, wlH, 9.0f, kLine2);
        const float scx = L + 11.0f + 6.0f, scy = y + wlH * 0.5f;
        const float ph = (float)std::fmod(tn, 1.0) / 1.0f;
        dl->AddCircle(ImVec2(scx, scy), 6.0f, kLine2, 24, 1.5f);
        const float a0 = ph * 2.0f * (float)kRPi;
        dl->PathArcTo(ImVec2(scx, scy), 6.0f, a0, a0 + 1.2f, 12);
        dl->PathStroke(kCyan, 0, 1.5f);
        float ly = y + 10.0f;
        for (const auto& ln : lines) {
            DrawTextRun(dl, L + 11.0f + 12.0f + 8.0f, ly, ln, loc, kT3, T_CHIP);
            ly += lineH;
        }
        y += wlH;
    }
    return y;
}

// ── 回执：.toast{left/right:14;bottom:54} ──────────────────────────────────
static float RToast(ImDrawList* dl, float x, float y, float w, float panelBot, int loc,
                    double tn) {
    const double now = glfwGetTime();
    if (g_toast.empty() || now - g_toast_at > 2.6) return 0.0f;
    (void)tn;
    const std::string a = g_toast, b = g_toast_sub;
    const float l1 = kTierPx[T_LABEL] * 1.35f;
    const float l2 = kTierPx[T_SM] * 1.4f;
    const float textH = l1 + (b.empty() ? 0.0f : 1.0f + l2);
    const float h = 9.0f + std::fmax(22.0f, textH) + 9.0f + 2.0f;
    const float L = x + 14.0f, R = x + w - 14.0f;
    const float ty = panelBot - 54.0f - h;
    dl->AddRectFilled(ImVec2(L, ty), ImVec2(R, ty + h), kRToastBg, 10.0f);
    dl->AddRect(ImVec2(L + 0.5f, ty + 0.5f), ImVec2(R - 0.5f, ty + h - 0.5f), kLine2, 10.0f, 0, 1.0f);
    const float iy = ty + 9.0f;
    dl->AddRectFilled(ImVec2(L + 11.0f, iy), ImVec2(L + 33.0f, iy + 22.0f), WithA(kAmber, 36), 6.0f);
    DrawIcon(dl, g_toast_icon == 4 ? RI_JUMP : RI_COPY, L + 11.0f + 4.0f, iy + 4.0f, 14.0f, kAmber);
    const float tx = L + 11.0f + 22.0f + 9.0f;
    float tw = R - 11.0f - tx;
    if (g_toast_go) tw -= TextWidth(T("toast_back_ov", loc), loc, T_LABEL) + 12.0f;
    DrawTextRun(dl, tx, ty + 9.0f, ClipToWidth(a, tw, loc, T_LABEL), loc, kT1, T_LABEL);
    if (!b.empty())
        DrawTextRun(dl, tx, ty + 9.0f + l1 + 1.0f, ClipToWidth(b, tw, loc, T_SM), loc, kT4, T_SM);
    if (g_toast_go) {
        const std::string go = T("toast_back_ov", loc);
        const float gw = TextWidth(go, loc, T_LABEL);
        DrawTextRight(dl, R - 11.0f, TextMid(ty + h * 0.5f, T_LABEL, loc), go, loc, kAmber, T_LABEL);
        AddHit(g_hits, R - 11.0f - gw - 8.0f, ty + 8.0f, R - 6.0f, ty + h - 8.0f,
               HIT_BACK, kNavRow, 0, loc, go, 0);
    }
    return h;
}

// ── 裁剪后命中区清理：滚出正文的整颗不再可点，半截的按裁剪框切齐 ────────────
static void RCullHits(size_t from, float x0, float y0, float x1, float y1) {
    for (size_t i = from; i < g_hits.size();) {
        Hit& h = g_hits[i];
        if (h.y1 <= y0 + 0.5f || h.y0 >= y1 - 0.5f || h.x1 <= x0 + 0.5f || h.x0 >= x1 - 0.5f) {
            g_hits.erase(g_hits.begin() + (std::ptrdiff_t)i);
            continue;
        }
        h.x0 = std::fmax(h.x0, x0);
        h.x1 = std::fmin(h.x1, x1);
        h.y0 = std::fmax(h.y0, y0);
        h.y1 = std::fmin(h.y1, y1);
        i++;
    }
}

// ── 一屏的总装 ─────────────────────────────────────────────────────────────
/// 返回面板真用到的高度（固定 640，屏幕预算更矮时由调用方裁）。
static float R20Draw(ImDrawList* dl, float x, float y, float w, float h, int loc, int view,
                     double tn) {
    RPanelFrame(dl, x, y, w, h);
    const bool detail = (view == 3 || view == 4);
    const bool setup = (view == 2);

    const float headH = detail ? kRDHeadH : kRHeadH;
    if (detail) {
        RDetailHead(dl, x, y, w, loc, SelectedRow(g_sel_key));
    } else {
        RHead(dl, x, y, w, loc, setup);
    }

    const float bodyTop = y + kRPaneBorder + headH;
    // 接线屏与详情屏一样铺到底：只有总览/历史/设置三屏底下有标签条。
    const bool hasTabs = !detail && !setup;
    const float bodyBot = hasTabs ? (y + h - kRPaneBorder - kRTabsH) : (y + h - kRPaneBorder);
    g_bodyTop[view] = ImVec2(x + kRPaneBorder, bodyTop);
    g_bodyBot[view] = ImVec2(x + w - kRPaneBorder, bodyBot);
    g_bodyValid[view] = true;
    if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];

    float padTop = kRInnerTop;
    if (setup) padTop = kRInnerTop + kRSetupTop;
    if (detail) padTop = kRInnerTop + kRDBodyTop;
    float padBot = kRInnerBot;
    if (setup) padBot = kRInnerBot + kRSetupBot;
    if (detail) padBot = kRInnerBot + kRDBodyBot;

    // .dbody / .setg 的横向内边距：详情 14，接线 16
    const float padX = setup ? kRSetupX : kRPadX;
    const float L = x + kRPaneBorder + padX;
    const float R = x + w - kRPaneBorder - padX;

    const size_t hits0 = g_hits.size();
    const float startY = bodyTop + padTop - g_scroll[view];
    dl->PushClipRect(ImVec2(x + kRPaneBorder, bodyTop), ImVec2(x + w - kRPaneBorder, bodyBot), true);
    float endY = startY;
    if (view == 0) endY = ROverview(dl, L, R, startY, bodyBot, loc, tn);
    else if (view == 1) endY = RSettings(dl, L, R, startY, loc);
    else if (view == 2) endY = RSetup(dl, L, R, startY, loc, tn);
    else if (view == 3) endY = RSessionView(dl, L, R, startY, loc, tn);
    else if (view == 4) endY = RPermView(dl, L, R, startY, loc, tn);
    else endY = RHistory(dl, L, R, startY, loc);
    dl->PopClipRect();

    // 每个 view 都返回到「画完的绝对 y」，内容高 = 末 y − 起 y + 底内边距。
    g_contentH[view] = endY - startY + padBot;
    g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
    RCullHits(hits0, x + kRPaneBorder, bodyTop, x + w - kRPaneBorder, bodyBot);

    // .fade{height:20px}：顶条在 scrollTop>4 时亮，底条在「还有内容」时亮
    const bool fadeTop = g_scroll[view] > 4.0f;
    const bool fadeBot = g_scroll[view] < g_scrollMax[view] - 4.0f;
    if (fadeTop)
        dl->AddRectFilledMultiColor(ImVec2(x + kRPaneBorder, bodyTop),
                                    ImVec2(x + w - kRPaneBorder, bodyTop + kRFadeH),
                                    kBg, kBg, WithA(kBg, 0), WithA(kBg, 0));
    if (fadeBot)
        dl->AddRectFilledMultiColor(ImVec2(x + kRPaneBorder, bodyBot - kRFadeH),
                                    ImVec2(x + w - kRPaneBorder, bodyBot),
                                    WithA(kBg, 0), WithA(kBg, 0), kBg, kBg);

    if (hasTabs) RTabs(dl, x, y + h - kRPaneBorder - kRTabsH, w, loc);
    RToast(dl, x, y, w, y + h - kRPaneBorder, loc, tn);
    return h;
}

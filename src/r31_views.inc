// ═══════════════════════════════════════════════════════════════════════════
// r31 版式层：一块 416×850 的浮层，六屏（总览 / 用量 / 账户 / 工具 / 详情 /
// 完成记录）加两处固定脚注。逐条对照 agent-notify/r31 的 app.css / base.css 与
// app.js 里的 render 函数；数字都是稿子的真实声明，不是按 340 版按比例放大的。
//
// 与 r20 的三处硬差别，写在这里，改之前先看：
//  ① 面板背景是半透明磨砂（backdrop-filter:blur(28px)）。这块窗口拿不到背后像素，
//     只能把底色压到接近不透明的竖向渐变 + 内描边/受光/阴影三条线——磨砂那一层
//     在 GLFW/ImGui 里没有等价物。这是唯一一处「看起来比稿子少一层」的地方。
//  ② 内容不是满屏滚动：总览/用量带横向来源导轨与标签条，详情/账户/工具/完成记录
//     各有固定头，脚注（今日完成 / 用量口径 / 主操作）也固定，只有中间那截滚。
//  ③ 每行点击就跳会话；ⓘ 才进详情。稿子把「跳转」和「看详情」分成两颗，这里照抄。
// ═══════════════════════════════════════════════════════════════════════════

// ── 色板：scene.dark 的变量，逐条照抄 ──────────────────────────────────────
// --glass / --spec / --shade 三条不在表里：它们已被 R31Glass 的九层配方取代。
static const ImU32 k31Fg       = IM_COL32(240, 242, 247, 255);  // --fg #F0F2F7
static const ImU32 k31Sec      = IM_COL32(192, 198, 208, 255);  // --secondary #C0C6D0
static const ImU32 k31Muted    = IM_COL32(168, 177, 191, 255);  // --muted #A8B1BF
static const ImU32 k31Faint    = IM_COL32(137, 149, 164, 255);  // --faint #8995A4
static const ImU32 k31Hover    = IM_COL32(235, 243, 255, 20);   // --hover 8%
static const ImU32 k31Sel      = IM_COL32(216, 230, 255, 27);   // --selected 10.5%
static const ImU32 k31Line     = IM_COL32(213, 229, 249, 32);   // --line 12.5%
static const ImU32 k31LineSoft = IM_COL32(213, 229, 249, 17);   // 分隔线的弱一档
static const ImU32 k31Rim      = IM_COL32(231, 241, 255, 64);   // --rim 25%
static const ImU32 k31Badge    = IM_COL32(255, 255, 255, 15);   // --badge 6%
static const ImU32 k31Sweep    = IM_COL32(255, 255, 255, 158);  // --sweep 62%
static const ImU32 k31Blue     = IM_COL32(172, 208, 255, 255);  // --blue #ACD0FF
static const ImU32 k31Red      = IM_COL32(255, 176, 155, 255);  // --red #FFB09B
static const ImU32 k31Green    = IM_COL32(141, 227, 191, 255);  // --green #8DE3BF
static const ImU32 k31White    = IM_COL32(255, 255, 255, 255);

// 面板底：稿子是 rgba(28,34,43,.46) 压在一张壁纸上再磨砂。这里没有壁纸也没有模糊，
// 46% 会让桌面上的任何亮色透进来、正文对比度掉到读不动。2026-10-28 定稿压成实色，
// 上下差 17 个亮度点；上半段再由 R31Frame 那层 150pt 白纱提回来。
static const ImU32 k31PanelTop = IM_COL32(24, 31, 42, 255);
static const ImU32 k31PanelBot = IM_COL32(7, 10, 15, 255);
static ImU32 R31PanelAt(float yy, float top, float h) {
    float t = h > 1.0f ? (yy - top) / h : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return MixA(k31PanelTop, k31PanelBot, t);
}

static const float k31HeadH = 54.0f;      // app.css 覆盖 base 的 55：.panel-head{height:54px}
static const float k31TabsH = 34.0f;      // .content-tabs{height:34px}
static const float k31TabsGap = 11.0f;    // .content-tabs{margin:0 17px 11px}
static const float k31PaneR = 23.0f;      // .panel{border-radius:23px}

// ── 24 viewBox 图标：稿子 icon() 里那 19 条 SVG，路径数据逐字搬 ─────────────
// 与 r20 的 16 viewBox 表分开：两套的坐标系不同，混用会把 24 系的图标画小一圈。
enum I31 { I31_SEARCH, I31_BACK, I31_CLOSE, I31_LAYERS, I31_LOCK, I31_QUESTION, I31_CHECK,
           I31_RIGHT, I31_LEFT, I31_COPY, I31_TERM, I31_GRID, I31_INFO, I31_LAUNCH,
           I31_CHART, I31_CARD, I31_CLOCK, I31_DOWN, I31_UP, I31_REFRESH, I31_GEAR, I31_N };
static const RShapeSpec k31Shape[I31_N][4] = {
    /* search  */ {{1, nullptr, 10.5f, 10.5f, 6.2f, 0}, {0, "M15 15 20 20"}},
    /* back    */ {{0, "m13 5-7 7 7 7M6 12h15"}},
    /* close   */ {{0, "m6 6 12 12M6 18 18 6"}},
    /* layers  */ {{0, "m12 3 9 5-9 5-9-5 9-5ZM3 12l9 5 9-5M3 16l9 5 9-5"}},
    /* lock    */ {{2, nullptr, 5, 10, 14, 11, 3}, {0, "M8 10V7a4 4 0 0 1 8 0v3M12 14v3"}},
    /* question*/ {{0, "M8 8a4 4 0 0 1 8 0c0 3-4 2-4 5M12 17h.01"},
                   {1, nullptr, 12, 12, 10, 0}},
    /* check   */ {{0, "m8 12 3 3 5-6"}, {1, nullptr, 12, 12, 9, 0}},
    /* right   */ {{0, "m9 5 7 7-7 7"}},
    /* left    */ {{0, "m15 5-7 7 7 7"}},
    /* copy    */ {{2, nullptr, 8, 8, 12, 13, 2}, {0, "M15 8V3H3v12h5"}},
    /* term    */ {{0, "m5 6 5 6-5 6M12 18h7"}},
    /* grid    */ {{2, nullptr, 3, 3, 7, 7, 1.5f}, {2, nullptr, 14, 3, 7, 7, 1.5f},
                   {2, nullptr, 3, 14, 7, 7, 1.5f}, {2, nullptr, 14, 14, 7, 7, 1.5f}},
    /* info    */ {{1, nullptr, 12, 12, 9, 0}, {0, "M12 11v6M12 7h.01"}},
    /* launch  */ {{0, "M14 3h7v7M21 3l-11 11M10 3H4a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h16a1 1 0 0 0 1-1v-6"}},
    /* chart   */ {{0, "M4 20h17M6 16v-5M12 16V4M18 16V8"}},
    /* card    */ {{2, nullptr, 3, 5, 18, 14, 3}, {0, "M3 10h18M7 15h4"}},
    /* clock   */ {{1, nullptr, 12, 12, 9, 0}, {0, "M12 6v6l4 2"}},
    /* down    */ {{0, "M12 3v17m-6-6 6 6 6-6"}},
    /* up      */ {{0, "M12 21V4m-6 6 6-6 6 6"}},
    /* refresh */ {{0, "M20 7v5h-5M4 17v-5h5M6 7a7 7 0 0 1 12-1l2 6M4 12l2 6a7 7 0 0 0 12-1"}},
    /* gear    */ {{0, "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"},
                   {1, nullptr, 12, 12, 3, 0}},
};
static std::vector<RIconDef> g_i31;
static void RBuildIcons31() {
    if (!g_i31.empty()) return;
    g_i31.resize(I31_N);
    for (int i = 0; i < I31_N; i++) {
        for (int k = 0; k < 4; k++) {
            const RShapeSpec& s = k31Shape[i][k];
            if (s.kind == 0 && s.d) {
                std::vector<RPoly> ps;
                RParseD(s.d, ps);
                for (auto& p : ps) g_i31[i].polys.push_back(std::move(p));
            } else if (s.kind == 1) {
                RPoly p;
                for (int n = 0; n <= 32; n++) {
                    const float a = -kRPi * 0.5f + 2 * kRPi * (float)n / 32.0f;
                    p.p.push_back(ImVec2(s.a + std::cos(a) * s.c, s.b + std::sin(a) * s.c));
                }
                p.closed = true;
                g_i31[i].polys.push_back(std::move(p));
            } else if (s.kind == 2) {
                RPoly p;
                const float x = s.a, y = s.b, w = s.c, h = s.e, r = s.f;
                const ImVec2 c[4] = {ImVec2(x + w - r, y + r), ImVec2(x + w - r, y + h - r),
                                     ImVec2(x + r, y + h - r), ImVec2(x + r, y + r)};
                const float a0[4] = {-kRPi * 0.5f, 0.0f, kRPi * 0.5f, kRPi};
                for (int k2 = 0; k2 < 4; k2++)
                    for (int n = 0; n <= 8; n++) {
                        const float a = a0[k2] + kRPi * 0.5f * (float)n / 8.0f;
                        p.p.push_back(ImVec2(c[k2].x + std::cos(a) * r, c[k2].y + std::sin(a) * r));
                    }
                p.closed = true;
                g_i31[i].polys.push_back(std::move(p));
            }
        }
    }
}
/// 24 viewBox / stroke 1.6 / 圆头。sw 是「每 24 个单位多少笔宽」，空态那枚要用 1.15。
static void I31Stroke(ImDrawList* dl, int id, float x, float y, float size, ImU32 col,
                      float sw = 1.6f) {
    if (id < 0 || id >= I31_N || size <= 0.0f) return;
    RBuildIcons31();
    const float k = size / 24.0f;
    const float th = sw * k;
    static std::vector<ImVec2> tmp;
    for (const auto& poly : g_i31[id].polys) {
        const int n = (int)poly.p.size();
        if (n < 2) continue;
        tmp.clear();
        tmp.reserve((size_t)n);
        for (const auto& q : poly.p) tmp.push_back(ImVec2(x + q.x * k, y + q.y * k));
        dl->AddPolyline(tmp.data(), n, col, poly.closed ? ImDrawFlags_Closed : 0, th);
        if (!poly.closed) {
            dl->AddCircleFilled(tmp.front(), th * 0.5f, col, 8);
            dl->AddCircleFilled(tmp.back(), th * 0.5f, col, 8);
        }
    }
}
static void I31(ImDrawList* dl, int id, float x, float y, float size, ImU32 col) {
    I31Stroke(dl, id, x, y, size, col);
}

// ── 数字：稿子的 Intl.NumberFormat 与 compact ───────────────────────────────
static std::string R31Group(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return n < 0 ? "-" + s : s;
}
/// 最多两位小数、去掉尾零，整数千分位。额度百分比、Credits 余额、tokens 都走这一条。
static std::string R31Num(double v) {
    const bool neg = v < 0;
    double a = std::fabs(v);
    long long ip = (long long)std::floor(a + 1e-9);
    int fr = (int)std::lround((a - (double)ip) * 100.0);
    if (fr >= 100) { ip++; fr = 0; }
    std::string s = R31Group(ip);
    if (fr > 0) {
        s += '.';
        s += (char)('0' + fr / 10);
        if (fr % 10) s += (char)('0' + fr % 10);
    }
    return neg ? "-" + s : s;
}
/// 稿子 compact()：≥1M 用 M、≥1K 用 K，其余原样（带千分位）。
static std::string R31Compact(double v) {
    if (v >= 1000000.0) return R31Num(v / 1000000.0) + "M";
    if (v >= 1000.0) return R31Num(v / 1000.0) + "K";
    return R31Num(v);
}

// ── 文本：折行 / 尾部贴合 / 「%d」分段上色 ──────────────────────────────────
static std::vector<std::string> R31Cps(const std::string& s) {
    std::vector<std::string> v;
    const unsigned char* p = (const unsigned char*)s.c_str();
    while (*p) {
        const size_t l = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
        v.emplace_back((const char*)p, l);
        p += l;
    }
    return v;
}
static std::string R31Lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
/// 按宽度贪心折行；\n 是硬换行。超过 maxLines 时把最后一行裁成省略号。
static std::vector<std::string> R31Wrap(const std::string& text, float maxw, int loc, int tier,
                                        int maxLines) {
    std::vector<std::string> out;
    if (maxw <= 4.0f || text.empty()) return out;
    const auto cps = R31Cps(text);
    std::string line;
    bool pendingBreak = false;
    auto flush = [&]() {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out.push_back(line);
        line.clear();
    };
    for (const auto& c : cps) {
        if (c == "\n") { flush(); pendingBreak = true; continue; }
        if (pendingBreak && c == " ") continue;
        pendingBreak = false;
        if (line.empty() && c == " ") continue;
        if (!line.empty() && TextWidth(line + c, loc, tier) > maxw) {
            // 拉丁文退到最后一个空格断行，不把词拆成两半（overflow-wrap 的浏览器行为）；
            // CJK 行里没有空格，或断点处只剩一个词，才按字符硬断。
            const size_t sp = (c == " ") ? std::string::npos : line.rfind(' ');
            if (sp != std::string::npos && sp > 0) {
                const std::string rest = line.substr(sp + 1);
                line.erase(sp);
                flush();
                line = rest;
            } else {
                flush();
                if (c == " ") continue;
            }
        }
        line += c;
    }
    if (!line.empty() || out.empty()) flush();
    if (maxLines > 0 && (int)out.size() > maxLines) {
        out.resize((size_t)maxLines);
        out.back() = ClipToWidth(out.back(), maxw, loc, tier);
    }
    return out;
}
/// 输入框里的字太长时保尾部（光标永远在最右，用户看的是刚打的字）。
static std::string R31TailFit(const std::string& s, float maxw, int loc, int tier) {
    if (TextWidth(s, loc, tier) <= maxw) return s;
    const auto cps = R31Cps(s);
    std::string out;
    for (int i = (int)cps.size() - 1; i >= 0; i--) {
        const std::string cand = cps[(size_t)i] + out;
        if (TextWidth(cand, loc, tier) > maxw) break;
        out = cand;
    }
    return out;
}
static float R31DrawWrap(ImDrawList* dl, const std::string& s, float x, float y, float maxw,
                         int loc, int tier, float lineH, ImU32 col, int maxLines = 0) {
    const auto lines = R31Wrap(s, maxw, loc, tier, maxLines);
    float cy = y;
    for (const auto& ln : lines) {
        DrawTextRun(dl, x, TextTop(cy, lineH, tier, loc), ln, loc, col, tier);
        cy += lineH;
    }
    return cy;
}
/// 一条带 %d 的文案：数字单独换色/换字重，前后缀走原色。返回总宽。
static float R31FmtN(ImDrawList* dl, float x, float y, const char* fmt, long n, int loc,
                     int tier, ImU32 col, ImU32 numCol, int numTier) {
    const std::string f = fmt;
    const size_t p = f.find("%d");
    if (p == std::string::npos) {
        DrawTextRun(dl, x, y, f, loc, col, tier);
        return TextWidth(f, loc, tier);
    }
    const std::string pre = f.substr(0, p), post = f.substr(p + 2);
    const std::string num = R31Num((double)n);
    float cx = x;
    if (!pre.empty()) cx = DrawTextRun(dl, cx, y, pre, loc, col, tier);
    cx = DrawTextRun(dl, cx, y, num, loc, numCol, numTier);
    if (!post.empty()) cx = DrawTextRun(dl, cx, y, post, loc, col, tier);
    return cx - x;
}

// ── 材质：一块玻璃卡 ────────────────────────────────────────────────────────
// 一块「液态玻璃」在 ImDrawList 里拆成九层，每层都是一个可数的量：
//   ① 底纱 veil     —— 把面板色提亮多少。玻璃主体要尽量透，靠边而不是靠底
//   ② 顶沿受光 spec —— 光从上沿打进来，往下两三成高度衰减
//   ③ 光边 rim      —— 描边一律是光：上沿最亮、两侧次之、下沿最弱（下沿也接光，只是弱）
//   ④ 内阴影 shade  —— 下沿内侧压暗，卡才有厚度。放在光边里侧，不做成暗描边
//   ⑤ 上角镜边 arc  —— 圆角处那一小段更亮，读成透镜边缘
//   ⑥ 外投影 shadow —— 让卡离面板浮起来。面板压成实色后，层级主要靠这层
//   ⑦ 边缘折射带    —— 左右内侧 2~3px 的弱光带，模拟看穿玻璃边缘时被压缩的背景
//   ⑧ 面板光场 field—— 面板内建一盏很弱的聚光，玻璃才有东西可反（仍在面板内部）
//   ⑨ 左上镜面池    —— 窄高光在卡面左上聚成一小滩；液态玻璃读起来「有液体」就靠它
// 拿不到的是真折射：那要采样窗口背后的像素。这里做的是同一套光学线索，不是
// backdrop-filter 的等价物——这一条在 README 里明写。
//
// 两条来自实拍的修正：
//  · 全环「上亮下暗」在第一版被量成 38→62→17 的亮暗夹线，读起来是塑料倒角。
//    改成描边全亮、暗只在内侧，才像玻璃。
//  · 主体均匀提亮会变成一块雾面板：glass 的辨识度在边上，所以清玻把 veil 压到 2%、
//    把 rim/arc/镜面池抬上去（2026-10-28 拍板：所有 UI 组件统一清玻）。
struct RGlass31 {
    float veil;      // ① 底纱（0..1，叠向冷白）
    float tintB;     // 冷色分量：0 中性白，1 偏蓝
    float spec;      // ② 顶沿受光峰值（0..255）
    float specF;     // ② 受光带高（占 h 的比例）
    float rim;       // ③ 上沿光边（0..255）
    float rimS;      // ③ 两侧 = rim × rimS
    float rimB;      // ③ 下沿 = rim × rimB
    float shade;     // ④ 下沿内阴影（0..255）
    float shadowA;   // ⑥ 外投影浓度（0..1）
    float shadowB;   // ⑥ 外投影 blur（pt）
    float shadowDy;  // ⑥ 外投影下移（pt）
    float lens;      // ⑦ 边缘折射带（0..255）
    float gloss;     // ⑨ 左上镜面池（0..255）
    float specR;     // ② 受光带右端 = spec × specR（<1 就是左亮右暗的方向光）
    float lift;      // 额外提亮/压暗（-1..1），烟熏玻璃用负值
    float field;     // ⑧ 面板光场强度（0 关）
};
static const RGlass31 kG31Clear = [] {
    RGlass31 g{};
    g.veil = 0.020f; g.tintB = 0.40f;
    g.spec = 46.0f;  g.specF = 0.09f; g.specR = 0.55f;
    g.rim = 190.0f;  g.rimS = 0.55f;  g.rimB = 0.32f;
    g.shade = 8.0f;
    g.shadowA = 0.22f; g.shadowB = 16.0f; g.shadowDy = 2.0f + 16.0f * 0.22f;
    g.lens = 12.0f;  g.gloss = 10.0f;
    g.lift = 0.00f;  g.field = 0.30f;
    return g;
}();

// 面板几何与当前配色：R31Frame 每帧回填，玻璃按卡中心采样「我背后是什么颜色」。
static float g31PX0 = 0.0f, g31PY0 = 0.0f, g31PW = 416.0f, g31PH = 850.0f;
static ImU32 g31PTop = k31PanelTop, g31PBot = k31PanelBot;
static ImU32 R31PanelNow(float yy) {
    float t = g31PH > 1.0f ? (yy - g31PY0) / g31PH : 0.0f;
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    return MixA(g31PTop, g31PBot, t);
}
// ⑧ 面板光场：左上偏中一盏很弱的椭圆聚光，归一到 0..1。画与采样用同一条式子。
static const float kFieldCX = 0.30f, kFieldCY = 0.16f;
static const float kFieldSX = 0.24f, kFieldSY = 0.18f;
static float R31FieldAt(float px, float py) {
    const float cx = g31PX0 + g31PW * kFieldCX, cy = g31PY0 + g31PH * kFieldCY;
    const float sx = g31PW * kFieldSX, sy = g31PH * kFieldSY;
    const float dx = (px - cx) / sx, dy = (py - cy) / sy;
    const float d2 = dx * dx + dy * dy;
    return d2 > 9.0f ? 0.0f : std::exp(-0.5f * d2);
}
static ImU32 R31Brighten(ImU32 c, float t) {
    if (t >= 0.0f) return MixA(c, IM_COL32(255, 255, 255, 255), t);
    return MixA(c, IM_COL32(0, 0, 0, 255), -t);
}
static float R31InsetAt(float yy, float top, float h, float r) {
    float rr = r > h * 0.5f ? h * 0.5f : r;
    float d = (yy < top + h * 0.5f) ? (top + rr - yy) : (yy - (top + h - rr));
    if (d <= 0.0f || rr <= 0.0f) return 0.0f;
    if (d >= rr) return rr;
    return rr - std::sqrt(rr * rr - d * d);
}
// 一条贴着圆角内侧的渐隐色带：top=true 从上沿往下衰减，false 从下沿往上。
static void R31Band(ImDrawList* dl, float x, float y, float w, float h, float r,
                    float bandH, ImU32 col, bool top, float power) {
    if (bandH <= 0.5f || ((col >> 24) & 0xFF) == 0) return;
    const int N = 10;
    const int a0 = (int)(col >> 24);
    for (int i = 0; i < N; i++) {
        const float t0 = (float)i / N, t1 = (float)(i + 1) / N;
        const float y0 = top ? y + bandH * t0 : y + h - bandH * t1;
        const float y1 = top ? y + bandH * t1 : y + h - bandH * t0;
        const float ins = std::max(R31InsetAt(y0, y, h, r), R31InsetAt(y1, y, h, r));
        const float f = std::pow(1.0f - 0.5f * (t0 + t1), power);
        const int a = (int)(a0 * f + 0.5f);
        if (a <= 0) continue;
        dl->AddRectFilled(ImVec2(x + ins, y0), ImVec2(x + w - ins, y1),
                          (col & 0x00FFFFFFu) | ((ImU32)a << 24));
    }
}
// 一枚软椭圆光斑：从外到内叠实心椭圆，每层补齐还差的覆盖率，得到平滑的高斯衰减。
static void R31Blob(ImDrawList* dl, ImVec2 c, float rx, float ry, ImU32 col, int N = 18) {
    const float A = (float)(col >> 24) / 255.0f;
    if (A <= 0.002f) return;
    float done = 0.0f;
    for (int i = 0; i < N; i++) {
        const float t = 1.0f - (float)i / N;
        const float a = A * std::exp(-0.5f * std::pow(t * 1.9f, 2.0f));
        if (a <= done) continue;
        const int al = (int)(255.0f * (a - done) / (1.0f - done) + 0.5f);
        if (al <= 0) continue;
        dl->AddEllipseFilled(c, ImVec2(rx * t, ry * t),
                             (col & 0x00FFFFFFu) | ((ImU32)al << 24), 0.0f, 48);
        done += (1.0f - done) * ((float)al / 255.0f);
    }
}
// 圆角光边一笔画完：直边与圆弧共用同一条中心线，alpha 沿轮廓连续插值
// （上沿最亮 → 两侧 → 下沿）。旧版是四条 AddLine + 四个 PathStroke 圆弧，
// 上沿 alpha 190、两侧 104、下沿 61、圆弧 110，每过一个切点亮度就跳一档，
// 2x 上看得见「两截拼起来」的接缝（2026-10-28 实拍角部放大 8 倍）。
// 顶点四层：内 fringe（alpha 0）/ 内沿 / 外沿 / 外 fringe（alpha 0），
// 两侧各扩半个 fringe，圆弧段数随半径走，弦高 < 0.02pt。
static void R31StrokeRound(ImDrawList* dl, float x, float y, float w, float h, float r,
                           int aTop, int aSide, int aBot, float th) {
    if (w <= 1.0f || h <= 1.0f) return;
    float rr = r;
    if (rr < 0.5f) {   // 直角框不参与插值：那种尺寸的框是发丝线，没有角可拼
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 255, 255, aTop), 0.0f, 0, th);
        return;
    }
    if (rr > h * 0.5f) rr = h * 0.5f;
    if (rr > w * 0.5f) rr = w * 0.5f;
    const float kPi = 3.14159265358979f;
    struct Node { float x, y, a; };
    std::vector<Node> path;
    auto push = [&](float px, float py, float pa) { path.push_back(Node{px, py, pa}); };
    auto arc = [&](float cx, float cy, float a0, float a1, float aA, float aB) {
        int seg = (int)(rr * 1.2f) + 4;
        if (seg < 6) seg = 6;
        if (seg > 24) seg = 24;
        for (int i = 0; i <= seg; i++) {
            const float t = (float)i / (float)seg;
            const float a = a0 + (a1 - a0) * t;
            push(cx + std::cos(a) * rr, cy + std::sin(a) * rr, aA + (aB - aA) * t);
        }
    };
    const float fT = (float)aTop, fS = (float)aSide, fB = (float)aBot;
    push(x + rr, y, fT);
    push(x + w - rr, y, fT);
    arc(x + w - rr, y + rr, -kPi * 0.5f, 0.0f, fT, fS);
    push(x + w, y + h - rr, fS);
    arc(x + w - rr, y + h - rr, 0.0f, kPi * 0.5f, fS, fB);
    push(x + rr, y + h, fB);
    arc(x + rr, y + h - rr, kPi * 0.5f, kPi, fB, fS);
    push(x, y + rr, fS);
    arc(x + rr, y + rr, kPi, kPi * 1.5f, fS, fT);
    path.pop_back();                    // 尾点与首点重合，去掉
    const int n = (int)path.size();
    if (n < 4) return;

    const float aa = dl->_FringeScale * 0.5f;
    const float half = th * 0.5f;
    dl->PrimReserve(n * 18, n * 4);
    const unsigned int base = dl->_VtxCurrentIdx;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    for (int i = 0; i < n; i++) {
        const Node& p0 = path[(size_t)((i + n - 1) % n)];
        const Node& p1 = path[(size_t)i];
        const Node& p2 = path[(size_t)((i + 1) % n)];
        float nx = 0.0f, ny = 0.0f;
        for (int k = 0; k < 2; k++) {
            const Node& a = k == 0 ? p0 : p1;
            const Node& b = k == 0 ? p1 : p2;
            const float ex = b.y - a.y, ey = -(b.x - a.x);
            const float d2 = ex * ex + ey * ey;
            if (d2 > 1e-6f) {
                const float inv = 1.0f / std::sqrt(d2);
                nx += ex * inv;
                ny += ey * inv;
            }
        }
        const float d2 = nx * nx + ny * ny;
        const float inv = d2 > 1e-6f ? 1.0f / std::sqrt(d2) : 0.0f;
        nx *= inv;
        ny *= inv;
        const int al = (int)(p1.a + 0.5f);
        const ImU32 ci = IM_COL32(255, 255, 255, al);
        const ImU32 c0 = ci & 0x00FFFFFFu;
        const float inA = half + aa, ouA = half;
        dl->PrimWriteVtx(ImVec2(p1.x + nx * inA, p1.y + ny * inA), uv, c0);
        dl->PrimWriteVtx(ImVec2(p1.x + nx * ouA, p1.y + ny * ouA), uv, ci);
        dl->PrimWriteVtx(ImVec2(p1.x - nx * ouA, p1.y - ny * ouA), uv, ci);
        dl->PrimWriteVtx(ImVec2(p1.x - nx * inA, p1.y - ny * inA), uv, c0);
    }
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        const unsigned int a0 = base + (unsigned int)(i * 4);
        const unsigned int b0 = base + (unsigned int)(j * 4);
        for (int s = 0; s < 3; s++) {   // 三条带：外 AA / 实心 / 内 AA
            dl->PrimWriteIdx((ImDrawIdx)(a0 + s));
            dl->PrimWriteIdx((ImDrawIdx)(a0 + s + 1));
            dl->PrimWriteIdx((ImDrawIdx)(b0 + s));
            dl->PrimWriteIdx((ImDrawIdx)(b0 + s));
            dl->PrimWriteIdx((ImDrawIdx)(a0 + s + 1));
            dl->PrimWriteIdx((ImDrawIdx)(b0 + s + 1));
        }
    }
}
static void R31Glass(ImDrawList* dl, float x, float y, float w, float h, float r,
                     float hover = 0.0f, ImU32 border = 0) {
    const RGlass31& g = kG31Clear;
    // 尺寸分档：26pt 以下（小胶囊）只留底纱+光边。
    const bool tiny = h < 26.0f;

    ImU32 pc = R31PanelNow(y + h * 0.5f);
    if (g.field > 0.0f) pc = R31Brighten(pc, g.field * 0.16f * R31FieldAt(x + w * 0.5f, y + h * 0.5f));
    if (g.lift != 0.0f) pc = R31Brighten(pc, g.lift);

    // ⑥ 外投影先画，随后被不透明底纱盖住内部——所以底纱必须算成不透明色。
    if (g.shadowA > 0.001f && !tiny) {
        const float sy = y + g.shadowDy;
        GlowRect(dl, ImVec2(x + 1.0f, sy), ImVec2(x + w - 1.0f, sy + h), r, g.shadowB, 0.0f,
                 IM_COL32(0, 0, 0, (int)(g.shadowA * 255.0f + 0.5f)));
    }

    const ImU32 tintCol = IM_COL32((int)(248.0f - 12.0f * g.tintB), (int)(251.0f - 5.0f * g.tintB),
                                   255, 255);
    float va = g.veil + hover * 0.045f;
    if (va > 0.55f) va = 0.55f;
    // 上亮下暗的竖向渐变：玻璃整体是弯的，不是一个平色块
    GradRound(dl, x, y, w, h, r, MixA(pc, tintCol, va), MixA(pc, tintCol, va * 0.45f), 0);

    // ⑨ 左上镜面池（先于受光带画，受光带压在它上沿）。必须裁到卡形里，否则溢出到面板上。
    if (!tiny && g.gloss > 0.5f) {
        dl->PushClipRect(ImVec2(x + 1.0f, y + 1.0f), ImVec2(x + w - 1.0f, y + h - 1.0f), true);
        R31Blob(dl, ImVec2(x + w * 0.18f, y + h * 0.14f), w * 0.20f, h * 0.20f,
                IM_COL32(255, 255, 255, (int)(g.gloss + 0.5f)));
        dl->PopClipRect();
    }
    // ② 顶沿受光：上沿最亮、指数衰减；左亮右暗＝光从左上方打过来。
    // 分 8 条画而不是一整块四角渐变：四角渐变的竖向是线性的，在带底会留一道看得见的台阶。
    if (!tiny && g.spec > 0.5f) {
        const float spH = std::min(h * g.specF, 20.0f);
        const float ins = std::min(r * 0.72f, w * 0.15f);
        const int N = 8;
        if (w > ins * 2.0f + 2.0f && spH > 1.0f) {
            for (int i = 0; i < N; i++) {
                const float t0 = (float)i / N, t1 = (float)(i + 1) / N;
                const float f0 = std::pow(1.0f - t0, 2.6f), f1 = std::pow(1.0f - t1, 2.6f);
                const int al0 = (int)(g.spec * f0 + 0.5f), ar0 = (int)(g.spec * g.specR * f0 + 0.5f);
                const int al1 = (int)(g.spec * f1 + 0.5f), ar1 = (int)(g.spec * g.specR * f1 + 0.5f);
                if (al0 <= 0 && ar0 <= 0) break;
                dl->AddRectFilledMultiColor(
                    ImVec2(x + ins, y + 1.0f + spH * t0), ImVec2(x + w - ins, y + 1.0f + spH * t1),
                    IM_COL32(255, 255, 255, al0), IM_COL32(255, 255, 255, ar0),
                    IM_COL32(255, 255, 255, ar1), IM_COL32(255, 255, 255, al1));
            }
        }
    }
    // ⑦ 边缘折射带：左右内侧的弱光（不做暗侧，暗只留给内侧阴影）
    if (!tiny && g.lens > 0.5f && h > r * 2.0f) {
        const float lw = std::min(2.5f, w * 0.018f);
        dl->AddRectFilled(ImVec2(x + 1.0f, y + r), ImVec2(x + 1.0f + lw, y + h - r),
                          IM_COL32(255, 255, 255, (int)(g.lens * 0.30f)), 1.0f);
        dl->AddRectFilled(ImVec2(x + w - 1.0f - lw, y + r), ImVec2(x + w - 1.0f, y + h - r),
                          IM_COL32(255, 255, 255, (int)(g.lens * 0.22f)), 1.0f);
    }
    // ④ 下沿内阴影（厚度）：画在光边里侧
    if (!tiny && g.shade > 0.5f)
        R31Band(dl, x, y, w, h, r, std::min(h * 0.12f, 9.0f),
                IM_COL32(0, 0, 0, (int)(g.shade + 0.5f)), false, 2.2f);

    // ③ 光边（一笔画完整圈：上沿最亮 → 两侧 → 下沿，过角时 alpha 连续过渡）
    if (border) {
        dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f),
                    border, r, 0, 1.0f);
    } else {
        const int aT = (int)(g.rim + 0.5f), aS = (int)(g.rim * g.rimS + 0.5f);
        const int aB = (int)(g.rim * g.rimB + 0.5f);
        R31StrokeRound(dl, x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f,
                       r > 0.5f ? r - 0.5f : 0.0f, aT, aS, aB, 1.1f);
    }
}
static void R31Frame(ImDrawList* dl, float x, float y, float w, float h, double tn = 0.0) {
    g31PX0 = x; g31PY0 = y; g31PW = w; g31PH = h; g31PTop = k31PanelTop; g31PBot = k31PanelBot;
    GradRound(dl, x, y, w, h, k31PaneR, k31PanelTop, k31PanelBot, 0);
    // 上沿 150pt 的一层白纱：面板上半段比下半段透一点，不是一块死色。
    // 必须跟着面板圆角走：方矩形会在弧外的透明区留下一撮白（4% 也看得见方肩）。
    GradRound(dl, x + 1.0f, y + 1.0f, w - 2.0f, 149.0f, k31PaneR,
              IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 0), 0);
    // ⑧ 面板光场：玻璃在这块底上才「有东西可反」。画在面板内部，桌面再怎么变都不影响。
    {
        const float fs = kG31Clear.field;
        if (fs > 0.001f) {
            const ImVec2 c(x + w * kFieldCX, y + h * kFieldCY);
            const float rx = w * kFieldSX * 3.0f, ry = h * kFieldSY * 3.0f;   // 3σ 外径，与 R31FieldAt 同一条式子
            const float A = 0.16f * fs;
            dl->PushClipRect(ImVec2(x + 1.0f, y + 1.0f), ImVec2(x + w - 1.0f, y + h - 1.0f), true);
            float done = 0.0f;
            for (int i = 0; i < 40; i++) {
                const float t = 1.0f - (float)i / 40.0f;               // 1 → 0.025
                const float a = A * std::exp(-0.5f * std::pow(t * 3.0f, 2.0f));
                if (a <= done) continue;
                const int al = (int)(255.0f * (a - done) / (1.0f - done) + 0.5f);
                if (al <= 0) continue;
                dl->AddEllipseFilled(c, ImVec2(rx * t, ry * t),
                                     IM_COL32(236, 244, 255, al), 0.0f, 64);
                done += (1.0f - done) * ((float)al / 255.0f);
            }
            dl->PopClipRect();
        }
    }
    // ::after 的 inset 0 0 0 .5px var(--rim)
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f),
                k31Rim, k31PaneR, 0, 1.0f);
    dl->AddLine(ImVec2(x + 18.0f, y + 1.4f), ImVec2(x + w - 18.0f, y + 1.4f),
                IM_COL32(255, 255, 255, 54), 1.0f);
    dl->AddLine(ImVec2(x + 18.0f, y + h - 1.4f), ImVec2(x + w - 18.0f, y + h - 1.4f),
                IM_COL32(0, 0, 0, 96), 1.0f);
    // ── 液体玻璃质感（系统 backdrop-blur 拿不到，这是可做的近似）──
    // ① 顶部 18pt 镜面反光：受光渐弱，像一块玻璃上沿
    // 端点收成圆角：这条带从 x+8 起画，而 y=1 处面板弧已经内缩了 16pt，
    // 直角会在透明底上露出 10% 的白角（r=8.5 的弧落在面板弧内侧 0.5pt）。
    GradRound(dl, x + 8.0f, y + 1.0f, w - 16.0f, 18.0f, 8.5f,
              IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 0), 0);
    // （流光版撤于 2026-10-28 用户拍板：不要流动光晕，只留静态镜面。）
    (void)tn;
}

// ── 工具身份 ────────────────────────────────────────────────────────────────
static const Tool* R31ToolOf(const std::string& id) {
    for (const auto& t : g_pd.tools) if (t.id == id) return &t;
    return nullptr;
}
/// 稿子的 providers 表：面板上念的是产品名，不是采集器文件名。
static std::string R31Agent(const std::string& id, int loc) {
    if (id == "claude") return "Claude Code";
    if (id == "qoder") return "Qoder";
    if (id == "codex") return "Codex";
    if (id == "zcode") return "ZCode";
    if (id == "workbuddy") return "WorkBuddy";
    if (id == "opencode") return "OpenCode";
    if (const Tool* t = R31ToolOf(id)) return t->name;
    (void)loc;
    return id;
}
static std::string R31Short(const std::string& id) {
    if (id == "claude") return "Claude";
    if (id == "qoder") return "Qoder";
    if (id == "codex") return "Codex";
    if (id == "zcode") return "ZCode";
    if (id == "workbuddy") return "WorkBuddy";
    if (id == "opencode") return "OpenCode";
    if (const Tool* t = R31ToolOf(id)) return t->initial.empty() ? t->name : t->name;
    return id;
}
static std::string R31Client(const std::string& id) {
    if (const Tool* t = R31ToolOf(id)) return t->client;
    return (id == "zcode" || id == "workbuddy") ? "IDE" : "CLI";
}
/// 品牌标：有官方像素的五路贴图；Codex 没有独立 logo，稿子给的就是通用终端块。
static void Brand31(ImDrawList* dl, float x, float y, float s, const std::string& id, int loc) {
    const bool textured = (id == "qoder" || id == "claude" || id == "zcode" ||
                           id == "workbuddy" || id == "opencode");
    if (textured) {
        const Tool* t = R31ToolOf(id);
        const ImU32 hue = t ? t->hue : IM_COL32(160, 170, 185, 255);
        const std::string ini = t ? t->initial : std::string("?");
        RAgentTile(dl, x, y, s, s * 0.235f, hue, ini, loc, id);
        return;
    }
    if (id == "codex" || id.empty()) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s), k31Sel, s * 0.235f);
        dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + s - 0.5f, y + s - 0.5f),
                    k31Line, s * 0.235f, 0, 1.0f);
        const float is = s * 0.68f;
        I31Stroke(dl, I31_TERM, x + (s - is) * 0.5f, y + (s - is) * 0.5f, is, k31Fg, 1.75f);
        return;
    }
    const Tool* t = R31ToolOf(id);
    const ImU32 hue = t ? t->hue : IM_COL32(160, 175, 195, 255);
    const std::string ini = t ? t->initial : std::string("?");
    RAgentTile(dl, x, y, s, s * 0.235f, hue, ini, loc, "");
}
/// `.client-tag`（详情身份那颗）：mono 10/500（实测框高 13，不是声明推的 16）。
static float R31ClientTag(ImDrawList* dl, float x, float cy, const std::string& id, int loc) {
    const std::string s = R31Client(id);
    const float tw = TextWidth(s, loc, T_M10B);
    const float w = tw + 6.0f + 2.0f, h = 13.0f;
    dl->AddRect(ImVec2(x + 0.5f, cy - h * 0.5f + 0.5f), ImVec2(x + w - 0.5f, cy + h * 0.5f - 0.5f),
                k31Line, 3.0f, 0, 1.0f);
    DrawTextRun(dl, x + 4.0f, TextTop(cy - h * 0.5f, h, T_M10B, loc), s, loc, k31Muted, T_M10B, 0.3f);
    return w;
}
/// `.agent-name`：12/600 / -.1px。
static float R31AgentName(ImDrawList* dl, float x, float cy, const std::string& id, int loc) {
    return DrawTextRun(dl, x, TextMid(cy, T_S12B, loc), R31Agent(id, loc), loc, k31Fg, T_S12B, -0.1f);
}

// ── 状态 ────────────────────────────────────────────────────────────────────
/// 一行的状态档：0 提问 / 1 授权 / 2 运行 / 3 排队 / 4 失败 / 5 完成。
/// 颜色不新造：稿子的 .ask/.permission 就是 --blue/--red，运行是 --green，
/// 完成与排队走 --muted。图标同稿：question/lock/refresh/clock/close/check。
static int R31KindOf(const UIRow& r) {
    if (GroupOf(r.state) == G_RUN) return 2;
    if (GroupOf(r.state) == G_QUEUE) return 3;
    if (GroupOf(r.state) == G_FAIL) return 4;
    if (GroupOf(r.state) == G_DONE) return 5;
    return RowAsksQuestion(r) ? 0 : 1;
}
static ImU32 R31KindColor(int k) {
    switch (k) {
        case 0: return k31Blue;
        case 1: return k31Red;
        case 2: return k31Green;
        case 4: return k31Red;
        default: return k31Muted;
    }
}
static const char* R31KindWord(int k) {
    switch (k) {
        case 0: return "r_ask_short";
        case 1: return "r_perm_short";
        case 2: return "r_running";
        case 3: return "r_queue_short";
        case 4: return "r_fail_short";
        default: return "r_done_today";
    }
}
static int R31KindIcon(int k) {
    switch (k) {
        case 0: return I31_QUESTION;
        case 1: return I31_LOCK;
        case 2: return I31_REFRESH;
        case 3: return I31_CLOCK;
        case 4: return I31_CLOSE;
        default: return I31_CHECK;
    }
}
/// `.status`：icon 11 + 4 + 11/500，右对齐时由调用方给 x1。
/// 完成但没看过的行在这一格写「待查看」而不是「今日完成」：它在待处理清单里，
/// 用完成的字眼会被读成「已经处理完了，为什么还列在这儿」。
static bool R31UnseenDone(const UIRow& r) {
    return GroupOf(r.state) == G_DONE && r.unseen;
}
static float R31StatusW(const UIRow& r, int loc) {
    return 11.0f + 4.0f + TextWidth(T(R31UnseenDone(r) ? "r_unseen_done" : R31KindWord(R31KindOf(r)),
                                      loc), loc, T_S11B);
}
static void R31StatusRight(ImDrawList* dl, float x1, float cy, const UIRow& r, int loc) {
    const int k = R31KindOf(r);
    const bool unseen = R31UnseenDone(r);
    const std::string w = T(unseen ? "r_unseen_done" : R31KindWord(k), loc);
    const float tw = TextWidth(w, loc, T_S11B);
    const ImU32 col = unseen ? k31Blue : R31KindColor(k);
    I31(dl, R31KindIcon(k), x1 - tw - 4.0f - 11.0f, cy - 5.5f, 11.0f, col);
    DrawTextRun(dl, x1 - tw, TextMid(cy, T_S11B, loc), w, loc, col, T_S11B);
}

// ── 查询与筛选 ──────────────────────────────────────────────────────────────
static bool R31Match(const UIRow& r, const std::string& q) {
    if (q.empty()) return true;
    const std::string hay = R31Lower(r.project + " " + R31Agent(r.agent_id, 0) + " " +
                                     R31Client(r.agent_id) + " " + r.question + " " +
                                     r.title + " " + r.tool);
    return hay.find(q) != std::string::npos;
}
static std::vector<const UIRow*> R31Rows(const std::string& src, const std::string& query) {
    std::vector<const UIRow*> v;
    const std::string q = R31Lower(query);
    for (const auto& r : g_pd.rows) {
        if (src != "all" && r.agent_id != src) continue;
        if (!R31Match(r, q)) continue;
        v.push_back(&r);
    }
    return v;
}
/// 「待处理」＝两段，先后固定：
///   ① 要你动手的（回答／授权／失败）—— 卡住不动的是它们，排在前；
///   ② 今天跑完但还没看过的 —— 只等你扫一眼，双击或点 ⓘ 看过就算处理完。
/// 段内沿用 RebuildPanel 的「等得最久在前」，段间不混排（信息性的不许插到要动手的前面）。
/// 第二段必须算进面板的条数，但不进菜单栏那串数字：它不催人。
static std::vector<const UIRow*> R31Waits() {
    const std::vector<const UIRow*> all = R31Rows(g_r31_src, g_r31_query);
    std::vector<const UIRow*> waits, unseen;
    for (const auto* r : all) {
        const int g = GroupOf(r->state);
        if (g == G_AWAIT || g == G_FAIL) waits.push_back(r);
        else if (g == G_DONE && r->unseen) unseen.push_back(r);
    }
    waits.insert(waits.end(), unseen.begin(), unseen.end());
    return waits;
}
/// 稿子 present()：今天有行、有账户用量、有余额、或有额度，就占一颗来源。
/// 采集器「本机没有」不占位（它连一条记录都读不出，摆上去就是一格假身份）。
static std::vector<const Tool*> R31PresentTools() {
    static const char* kOrder[] = {"claude", "qoder", "zcode", "codex", "workbuddy", "opencode"};
    std::vector<const Tool*> v;
    for (const char* id : kOrder)
        for (const auto& t : g_pd.tools)
            if (t.id == id && t.present()) { v.push_back(&t); break; }
    for (const auto& t : g_pd.tools) {
        bool seen = false;
        for (const char* id : kOrder) if (t.id == id) { seen = true; break; }
        if (!seen && t.present()) v.push_back(&t);
    }
    return v;
}
static bool R31QuotaAny(const Tool& t) {
    return t.quota.known && (t.quota.five.known || t.quota.week.known);
}
static bool R31HasUsage(const Tool& t) { return t.usage.total() > 0 || t.has_credits; }
static int R31RemainPct(const ap::QuotaWindow& w) {
    double rem = (1.0 - w.used) * 100.0;
    if (rem < 0) rem = 0;
    if (rem > 100) rem = 100;
    return (int)std::lround(rem);
}
/// 读数过期：超过 10 分钟没被重读，或窗口早就该重置了。两种都把数字压灰、写「上次读数」。
static bool R31QuotaStale(const ap::QuotaWindow& w, double now) {
    if (!w.known) return false;
    if (w.seen_at > 0 && now - w.seen_at > 600.0) return true;
    if (w.resets_at > 0 && w.resets_at < now) return true;
    return false;
}

// ── 排版零件 ────────────────────────────────────────────────────────────────
static float R31LH(int tier, float mult) { return kTierPx[tier] * mult; }
/// 在一行中间放一段字。cy 是这一行的竖向中心。
static float R31Mid(ImDrawList* dl, float x, float cy, const std::string& s, int loc, ImU32 col,
                    int tier, float track = 0.0f) {
    return DrawTextRun(dl, x, TextMid(cy, tier, loc), s, loc, col, tier, track);
}
/// 稿子的 .mode-tag：9px / 1px 边 / 圆角 5 / 内边距 2 5 / 行高 18。
static float R31ModeW(const std::string& s, int loc) { return TextWidth(s, loc, T_S9) + 12.0f; }
static void R31Mode(ImDrawList* dl, float x, float cy, const std::string& s, int loc) {
    const float w = R31ModeW(s, loc), h = 18.0f;
    dl->AddRect(ImVec2(x + 0.5f, cy - h * 0.5f + 0.5f), ImVec2(x + w - 0.5f, cy + h * 0.5f - 0.5f),
                k31Line, 5.0f, 0, 1.0f);
    DrawTextRun(dl, x + 6.0f, TextMid(cy, T_S9, loc), s, loc, k31Muted, T_S9);
}
/// 稿子的 .icon-button：29×29 / 圆角 7 / 按下态（aria-pressed）铺 --selected。
static void R31IconBtn(ImDrawList* dl, float cx, float cy, int icon, int isz, bool on, ImU32 col) {
    if (on) dl->AddRectFilled(ImVec2(cx - 14.5f, cy - 14.5f), ImVec2(cx + 14.5f, cy + 14.5f),
                              k31Sel, 7.0f);
    I31(dl, icon, cx - isz * 0.5f, cy - isz * 0.5f, (float)isz, col);
}
static std::string R31ModeText(int loc) {
    return T(getenv("AGENTPING_FIXTURE") ? "r_sample_data" : "r_local_data", loc);
}
/// 子屏左上那颗「← 来处」：稿子写目的地，不写「返回」。
static const char* R31BackLabel(int from, int loc) {
    switch (from) {
        case 5: return T("r_back_history", loc);
        case 6: return T("r_back_usage", loc);
        case 7: return T("r_back_account", loc);
        case 8: return T("r_back_account", loc);
        case 3: return T("r_back_detail", loc);
        default: return T("r_back_sessions", loc);
    }
}

// ── 在跑卡 ──────────────────────────────────────────────────────────────────
/// 转圈那颗：底轨 27% 绿，两段 180° 的弧每 2.6s 转一圈；中心一颗点（inset 6 → 半径 2）。
static void R31Ring(ImDrawList* dl, float x, float y, double tn) {
    const float c = 8.0f;
    const ImU32 track = IM_COL32(141, 227, 191, 69);
    dl->AddCircle(ImVec2(x + c, y + c), c - 0.8f, track, 24, 1.6f);
    const float a = (float)std::fmod(tn / 2.6, 1.0) * 2.0f * kRPi;
    dl->PathArcTo(ImVec2(x + c, y + c), c - 0.8f, a, a + kRPi, 18);
    dl->PathStroke(k31Green, 0, 1.6f);
    dl->AddCircleFilled(ImVec2(x + c, y + c), 2.0f, k31Green, 12);
}
/// 跑马灯：55% 宽的亮带从右往左扫，2.1s 一轮，透明度 .45。
static void R31March(ImDrawList* dl, float x, float y, float w, float h, double tn) {
    const float band = w * 0.55f;
    const float p = (float)std::fmod(tn / 2.1, 1.0);
    const float bx = x + (w + band) * p - band;
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    const float x0 = std::fmax(x, bx), x1 = std::fmin(x + w, bx + band);
    if (x1 > x0 + 1.0f) {
        const float mid = (x0 + x1) * 0.5f;
        dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(mid, y + h), WithA(k31Sweep, 0),
                                    WithA(k31Sweep, 115), WithA(k31Sweep, 115), WithA(k31Sweep, 0));
        dl->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(x1, y + h), WithA(k31Sweep, 115),
                                    WithA(k31Sweep, 0), WithA(k31Sweep, 0), WithA(k31Sweep, 115));
    }
    dl->PopClipRect();
}
// ═══════════════════════════════════════════════════════════════════════════
// 六屏绘制层（2026-10-28 按 r31_spec/plan_2026-10-28.md 重写）。
// 版面数字的唯一真源是 r31_spec/geom_1440.json（1440 视口实测、面板原点已减去）；
// css_rules_flat.txt 里带 @media 前缀的降级值一律不用。坐标全部相对面板左上。
// ═══════════════════════════════════════════════════════════════════════════

// ── 表头：根屏＝产品名 + 模式标 + 齿轮 + 搜索；子屏＝← 来处 ──────────────────
static void R31Head(ImDrawList* dl, float x, float y, float w, int loc, int view) {
    const float L = x + 18.0f, R = x + w - 18.0f, cy = y + 27.0f;
    const bool root = (view == 0 || view == 6);
    const std::string mode = R31ModeText(loc);
    const float mw = R31ModeW(mode, loc);
    if (root) {
        BrandMark(dl, L, y + 14.5f, 25.0f);
        DrawTextRun(dl, x + 52.0f, TextMid(cy, T_S14B, loc), "Agent Ping", loc, k31Fg, T_S14B,
                    -0.3f);
        // 与批准稿的唯一有意偏差：生产要留设置入口 → 齿轮插在搜索左边，模式标右界 360→322。
        R31Mode(dl, x + 322.0f - mw, cy, mode, loc);
        const float gx = x + 331.0f, sx = x + 369.0f;
        {
            const float hov = TxPeekNew(Zid(loc, 300, HIT_GEAR, 0), 0.0f);
            if (hov > 0.01f)
                dl->AddRectFilled(ImVec2(gx, cy - 14.5f), ImVec2(gx + 29.0f, cy + 14.5f),
                                  k31Hover, 7.0f);
            I31(dl, I31_GEAR, gx + 5.0f, cy - 9.5f, 19.0f, k31Sec);
            AddHit(g_hits, gx, cy - 14.5f, gx + 29.0f, cy + 14.5f, HIT_GEAR, 300, 0, loc,
                   T("set_title", loc), 0);
        }
        {
            const float hov = TxPeekNew(Zid(loc, 301, HIT_SEARCHTOG, 0), 0.0f);
            if (g_r31_search || hov > 0.01f)
                dl->AddRectFilled(ImVec2(sx, cy - 14.5f), ImVec2(sx + 29.0f, cy + 14.5f),
                                  g_r31_search ? k31Sel : k31Hover, 7.0f);
            I31(dl, I31_SEARCH, sx + 5.0f, cy - 9.5f, 19.0f, k31Sec);
            AddHit(g_hits, sx, cy - 14.5f, sx + 29.0f, cy + 14.5f, HIT_SEARCHTOG, 301, 0, loc,
                   T("r_search", loc), g_r31_search ? 1 : 0);
        }
        return;
    }
    const int from = g_r31_trail.empty() ? g_tab : g_r31_trail.back();
    const std::string back = R31BackLabel(from, loc);
    const float bw = 15.0f + 5.0f + TextWidth(back, loc, T_S11);
    I31(dl, I31_BACK, L, cy - 7.5f, 15.0f, k31Sec);
    DrawTextRun(dl, L + 20.0f, TextMid(cy, T_S11, loc), back, loc, k31Sec, T_S11);
    AddHit(g_hits, L - 6.0f, cy - 15.0f, L + bw + 8.0f, cy + 15.0f, HIT_R31BACK, 302, 0, loc,
           back, 0);
    R31Mode(dl, R - mw, cy, mode, loc);
}

// ── 来源导轨（54..108）：全部 + 横向滑动的六路 + 箭头 + 全部工具 ─────────────
/// g_r31_rail 是像素偏移；这里把越界的值收回真上限，摆不下的 item 直接不画。
static float R31Sources(ImDrawList* dl, float x, float y, float w, int loc) {
    const auto tools = R31PresentTools();
    const float nx = x + 16.0f, nw = w - 32.0f, ny = y + k31HeadH;   // 16..400 / 54..108
    const float navH = 54.0f, iy = ny + 5.0f, ih = 44.0f;
    R31Glass(dl, nx, ny, nw, navH, 13.0f);
    // 「全部」：43×44 r9，竖排 icon17（y+6）+ 标签 9/600（y+25）
    const float ax = nx + 4.0f, allW = 43.0f;
    const bool allOn = (g_r31_src == "all");
    {
        const float hov = TxPeekNew(Zid(loc, 499, HIT_SRC, 0), 0.0f);
        if (allOn)
            dl->AddRectFilled(ImVec2(ax, iy), ImVec2(ax + allW, iy + ih), k31Sel, 9.0f);
        else if (hov > 0.01f)
            dl->AddRectFilled(ImVec2(ax, iy), ImVec2(ax + allW, iy + ih), k31Hover, 9.0f);
        if (allOn)
            dl->AddRectFilled(ImVec2(ax + (allW - 12.0f) * 0.5f, iy + ih - 5.0f),
                              ImVec2(ax + (allW + 12.0f) * 0.5f, iy + ih - 3.0f), k31Sec, 2.0f);
        I31(dl, I31_LAYERS, ax + (allW - 17.0f) * 0.5f, iy + 6.0f, 17.0f, allOn ? k31Fg : k31Sec);
        const std::string all = T("r_all", loc);
        const float tw = TextWidth(all, loc, T_S9B);
        DrawTextRun(dl, ax + (allW - tw) * 0.5f, TextTop(iy + 25.0f, 12.0f, T_S9B, loc), all, loc,
                    allOn ? k31Fg : k31Sec, T_S9B);
        AddHit(g_hits, ax, iy, ax + allW, iy + ih, HIT_SRC, 499, 0, loc, T("r_all_sessions", loc),
               allOn ? 1 : 0);
        g_hits.back().key = "all";
    }

    const float toolR = nx + nw - 4.0f;              // 396
    const float toolsX = toolR - 29.0f;              // 367..396
    const float nextX = toolsX - 1.0f - 26.0f;       // 340..366
    const float railX0 = ax + allW + 3.0f;           // 66
    const float railX1 = nextX;
    const float railW = railX1 - railX0;
    const int n = (int)tools.size();
    // item 宽 = 7 + 23 + 6 + 短名 + 7，下限 76；gap 3
    float totalW = 0.0f;
    std::vector<float> itemW((size_t)n);
    for (int i = 0; i < n; i++) {
        const float tw = TextWidth(R31Short(tools[(size_t)i]->id), loc, T_S105);
        itemW[(size_t)i] = std::fmax(76.0f, 7.0f + 23.0f + 6.0f + tw + 7.0f);
        totalW += itemW[(size_t)i];
    }
    if (n > 1) totalW += (float)(n - 1) * 3.0f;
    const float maxOff = std::fmax(0.0f, totalW - railW);
    if (g_r31_rail < 0) g_r31_rail = 0;
    if ((float)g_r31_rail > maxOff + 0.5f) g_r31_rail = (int)std::lround(maxOff);
    const float off = (float)g_r31_rail;

    const size_t h0 = g_hits.size();
    dl->PushClipRect(ImVec2(railX0, iy), ImVec2(railX1, iy + ih), true);
    float ix = railX0 - off;
    for (int i = 0; i < n; i++) {
        const Tool& t = *tools[(size_t)i];
        const float iw = itemW[(size_t)i];
        // 摆不下的整颗不画（稿子不放半颗溢出）
        if (ix >= railX0 - 0.5f && ix + iw <= railX1 + 0.5f) {
            const bool on = (g_r31_src == t.id);
            const float hov = TxPeekNew(Zid(loc, 500 + i, HIT_SRC, 0), 0.0f);
            if (on)
                dl->AddRectFilled(ImVec2(ix, iy), ImVec2(ix + iw, iy + ih), k31Sel, 9.0f);
            else if (hov > 0.01f)
                dl->AddRectFilled(ImVec2(ix, iy), ImVec2(ix + iw, iy + ih), k31Hover, 9.0f);
            if (on)
                dl->AddRectFilled(ImVec2(ix + (iw - 12.0f) * 0.5f, iy + ih - 5.0f),
                                  ImVec2(ix + (iw + 12.0f) * 0.5f, iy + ih - 3.0f), k31Sec, 2.0f);
            Brand31(dl, ix + 7.0f, iy + 10.5f, 23.0f, t.id, loc);
            const std::string label = R31Short(t.id);
            const float lx = ix + 7.0f + 23.0f + 6.0f;
            DrawTextRun(dl, lx, TextMid(iy + ih * 0.5f, T_S105, loc),
                        ClipToWidth(label, ix + iw - 7.0f - lx, loc, T_S105), loc,
                        on ? k31Fg : k31Sec, T_S105);
            AddHit(g_hits, ix, iy, ix + iw, iy + ih, HIT_SRC, 500 + i, 0, loc, R31Agent(t.id, loc),
                   on ? 1 : 0);
            g_hits.back().key = t.id;
        }
        ix += iw + 3.0f;
    }
    dl->PopClipRect();
    RCullHits(h0, railX0, iy, railX1, iy + ih);

    // 右端 12px 渐隐：取导轨那一行真会透出来的面板底色
    const bool atEnd = (off >= maxOff - 0.5f);
    if (!atEnd && totalW > railW + 1.0f) {
        const ImU32 bg = R31PanelAt(ny, y, kCanvasH);
        dl->AddRectFilledMultiColor(ImVec2(railX1 - 12.0f, iy), ImVec2(railX1, iy + ih),
                                    WithA(bg, 0), bg, bg, WithA(bg, 0));
    }
    if (totalW > railW + 1.0f) {
        const float bx = nextX + 13.0f, by = ny + navH * 0.5f;
        if (atEnd)
            dl->AddRectFilled(ImVec2(bx - 6.0f, by - 1.0f), ImVec2(bx + 6.0f, by + 1.0f), k31Sec, 1.0f);
        I31(dl, atEnd ? I31_LEFT : I31_RIGHT, bx - 6.5f, by - 6.5f, 13.0f, k31Muted);
        AddHit(g_hits, nextX, by - 17.0f, nextX + 26.0f, by + 17.0f, HIT_RAILNEXT, 502, 0, loc,
               T("r_scroll_more", loc), 0);
    }
    {
        const float hov = TxPeekNew(Zid(loc, 503, HIT_TOOLS31, 0), 0.0f);
        if (hov > 0.01f)
            dl->AddRectFilled(ImVec2(toolsX, iy), ImVec2(toolR, iy + ih), k31Hover, 7.0f);
        I31(dl, I31_GRID, toolsX + 7.0f, iy + ih * 0.5f - 7.5f, 15.0f, k31Sec);
        AddHit(g_hits, toolsX, iy, toolR, iy + ih, HIT_TOOLS31, 503, 0, loc,
               T("r_all_tools_list", loc), 0);
    }
    return ny + navH;
}

// ── 标签条（117..151）：会话 N / 用量与额度 ─────────────────────────────────
static void R31Tabs(ImDrawList* dl, float x, float y, float w, int loc) {
    const float L = x + 17.0f, R = x + w - 17.0f;
    RHair(dl, L, y + 33.0f, R, k31Line);
    const std::vector<const UIRow*> rows = R31Rows(g_r31_src, g_r31_query);
    float tx = L;
    {
        const bool on = (g_view == 0);
        const std::string t0 = T("r_sessions", loc);
        const float tw = TextWidth(t0, loc, T_S12);
        R31Mid(dl, tx, y + 17.0f, t0, loc, on ? k31Fg : k31Muted, on ? T_S12B : T_S12);
        if (on) dl->AddRectFilled(ImVec2(tx, y + 32.0f), ImVec2(tx + tw, y + 34.0f), k31Sec);
        char cb[24];
        snprintf(cb, sizeof cb, "%d", (int)rows.size());
        const float bw = TextWidth(cb, loc, T_S10B) + 8.0f;
        dl->AddRectFilled(ImVec2(tx + tw + 3.0f, y + 9.0f), ImVec2(tx + tw + 3.0f + bw, y + 25.0f),
                          k31Badge, 4.0f);
        DrawTextRun(dl, tx + tw + 7.0f, TextMid(y + 17.0f, T_S10B, loc), cb, loc, k31Muted, T_S10B);
        AddHit(g_hits, tx - 2.0f, y, tx + tw + 3.0f + bw + 2.0f, y + 34.0f, HIT_TAB31, 600, 0,
               loc, t0, on ? 1 : 0);
        tx += tw + 3.0f + bw + 18.0f;
    }
    {
        const bool on = (g_view == 6);
        const std::string t1 = T("r_usage_tab", loc);
        const float tw = TextWidth(t1, loc, T_S12);
        R31Mid(dl, tx, y + 17.0f, t1, loc, on ? k31Fg : k31Muted, on ? T_S12B : T_S12);
        if (on) dl->AddRectFilled(ImVec2(tx, y + 32.0f), ImVec2(tx + tw, y + 34.0f), k31Sec);
        AddHit(g_hits, tx - 2.0f, y, tx + tw + 2.0f, y + 34.0f, HIT_TAB31, 600, 6, loc, t1,
               on ? 1 : 0);
    }
}

// ── 空态：整块在 h 内垂直居中（稿子 justify-content:center）──────────────────
/// h 由调用方给：总览整屏空 420，其余屏与队列空态 180。actLabel 传 nullptr 就没有按钮。
static float R31Empty(ImDrawList* dl, float x, float y, float w, float h, const std::string& title,
                      const std::string& hint, const char* actLabel, int act, int loc) {
    const float cx = x + w * 0.5f;
    const auto hintLines = R31Wrap(hint, std::fmin(245.0f, w - 80.0f), loc, T_CHIP, 3);
    const float contentH = 33.0f + 15.0f + 24.0f + 10.0f +
                           (float)hintLines.size() * 19.25f + 16.0f + (actLabel ? 29.0f : 0.0f);
    float ty = y + std::fmax(0.0f, (h - contentH) * 0.5f);
    I31Stroke(dl, I31_TERM, cx - 16.5f, ty, 33.0f, k31Muted, 1.15f);
    ty += 33.0f + 15.0f;
    {
        const float tw = TextWidth(title, loc, T_S16B);
        DrawTextRun(dl, cx - tw * 0.5f, TextMid(ty + 12.0f, T_S16B, loc), title, loc, k31Fg,
                    T_S16B, -0.3f);
        ty += 24.0f + 10.0f;
    }
    for (const auto& ln : hintLines) {
        const float lw = TextWidth(ln, loc, T_CHIP);
        DrawTextRun(dl, cx - lw * 0.5f, TextTop(ty, 19.25f, T_CHIP, loc), ln, loc, k31Sec, T_CHIP);
        ty += 19.25f;
    }
    ty += 16.0f;
    if (actLabel) {
        const std::string al = actLabel;
        const float aw = TextWidth(al, loc, T_CHIP) + 30.0f;
        dl->AddRect(ImVec2(cx - aw * 0.5f + 0.5f, ty + 0.5f), ImVec2(cx + aw * 0.5f - 0.5f, ty + 28.5f),
                    k31Line, 8.0f, 0, 1.0f);
        DrawTextRun(dl, cx - TextWidth(al, loc, T_CHIP) * 0.5f, TextMid(ty + 14.5f, T_CHIP, loc),
                    al, loc, k31Fg, T_CHIP);
        AddHit(g_hits, cx - aw * 0.5f, ty, cx + aw * 0.5f, ty + 29.0f, act, 1201, 0, loc, al, 0);
    }
    return y + h;
}

// ── 总览的一行（等你 / 授权 / 失败）─────────────────────────────────────────
static std::string R31RowName(const UIRow& r) {
    // 标题优先：zcode 的 project 恒为工作区目录名（同一目录十几条全叫「_项目代码」），
    // 分不出谁是谁；标题才是 App 里那条会话的名字（【示例工程】优化升级 这类）。
    // 没有标题的（老采集器只给目录）退回项目名。
    if (!r.title.empty()) return r.title;
    if (!r.project.empty()) return r.project;
    return r.agent;
}
/// 行内 client 标签：mono 9（就近档 T_MICRO）、实测框高 16（line-height 14 + 上下边框）。
static float R31RowTag(ImDrawList* dl, float x, float cy, const std::string& id, int loc) {
    const std::string s = R31Client(id);
    const float tw = TextWidth(s, loc, T_MICRO, 0.3f);
    const float w = tw + 6.0f + 2.0f, h = 16.0f;
    dl->AddRect(ImVec2(x + 0.5f, cy - h * 0.5f + 0.5f), ImVec2(x + w - 0.5f, cy + h * 0.5f - 0.5f),
                k31Line, 3.0f, 0, 1.0f);
    DrawTextRun(dl, x + 4.0f, TextMid(cy, T_MICRO, loc), s, loc, k31Muted, T_MICRO, 0.3f);
    return w;
}
/// 子对话的显示标签：库里 title 存的是派单 prompt 原文（「你在 /Users/…（pnpm
/// monorepo）…」这类），整段贴不上界面。剔掉路径（含跟着的全角括号注记）与开场白，
/// 取剩下的第一截。纯字符串规则；空串由调用方兜底。跳转用的检索文本仍是原文。
static std::string R31ChildLabel(const std::string& raw) {
    std::string t = raw;
    const size_t nl = t.find_first_of("\r\n");
    if (nl != std::string::npos) t = t.substr(0, nl);
    for (const char* sep : {"。", "；", "！", "？"}) {
        const size_t q = t.find(sep);
        if (q != std::string::npos && q >= 8) { t = t.substr(0, q); break; }
    }
    size_t i = 0;
    while (i < t.size()) {
        const bool at_tok = (i == 0 || t[i - 1] == ' ');
        const bool path_start =
            t[i] == '/' || (t[i] == '~' && i + 1 < t.size() && t[i + 1] == '/');
        if (!at_tok || !path_start) { i++; continue; }
        // 路径到空格为止；若中途全角括号没闭合（「（pnpm monorepo）」），连括号一起吞到闭合
        size_t end = i;
        for (;;) {
            end = t.find(' ', end);
            if (end == std::string::npos) { end = t.size(); break; }
            int bal = 0;
            for (size_t k = i; k < end;) {
                if (t.compare(k, 6, "\xEF\xBC\x88") == 0) { bal++; k += 3; }
                else if (t.compare(k, 6, "\xEF\xBC\x89") == 0) { bal--; k += 3; }
                else k++;
            }
            if (bal <= 0) break;
            end++;
        }
        t.erase(i, end - i);
        while (i < t.size() && t[i] == ' ') t.erase(i, 1);
    }
    for (const char* pre : {"你在 ", "你在", "你是", "在 ", "帮我 ", "请 "})
        if (t.rfind(pre, 0) == 0) { t = t.substr(strlen(pre)); break; }
    while (!t.empty() && (t.back() == ' ' || t.back() == ',' || t.back() == ','))
        t.pop_back();
    return t;
}
/// 「⌄ N 条子对话」展开芯片：点它在 g_r31_subopen 里进出，跨屏保留。返回占宽。
static float R31SubChip(ImDrawList* dl, float x, float cy, const UIRow& r, int rowbase, int loc) {
    const std::string lab = RFmt(T("r_children", loc), (int)r.children.size());
    const bool open = g_r31_subopen.count(r.key) > 0;
    const float tw = TextWidth(lab, loc, T_S9);
    I31(dl, open ? I31_DOWN : I31_RIGHT, x + 2.0f, cy - 5.0f, 10.0f, k31Muted);
    DrawTextRun(dl, x + 16.0f, TextMid(cy, T_S9, loc), lab, loc, k31Muted, T_S9);
    AddHit(g_hits, x - 4.0f, cy - 11.0f, x + 16.0f + tw + 4.0f, cy + 11.0f, HIT_SUBTOG, rowbase,
           0, loc, lab, open ? 1 : 0);
    g_hits.back().key = r.key;
    return 20.0f + tw;
}
/// 展开后的子对话列表：缩进玻璃卡，一行一个子代理（状态点 + 标题 + 时刻），点行即跳。
static float R31SubList(ImDrawList* dl, float x, float y, float w, const UIRow& r, int rowbase,
                        int loc) {
    const float h = 6.0f + (float)r.children.size() * 24.0f + 6.0f;
    R31Glass(dl, x, y, w, h, 9.0f);
    for (size_t i = 0; i < r.children.size(); i++) {
        const auto& c = r.children[i];
        const float cy = y + 6.0f + (float)i * 24.0f + 12.0f;
        dl->AddCircleFilled(ImVec2(x + 13.0f, cy), 2.5f, c.running ? k31Green : WithA(k31Muted, 120));
        int hh = 0, mm = 0;
        RTimeOfDay(c.updated, &hh, &mm);
        char tb[8];
        snprintf(tb, sizeof tb, "%02d:%02d", hh, mm);
        const float tw = TextWidth(tb, loc, T_S9);
        std::string lab = R31ChildLabel(c.title);
        if (lab.empty()) lab = "(子任务)";
        DrawTextRun(dl, x + 22.0f, TextMid(cy, T_CHIP, loc),
                    ClipToWidth(lab, w - 22.0f - tw - 14.0f - 8.0f, loc, T_CHIP), loc,
                    c.running ? k31Sec : k31Muted, T_CHIP);
        DrawTextRight(dl, x + w - 12.0f, TextMid(cy, T_S9, loc), tb, loc, k31Muted, T_S9);
        AddHit(g_hits, x + 6.0f, y + 6.0f + (float)i * 24.0f, x + w - 6.0f,
               y + 6.0f + (float)(i + 1) * 24.0f, HIT_OPEN, rowbase + (int)i, 0, loc, c.title, 0);
        Hit& h2 = g_hits.back();
        h2.key = r.agent_id + ":" + c.id;
        h2.agent_id = r.agent_id;
        h2.native_id = c.id;
        h2.title = c.title;
    }
    return y + h;
}
/// 一整行：状态色轨 / 品牌 / 工具·客户端·状态 / 项目·等多久 / 请求原文 / 用量 / 等待条 / ⓘ / 跳转。
/// 行高 106（.question 单行钳制，app.css 11.5/17）。
static float R31WaitRow(ImDrawList* dl, float x, float y, float w, const UIRow& r, int idx,
                        double maxAge, int loc) {
    const float h = 106.0f;
    const long zid = Zid(loc, 900 + idx, HIT_OPEN, 0);
    const float hov = TxPeekNew(zid, 0.0f);
    R31Glass(dl, x, y, w, h, 12.0f, hov, hov > 0.01f ? k31Rim : k31Line);
    const int kind = R31KindOf(r);
    // 完成未看的行用蓝色（与页脚那颗「有没看过的」小点同色），别和灰色完成行混在一起。
    const ImU32 kc = R31UnseenDone(r) ? k31Blue : R31KindColor(kind);
    dl->AddRectFilled(ImVec2(x, y + 14.0f), ImVec2(x + 2.0f, y + h - 14.0f), WithA(kc, 199), 2.0f);
    // 整行命中区先注册：芯片/ⓘ 这类行内小按钮后注册，命中判定从后往前扫才能优先命中小件
    AddHit(g_hits, x, y, x + w, y + h, HIT_OPEN, 900 + idx, 0, r, loc, R31RowName(r));

    Brand31(dl, x + 11.0f, y + 12.0f, 23.0f, r.agent_id, loc);
    const float mx = x + 44.0f;                    // 主列：11 + 25 + 8
    const float mainR = x + w - 33.0f;             // 1fr 右缘：w=382 时 366（= 44 + 305）
    const float rightX = x + w - 25.0f;            // 右列 14 宽的起点（w=382 时 374）
    // row-top：y+10..+26
    const float topCy = y + 18.0f;
    float cx = R31AgentName(dl, mx, topCy, r.agent_id, loc);
    cx += 5.0f;
    cx += R31RowTag(dl, cx, topCy, r.agent_id, loc) + 5.0f;
    R31StatusRight(dl, mainR - 2.0f, topCy, r, loc);
    // row-title：y+30..+46（有子对话时，项目名给「⌄ N 条子对话」芯片让位）
    {
        const std::string name = R31RowName(r);
        const std::string age = Dur(r.age, loc);
        const float ageW = TextWidth(age, loc, T_S11);
        float reserve = 0.0f;
        if (!r.children.empty()) {       // 芯片右对齐到时长左侧：先量宽再定起点，不然压到「49m」上
            const float cw = 20.0f + TextWidth(RFmt(T("r_children", loc), (int)r.children.size()),
                                               loc, T_S9);
            R31SubChip(dl, mainR - ageW - 8.0f - cw, y + 38.0f, r, 1850 + idx, loc);
            reserve = cw + 8.0f;
        }
        DrawTextRun(dl, mx, TextMid(y + 38.0f, T_S13B, loc),
                    ClipToWidth(name, mainR - mx - ageW - 8.0f - reserve, loc, T_S13B), loc, k31Fg,
                    T_S13B, -0.15f);
        DrawTextRight(dl, mainR, TextMid(y + 38.0f, T_S11, loc), age, loc, k31Muted, T_S11);
    }
    // question：y+50..+67，单行省略
    if (!r.question.empty())
        DrawTextRun(dl, mx, TextTop(y + 50.0f, 17.0f, T_S115, loc),
                    ClipToWidth(r.question, mainR - mx, loc, T_S115), loc, k31Sec, T_S115);
    // row-usage：y+71..+85。稿子 r.tokens!=null 才有这一行 —— 没有 token 明细整行不画。
    if (r.has_tokens) {
        float ux = R31Mid(dl, mx, y + 78.0f, R31Compact((double)r.tokens) + " tokens", loc,
                          k31Muted, T_S9);
        if (r.has_credits) {
            dl->AddRectFilled(ImVec2(ux + 3.0f, y + 74.0f), ImVec2(ux + 4.0f, y + 82.0f), k31Line);
            R31Mid(dl, ux + 7.0f, y + 78.0f, R31Num(r.credits) + " Credits", loc, k31Muted, T_S9);
        }
    }
    // wait-meter：y+91..+93
    {
        dl->AddRectFilled(ImVec2(mx, y + 91.0f), ImVec2(mainR, y + 93.0f), k31Line, 1.0f);
        if (maxAge > 1.0) {
            float p = (float)(r.age / maxAge);
            if (p < 0.03f) p = 0.03f;
            if (p > 1.0f) p = 1.0f;
            dl->AddRectFilled(ImVec2(mx, y + 91.0f), ImVec2(mx + (mainR - mx) * p, y + 93.0f),
                              WithA(kc, 179), 1.0f);
        }
    }
    I31(dl, I31_LAUNCH, rightX + 1.0f, y + 45.5f, 12.0f, k31Faint);
    {
        const float ibx = x + w - 30.0f, iby = y + 4.0f;    // ⓘ 25×25，右缘距行 5
        const float ihov = TxPeekNew(Zid(loc, 900 + idx, HIT_INFO31, 0), 0.0f);
        if (ihov > 0.01f)
            dl->AddRectFilled(ImVec2(ibx, iby), ImVec2(ibx + 25.0f, iby + 25.0f), k31Sel, 7.0f);
        I31(dl, I31_INFO, ibx + 6.0f, iby + 6.0f, 13.0f, ihov > 0.5f ? k31Fg : k31Faint);
        AddHit(g_hits, ibx, iby, ibx + 25.0f, iby + 25.0f, HIT_INFO31, 900 + idx, 0, r, loc,
               T("r_details", loc));
    }
    return y + h;
}

// ── 在跑卡 ──────────────────────────────────────────────────────────────────
static float R31RunCard(ImDrawList* dl, float x, float y, float w, const UIRow& r, int idx,
                        int maxTurns, int loc, double tn) {
    const float inL = x + 13.0f;      // 29
    const float inR = x + w - 13.0f;  // 387
    const float bodyL = x + 46.0f;    // 59 = 29 + 30
    const int kind = R31KindOf(r);
    const ImU32 kc = R31KindColor(kind);
    const bool running = (kind == 2);
    // 高度先合成再画底，两遍走同一组偏移（演示值 130 = 10+24+36+4+7+15+8+15+11）
    int explainLines = 0;
    if (g_r31_explain)
        explainLines = std::fmax(1, (int)R31Wrap(T("r_turn_explain", loc), inR - bodyL, loc, T_S11, 3).size());
    const bool hasUsage = r.has_tokens || r.has_credits;
    const bool beyond = (maxTurns > 0 && r.turns > maxTurns);
    float h = 10.0f + 24.0f + 36.0f + (maxTurns > 0 ? 4.0f : 0.0f) + 7.0f + 15.0f;
    if (!r.children.empty()) h += 4.0f + 15.0f;      // 「⌄ N 条子对话」芯片那一行
    if (hasUsage) h += 8.0f + 15.0f;
    if (g_r31_explain) h += 8.0f + (float)explainLines * 17.6f;
    if (beyond) h += 8.0f + 17.6f;
    h += 11.0f;
    R31Glass(dl, x, y, w, h, 14.0f);
    dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f),
                IM_COL32(141, 227, 191, 56), 14.0f, 0, 1.0f);
    // 整卡命中先注册，芯片（⌄ N 条子对话）后注册、优先命中
    AddHit(g_hits, inL, y, inR, y + h - 8.0f, HIT_OPEN, 700 + idx, 0, r, loc, R31RowName(r));
    // identity：y+10..+34（品牌 24 / 名 12/600 / tag / 刚刚 / ⓘ 22×22 在 x370）
    Brand31(dl, inL, y + 10.0f, 24.0f, r.agent_id, loc);
    float nx = R31AgentName(dl, inL + 24.0f + 6.0f, y + 22.0f, r.agent_id, loc);
    nx += 5.0f;
    nx += R31ClientTag(dl, nx, y + 22.0f, r.agent_id, loc) + 6.0f;
    {
        const float ibx = x + 354.0f;   // 370..392
        DrawTextRight(dl, ibx - 6.0f, TextMid(y + 22.0f, T_S10, loc), T("just_now", loc), loc,
                      k31Muted, T_S10);
        const float ihov = TxPeekNew(Zid(loc, 700 + idx, HIT_INFO31, 0), 0.0f);
        if (ihov > 0.01f)
            dl->AddRectFilled(ImVec2(ibx, y + 11.0f), ImVec2(ibx + 22.0f, y + 33.0f), k31Sel, 6.0f);
        I31(dl, I31_INFO, ibx + 4.5f, y + 16.5f, 13.0f, ihov > 0.5f ? k31Fg : k31Faint);
        AddHit(g_hits, ibx, y + 11.0f, ibx + 22.0f, y + 33.0f, HIT_INFO31, 700 + idx, 0, r, loc,
               T("r_details", loc));
    }
    // run-main：y+34..+70。右端＝[环 16] 状态字 + launch 12，右缘 387。
    {
        const std::string proj = R31RowName(r);
        const std::string state = T(R31KindWord(kind), loc);
        const float launchX = inR - 12.0f;
        float sx = launchX - 6.0f - TextWidth(state, loc, T_S10);
        if (running) {
            R31Ring(dl, sx - 6.0f - 16.0f, y + 43.0f, tn);
        } else {
            I31(dl, R31KindIcon(kind), sx - 6.0f - 11.0f, y + 45.5f, 11.0f, kc);
        }
        DrawTextRun(dl, sx, TextMid(y + 51.0f, T_S10, loc), state, loc, kc, T_S10);
        I31(dl, I31_LAUNCH, launchX, y + 45.0f, 12.0f, kc);
        DrawTextRun(dl, bodyL, TextMid(y + 51.0f, T_S15B, loc),
                    ClipToWidth(proj, sx - 12.0f - bodyL, loc, T_S15B), loc, k31Fg, T_S15B, -0.3f);
    }
    // rprog：y+70..+74（x59..387）
    if (maxTurns > 0) {
        const float pw = inR - bodyL;
        dl->AddRectFilled(ImVec2(bodyL, y + 70.0f), ImVec2(inR, y + 74.0f),
                          IM_COL32(140, 162, 184, 26), 4.0f);
        dl->AddRect(ImVec2(bodyL + 0.25f, y + 70.25f), ImVec2(inR - 0.25f, y + 73.75f), k31Line,
                    4.0f, 0, 0.5f);
        float p = (float)r.turns / (float)maxTurns;
        if (p > 1.0f) p = 1.0f;
        // 可见下限：刚起跑的任务（3/331 轮 ≈ 0.9%）填充只有 3pt、流动条也被裁没了，
        // 整条看起来是空的。与 wait-meter 的 3% 下限同一精神：比例失真半格，换来「在跑」读得出。
        if (p > 0.0f && p < 0.03f) p = 0.03f;
        if (p > 0.0f) {
            dl->PushClipRect(ImVec2(bodyL, y + 70.0f), ImVec2(bodyL + pw * p, y + 74.0f), true);
            dl->AddRectFilled(ImVec2(bodyL, y + 70.0f), ImVec2(inR, y + 74.0f),
                              WithA(k31Green, 115), 4.0f);
            R31March(dl, bodyL, y + 70.0f, pw, 4.0f, tn);
            dl->PopClipRect();
        }
    }
    // run-labels：y+(81)。左 第 N 轮；右「今日最长 M 轮」按钮（整段文字下 1pt 点线）
    {
        const float ly = y + 70.0f + (maxTurns > 0 ? 4.0f : 0.0f) + 7.0f;
        const float cy = ly + 7.5f;
        R31FmtN(dl, bodyL, TextMid(cy, T_S10, loc), T("r_turn", loc), (long)r.turns, loc, T_S10,
                k31Muted, k31Sec, T_S10B);
        if (maxTurns > 0) {
            const std::string lab = RFmt(T("r_longest", loc), maxTurns);
            const float lw = TextWidth(lab, loc, T_S10);
            const float bx = inR - lw;
            DrawTextRun(dl, bx, TextMid(cy, T_S10, loc), lab, loc, k31Muted, T_S10);
            dl->AddLine(ImVec2(bx, ly + 15.0f), ImVec2(inR, ly + 15.0f), WithA(k31Faint, 140), 1.0f);
            AddHit(g_hits, bx - 3.0f, ly - 2.0f, inR + 3.0f, ly + 17.0f, HIT_EXPLAIN, 701, 0, loc,
                   lab, g_r31_explain ? 1 : 0);
        }
    }
    // 「⌄ N 条子对话」芯片行：紧接 run-labels
    if (!r.children.empty())
        R31SubChip(dl, bodyL, y + 70.0f + (maxTurns > 0 ? 4.0f : 0.0f) + 7.0f + 15.0f + 11.5f, r,
                   1840 + idx, loc);
    // run-usage：y+(104..119)
    float cy = y + 70.0f + (maxTurns > 0 ? 4.0f : 0.0f) + 7.0f + 15.0f +
               (r.children.empty() ? 0.0f : 19.0f);
    if (hasUsage) {
        cy += 8.0f;
        const float ucy = cy + 7.5f;
        float ux = bodyL;
        if (r.has_tokens)
            ux = R31Mid(dl, ux, ucy, R31Compact((double)r.tokens) + " tokens", loc, k31Sec, T_S10);
        if (r.has_credits) {
            if (r.has_tokens) ux += 8.0f;
            R31Mid(dl, ux, ucy, R31Num(r.credits) + " Credits", loc, k31Sec, T_S10);
        }
        DrawTextRight(dl, inR, TextMid(ucy, T_S9, loc), T("r_session_total", loc), loc, k31Muted, T_S9);
        cy += 15.0f;
    }
    if (g_r31_explain) {
        cy += 8.0f;
        cy = R31DrawWrap(dl, T("r_turn_explain", loc), bodyL, cy, inR - bodyL, loc, T_S11, 17.6f,
                         k31Sec, 3);
    }
    if (beyond) {
        cy += 8.0f;
        cy = R31DrawWrap(dl, T("r_beyond", loc), bodyL, cy, inR - bodyL, loc, T_S11, 17.6f, k31Sec, 1);
    }
    return y + h;
}

// ── 账户额度预览（总览正文里）────────────────────────────────────────────────
/// 单路：一行 44「今日用量 · X tokens · Y Credits · 明细 →」。tokens 列仅当 total>0。
static float R31PeekSingle(ImDrawList* dl, float x, float y, float w, const Tool& t, int loc) {
    const float h = 44.0f;
    R31Glass(dl, x, y, w, h, 12.0f);
    const float cy = y + h * 0.5f;
    float cx = x + 12.0f;
    I31(dl, I31_CHART, cx, cy - 6.0f, 12.0f, k31Muted);
    cx += 12.0f + 5.0f;
    cx = R31Mid(dl, cx, cy, T("r_today_usage", loc), loc, k31Muted, T_S10) + 8.0f;
    if (t.usage.total() > 0) {
        cx = R31Mid(dl, cx, cy, R31Compact((double)t.usage.total()), loc, k31Fg, T_S14B);
        cx = R31Mid(dl, cx + 3.0f, cy, "tokens", loc, k31Muted, T_S9) + 8.0f;
    }
    if (t.has_credits)
        R31Mid(dl, cx, cy, R31Num(t.credits) + " Credits", loc, k31Sec, T_S10);
    {
        const std::string det = T("r_details", loc);
        const float dw = TextWidth(det, loc, T_S10);
        const float rx = x + w - 12.0f;
        DrawTextRun(dl, rx - 12.0f - 6.0f - dw, TextMid(cy, T_S10, loc), det, loc, k31Muted, T_S10);
        I31(dl, I31_RIGHT, rx - 12.0f, cy - 6.0f, 12.0f, k31Faint);
        AddHit(g_hits, x, y, x + w, y + h, HIT_ACCOUNT31, 1100, 0, loc, t.name, 0);
        g_hits.back().key = t.id;
    }
    return y + h;
}
/// 全部：标题条 32 + 两列 33 高的格（pad 0 10 10，列间 1pt 竖线）。
static float R31PeekAll(ImDrawList* dl, float x, float y, float w, int loc, double now) {
    std::vector<const Tool*> qs;
    for (const Tool& t : g_pd.tools)
        // 只摆读数新鲜的：额度是 Codex 这类工具「跑的时候」才写的，闲置几小时后必陈旧，
        // 常驻一颗红「待更新」在首页只是噪音。陈旧的退到用量屏，那里有上次读数的完整口径。
        if (t.present() && R31QuotaAny(t) &&
            !(R31QuotaStale(t.quota.five, now) || R31QuotaStale(t.quota.week, now)))
            qs.push_back(&t);
    if (qs.empty()) return 0.0f;
    const float headH = 32.0f;    // geom 实测 32（CSS 声明 30，冲突以实测为准）
    const int rows = (int)((qs.size() + 1) / 2);
    const float h = headH + (float)rows * 33.0f + (float)(rows - 1) * 10.0f + 10.0f;
    R31Glass(dl, x, y, w, h, 12.0f);
    {
        const float hy = y + headH * 0.5f;
        float hx = x + 10.0f;
        I31(dl, I31_CHART, hx, hy - 6.0f, 12.0f, k31Muted);
        hx += 12.0f + 5.0f;
        R31Mid(dl, hx, hy, T("r_account_limits", loc), loc, k31Muted, T_S9);
        double sum = g_pd.has_today_tokens ? (double)g_pd.today_tokens : 0.0;
        if (!g_pd.has_today_tokens)
            for (const Tool& t : g_pd.tools)
                if (t.present()) sum += (double)t.usage.total();
        const std::string right = std::string(T("r_today", loc)) + " " + R31Compact(sum) + " tokens";
        const float rw = TextWidth(right, loc, T_S9);
        const float rx = x + w - 10.0f - 12.0f;
        DrawTextRun(dl, rx - 6.0f - rw, TextMid(hy, T_S9, loc), right, loc, k31Muted, T_S9);
        I31(dl, I31_RIGHT, rx, hy - 6.0f, 12.0f, k31Faint);
        AddHit(g_hits, x, y, x + w, y + headH, HIT_TAB31, 1102, 6, loc,
               T("r_account_limits", loc), 0);
    }
    const float gx0 = x + 10.0f;
    const float half = (w - 20.0f - 10.0f) / 2.0f;      // 177
    dl->AddRectFilled(ImVec2(gx0 + half + 4.5f, y + headH), ImVec2(gx0 + half + 5.5f, y + h - 10.0f),
                      k31Line);
    for (size_t i = 0; i < qs.size(); i++) {
        const Tool& t = *qs[i];
        const int col = (int)i % 2, row = (int)i / 2;
        const float cxx = gx0 + (float)col * (half + 10.0f);
        const float cyy = y + headH + (float)row * (33.0f + 10.0f);
        Brand31(dl, cxx, cyy + 5.5f, 22.0f, t.id, loc);
        const float tx = cxx + 22.0f + 6.0f;
        R31Mid(dl, tx, cyy + 11.0f, R31Short(t.id), loc, k31Fg, T_S11B);
        R31Mid(dl, tx, cyy + 24.0f, T("r_5h", loc), loc, k31Muted, T_S9);
        const ap::QuotaWindow& q = t.quota.five.known ? t.quota.five : t.quota.week;
        const bool stale = R31QuotaStale(q, now);
        const std::string val = stale ? T("r_stale", loc) : std::to_string(R31RemainPct(q)) + "%";
        const int vtier = stale ? T_S10 : T_N23;
        const float vw = TextWidth(val, loc, vtier);
        DrawTextRun(dl, cxx + half - 2.0f - vw, TextMid(cyy + 16.5f, vtier, loc), val, loc,
                    stale ? k31Red : k31Fg, vtier, -1.0f);
        AddHit(g_hits, cxx - 2.0f, cyy, cxx + half + 2.0f, cyy + 33.0f, HIT_ACCOUNT31,
               1103 + (int)i, 0, loc, t.name, 0);
        g_hits.back().key = t.id;
    }
    return y + h;
}

// ── 总览页脚（793..850）：今日完成 + 一日时间轴 ─────────────────────────────
static float R31FootRoot(ImDrawList* dl, float x, float y, float w, int loc) {
    const float h = 57.0f;
    RHair(dl, x, y, x + w, k31Line);
    const std::vector<const UIRow*> all = R31Rows(g_r31_src, g_r31_query);
    int doneN = 0;
    for (const auto* r : all) if (GroupOf(r->state) == G_DONE) doneN++;
    float linkEnd = x + 125.0f;    // done-link 右缘（稿子 125；长语会更右，时间轴跟着让）
    {
        const float lx = x + 18.0f, cy = y + 29.0f;      // done-link 810..834
        I31(dl, I31_CHECK, lx, cy - 7.5f, 15.0f, k31Sec);
        int unseenN = 0;
        for (const auto* r : all) if (r->unseen) unseenN++;
        if (unseenN > 0)
            dl->AddCircleFilled(ImVec2(lx + 13.5f, cy - 6.0f), 3.0f, k31Blue);
        float tx = R31Mid(dl, lx + 22.0f, cy, T("r_done_today", loc), loc, k31Sec, T_S11);
        tx = R31Mid(dl, tx + 4.0f, cy, std::to_string(doneN), loc, k31Fg, T_S12B);
        // 箭头在稿子里钉在 x110；长语（de「Heute erledigt」）会把数字顶过 105，
        // 那时跟着数字走，不叠字。命中区照着实际内容收口。
        const float ax = std::fmax(x + 110.0f, tx + 5.0f);
        I31(dl, I31_RIGHT, ax, cy - 7.5f, 15.0f, k31Sec);
        linkEnd = std::fmax(x + 125.0f, ax + 15.0f);
        AddHit(g_hits, lx - 4.0f, y + 17.0f, linkEnd + 2.0f, y + 41.0f,
               HIT_HISTORY31, 1200, 0, loc,
               std::string(T("r_done_today", loc)) + " " + std::to_string(doneN), 0);
    }
    // 右端 Credits（Qoder 一路自记计数，>0 才画）；在就把时间轴右界让出来
    float creditsW = 0.0f;
    if (g_pd.credits > 0.0) {
        const std::string cr = R31Num(g_pd.credits) + " Credits";
        creditsW = TextWidth(cr, loc, T_S10);
        DrawTextRun(dl, x + w - 18.0f - creditsW, TextMid(y + 41.0f, T_S10, loc), cr, loc,
                    k31Muted, T_S10);
    }
    const float dl0 = std::fmax(x + 133.0f, linkEnd + 8.0f);
    const float dl1 = creditsW > 0.0f ? x + w - 18.0f - creditsW - 12.0f : x + 364.0f;
    if (dl1 - dl0 >= 60.0f && !all.empty()) {
        const float trackTop = y + 13.0f, trackH = 20.0f;    // 806..826
        double end = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
        double start = end;
        for (const auto* r : all) {
            const double t = r->at > 0.0 ? r->at : end - r->age;
            if (t < start) start = t;
        }
        if (end - start < 60.0) start = end - 3600.0;
        const float span = (float)(end - start);
        RHair(dl, dl0, trackTop + trackH - 1.0f, dl1, k31Line);
        for (const auto* r : all) {
            const double t = r->at > 0.0 ? r->at : end - r->age;
            float p = (float)((t - start) / (span > 1.0 ? span : 1.0));
            if (p < 0.005f) p = 0.005f;
            if (p > 0.995f) p = 0.995f;
            const int kind = R31KindOf(*r);
            float mh = 7.0f;                      // 完成 / 排队
            ImU32 mc = k31Faint;
            if (kind == 0) { mh = 13.0f; mc = k31Blue; }
            else if (kind == 1 || kind == 4) { mh = 17.0f; mc = k31Red; }
            else if (kind == 2) { mh = 19.0f; mc = k31Green; }
            const float mx = dl0 + p * (dl1 - dl0) - 1.0f;
            dl->AddRectFilled(ImVec2(mx, trackTop + trackH - mh), ImVec2(mx + 2.0f, trackTop + trackH),
                              mc, 1.0f);
        }
        int hh = 0, mm = 0;
        RTimeOfDay(start, &hh, &mm);
        char tb[8];
        snprintf(tb, sizeof tb, "%02d:%02d", hh, mm);
        RTimeOfDay(end, &hh, &mm);
        char te[8];
        snprintf(te, sizeof te, "%02d:%02d", hh, mm);
        DrawTextRun(dl, dl0, y + 36.0f, tb, loc, k31Muted, T_S8);
        const float ew = TextWidth(te, loc, T_S8);
        DrawTextRun(dl, dl1 - ew, y + 36.0f, te, loc, k31Muted, T_S8);
    }
    return y + h;
}

/// 会话的「初见序号」：第一次被面板看到时发一个递增号，之后只查不发。
/// 运行卡按它排（新任务从顶部插入一次），不再按「最后动静」排——3 秒一轮的重扫里
/// 谁有输出谁跳前排，卡的位置就一直在漂，双击的时候点错行（2026-10-28 产品反馈）。
static long R31SeenSeq(const std::string& key) {
    static std::map<std::string, long> seen;
    static long counter = 0;
    auto it = seen.find(key);
    if (it != seen.end()) return it->second;
    if (seen.size() > 8192) seen.clear();     // 天花板：清一次只乱一回，几年也遇不上
    const long v = ++counter;
    seen[key] = v;
    return v;
}

// ── 总览正文：固定段（搜索框 → 运行卡 → 额度预览 → 「等你」标题）────────────
/// 返回可滚动队列区的顶 y。
static float R31OverviewFixed(ImDrawList* dl, float x, float y, float w, int loc, double tn) {
    const std::vector<const UIRow*> all = R31Rows(g_r31_src, g_r31_query);
    std::vector<const UIRow*> runs;
    for (const auto* r : all) {
        const int g = GroupOf(r->state);
        if (g == G_RUN || g == G_QUEUE) runs.push_back(r);
    }
    const std::vector<const UIRow*> waits = R31Waits();
    std::stable_sort(runs.begin(), runs.end(), [](const UIRow* a, const UIRow* b) {
        return R31SeenSeq(a->key) > R31SeenSeq(b->key);   // 新在上、老不动
    });
    float cy = y + 162.0f;
    // 搜索框（仅展开时）
    if (g_r31_search) {
        const float sx = x + 16.0f, sw = w - 32.0f, sh = 36.0f;
        R31Glass(dl, sx, cy, sw, sh, 10.0f);
        I31(dl, I31_SEARCH, sx + 10.0f, cy + 10.0f, 16.0f, k31Muted);
        const bool focused = (g_r31_focus == 1);
        const float tw = sx + sw - 10.0f - (g_r31_query.empty() ? 0.0f : 26.0f) - (sx + 31.0f);
        if (g_r31_query.empty()) {
            DrawTextRun(dl, sx + 31.0f, TextMid(cy + 18.0f, T_S12, loc), T("r_search_ph", loc), loc,
                        k31Faint, T_S12);
            // 空串也要画光标：不画的话点进来和没点一样，看不出这颗框已经在收字。
            if (focused && std::fmod(tn, 1.06) < 0.53)
                dl->AddRectFilled(ImVec2(sx + 31.0f, cy + 8.0f), ImVec2(sx + 32.0f, cy + 28.0f),
                                  k31Fg);
        } else {
            const std::string shown = R31TailFit(g_r31_query, tw, loc, T_S12);
            const float drawn = DrawTextRun(dl, sx + 31.0f, TextMid(cy + 18.0f, T_S12, loc), shown,
                                            loc, k31Fg, T_S12);
            if (focused && std::fmod(tn, 1.06) < 0.53)
                dl->AddRectFilled(ImVec2(sx + 31.0f + drawn + 1.0f, cy + 8.0f),
                                  ImVec2(sx + 31.0f + drawn + 2.0f, cy + 28.0f), k31Fg);
        }
        // 整条框先登记、✕ 后登记：命中是倒序扫的，晚登记的先问着——否则整条框会把 ✕ 盖死。
        AddHit(g_hits, sx, cy, sx + sw, cy + sh, HIT_SEARCHFIELD, 1301, 1, loc, T("r_search", loc), 0);
        if (!g_r31_query.empty()) {
            const float bx = sx + sw - 26.0f;
            I31(dl, I31_CLOSE, bx + 5.0f, cy + 12.5f, 11.0f, k31Faint);
            AddHit(g_hits, bx - 4.0f, cy + 6.0f, bx + 24.0f, cy + 30.0f, HIT_CLEARQ, 1300, 0, loc,
                   T("r_clear", loc), 0);
        }
        cy += sh + 12.0f;
    }
    if (all.empty()) {
        // 有搜索词却一条都不剩，不能说「目前没有会话」——那是「从没用过工具」的文案，
        // 会让人以为数据丢了。给「没有匹配」＋一键回到全部（2026-10-29 实测：搜「赚钱」后这么写的）。
        if (!g_r31_query.empty())
            return R31Empty(dl, x + 16.0f, cy, w - 32.0f, 420.0f, T("r_search_none", loc),
                            T("r_search_none_hint", loc), T("r_show_all", loc), HIT_SHOWALL31,
                            loc);
        R31Empty(dl, x + 16.0f, cy, w - 32.0f, 420.0f, T("r_no_sessions", loc),
                 T("r_no_sessions_hint", loc), nullptr, 0, loc);
        return cy + 420.0f;
    }
    // 运行卡（今日最长对照取全部完成行的最大轮次，与稿子 d.done 同口径）
    int maxTurns = 0;
    for (const auto& row : g_pd.rows)
        if (GroupOf(row.state) == G_DONE && row.turns > maxTurns) maxTurns = row.turns;
    for (size_t i = 0; i < runs.size(); i++) {
        const UIRow& row = *runs[i];
        cy = R31RunCard(dl, x + 16.0f, cy, w - 32.0f, row, (int)i, maxTurns, loc, tn);
        if (!row.children.empty() && g_r31_subopen.count(row.key))
            cy = R31SubList(dl, x + 46.0f, cy + 2.0f, w - 62.0f, row, 1840 + (int)i, loc);
        cy += 10.0f;
    }
    // 额度预览（g_r31_query 非空时不画，与稿子一致）；读数陈旧的工具不进 peek
    if (g_r31_query.empty()) {
        if (g_r31_src != "all") {
            const Tool* t = R31ToolOf(g_r31_src);
            const double now = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
            const bool stale = t &&
                ((t->quota.five.known && R31QuotaStale(t->quota.five, now)) ||
                 (t->quota.week.known && R31QuotaStale(t->quota.week, now)));
            if (t && t->present() && R31HasUsage(*t) && !stale)
                cy = R31PeekSingle(dl, x + 16.0f, cy, w - 32.0f, *t, loc) + 13.0f;
        } else {
            const float ph = R31PeekAll(dl, x + 16.0f, cy, w - 32.0f, loc, g_pd.now);
            if (ph > cy) cy = ph + 13.0f;
        }
    }
    // 「等你」标题：满宽 pad 0 21 9，高 26
    {
        const float hy = cy;
        float hx = R31Mid(dl, x + 21.0f, hy + 8.5f, T("r_needs_you", loc), loc, k31Sec, T_S12B);
        if (!waits.empty()) {
            char cb[24];
            snprintf(cb, sizeof cb, "%d", (int)waits.size());
            const float cw2 = TextWidth(cb, loc, T_S11);
            const float bw = std::fmax(18.0f, cw2 + 10.0f);
            dl->AddRectFilled(ImVec2(hx + 5.0f, hy + 1.0f), ImVec2(hx + 5.0f + bw, hy + 17.0f),
                              k31Badge, 4.0f);
            DrawTextRun(dl, hx + 5.0f + (bw - cw2) * 0.5f, TextMid(hy + 9.0f, T_S11, loc), cb, loc,
                        k31Muted, T_S11);
        }
        DrawTextRight(dl, x + w - 21.0f, TextMid(hy + 8.5f, T_S10, loc), T("r_longest_first", loc),
                      loc, k31Muted, T_S10);
        cy = hy + 26.0f;
    }
    return cy;
}
/// 队列的自然内容高（与 R31Queue 同公式）：面板收放要先量后画。
static float R31QueueH(int loc) {
    (void)loc;
    const auto all = R31Rows(g_r31_src, g_r31_query);
    if (all.empty()) return 2.0f;
    const std::vector<const UIRow*> waits = R31Waits();
    if (waits.empty()) return 180.0f;
    const size_t cap = (waits.size() > 8 && !g_r31_queue_all) ? 8 : waits.size();
    float h = 1.0f;
    for (size_t i = 0; i < cap; i++) {
        h += 106.0f;
        const UIRow& r = *waits[i];
        if (!r.children.empty() && g_r31_subopen.count(r.key))
            h += 6.0f + (float)r.children.size() * 24.0f;
        h += (i + 1 < cap) ? 7.0f : 0.0f;
    }
    if (cap < waits.size()) h += 6.0f + 30.0f;
    return h + 2.0f;
}

/// 可滚动队列（.queue-list）：行 x17..399、gap 7、pad 1 1 2。
/// 超过 8 行只摆前 8 行 + 一颗「还有 N 条待处理」（点开全量）——首页是扫一眼的
/// 地方，不是清单；误触双击别的行比多滚几屏更伤（2026-10-28 产品反馈）。
/// 开关 g_r31_queue_all 在壳层（与其它 r31 状态同块）。
static float R31Queue(ImDrawList* dl, float x, float yTop, float w, int loc) {
    const std::vector<const UIRow*> all = R31Rows(g_r31_src, g_r31_query);
    if (all.empty()) return yTop + 1.0f;      // 整屏空态已由固定段画过
    const std::vector<const UIRow*> waits = R31Waits();
    double maxAge = 1.0;
    for (const auto* r : waits)
        if (r->age > maxAge) maxAge = r->age;
    float cy = yTop + 1.0f;
    if (waits.empty()) {
        const bool filtered = !g_r31_query.empty() || g_r31_src != "all";
        if (filtered)
            return R31Empty(dl, x + 17.0f, cy, w - 34.0f, 180.0f,
                            T("r_no_match", loc), T("r_try_other", loc),
                            T("r_show_all", loc), HIT_SHOWALL31, loc);
        // 队列空了不该是一块空白：给一个真落点——今天已经完成的那些会话。
        // 零完成时不画按钮（0 条的入口是噪声，页脚已经写了「今日完成 0」）。
        int doneN = 0;
        for (const auto* r : all) if (GroupOf(r->state) == G_DONE) doneN++;
        const std::string act = RFmt(T("r_view_done", loc), doneN);
        return R31Empty(dl, x + 17.0f, cy, w - 34.0f, 180.0f,
                        T("r_nothing", loc), T("r_try_other", loc),
                        doneN > 0 ? act.c_str() : nullptr, HIT_HISTORY31, loc);
    }
    const size_t cap = (waits.size() > 8 && !g_r31_queue_all) ? 8 : waits.size();
    for (size_t i = 0; i < cap; i++) {
        const UIRow& row = *waits[i];
        cy = R31WaitRow(dl, x + 17.0f, cy, w - 34.0f, row, (int)i, maxAge, loc);
        if (!row.children.empty() && g_r31_subopen.count(row.key))
            cy = R31SubList(dl, x + 44.0f, cy + 3.0f, w - 61.0f, row, 1850 + (int)i, loc);
        // gap 7 只作用在行与行之间，不垫在最后一行后（pad 1 1 2 由返回值补）
        cy += (i + 1 < cap) ? 7.0f : 0.0f;
    }
    if (cap < waits.size()) {
        const std::string lab = RFmt(T("more", loc), (int)(waits.size() - cap));
        const float ly2 = cy + 6.0f;
        const float lw2 = std::fmin(382.0f - 34.0f, 20.0f + TextWidth(lab, loc, T_S10) + 26.0f);
        const float lx2 = x + 17.0f + (w - 34.0f - lw2) * 0.5f;
        R31Glass(dl, lx2, ly2, lw2, 30.0f, 9.0f);
        const float tx2 = lx2 + (lw2 - TextWidth(lab, loc, T_S10) - 18.0f) * 0.5f;
        R31Mid(dl, tx2, ly2 + 15.0f, lab, loc, k31Sec, T_S10);
        I31(dl, I31_DOWN, tx2 + TextWidth(lab, loc, T_S10) + 7.0f, ly2 + 9.5f, 11.0f, k31Faint);
        AddHit(g_hits, lx2, ly2, lx2 + lw2, ly2 + 30.0f, HIT_QUEUEALL, 1900, 0, loc, lab, 0);
        cy = ly2 + 30.0f;
    }
    return cy + 2.0f;
}

// ── 额度格（用量卡 / 账户页共用）：高 88，stale 走灰数 + faint 条 ─────────────
static float R31QuotaCell(ImDrawList* dl, float x, float y, float w, const ap::QuotaWindow& q,
                          const char* labelKey, int loc, double now) {
    const bool stale = R31QuotaStale(q, now);
    R31Mid(dl, x, y + 9.0f, T(labelKey, loc), loc, k31Sec, T_S10);
    {
        char vb[16];
        snprintf(vb, sizeof vb, "%d", R31RemainPct(q));
        const float nw = TextWidth(vb, loc, T_N28, -0.6f);
        DrawTextRun(dl, x, TextMid(y + 40.1f, T_N28, loc), vb, loc, stale ? k31Muted : k31Fg,
                    T_N28, -0.6f);
        DrawTextRun(dl, x + nw + 2.0f, TextTop(y + 36.7f, 19.5f, T_S13B, loc), "%", loc, k31Muted,
                    T_S13B);
    }
    dl->AddRectFilled(ImVec2(x, y + 64.2f), ImVec2(x + w, y + 67.2f), k31Line, 3.0f);
    {
        float p = (float)R31RemainPct(q) / 100.0f;
        if (p > 1.0f) p = 1.0f;
        dl->AddRectFilled(ImVec2(x, y + 64.2f), ImVec2(x + w * p, y + 67.2f),
                          stale ? WithA(k31Faint, 115) : WithA(k31Blue, 230), 3.0f);
    }
    if (stale) {
        R31Mid(dl, x, y + 80.9f, T("r_last_reading", loc), loc, k31Muted, T_S9);
    } else if (q.resets_at > now) {
        R31Mid(dl, x, y + 80.9f, RFmt(T("r_after_reset", loc), Dur(q.resets_at - now, loc).c_str()),
               loc, k31Muted, T_S9);
    }
    return y + 88.0f;
}

// ── 用量与额度（view 6）：整块正文滚动（162..809），页脚 41 ───────────────────
static float R31Usage(ImDrawList* dl, float x, float y, float w, int loc) {
    const double now = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
    const float L = x + 16.0f, listW = w - 32.0f;
    bool anyUsage = g_pd.has_today_tokens && g_pd.today_tokens > 0;
    for (const Tool& t : g_pd.tools) if (R31HasUsage(t)) anyUsage = true;
    if (!anyUsage)
        return R31Empty(dl, L, y, listW, 180.0f, T("r_no_usage", loc), T("r_no_usage_hint", loc),
                        nullptr, 0, loc);
    float cy = y;
    // usage-head：块高 107
    {
        const float hx = L + 7.0f;
        R31Mid(dl, hx, cy + 12.25f, T("r_total_today", loc), loc, k31Muted, T_S11);
        double sum = g_pd.has_today_tokens ? (double)g_pd.today_tokens : 0.0;
        if (!g_pd.has_today_tokens)
            for (const Tool& t : g_pd.tools)
                if (t.present()) sum += (double)t.usage.total();
        const std::string big = R31Compact(sum);
        const float nw = TextWidth(big, loc, T_N35, -1.2f);
        DrawTextRun(dl, hx, TextTop(cy + 25.0f, 42.0f, T_N35, loc), big, loc, k31Fg, T_N35, -1.2f);
        DrawTextRun(dl, hx + nw + 3.0f, TextTop(cy + 50.5f, 16.5f, T_CHIP, loc), "tokens", loc,
                    k31Muted, T_CHIP);
        int n = 0;
        for (const Tool& t : g_pd.tools) if (R31HasUsage(t) || R31QuotaAny(t)) n++;
        R31Mid(dl, hx, cy + 81.5f, RFmt(T("r_tools_log", loc), n), loc, k31Muted, T_S10);
        cy += 107.0f;
    }
    // 每个有额度的工具一张卡：head 53 + pair(5+88+14) [+ stale note]
    for (const Tool& t : g_pd.tools) {
        if (!R31QuotaAny(t)) continue;
        const bool staleF = R31QuotaStale(t.quota.five, now);
        const bool staleW = R31QuotaStale(t.quota.week, now);
        const bool stale = staleF || staleW;
        const bool f5 = t.quota.five.known, wk = t.quota.week.known;
        float ch = 53.0f + 5.0f + 88.0f + 14.0f;
        if (stale) ch += 4.0f + 13.5f;
        const float cardY = cy;
        R31Glass(dl, L, cardY, listW, ch, 13.0f);
        {
            Brand31(dl, L + 13.0f, cardY + 12.0f, 24.0f, t.id, loc);
            const float nx = L + 13.0f + 24.0f + 9.0f;
            R31Mid(dl, nx, cardY + 19.5f, R31Agent(t.id, loc), loc, k31Fg, T_S12B);
            const std::string plan = t.quota.plan.empty()
                                         ? std::string(T("r_personal", loc))
                                         : t.quota.plan + " · " + T("r_personal", loc);
            R31Mid(dl, nx, cardY + 37.75f, plan, loc, k31Muted, T_S9);
            const double seen = std::fmax(t.quota.five.seen_at, t.quota.week.seen_at);
            int hh = 0, mm = 0;
            RTimeOfDay(seen, &hh, &mm);
            char sb[8];
            snprintf(sb, sizeof sb, "%02d:%02d", hh, mm);
            const std::string fresh = RFmt(T("r_updated", loc), sb);
            const float fw2 = TextWidth(fresh, loc, T_S9);
            const float iconX = L + listW - 13.0f - 12.0f;
            DrawTextRun(dl, iconX - 9.0f - fw2, TextMid(cardY + 26.5f, T_S9, loc), fresh, loc,
                        stale ? k31Red : k31Muted, T_S9);
            I31(dl, I31_RIGHT, iconX, cardY + 20.5f, 12.0f, k31Faint);
            AddHit(g_hits, L, cardY, L + listW, cardY + 53.0f, HIT_ACCOUNT31, 1110, 0, loc, t.name, 0);
            g_hits.back().key = t.id;
        }
        {
            const float py = cardY + 58.0f;
            const float fullW = listW - 28.0f;
            const float cellW = (fullW - 20.0f) / 2.0f;
            if (f5 && wk) {
                R31QuotaCell(dl, L + 14.0f, py, cellW, t.quota.five, "r_5h", loc, now);
                R31QuotaCell(dl, L + 14.0f + cellW + 20.0f, py, cellW, t.quota.week, "r_week", loc, now);
            } else if (f5) {
                R31QuotaCell(dl, L + 14.0f, py, fullW, t.quota.five, "r_5h", loc, now);
            } else {
                R31QuotaCell(dl, L + 14.0f, py, fullW, t.quota.week, "r_week", loc, now);
            }
            if (stale)
                R31DrawWrap(dl, T("r_stale_note", loc), L + 14.0f, py + 92.0f, fullW, loc, T_S9,
                            13.5f, k31Muted, 2);
        }
        cy = cardY + ch + 10.0f;
    }
    // section-heading：margin 17 5 10
    {
        cy += 17.0f;
        R31Mid(dl, L + 5.0f, cy + 8.0f, T("r_usage_by", loc), loc, k31Sec, T_S11B);
        DrawTextRight(dl, L + listW - 5.0f, TextMid(cy + 8.0f, T_S10, loc), T("r_today", loc), loc,
                      k31Muted, T_S10);
        cy += 26.0f;
    }
    // tool-usage-list：每行 59，行间 1pt；没读数的工具数字列整列不画
    {
        std::vector<const Tool*> us;
        for (const Tool& t : g_pd.tools)
            if (t.present() && (R31HasUsage(t) || R31QuotaAny(t))) us.push_back(&t);
        if (!us.empty()) {
            const float listY = cy;
            const float listH = (float)us.size() * 59.0f;
            R31Glass(dl, L, listY, listW, listH, 12.0f);
            for (size_t i = 0; i < us.size(); i++) {
                const Tool& t = *us[i];
                const float ry = listY + (float)i * 59.0f;
                if (i > 0) RHair(dl, L, ry, L + listW, k31Line);
                Brand31(dl, L + 12.0f, ry + 17.5f, 24.0f, t.id, loc);
                const float nx = L + 12.0f + 24.0f + 10.0f;
                R31Mid(dl, nx, ry + 20.0f, R31Agent(t.id, loc), loc, k31Fg, T_S12B);
                const std::string sub = t.has_credits ? R31Num(t.credits) + " Credits"
                                                      : R31Client(t.id);
                R31Mid(dl, nx, ry + 38.0f, sub, loc, k31Muted, T_S9);
                if (t.usage.total() > 0) {
                    const std::string v = R31Compact((double)t.usage.total());
                    const float vw = TextWidth(v, loc, T_S14B);
                    const float iconX = L + listW - 12.0f - 12.0f;
                    DrawTextRun(dl, iconX - 6.0f - vw, TextMid(ry + 29.5f, T_S14B, loc), v, loc,
                                k31Fg, T_S14B);
                    I31(dl, I31_RIGHT, iconX, ry + 23.5f, 12.0f, k31Faint);
                }
            }
            cy = listY + listH;
        }
    }
    // data-note：margin 15 5 0
    cy += 15.0f;
    cy = R31DrawWrap(dl, T("r_data_note", loc), L + 5.0f, cy, listW - 10.0f, loc, T_S10, 17.5f,
                     k31Muted, 3);
    return cy + 16.0f;
}
/// 用量页脚（809..850）。
static void R31UsageFoot(ImDrawList* dl, float x, float y, float w, int loc) {
    RHair(dl, x, y, x + w, k31Line);
    const double now = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
    int hh = 0, mm = 0;
    RTimeOfDay(now, &hh, &mm);
    char sb[8];
    snprintf(sb, sizeof sb, "%02d:%02d", hh, mm);
    const std::string left = std::string(R31ModeText(loc)) + " " + sb;
    float tx = x + 19.0f;
    I31(dl, I31_CLOCK, tx, y + 20.5f - 6.0f, 12.0f, k31Muted);
    DrawTextRun(dl, tx + 17.0f, TextMid(y + 20.5f, T_S9, loc), left, loc, k31Muted, T_S9);
    DrawTextRight(dl, x + w - 19.0f, TextMid(y + 20.5f, T_S9, loc), T("r_not_billing", loc), loc,
                  k31Muted, T_S9);
}

// ── 账户（view 7）：整块滚动（54..789），页脚 61 ─────────────────────────────
static float R31Account(ImDrawList* dl, float x, float y, float w, int loc) {
    const double now = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
    const Tool* t = R31ToolOf(g_r31_account);
    const float L = x + 20.0f, cw2 = w - 40.0f;
    float cy = y + 7.0f;
    if (!t || !t->present())
        return R31Empty(dl, L, cy, cw2, 180.0f, T("r_no_account", loc),
                        T("r_no_account_hint", loc), nullptr, 0, loc);
    // hero：pad 4 2 20，块高 66
    {
        Brand31(dl, L + 2.0f, cy + 4.0f, 42.0f, t->id, loc);
        const float nx = L + 2.0f + 42.0f + 12.0f;
        const std::string cap = T("r_today", loc);
        const float capW = TextWidth(cap, loc, T_S10) + 18.0f;
        const float capX = x + w - 22.0f - capW;
        DrawTextRun(dl, nx, TextMid(cy + 15.5f, T_S20, loc),
                    ClipToWidth(R31Agent(t->id, loc), capX - 8.0f - nx, loc, T_S20), loc, k31Fg,
                    T_S20, -0.5f);
        const std::string sub = t->quota.plan.empty() ? std::string(T("r_local_usage", loc))
                                                      : t->quota.plan + " · " + T("r_personal", loc);
        R31Mid(dl, nx, cy + 40.5f, sub, loc, k31Muted, T_S10);
        dl->AddRect(ImVec2(capX + 0.5f, cy + 12.5f), ImVec2(capX + capW - 0.5f, cy + 37.5f),
                    k31Line, 6.0f, 0, 1.0f);
        DrawTextRun(dl, capX + 9.0f, TextMid(cy + 25.0f, T_S10, loc), cap, loc, k31Sec, T_S10);
        cy += 66.0f;
    }
    // account-total：pad 0 3 17
    {
        const float tx = L + 3.0f;
        R31Mid(dl, tx, cy + 8.25f, T("r_total_usage", loc), loc, k31Muted, T_S11);
        const std::string big = R31Compact((double)t->usage.total());
        const float nw = TextWidth(big, loc, T_N39, -1.5f);
        DrawTextRun(dl, tx, TextTop(cy + 21.0f, 44.85f, T_N39, loc), big, loc, k31Fg, T_N39, -1.5f);
        DrawTextRun(dl, tx + nw + 7.0f, TextTop(cy + 47.85f, 18.0f, T_S12, loc), "tokens", loc,
                    k31Muted, T_S12);
        cy += 21.0f + 44.85f;
        if (t->has_credits) {
            cy += 8.0f;
            R31Mid(dl, tx, cy + 8.25f, RFmt(T("r_consumed", loc), R31Num(t->credits).c_str()), loc,
                   k31Sec, T_S11);
            cy += 16.5f;
        }
        cy += 17.0f;
    }
    // 额度卡（整块仅当 quota 有已知窗）
    if (t->quota.known && (t->quota.five.known || t->quota.week.known)) {
        const bool staleF = R31QuotaStale(t->quota.five, now);
        const bool staleW = R31QuotaStale(t->quota.week, now);
        const bool stale = staleF || staleW;
        const bool f5 = t->quota.five.known, wk = t->quota.week.known;
        float qh = 13.0f + 88.0f + 14.0f;
        if (stale) qh += 4.0f + 13.5f;
        const float qy = cy;
        R31Glass(dl, L, qy, cw2, qh, 12.0f);
        const float fullW = cw2 - 28.0f;
        const float cellW = (fullW - 20.0f) / 2.0f;
        const float py = qy + 13.0f;
        if (f5 && wk) {
            R31QuotaCell(dl, L + 14.0f, py, cellW, t->quota.five, "r_5h", loc, now);
            R31QuotaCell(dl, L + 14.0f + cellW + 20.0f, py, cellW, t->quota.week, "r_week", loc, now);
        } else if (f5) {
            R31QuotaCell(dl, L + 14.0f, py, fullW, t->quota.five, "r_5h", loc, now);
        } else {
            R31QuotaCell(dl, L + 14.0f, py, fullW, t->quota.week, "r_week", loc, now);
        }
        if (stale)
            R31DrawWrap(dl, T("r_stale_note", loc), L + 14.0f, py + 92.0f, fullW, loc, T_S9, 13.5f,
                        k31Muted, 2);
        cy = qy + qh;
    }
    // 小时柱图：margin 19 3 20；块内 heading + 370×89 图 + 刻度
    bool activityDrawn = false;
    {
        long hmax = 0;
        for (int i = 0; i < 24; i++) hmax = std::fmax(hmax, (double)t->hourly[(size_t)i]);
        if (hmax > 0) {
            activityDrawn = true;
            cy += 19.0f;
            const float bx0 = L + 3.0f, bw2 = w - 46.0f;    // 370
            R31Mid(dl, bx0, cy + 8.0f, T("r_activity", loc), loc, k31Sec, T_S11B);
            DrawTextRight(dl, bx0 + bw2, TextMid(cy + 8.0f, T_S10, loc), T("r_hourly", loc), loc,
                          k31Muted, T_S10);
            cy += 28.0f;
            const float chartH = 89.0f;
            RHair(dl, bx0, cy + 29.0f, bx0 + bw2, k31LineSoft);
            RHair(dl, bx0, cy + 58.0f, bx0 + bw2, k31LineSoft);
            const float gap = 5.0f, barW = (bw2 - 23.0f * gap) / 24.0f;
            int peak = 0;
            for (int i = 1; i < 24; i++)
                if (t->hourly[(size_t)i] > t->hourly[(size_t)peak]) peak = i;
            for (int i = 0; i < 24; i++) {
                if (t->hourly[(size_t)i] <= 0) continue;
                float hgt = (float)t->hourly[(size_t)i] / (float)hmax * 88.0f;
                if (hgt < 1.0f) hgt = 1.0f;
                const float bxx = bx0 + (float)i * (barW + gap);
                const ImU32 col = (i == peak) ? WithA(k31Blue, 230) : WithA(k31Sec, 71);
                dl->AddRectFilled(ImVec2(bxx, cy + chartH - hgt), ImVec2(bxx + barW, cy + chartH),
                                  col, 2.0f);
            }
            RHair(dl, bx0, cy + chartH - 1.0f, bx0 + bw2, k31Line);
            cy += chartH + 7.0f;
            {
                const char* ticks[5] = {"00:00", "06:00", "12:00", "18:00", "23:00"};
                float tws[5];
                float totalW = 0.0f;
                for (int i = 0; i < 5; i++) {
                    tws[i] = TextWidth(ticks[i], loc, T_S8);
                    totalW += tws[i];
                }
                const float g2 = (bw2 - totalW) / 4.0f;
                float tx2 = bx0;
                for (int i = 0; i < 5; i++) {
                    DrawTextRun(dl, tx2, TextTop(cy, 10.0f, T_S8, loc), ticks[i], loc, k31Muted, T_S8);
                    tx2 += tws[i] + g2;
                }
                cy += 10.0f;
            }
            cy += 20.0f;
        }
    }
    // 拆解 2×2：格 188×72（无边距，奇数列右 1pt / 首行底 1pt）；全零整块不画
    if (t->usage.total() > 0) {
        if (!activityDrawn) cy += 19.0f;    // activity 缺席时补上它的 margin-top
        const int order[4][2] = {{0, I31_DOWN}, {1, I31_UP}, {2, I31_LAYERS}, {3, I31_COPY}};
        const long vals[4] = {t->usage.input, t->usage.output, t->usage.cache_read,
                              t->usage.cache_write};
        const char* keys4[4] = {"r_input", "r_output", "r_cache_read", "r_cache_write"};
        const float gw = cw2, cw3 = gw / 2.0f;
        const float gy = cy;
        R31Glass(dl, L, gy, gw, 144.0f, 12.0f);
        dl->AddRectFilled(ImVec2(L + cw3, gy), ImVec2(L + cw3 + 1.0f, gy + 144.0f), k31Line);
        dl->AddRectFilled(ImVec2(L, gy + 72.0f), ImVec2(L + gw, gy + 73.0f), k31Line);
        for (int ci = 0; ci < 4; ci++) {
            const int col = ci % 2, row = ci / 2;
            const float cxx = L + (float)col * cw3;
            const float cyy = gy + (float)row * 72.0f;
            I31(dl, order[ci][1], cxx + 14.0f, cyy + 15.0f, 15.0f, k31Muted);
            DrawTextRun(dl, cxx + 38.0f, TextTop(cyy + 14.0f, 15.0f, T_S10, loc),
                        T(keys4[ci], loc), loc, k31Muted, T_S10);
            DrawTextRun(dl, cxx + 38.0f, TextMid(cyy + 45.5f, T_N20, loc),
                        R31Compact((double)vals[ci]), loc, k31Fg, T_N20, -0.4f);
        }
        cy = gy + 144.0f;
    }
    // 按模型：toggle 46 高 + 展开列表
    {
        float tx = L + 4.0f;
        I31(dl, I31_LAYERS, tx, cy + 16.0f, 14.0f, k31Sec);
        tx += 14.0f + 8.0f;
        tx = R31Mid(dl, tx, cy + 23.0f, T("r_by_model", loc), loc, k31Sec, T_S11);
        {
            char nb[8];
            snprintf(nb, sizeof nb, "%d", (int)t->models.size());
            DrawTextRun(dl, tx + 4.0f, TextMid(cy + 23.0f, T_S10, loc), nb, loc, k31Muted, T_S10);
        }
        I31(dl, g_r31_models ? I31_DOWN : I31_RIGHT, L + cw2 - 4.0f - 12.0f, cy + 17.0f, 12.0f,
            k31Muted);
        AddHit(g_hits, L, cy, L + cw2, cy + 46.0f, HIT_MODELSTOG, 1400, 0, loc,
               T("r_by_model", loc), g_r31_models ? 1 : 0);
        cy += 46.0f;
        if (g_r31_models && !t->models.empty()) {
            const float rowH = 35.5f;
            const float lh2 = 4.0f + (float)t->models.size() * rowH;
            R31Glass(dl, L, cy, cw2, lh2, 9.0f);
            for (size_t m2 = 0; m2 < t->models.size(); m2++) {
                const ap::ModelUse& mu = t->models[m2];
                const float ry = cy + 2.0f + (float)m2 * rowH;
                if (m2 > 0) RHair(dl, L + 12.0f, ry, L + cw2 - 12.0f, k31Line);
                R31Mid(dl, L + 12.0f, ry + 17.75f, mu.name, loc, k31Sec, T_CHIP);
                DrawTextRight(dl, L + cw2 - 12.0f, TextMid(ry + 17.75f, T_S11, loc),
                              R31Compact((double)mu.tokens), loc, k31Fg, T_S11);
            }
            cy += lh2;
        }
    }
    // data-note：r_model_note
    cy += 15.0f;
    cy = R31DrawWrap(dl, T("r_model_note", loc), L + 3.0f, cy, cw2 - 6.0f, loc, T_S10, 17.5f,
                     k31Muted, 3);
    return cy + 18.0f;
}
/// 账户页脚（789..850）：一颗满宽「查看此工具的会话」。
static void R31AccountFoot(ImDrawList* dl, float x, float y, float w, int loc) {
    RHair(dl, x, y, x + w, k31Line);
    const float px = x + 22.0f, pw = w - 44.0f;
    const float by = y + 13.0f;
    const float hov = TxPeekNew(Zid(loc, 1410, HIT_ACCTSESS, 0), 0.0f);
    if (hov > 0.01f)
        dl->AddRectFilled(ImVec2(px, by), ImVec2(px + pw, by + 36.0f), k31Hover, 9.0f);
    dl->AddRect(ImVec2(px + 0.5f, by + 0.5f), ImVec2(px + pw - 0.5f, by + 35.5f), k31Line, 9.0f, 0,
                1.0f);
    I31(dl, I31_RIGHT, px + 12.0f, by + 12.0f, 12.0f, k31Sec);
    DrawTextRun(dl, px + 31.0f, TextMid(by + 18.0f, T_S11, loc), T("r_tool_sessions", loc), loc,
                k31Sec, T_S11);
    AddHit(g_hits, px, by, px + pw, by + 36.0f, HIT_ACCTSESS, 1410, 0, loc,
           T("r_tool_sessions", loc), 0);
}

// ── 全部工具（view 8）：heading + 搜索固定（54..154），列表滚动（166..850）────
static void R31ToolsFixed(ImDrawList* dl, float x, float y, float w, int loc, double tn) {
    const auto present = R31PresentTools();
    DrawTextRun(dl, x + 22.0f, TextTop(y + 64.0f, 32.0f, T_S23B, loc), T("r_all_tools", loc), loc,
                k31Fg, T_S23B, -0.5f);
    DrawTextRight(dl, x + 394.0f, TextMid(y + 84.0f, T_S11, loc),
                  RFmt(T("r_tools_n", loc), (int)present.size()), loc, k31Muted, T_S11);
    const float sx = x + 16.0f, sw = w - 32.0f, sy = y + 118.0f, sh = 36.0f;
    R31Glass(dl, sx, sy, sw, sh, 10.0f);
    I31(dl, I31_SEARCH, sx + 10.0f, sy + 10.0f, 16.0f, k31Muted);
    const bool focused = (g_r31_focus == 2);
    const float tw = sx + sw - 10.0f - (g_r31_toolq.empty() ? 0.0f : 26.0f) - (sx + 34.0f);
    if (g_r31_toolq.empty()) {
        DrawTextRun(dl, sx + 34.0f, TextMid(sy + 18.0f, T_S12, loc), T("r_search_tools", loc), loc,
                    k31Faint, T_S12);
        // 空串也要画光标：点进来却看不出这颗框在收字，和没点一样。
        if (focused && std::fmod(tn, 1.06) < 0.53)
            dl->AddRectFilled(ImVec2(sx + 34.0f, sy + 8.0f), ImVec2(sx + 35.0f, sy + 28.0f),
                              k31Fg);
    } else {
        const std::string shown = R31TailFit(g_r31_toolq, tw, loc, T_S12);
        const float drawn = DrawTextRun(dl, sx + 34.0f, TextMid(sy + 18.0f, T_S12, loc), shown, loc,
                                        k31Fg, T_S12);
        if (focused && std::fmod(tn, 1.06) < 0.53)
            dl->AddRectFilled(ImVec2(sx + 34.0f + drawn + 1.0f, sy + 8.0f),
                              ImVec2(sx + 34.0f + drawn + 2.0f, sy + 28.0f), k31Fg);
    }
    // 同上：整条框先登记，✕ 压在它上面。
    AddHit(g_hits, sx, sy, sx + sw, sy + sh, HIT_SEARCHFIELD, 1311, 2, loc,
           T("r_search_tools", loc), 0);
    if (!g_r31_toolq.empty()) {
        const float bx = sx + sw - 28.0f;
        I31(dl, I31_CLOSE, bx + 5.0f, sy + 12.5f, 11.0f, k31Faint);
        AddHit(g_hits, bx - 4.0f, sy + 6.0f, bx + 26.0f, sy + 30.0f, HIT_CLEARQ, 1310, 2, loc,
               T("r_clear", loc), 0);
    }
}
static float R31ToolsList(ImDrawList* dl, float x, float yTop, float w, int loc) {
    const auto present = R31PresentTools();
    std::vector<const Tool*> keys;
    const std::string q = R31Lower(g_r31_toolq);
    for (const auto* t : present) {
        if (!q.empty()) {
            const std::string hay = R31Lower(R31Agent(t->id, 0) + " " + R31Client(t->id) + " " + t->name);
            if (hay.find(q) == std::string::npos) continue;
        }
        keys.push_back(t);
    }
    float cy = yTop + 4.0f;
    if (keys.empty())
        return R31Empty(dl, x + 16.0f, cy, w - 32.0f, 180.0f, T("r_no_tools", loc),
                        T("r_try_tool", loc), nullptr, 0, loc);
    const float listX = x + 17.0f, listW = w - 34.0f;
    const float listH = (float)keys.size() * 68.0f;
    R31Glass(dl, listX, cy, listW, listH, 12.0f);
    for (size_t i = 0; i < keys.size(); i++) {
        const Tool& t = *keys[i];
        const float ry = cy + (float)i * 68.0f;
        if (i > 0) RHair(dl, listX, ry, listX + listW, k31Line);
        const float hov = TxPeekNew(Zid(loc, 1500 + (int)i, HIT_PICKTOOL, 0), 0.0f);
        if (hov > 0.01f)
            dl->AddRectFilled(ImVec2(listX, ry), ImVec2(listX + listW, ry + 68.0f), k31Hover);
        Brand31(dl, listX + 15.0f, ry + 19.0f, 30.0f, t.id, loc);
        const float nx = listX + 15.0f + 30.0f + 12.0f;
        float cx2 = R31Mid(dl, nx, ry + 23.0f, R31Agent(t.id, loc), loc, k31Fg, T_S13B);
        R31RowTag(dl, cx2 + 6.0f, ry + 23.0f, t.id, loc);
        int rows = 0, wait = 0;
        for (const auto& r : g_pd.rows) {
            if (r.agent_id != t.id) continue;
            rows++;
            const int g = GroupOf(r.state);
            if (g == G_AWAIT || g == G_FAIL) wait++;
        }
        std::string sub = RFmt(T("r_sessions_n", loc), rows);
        if (wait > 0) sub += " · " + RFmt(T("r_waiting_n", loc), wait);
        DrawTextRun(dl, nx, TextMid(ry + 45.0f, T_S10, loc),
                    ClipToWidth(sub, listX + listW - 15.0f - 13.0f - 6.0f - nx, loc, T_S10), loc,
                    k31Muted, T_S10);
        I31(dl, g_r31_src == t.id ? I31_CHECK : I31_RIGHT, listX + listW - 15.0f - 13.0f,
            ry + 27.5f, 13.0f, k31Muted);
        AddHit(g_hits, listX, ry, listX + listW, ry + 68.0f, HIT_PICKTOOL, 1500 + (int)i, 0, loc,
               R31Agent(t.id, loc), 0);
        g_hits.back().key = t.id;
    }
    cy += listH;
    cy += 18.0f;
    cy = R31DrawWrap(dl, RFmt(T("r_tools_log", loc), (int)keys.size()), x + 40.0f, cy, w - 80.0f,
                     loc, T_S10, 17.5f, k31Muted, 3);
    return cy;
}

// ── 会话详情（view 3）：头 54..198 固定，正文滚动（198..748），页脚 102 ────────
static void R31DetailHead(ImDrawList* dl, float x, float y, float w, int loc, const UIRow* r) {
    if (!r) return;
    Brand31(dl, x + 24.0f, y + 67.5f, 43.0f, r->agent_id, loc);
    const float nx = x + 79.0f;
    DrawTextRun(dl, nx, TextMid(y + 77.0f, T_S17B, loc),
                ClipToWidth(R31Agent(r->agent_id, loc), x + 392.0f - nx, loc, T_S17B), loc, k31Fg,
                T_S17B);
    R31ClientTag(dl, nx, y + 103.5f, r->agent_id, loc);
    DrawTextRun(dl, x + 24.0f, TextTop(y + 129.0f, 26.0f, T_S22, loc),
                ClipToWidth(R31RowName(*r), 368.0f, loc, T_S22), loc, k31Fg, T_S22, -0.5f);
    {
        const int kind = R31KindOf(*r);
        const std::string word = T(R31KindWord(kind), loc);
        const ImU32 kc = R31KindColor(kind);
        const float tw = TextWidth(word, loc, T_S11B);
        float mx = x + 24.0f;
        I31(dl, R31KindIcon(kind), mx, y + 164.0f + 2.5f, 11.0f, kc);
        DrawTextRun(dl, mx + 15.0f, TextMid(y + 172.0f, T_S11B, loc), word, loc, kc, T_S11B);
        mx += 15.0f + 4.0f + tw + 12.0f;
        const std::string when = r->at > 0.0 || r->age > 0 ? Dur(r->age, loc) : std::string();
        if (!when.empty())
            DrawTextRun(dl, mx, TextMid(y + 172.0f, T_S11, loc), when, loc, k31Muted, T_S11);
    }
}
static float R31DetailBody(ImDrawList* dl, float x, float yTop, float w, int loc, const UIRow& r) {
    const float L = x + 22.0f, RW2 = w - 44.0f;
    const bool done = (R31KindOf(r) == 5);
    float cy = yTop + 20.0f;
    DrawTextRun(dl, L, TextMid(cy + 8.0f, T_S11B, loc),
                done ? T("r_record", loc) : T("r_original", loc), loc, k31Muted, T_S11B);
    cy += 28.0f;
    float pend = 16.0f;   // full-question 的 margin-bottom；CSS 相邻折叠取大者
    {
        std::string body;
        if (!r.question.empty()) {
            body = r.question;
        } else if (done) {
            int hh = 0, mm = 0;
            RTimeOfDay(r.at > 0.0 ? r.at : (g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr)) - r.age,
                       &hh, &mm);
            char tb[8];
            snprintf(tb, sizeof tb, "%02d:%02d", hh, mm);
            body = RFmt(T("r_completed_at", loc), tb, r.turns);
        } else {
            body = T("r_running_none", loc);
        }
        cy = R31DrawWrap(dl, body, L, cy, RW2, loc, T_S14, 25.2f, k31Fg, 0);
    }
    if (r.inferred) {
        cy += std::fmax(pend, 18.0f);
        cy = R31DrawWrap(dl, T("r_truncated", loc), L, cy, RW2, loc, T_S11, 18.7f, k31Muted, 2);
        pend = 18.0f;
    }
    if (!r.options.empty()) {
        cy += std::fmax(pend, 18.0f);   // detail-label 的 margin-top（与上文折叠）
        DrawTextRun(dl, L, TextMid(cy + 8.0f, T_S11B, loc), T("r_options", loc), loc, k31Muted, T_S11B);
        cy += 32.0f;                    // 标签行 16 + margin-bottom 16
        for (size_t i = 0; i < r.options.size(); i++) {
            const float oy = cy;
            R31Glass(dl, L, oy, RW2, 40.0f, 9.0f);
            char nb[4];
            snprintf(nb, sizeof nb, "%zu", i + 1);
            const bool hasDesc = i < r.option_desc.size() && !r.option_desc[i].empty();
            DrawTextRun(dl, L + 12.0f, TextMid(oy + (hasDesc ? 14.0f : 20.0f), T_S9, loc), nb, loc,
                        k31Muted, T_S9);
            const std::string op = ClipToWidth(r.options[i], RW2 - 12.0f - 16.0f - 10.0f - 12.0f,
                                               loc, T_S12);
            DrawTextRun(dl, L + 38.0f, TextMid(oy + (hasDesc ? 14.0f : 20.0f), T_S12, loc), op, loc,
                        k31Fg, T_S12);
            if (hasDesc)
                DrawTextRun(dl, L + 38.0f, TextMid(oy + 30.5f, T_S9, loc),
                            ClipToWidth(r.option_desc[i], RW2 - 50.0f, loc, T_S9), loc, k31Muted, T_S9);
            cy += 47.0f;
        }
        cy -= 7.0f;        // 去掉末行的 gap 7（行间 gap 不作用在最后一行）
        pend = 18.0f;      // option-list 的 margin-bottom
    }
    if (r.has_tokens || r.has_credits) {
        cy += pend;
        R31Glass(dl, L, cy, RW2, 46.0f, 10.0f);
        const float cy2 = cy + 23.0f;
        float tx = R31Mid(dl, L + 13.0f, cy2, T("r_session_total", loc), loc, k31Muted, T_S10) + 8.0f;
        if (r.has_tokens) {
            tx = R31Mid(dl, tx, cy2, R31Compact((double)r.tokens), loc, k31Fg, T_S17);
            R31Mid(dl, tx + 3.0f, cy2, "tokens", loc, k31Muted, T_S10);
        }
        if (r.has_credits)
            DrawTextRight(dl, L + RW2 - 13.0f, TextMid(cy2, T_S11, loc),
                          R31Num(r.credits) + " Credits", loc, k31Sec, T_S11);
        cy += 46.0f;
        pend = 18.0f;
    }
    cy += pend;
    cy = R31DrawWrap(dl, T("r_boundary", loc), L, cy, RW2, loc, T_S11, 18.7f, k31Muted, 2);
    return cy + 8.0f;
}
static void R31DetailFoot(ImDrawList* dl, float x, float y, float w, int loc, const UIRow* r) {
    const float fy = y + 748.0f;
    {
        const float px = x + 22.0f, pw = w - 44.0f, by = fy + 13.0f;
        const float hov = r ? TxPeekNew(Zid(loc, 1600, HIT_OPEN, 0), 0.0f) : 0.0f;
        dl->AddRectFilled(ImVec2(px, by), ImVec2(px + pw, by + 38.0f),
                          hov > 0.01f ? MixA(k31Sel, k31Hover, hov) : k31Sel, 9.0f);
        dl->AddRect(ImVec2(px + 0.5f, by + 0.5f), ImVec2(px + pw - 0.5f, by + 37.5f), k31Line, 9.0f,
                    0, 1.0f);
        const bool zcode = (r && r->agent_id == "zcode");
        const std::string lab = T(zcode ? "r_open_zcode" : "r_open_session", loc);
        const float tw = 13.0f + 7.0f + TextWidth(lab, loc, T_S12B);
        const float cx2 = x + w * 0.5f - tw * 0.5f;
        I31(dl, I31_LAUNCH, cx2, by + 12.5f, 13.0f, k31Fg);
        DrawTextRun(dl, cx2 + 20.0f, TextMid(by + 19.0f, T_S12B, loc), lab, loc, k31Fg, T_S12B);
        if (r) AddHit(g_hits, px, by, px + pw, by + 38.0f, HIT_OPEN, 1600, 1, *r, loc, lab);
    }
    if (r && !r->question.empty()) {
        const float px = x + 22.0f, pw = w - 44.0f, by = fy + 58.0f;
        const float hov = TxPeekNew(Zid(loc, 1620, HIT_COPYTEXT, 0), 0.0f);
        if (hov > 0.01f)
            dl->AddRectFilled(ImVec2(px, by), ImVec2(px + pw, by + 32.0f), k31Hover, 9.0f);
        dl->AddRect(ImVec2(px + 0.5f, by + 0.5f), ImVec2(px + pw - 0.5f, by + 31.5f), k31Line, 9.0f,
                    0, 1.0f);
        I31(dl, I31_COPY, px + 12.0f, by + 9.5f, 13.0f, k31Sec);
        DrawTextRun(dl, px + 32.0f, TextMid(by + 16.0f, T_S11, loc), T("r_copy_request", loc), loc,
                    k31Sec, T_S11);
        AddHit(g_hits, px, by, px + pw, by + 32.0f, HIT_COPYTEXT, 1620, 0, loc,
               T("r_copy_request", loc), 0);
        g_hits.back().title = r->question;   // 复制的是请求原文，装在 title 里（沿用接线屏的约定）
    }
}

// ── 今日完成（view 5）：heading 固定（54..110），列表滚动（110..850）──────────
/// 完成记录分页：一页 12 条，滚到贴近底部自动续页（懒加载）；列表撑不满一屏又有
/// 余量时，给一颗「还有 N 条」按钮补位。页大小 g_r31_hist_n 在壳层（跨屏保留）。
static void R31HistoryHead(ImDrawList* dl, float x, float y, float w, int loc) {
    const std::vector<const UIRow*> all = R31Rows(g_r31_src, g_r31_query);
    int doneN = 0;
    for (const auto* r : all) if (GroupOf(r->state) == G_DONE) doneN++;
    DrawTextRun(dl, x + 22.0f, TextTop(y + 62.0f, 32.0f, T_S23B, loc), T("r_completed_today", loc),
                loc, k31Fg, T_S23B, -0.5f);
    DrawTextRight(dl, x + 394.0f, TextMid(y + 82.0f, T_S11, loc),
                  RFmt(T("r_sessions_n", loc), doneN), loc, k31Muted, T_S11);
}
static float R31HistoryList(ImDrawList* dl, float x, float yTop, float w, int loc) {
    std::vector<const UIRow*> done;
    const double nowRef = g_pd.now > 0 ? g_pd.now : (double)std::time(nullptr);
    for (const auto* r : R31Rows(g_r31_src, g_r31_query))
        if (GroupOf(r->state) == G_DONE) done.push_back(r);
    std::sort(done.begin(), done.end(),
              [nowRef](const UIRow* a, const UIRow* b) {
                  const double ta = a->at > 0.0 ? a->at : nowRef - a->age;
                  const double tb = b->at > 0.0 ? b->at : nowRef - b->age;
                  return ta > tb;
              });
    float cy = yTop;
    if (done.empty())
        return R31Empty(dl, x + 16.0f, cy, w - 32.0f, 180.0f, T("r_no_completed", loc),
                        T("r_no_completed_hint", loc), nullptr, 0, loc);
    if (g_r31_hist_n > (int)done.size()) g_r31_hist_n = (int)done.size();
    if (g_r31_hist_n < 12) g_r31_hist_n = (int)std::fmin((float)done.size(), 12.0f);
    const int shown = (int)std::fmin((float)g_r31_hist_n, (float)done.size());
    const bool more = shown < (int)done.size();
    const float listX = x + 16.0f, listW = w - 32.0f;
    float listH = (float)shown * 63.0f;
    for (int i = 0; i < shown; i++) {                         // 展开着的行各加一块
        const UIRow* r = done[(size_t)i];
        if (!r->children.empty() && g_r31_subopen.count(r->key))
            listH += 6.0f + (float)r->children.size() * 24.0f + 6.0f;
    }
    if (more) listH += 40.0f;                                 // 「还有 N 条」补位行
    R31Glass(dl, listX, cy, listW, listH, 13.0f);
    float cy2 = cy;
    for (int i = 0; i < shown; i++) {
        const UIRow& r = *done[(size_t)i];
        const float iy = cy2;
        cy2 = iy + 63.0f;
        if (i > 0) RHair(dl, listX, iy, listX + listW, k31Line);
        const float hov = TxPeekNew(Zid(loc, 1700 + (int)i, HIT_OPEN, 0), 0.0f);
        if (hov > 0.01f)
            dl->AddRectFilled(ImVec2(listX, iy), ImVec2(listX + 350.0f, iy + 62.0f), k31Hover);
        AddHit(g_hits, listX, iy, listX + 350.0f, iy + 62.0f, HIT_OPEN, 1700 + (int)i, 0, r, loc,
               R31RowName(r));
        Brand31(dl, x + 28.0f, iy + 19.5f, 23.0f, r.agent_id, loc);
        const float nx = x + 60.0f;
        {
            int hh = 0, mm = 0;
            RTimeOfDay(r.at > 0.0 ? r.at : nowRef - r.age, &hh, &mm);
            char sb[8];
            snprintf(sb, sizeof sb, "%02d:%02d", hh, mm);
            const float tw = TextWidth(sb, loc, T_S10);
            DrawTextRun(dl, x + 339.0f - tw, TextMid(iy + 20.5f, T_S10, loc), sb, loc, k31Muted, T_S10);
            if (r.unseen)   // 完成了还没看过：点一下（双击跳走/ⓘ 详情）就消
                dl->AddCircleFilled(ImVec2(x + 339.0f - tw - 9.0f, iy + 20.5f), 3.0f, k31Blue);
            DrawTextRun(dl, nx, TextMid(iy + 20.5f, T_S12B, loc),
                        ClipToWidth(R31RowName(r), x + 339.0f - tw - 8.0f - nx, loc, T_S12B), loc,
                        k31Fg, T_S12B);
        }
        {
            float sx = R31Mid(dl, nx, iy + 41.0f, R31Agent(r.agent_id, loc), loc, k31Muted, T_S10);
            sx += 5.0f;
            sx += R31RowTag(dl, sx, iy + 41.0f, r.agent_id, loc) + 5.0f;
            sx += R31Mid(dl, sx, iy + 41.0f, RFmt(T("r_turn", loc), r.turns), loc, k31Muted, T_S10);
            if (!r.children.empty()) {                     // 副行右端挂「⌄ N 条子对话」
                const std::string lab = RFmt(T("r_children", loc), (int)r.children.size());
                const float cw2 = 20.0f + TextWidth(lab, loc, T_S9);
                R31SubChip(dl, x + 339.0f - cw2, iy + 41.0f, r, 1860 + (int)i, loc);
            }
        }
        I31(dl, I31_LAUNCH, x + 346.0f, iy + 25.0f, 12.0f, k31Muted);
        {
            const float ibx = x + 366.0f, iby = iy + 16.5f;
            const float ihov = TxPeekNew(Zid(loc, 1700 + (int)i, HIT_INFO31, 0), 0.0f);
            if (ihov > 0.01f)
                dl->AddRectFilled(ImVec2(ibx, iby), ImVec2(ibx + 29.0f, iby + 29.0f), k31Sel, 7.0f);
            I31(dl, I31_INFO, ibx + 5.0f, iby + 5.0f, 19.0f, ihov > 0.5f ? k31Fg : k31Faint);
            AddHit(g_hits, ibx, iby, ibx + 29.0f, iby + 29.0f, HIT_INFO31, 1700 + (int)i, 0, r, loc,
                   T("r_details", loc));
        }
        if (!r.children.empty() && g_r31_subopen.count(r.key))
            cy2 = R31SubList(dl, x + 60.0f, cy2 + 3.0f, 338.0f, r, 1860 + (int)i, loc);
    }
    if (more) {
        const std::string lab = RFmt(T("more", loc), (int)done.size() - shown);
        const float ly2 = cy2 + 4.0f;
        R31Mid(dl, x + w * 0.5f - TextWidth(lab, loc, T_S11) * 0.5f, ly2 + 16.0f, lab, loc,
               k31Muted, T_S11);
        I31(dl, I31_DOWN, x + w * 0.5f + TextWidth(lab, loc, T_S11) * 0.5f + 8.0f, ly2 + 10.5f,
            11.0f, k31Faint);
        AddHit(g_hits, listX + 6.0f, ly2, listX + listW - 6.0f, ly2 + 36.0f, HIT_H31MORE,
               1201, 0, loc, lab, 0);
    }
    return cy + listH;
}

// ── 一屏总装 ─────────────────────────────────────────────────────────────────
/// 返回面板高（固定 850）。签名与 R20Draw 对齐，body 账本同样回写。
static float R31Draw(ImDrawList* dl, float x, float y, float w, float h, int loc, int view,
                     double tn) {
    float panelOut = h;      // 本屏内容高（收放目标）；各屏覆写，返回给 drawPanel 驱动窗口
    R31Frame(dl, x, y, w, h, tn);
    R31Head(dl, x, y, w, loc, view);
    if (view == 0 || view == 6) {
        R31Sources(dl, x, y, w, loc);
        R31Tabs(dl, x, y + 117.0f, w, loc);
    }
    if (view == 0) {
        const float queueTop = R31OverviewFixed(dl, x, y, w, loc, tn);
        // 面板收放：队列短就收成「固定段＋队列＋页脚」；长了封顶当前窗口高、内部滚动。
        const float queueH = R31QueueH(loc);
        // 底部要留 57（页脚）+ 4（队列下垫）＝61；这里若多减 4，滚动档的
        // panelOut 就恒等于「当前高 − 4」，窗口每帧收一寸，六语并排一轮能掉 20pt，
        // 空态那颗按钮就被页脚越压越低（实测 184/180/176/172/168/164）。
        const float avail = std::fmax(120.0f, (y + h) - queueTop - 57.0f - 4.0f);
        const float vis = std::fmin(queueH, avail);
        const float qBot = queueTop + vis + 4.0f;
        g_bodyTop[view] = ImVec2(x + 16.0f, queueTop);
        g_bodyBot[view] = ImVec2(x + w - 16.0f, qBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 16.0f, queueTop), ImVec2(x + w - 16.0f, qBot), true);
        const float startY = queueTop - g_scroll[view];
        const float endY = R31Queue(dl, x, startY, w, loc);
        dl->PopClipRect();
        g_contentH[view] = endY - startY;
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (qBot - 4.0f - queueTop));
        RCullHits(hits0, x + 16.0f, queueTop, x + w - 16.0f, qBot);
        R31FootRoot(dl, x, qBot, w, loc);
        panelOut = qBot + 57.0f - y;
    } else if (view == 6) {
        const float bodyTop = y + 162.0f, bodyBot = y + h - 41.0f;
        g_bodyTop[view] = ImVec2(x + 16.0f, bodyTop);
        g_bodyBot[view] = ImVec2(x + w - 16.0f, bodyBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 16.0f, bodyTop), ImVec2(x + w - 16.0f, bodyBot), true);
        const float startY = bodyTop - g_scroll[view];
        const float endY = R31Usage(dl, x, startY, w, loc);
        dl->PopClipRect();
        g_contentH[view] = endY - startY;
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
        RCullHits(hits0, x + 16.0f, bodyTop, x + w - 16.0f, bodyBot);
        R31UsageFoot(dl, x, y + h - 41.0f, w, loc);
        panelOut = std::fmin(endY - startY + 41.0f + 4.0f + (bodyTop - y), (float)(y + h) - y);
    } else if (view == 7) {
        const float bodyTop = y + 54.0f, bodyBot = y + h - 61.0f;
        g_bodyTop[view] = ImVec2(x + 20.0f, bodyTop);
        g_bodyBot[view] = ImVec2(x + w - 20.0f, bodyBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 20.0f, bodyTop), ImVec2(x + w - 20.0f, bodyBot), true);
        const float startY = bodyTop - g_scroll[view];
        const float endY = R31Account(dl, x, startY, w, loc);
        dl->PopClipRect();
        g_contentH[view] = endY - startY;
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
        RCullHits(hits0, x + 20.0f, bodyTop, x + w - 20.0f, bodyBot);
        R31AccountFoot(dl, x, y + h - 61.0f, w, loc);
        panelOut = std::fmin(endY - startY + 61.0f + 4.0f + (bodyTop - y), (float)(y + h) - y);
    } else if (view == 8) {
        R31ToolsFixed(dl, x, y, w, loc, tn);
        const float bodyTop = y + 166.0f, bodyBot = y + h;
        g_bodyTop[view] = ImVec2(x + 16.0f, bodyTop);
        g_bodyBot[view] = ImVec2(x + w - 16.0f, bodyBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 16.0f, bodyTop), ImVec2(x + w - 16.0f, bodyBot), true);
        const float startY = bodyTop - g_scroll[view];
        const float endY = R31ToolsList(dl, x, startY, w, loc);
        dl->PopClipRect();
        g_contentH[view] = endY - startY;
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
        RCullHits(hits0, x + 16.0f, bodyTop, x + w - 16.0f, bodyBot);
        panelOut = std::fmin(endY - startY + 16.0f + (bodyTop - y), (float)(y + h) - y);
    } else if (view == 3) {
        const UIRow* r = SelectedRow(g_sel_key);
        R31DetailHead(dl, x, y, w, loc, r);
        const float bodyTop = y + 198.0f, bodyBot = y + 748.0f;
        g_bodyTop[view] = ImVec2(x + 22.0f, bodyTop);
        g_bodyBot[view] = ImVec2(x + w - 22.0f, bodyBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 22.0f, bodyTop), ImVec2(x + w - 22.0f, bodyBot), true);
        float endY = bodyTop;
        if (r) {
            const float startY = bodyTop - g_scroll[view];
            endY = R31DetailBody(dl, x, startY, w, loc, *r);
            g_contentH[view] = endY - startY;
        } else {
            R31Empty(dl, x + 22.0f, bodyTop + 20.0f, w - 44.0f, 180.0f, T("r_no_sessions", loc),
                     T("r_no_sessions_hint", loc), nullptr, 0, loc);
            g_contentH[view] = 200.0f;
        }
        dl->PopClipRect();
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
        RCullHits(hits0, x + 22.0f, bodyTop, x + w - 22.0f, bodyBot);
        R31DetailFoot(dl, x, y, w, loc, r);
    } else if (view == 5) {
        R31HistoryHead(dl, x, y, w, loc);
        const float bodyTop = y + 110.0f, bodyBot = y + h;
        g_bodyTop[view] = ImVec2(x + 16.0f, bodyTop);
        g_bodyBot[view] = ImVec2(x + w - 16.0f, bodyBot);
        g_bodyValid[view] = true;
        if (g_scroll[view] > g_scrollMax[view]) g_scroll[view] = g_scrollMax[view];
        const size_t hits0 = g_hits.size();
        dl->PushClipRect(ImVec2(x + 16.0f, bodyTop), ImVec2(x + w - 16.0f, bodyBot), true);
        const float startY = bodyTop - g_scroll[view];
        const float endY = R31HistoryList(dl, x, startY, w, loc);
        dl->PopClipRect();
        g_contentH[view] = endY - startY;
        g_scrollMax[view] = std::fmax(0.0f, g_contentH[view] - (bodyBot - bodyTop));
        RCullHits(hits0, x + 16.0f, bodyTop, x + w - 16.0f, bodyBot);
        panelOut = std::fmin(endY - startY + 12.0f + (bodyTop - y), (float)(y + h) - y);
        // 懒加载：滚到贴近底部（或列表本身就不够一屏滚）就自动续一页，下一帧生效
        {
            int doneTotal = 0;
            for (const auto* r : R31Rows(g_r31_src, g_r31_query))
                if (GroupOf(r->state) == G_DONE) doneTotal++;
            if (g_r31_hist_n < doneTotal &&
                (g_scrollMax[view] < 40.0f || g_scroll[view] >= g_scrollMax[view] - 60.0f))
                g_r31_hist_n += 12;
        }
    }
    // 回执钉在**看得见**的那一屏底沿：内容超过窗口时 panelOut 是整块内容的高，
    // 照它摆会把回执画到窗口外——点了行、回执其实出了，只是他一个字也看不见
    // （2026-10-29 实拍：四路会话同时在跑，固定段 935pt > 窗口 850pt，回执落在 848pt 以下）。
    RToast(dl, x, y, w, y + std::fmin(panelOut, (float)h), loc, tn);
    return panelOut;
}

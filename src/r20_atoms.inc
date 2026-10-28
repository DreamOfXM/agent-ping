// ─────────────────────────────────────────────────────────────────────────────
// r20 原子层：图标、品牌标、开关、发丝线、多色折行。
//
// 这一层只按 agent-notify/r20/interaction.src.html 的 CSS 抄，不含任何一屏的版式。
// 每个常量后面都写着它对应的那一条声明；改样式先改稿子，再回来改这里，别反过来。
// 图标是稿子里那 18 个 16-viewBox / 1.5 描边的 SVG，路径数据逐字搬过来由下面的
// 迷你路径解析器现场拍平成折线 —— ImGui 没有 SVG，也没有圆头线帽和虚线，这两样
// 都得自己补（见 DrawIcon 与 DashRoundRect）。
// ─────────────────────────────────────────────────────────────────────────────
#include "brand_icon.h"
#include "agent_icons.h"
#include "agent_icons31.h"

// ImGui 只在 imgui_internal.h 里给 IM_PI；这一层不引内部头，自己留一份同值常量。
static const float kRPi = 3.14159265358979323846f;

// ── 几何：全部来自 r20 的 CSS 声明 ───────────────────────────────────────────
static const float kRPaneBorder = 1.0f;      // .panel{border:1px solid #262C34}
static const float kRHeadH     = 42.0f;      // .head{height:42px}（含 1px border-bottom）
static const float kRTabsH     = 44.0f;      // .tabs{height:44px}（含 1px border-top）
static const float kRDHeadH    = 40.0f;      // .dhead{height:40px}（含 1px border-bottom）
static const float kRInnerTop  = 4.0f;       // .inner{padding:4px 0 14px}
static const float kRInnerBot  = 14.0f;
static const float kRPadX      = 14.0f;      // .rows/.live/.rail/.setg 的左右 14
static const float kRInX       = 2.0f;       // .qrow/.prow/.srow 再加的左右 2
static const float kRSetupX    = 16.0f;      // .setup{padding:14px 16px 18px}
static const float kRSetupTop  = 14.0f;
static const float kRSetupBot  = 18.0f;
static const float kRDBodyTop  = 12.0f;      // .dbody{padding:12px 14px 16px}
static const float kRDBodyBot  = 16.0f;
static const float kRFadeH     = 20.0f;      // .fade{height:20px}

// 稿子里只在这里出现一次的色：逐条照抄，别拿邻近的 token 顶替。
static const ImU32 kRPanelLine = IM_COL32(38, 44, 52, 255);     // #262C34
static const ImU32 kRInset     = IM_COL32(255, 255, 255, 15);   // inset 0 1px 0 rgba(255,255,255,.06)
static const ImU32 kRToastBg   = IM_COL32(27, 33, 42, 255);     // .toast{background:#1B212A}
static const ImU32 kRGoHover   = IM_COL32(27, 33, 42, 255);     // .gobtn:hover{background:#1B212A}
static const ImU32 kRSwOffBg   = IM_COL32(42, 49, 58, 255);     // .sw{background:#2A313A}
static const ImU32 kRSwOffKnob = IM_COL32(138, 147, 157, 255);  // .sw::after{background:#8A939D}
static const ImU32 kRAmberDim  = IM_COL32(138, 95, 30, 255);    // --amber-d #8A5F1E

// ── 1px 发丝线：一律用「整数边界的实心矩形」而不是 AddLine ────────────────────
// AddLine 在 1.0pt 时按端点做抗锯齿，落在半像素上会摊成两条 0.5pt 的灰线；
// CSS 的 1px border 是硬的。
static void RHair(ImDrawList* dl, float x0, float y, float x1, ImU32 col) {
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1.0f), col);
}
static void RHairV(ImDrawList* dl, float x, float y0, float y1, ImU32 col) {
    dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1.0f, y1), col);
}

// ── SVG 迷你解析器 ──────────────────────────────────────────────────────────
// 只覆盖稿子里用到的命令：M L H V C S A Z（大小写都收）。曲线按 12 段拍平，
// 弧按每 15° 一段 —— 14pt 的图标上这些段数早就超过肉眼分辨率。
struct RPoly { std::vector<ImVec2> p; bool closed = false; };
struct RIconDef { std::vector<RPoly> polys; };

static bool RIsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static void RSkipSep(const char*& p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == ',') { p++; continue; }
        break;
    }
}
/// 吃一个数。SVG 允许 "2.6.9" 与 "-1.6-1.6" 这种不带分隔符的连写，
/// 所以数到哪儿停由符号/小数点自己决定，不靠逗号。
static bool RNum(const char*& p, float& out) {
    const char* s = p;
    if (*s == '+' || *s == '-') s++;
    bool dig = false;
    while (*s >= '0' && *s <= '9') { s++; dig = true; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { s++; dig = true; } }
    if (!dig) return false;
    out = (float)atof(std::string(p, (size_t)(s - p)).c_str());
    p = s;
    return true;
}
static void RCubic(std::vector<ImVec2>& o, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d) {
    const int kN = 12;
    for (int i = 1; i <= kN; i++) {
        const float t = (float)i / kN, u = 1.0f - t;
        o.push_back(ImVec2(u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                           u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y));
    }
}
/// SVG 端点式弧 → 圆心式，再按角度采样。rx/ry 太小时按规范整体放大到刚好装下。
static void RArc(std::vector<ImVec2>& o, ImVec2 p0, float rx, float ry, float phi,
                 bool large, bool sweep, ImVec2 p1) {
    rx = std::fabs(rx); ry = std::fabs(ry);
    if (rx < 1e-6f || ry < 1e-6f) { o.push_back(p1); return; }
    const float cp = std::cos(phi), sp = std::sin(phi);
    const float dx = (p0.x - p1.x) * 0.5f, dy = (p0.y - p1.y) * 0.5f;
    const float x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    const float lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lam > 1.0f) { const float s = std::sqrt(lam); rx *= s; ry *= s; }
    const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const float k = den <= 1e-9f ? 0.0f
                    : std::sqrt(std::fmax(0.0f, num / den)) * ((large != sweep) ? 1.0f : -1.0f);
    const float cxp = k * rx * y1 / ry, cyp = -k * ry * x1 / rx;
    const float cx = cp * cxp - sp * cyp + (p0.x + p1.x) * 0.5f;
    const float cy = sp * cxp + cp * cyp + (p0.y + p1.y) * 0.5f;
    const float th1 = std::atan2((y1 - cyp) / ry, (x1 - cxp) / rx);
    const float th2 = std::atan2((-y1 - cyp) / ry, (-x1 - cxp) / rx);
    float dth = th2 - th1;
    if (!sweep && dth > 0) dth -= 2.0f * kRPi;
    if (sweep && dth < 0) dth += 2.0f * kRPi;
    const int n = (int)(std::fabs(dth) / (kRPi / 12.0f)) + 1;
    for (int i = 1; i <= n; i++) {
        const float th = th1 + dth * (float)i / n;
        o.push_back(ImVec2(cx + rx * std::cos(th) * cp - ry * std::sin(th) * sp,
                           cy + rx * std::cos(th) * sp + ry * std::sin(th) * cp));
    }
}
static void RParseD(const char* d, std::vector<RPoly>& out) {
    const char* p = d;
    RPoly* cur = nullptr;
    ImVec2 cp(0, 0), sub(0, 0), lastCtrl(0, 0);
    bool haveCtrl = false;
    char act = 0;
    auto pt = [&](float x, float y) {
        if (!cur) { out.push_back(RPoly()); cur = &out.back(); cur->p.push_back(cp); }
        cur->p.push_back(ImVec2(x, y));
    };
    while (*p) {
        RSkipSep(p);
        if (!*p) break;
        if (RIsAlpha(*p)) act = *p++;
        if (!act) break;
        const bool rel = (act >= 'a');
        const char C = rel ? (char)(act - 32) : act;
        if (C == 'Z' || C == 'z') {
            if (cur) cur->closed = true;
            cur = nullptr;
            cp = sub;
            haveCtrl = false;
            act = 0;
            continue;
        }
        float a[7] = {0};
        int need = (C == 'M' || C == 'L') ? 2 : (C == 'H' || C == 'V') ? 1
                 : (C == 'C') ? 6 : (C == 'S') ? 4 : (C == 'A') ? 7 : -1;
        if (need < 0) return;
        for (int i = 0; i < need; i++) {
            RSkipSep(p);
            if (!RNum(p, a[i])) return;
        }
        const float ox = rel ? cp.x : 0.0f, oy = rel ? cp.y : 0.0f;
        if (C == 'M') {
            cp = ImVec2(ox + a[0], oy + a[1]);
            sub = cp;
            cur = nullptr;
            pt(cp.x, cp.y);
            haveCtrl = false;
            act = rel ? 'l' : 'L';        // 紧跟的坐标对按 lineto 解释
            continue;
        }
        if (C == 'L') {
            cp = ImVec2(ox + a[0], oy + a[1]);
            pt(cp.x, cp.y);
            haveCtrl = false;
        } else if (C == 'H') {
            cp = ImVec2(ox + a[0], cp.y);
            pt(cp.x, cp.y);
            haveCtrl = false;
        } else if (C == 'V') {
            cp = ImVec2(cp.x, oy + a[0]);
            pt(cp.x, cp.y);
            haveCtrl = false;
        } else if (C == 'C') {
            const ImVec2 c1(ox + a[0], oy + a[1]), c2(ox + a[2], oy + a[3]), e(ox + a[4], oy + a[5]);
            if (!cur) pt(cp.x, cp.y);
            RCubic(cur->p, cp, c1, c2, e);
            lastCtrl = c2;
            haveCtrl = true;
            cp = e;
        } else if (C == 'S') {
            const ImVec2 c1 = haveCtrl ? ImVec2(2 * cp.x - lastCtrl.x, 2 * cp.y - lastCtrl.y) : cp;
            const ImVec2 c2(ox + a[0], oy + a[1]), e(ox + a[2], oy + a[3]);
            if (!cur) pt(cp.x, cp.y);
            RCubic(cur->p, cp, c1, c2, e);
            lastCtrl = c2;
            haveCtrl = true;
            cp = e;
        } else if (C == 'A') {
            const ImVec2 e(ox + a[5], oy + a[6]);
            if (!cur) pt(cp.x, cp.y);
            RArc(cur->p, cp, a[0], a[1], a[2] * (float)(kRPi / 180.0), a[3] != 0, a[4] != 0, e);
            cp = e;
            haveCtrl = false;
        }
    }
}

// ── 18 个图标：路径数据逐字搬自 r20 的 ICON 表 ──────────────────────────────
enum RIconId { RI_LIST, RI_CLOCK, RI_SLIDERS, RI_COPY, RI_JUMP, RI_BACK, RI_CHEV, RI_TERM,
               RI_FILE, RI_CHECK, RI_BELL, RI_BELLOFF, RI_PLUG, RI_SHIELD, RI_INFO,
               RI_LAUNCH, RI_GLOBE, RI_EYE, RI_N };
/// kind 0＝path（d）；1＝circle（a,b,c = cx,cy,r）；2＝rect（a,b,c,e,f = x,y,w,h,rx）
struct RShapeSpec { int kind; const char* d; float a, b, c, e, f; };
static const RShapeSpec kRShape[RI_N][4] = {
    /* list    */ {{0, "M2.5 4h11M2.5 8h11M2.5 12h7"}},
    /* clock   */ {{1, nullptr, 8, 8, 5.6f, 0}, {0, "M8 4.9V8l2.1 1.6"}},
    /* sliders */ {{0, "M2.5 4.5h7M12.5 4.5h1M2.5 11.5h1M6.5 11.5h7"},
                   {1, nullptr, 11, 4.5f, 1.6f, 0}, {1, nullptr, 5, 11.5f, 1.6f, 0}},
    /* copy    */ {{2, nullptr, 5.4f, 5.4f, 8, 8, 1.6f},
                   {0, "M10.6 5.4V4a1.6 1.6 0 0 0-1.6-1.6H4A1.6 1.6 0 0 0 2.4 4v5a1.6 1.6 0 0 0 1.6 1.6h1.4"}},
    /* jump    */ {{0, "M6 4H4a1.6 1.6 0 0 0-1.6 1.6V12A1.6 1.6 0 0 0 4 13.6h6.4A1.6 1.6 0 0 0 12 12v-2"},
                   {0, "M9.4 2.6h4v4M13.2 2.8 7.6 8.4"}},
    /* back    */ {{0, "M9.8 3.4 5.2 8l4.6 4.6"}},
    /* chev    */ {{0, "M6.2 3.8 10.4 8l-4.2 4.2"}},
    /* term    */ {{2, nullptr, 1.8f, 2.8f, 12.4f, 10.4f, 1.8f}, {0, "M4.4 6.2 6.6 8l-2.2 1.8M8.4 10.2h3.2"}},
    /* file    */ {{0, "M9 2.4H4.6A1.6 1.6 0 0 0 3 4v8a1.6 1.6 0 0 0 1.6 1.6h6.8A1.6 1.6 0 0 0 13 12V6.4Z"},
                   {0, "M9 2.4V6.4h4"}},
    /* check   */ {{0, "M3.4 8.4 6.6 11.4 12.6 4.8"}},
    /* bell    */ {{0, "M8 2.6a3.6 3.6 0 0 1 3.6 3.6c0 2.6.9 3.6 1.4 4.2H3c.5-.6 1.4-1.6 1.4-4.2A3.6 3.6 0 0 1 8 2.6Z"},
                   {0, "M6.6 12.8a1.5 1.5 0 0 0 2.8 0"}},
    /* bellOff */ {{0, "M5 3.6a3.6 3.6 0 0 1 6.6 2.6c0 2.6.9 3.6 1.4 4.2H6.4M4.2 6.2c0 2.6-.8 3.6-1.2 4.2"},
                   {0, "M6.6 12.8a1.5 1.5 0 0 0 2.8 0M2.6 2.6l10.8 10.8"}},
    /* plug    */ {{0, "M6 2.6v3M10 2.6v3M4.4 5.6h7.2v2.2a3.6 3.6 0 0 1-7.2 0Z"}, {0, "M8 11.4v2"}},
    /* shield  */ {{0, "M8 2.2 3.4 4v4c0 3 2 4.9 4.6 5.8 2.6-.9 4.6-2.8 4.6-5.8V4Z"},
                   {0, "M6.2 8l1.4 1.4 2.4-2.6"}},
    /* info    */ {{1, nullptr, 8, 8, 5.6f, 0}, {0, "M8 7.2v3.4M8 5.1v.1"}},
    /* launch  */ {{0, "M8 2.8v6.4M5.4 5.2 8 2.6l2.6 2.6"},
                   {0, "M3.4 9.6v2.6A1.6 1.6 0 0 0 5 13.8h6a1.6 1.6 0 0 0 1.6-1.6V9.6"}},
    /* globe   */ {{1, nullptr, 8, 8, 5.6f, 0},
                   {0, "M2.4 8h11.2M8 2.4c-1.6 1.6-2.4 3.5-2.4 5.6s.8 4 2.4 5.6c1.6-1.6 2.4-3.5 2.4-5.6S9.6 4 8 2.4Z"}},
    /* eye     */ {{0, "M1.8 8s2.3-3.8 6.2-3.8S14.2 8 14.2 8 11.9 11.8 8 11.8 1.8 8 1.8 8Z"},
                   {1, nullptr, 8, 8, 1.7f, 0}},
};
static std::vector<RIconDef> g_ricon;
static void RBuildIcons() {
    if (!g_ricon.empty()) return;
    g_ricon.resize(RI_N);
    for (int i = 0; i < RI_N; i++) {
        for (int k = 0; k < 4; k++) {
            const RShapeSpec& s = kRShape[i][k];
            if (s.kind == 0 && s.d) {
                std::vector<RPoly> ps;
                RParseD(s.d, ps);
                for (auto& p : ps) g_ricon[i].polys.push_back(std::move(p));
            } else if (s.kind == 1) {
                RPoly p;
                for (int n = 0; n <= 24; n++) {
                    const float a = -kRPi * 0.5f + 2 * kRPi * (float)n / 24.0f;
                    p.p.push_back(ImVec2(s.a + std::cos(a) * s.c, s.b + std::sin(a) * s.c));
                }
                p.closed = true;
                g_ricon[i].polys.push_back(std::move(p));
            } else if (s.kind == 2) {
                RPoly p;
                const float x = s.a, y = s.b, w = s.c, h = s.e, r = s.f;
                const ImVec2 c[4] = {ImVec2(x + w - r, y + r), ImVec2(x + w - r, y + h - r),
                                     ImVec2(x + r, y + h - r), ImVec2(x + r, y + r)};
                const float a0[4] = {-kRPi * 0.5f, 0.0f, kRPi * 0.5f, kRPi};
                for (int k2 = 0; k2 < 4; k2++)
                    for (int n = 0; n <= 6; n++) {
                        const float a = a0[k2] + kRPi * 0.5f * (float)n / 6.0f;
                        p.p.push_back(ImVec2(c[k2].x + std::cos(a) * r, c[k2].y + std::sin(a) * r));
                    }
                p.closed = true;
                g_ricon[i].polys.push_back(std::move(p));
            }
        }
    }
}
/// 按稿子的 `width/height=14 viewBox=16 stroke-width=1.5 linecap=round` 画一枚图标。
/// ImGui 的 AddPolyline 没有圆头线帽，开放子路径的两端点各补一颗半径＝线宽一半的实心圆。
static void DrawIcon(ImDrawList* dl, int id, float x, float y, float size, ImU32 col) {
    if (id < 0 || id >= RI_N) return;
    RBuildIcons();
    const float k = size / 16.0f;
    const float th = 1.5f * k;
    static std::vector<ImVec2> tmp;
    for (const auto& poly : g_ricon[id].polys) {
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

// ── 品牌标：稿子的 <img class="mark"> / <img> 用的是同一张 1024 图标 ──────────
// 本工程没有 PNG 解码器，所以两张位图（18pt 用的 36px、34pt 用的 68px）在
// brand_icon.h 里以裸 RGBA 十六进制存着，启动后上传成 GL 纹理。
static ImU64 RMakeTex(int n, const char* hex) {
    std::vector<unsigned char> px((size_t)n * (size_t)n * 4);
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i < px.size(); i++) px[i] = (unsigned char)(hv(hex[i * 2]) * 16 + hv(hex[i * 2 + 1]));
    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    return (ImU64)id;
}
/// 建好 GL 上下文之后再调（main 里 ImGui_ImplOpenGL3_Init 之后一行）。
static void BrandUpload() {
    if (g_brandTex36) return;
    g_brandTex36 = RMakeTex(kBrandN36, kBrandHex36);
    g_brandTex68 = RMakeTex(kBrandN68, kBrandHex68);
    g_agentTexQoder = RMakeTex(kAgentN64, kAgentHexQoder);
    g_agentTexClaude = RMakeTex(kAgentN64, kAgentHexClaude);
    g_agent31TexZCode = RMakeTex(kAgent31N, kAgent31HexZCode31);
    g_agent31TexWorkBuddy = RMakeTex(kAgent31N, kAgent31HexWorkBuddy31);
    g_agent31TexOpenCode = RMakeTex(kAgent31N, kAgent31HexOpenCode31);
}
static void BrandMark(ImDrawList* dl, float x, float y, float s) {
    const ImU64 tex = (s > 24.0f) ? g_brandTex68 : g_brandTex36;
    if (!tex) {   // 纹理没上来时画个同尺寸的空位，不要画成别的形状骗人
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s), WithA(kT5, 60), s * 0.235f);
        return;
    }
    dl->AddImage((ImTextureID)tex, ImVec2(x, y), ImVec2(x + s, y + s), ImVec2(0, 0), ImVec2(1, 1));
}

// ── 代理头像：稿子 `.ag img` / `.m1 img` 贴的是各家 logo ──────────────────────
// Qoder 的白底圆角方块与 Claude 的珊瑚色星芒都是官方像素（agent_icons.h，64px
// RGBA 上传成纹理）；没有位图的代理退回「同尺寸圆角块 + 首字母 + 该代理的色相」。
static void RAgentTile(ImDrawList* dl, float x, float y, float s, float r, ImU32 hue,
                       const std::string& initial, int loc, const std::string& agent = std::string()) {
    const ImU64 tex = (agent == "qoder") ? g_agentTexQoder
                    : (agent == "claude") ? g_agentTexClaude
                    : (agent == "zcode") ? g_agent31TexZCode
                    : (agent == "workbuddy") ? g_agent31TexWorkBuddy
                    : (agent == "opencode") ? g_agent31TexOpenCode : 0;
    if (tex) {
        dl->AddImageRounded((ImTextureID)tex, ImVec2(x, y), ImVec2(x + s, y + s),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
        return;
    }
    GradRound(dl, x, y, s, s, r, WithA(hue, 60), WithA(hue, 16), WithA(hue, 120));
    DrawTextRun(dl, x + (s - TextWidth(initial, loc, T_GLYPH9)) * 0.5f, TextMid(y + s * 0.5f, T_GLYPH9, loc),
                initial, loc, hue, T_GLYPH9);
}

// ── 状态点：.dot + .pulse（1.6s 的 box-shadow 扩散）───────────────────────────
static void RDot(ImDrawList* dl, float cx, float cy, ImU32 col, double tn, bool pulse) {
    dl->AddCircleFilled(ImVec2(cx, cy), 5.0f, WithA(col, 36), 16);   // 静态 2px 光晕
    if (pulse) {
        const float p = (float)std::fmod(tn, 1.6) / 1.6f;
        const float e = 1.0f - (1.0f - p) * (1.0f - p);              // ease-out
        const int a = (int)(115.0f * (1.0f - e));
        if (a > 1) dl->AddCircleFilled(ImVec2(cx, cy), 3.0f + 6.0f * e, WithA(col, a), 20);
    }
    dl->AddCircleFilled(ImVec2(cx, cy), 3.0f, col, 14);
}

// ── 开关：.sw{32×19, radius 10} + .sw::after{13×13, top/left 2 → 15} ─────────
static void RSwitch(ImDrawList* dl, float x, float y, float t, bool on, bool amb) {
    const float w = 32.0f, h = 19.0f;
    // CSS：关＝#2A313A + line2 描边；开＝色相 30%/28% 底 + 50% 描边。
    const ImU32 bgOff = kRSwOffBg;
    // 开关的通用语言：开＝亮色实心轨 + 白滑块，关＝深灰轨 + 灰滑块（iOS 式）。
    // 之前用半透明 tint 叠暗轨，「开」看起来和「关」一样，用户没法建立因果（实测两轮）。
    const ImU32 bgOn = amb ? kAmber : kGreen;
    const ImU32 fill = MixA(bgOff, bgOn, t);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, 10.0f);
    const float knobX = x + 2.0f + 13.0f * t;
    const ImU32 knob = MixA(kRSwOffKnob, IM_COL32(252, 252, 252, 255), t);
    dl->AddCircleFilled(ImVec2(knobX + 6.5f, y + 2.0f + 6.5f), 6.5f, knob, 20);
}

// ── 虚线圆角框：.waitline{border:1px dashed var(--line2)} ────────────────────
// ImGui 没有虚线，按周长走「3 亮 3 灭」——稿子 dashed 在 1px 上就是这个节奏。
static void DashRoundRect(ImDrawList* dl, float x, float y, float w, float h, float r,
                          ImU32 col, float on = 3.0f, float off = 3.0f) {
    std::vector<ImVec2> pts;
    const float rr = std::fmin(r, std::fmin(w, h) * 0.5f);
    const ImVec2 c[4] = {ImVec2(x + w - rr, y + rr), ImVec2(x + w - rr, y + h - rr),
                         ImVec2(x + rr, y + h - rr), ImVec2(x + rr, y + rr)};
    const float a0[4] = {-kRPi * 0.5f, 0.0f, kRPi * 0.5f, kRPi};
    for (int k = 0; k < 4; k++)
        for (int n = 0; n <= 4; n++) {
            const float a = a0[k] + kRPi * 0.5f * (float)n / 4.0f;
            pts.push_back(ImVec2(c[k].x + std::cos(a) * rr, c[k].y + std::sin(a) * rr));
        }
    float acc = 0.0f;
    bool draw = true;
    const int n = (int)pts.size();
    for (int i = 0; i < n; i++) {
        const ImVec2 a = pts[i], b = pts[(i + 1) % n];
        const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        if (len < 1e-4f) continue;
        float t = 0.0f;
        while (t < len - 1e-4f) {
            const float need = (draw ? on : off) - acc;
            const float step = std::fmin(need, len - t);
            if (draw && step > 0.01f) {
                const float t0 = t / len, t1 = (t + step) / len;
                dl->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                            ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1), col, 1.0f);
            }
            t += step;
            acc += step;
            if (step >= need - 1e-4f) { acc = 0.0f; draw = !draw; }
        }
    }
}

// ── 多色折行：稿子的 `.stepnote` 里夹 <b>，那截要换色换字重 ──────────────────
// 按「CJK 一字一原子、拉丁一词一原子」切开，贪心排满一行；同一色的相邻原子
// 合并成一次 DrawTextRun，颜色边界不会被拆成两个字形基线。
struct RRunIn { std::string text; ImU32 col; };
struct RAtom { std::string s; int col; float w; bool sp; };
static std::vector<RAtom> RAtomize(const std::vector<RRunIn>& runs, int loc, int tier) {
    std::vector<RAtom> out;
    for (int ri = 0; ri < (int)runs.size(); ri++) {
        const std::string& s = runs[ri].text;
        size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = (unsigned char)s[i];
            const int l1 = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
            const unsigned int cp = DecodeUTF8(s.substr(i, (size_t)l1).c_str())[0];
            size_t j = i + (size_t)l1;
            if (!IsCjkCp(cp) && cp != ' ') {
                while (j < s.size()) {
                    const unsigned char d = (unsigned char)s[j];
                    const int l2 = (d >= 0xF0) ? 4 : (d >= 0xE0) ? 3 : (d >= 0xC0) ? 2 : 1;
                    const unsigned int cp2 = DecodeUTF8(s.substr(j, (size_t)l2).c_str())[0];
                    if (IsCjkCp(cp2) || cp2 == ' ') break;
                    j += (size_t)l2;
                }
            }
            RAtom a;
            a.s = s.substr(i, j - i);
            a.col = ri;
            a.w = TextWidth(a.s, loc, tier);
            a.sp = (cp == ' ');
            out.push_back(a);
            i = j;
        }
    }
    return out;
}
/// 返回总高。maxlines>0 时最后一行超宽就补省略号（与 CSS 的 -webkit-line-clamp 同一效果）。
static float DrawRuns(ImDrawList* dl, float L, float R, float y, const std::vector<RRunIn>& runs,
                      int loc, int tier, float lineH, int maxlines = 0) {
    const std::vector<RAtom> atoms = RAtomize(runs, loc, tier);
    const float maxw = R - L;
    std::vector<std::vector<int>> lines(1);
    float cur = 0.0f;
    bool wrapped = false;
    for (int i = 0; i < (int)atoms.size(); i++) {
        const RAtom& a = atoms[i];
        if (a.sp && lines.back().empty()) continue;                 // 行首空格不占位
        if (cur + a.w > maxw && !lines.back().empty()) {
            if (maxlines > 0 && (int)lines.size() >= maxlines) { wrapped = true; break; }
            lines.emplace_back();
            cur = 0.0f;
            if (a.sp) continue;
        }
        lines.back().push_back(i);
        cur += a.w;
    }
    float yy = y;
    for (int li = 0; li < (int)lines.size(); li++) {
        float x = L;
        int prevCol = -1;
        std::string buf;
        auto flush = [&]() {
            if (buf.empty()) return;
            DrawTextRun(dl, x, TextTop(yy, lineH, tier, loc), buf, loc, runs[prevCol].col, tier);
            x += TextWidth(buf, loc, tier);
            buf.clear();
        };
        for (int ai : lines[li]) {
            const RAtom& a = atoms[ai];
            if (a.col != prevCol) { flush(); prevCol = a.col; }
            buf += a.s;
        }
        flush();
        if (wrapped && li == (int)lines.size() - 1) {
            DrawTextRun(dl, x, TextTop(yy, lineH, tier, loc), "…", loc,
                        runs[prevCol < 0 ? 0 : prevCol].col, tier);
        }
        yy += lineH;
    }
    return (yy - y);
}

// ── 节头：.slabel{display:flex;align-items:center;gap:7px;padding:…} ─────────
// count<0 表示这一屏的节头没有计数胶囊（设置屏那三节就是）。
static float RSLabel(ImDrawList* dl, float L, float R, float y, int loc, const char* txKey,
                     int count, bool hot, const std::string& rt, bool rtHot,
                     float padTop, float padBot) {
    const float track = kTierPx[T_10S] * 0.14f;      // letter-spacing:.14em
    const float cH = 14.0f;                          // .ct{line-height:14px}
    const float contentH = count >= 0 ? cH : kTierPx[T_10S];
    float x = L;
    const float ty = TextTop(y + padTop + (contentH - kTierPx[T_10S]) * 0.5f, kTierPx[T_10S], T_10S, loc);
    x = DrawTextRun(dl, x, ty, T(txKey, loc), loc, kT3, T_10S, track) + 7.0f;
    if (count >= 0) {
        char nb[16]; snprintf(nb, sizeof nb, "%d", count);
        const float tw = TextWidth(nb, loc, T_MLBL);
        const float bw = std::fmax(16.0f, tw + 8.0f);
        const float by = y + padTop + (contentH - cH) * 0.5f;
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw, by + cH),
                          hot ? WithA(kAmber, 33) : W(18), 4.0f);      // 13% / 7%
        DrawTextRun(dl, x + (bw - tw) * 0.5f, TextTop(by, cH, T_MLBL, loc), nb, loc,
                    hot ? kAmber : kT2, T_MLBL);
        x += bw + 7.0f;
    }
    if (!rt.empty()) {
        const float rw = TextWidth(rt, loc, T_FAIL);
        const float rx = R - rw;
        if (rx > x + 8.0f) {
            RHair(dl, x, y + padTop + contentH * 0.5f, rx - 7.0f, kLine);
            DrawTextRun(dl, rx, TextTop(y + padTop + (contentH - kTierPx[T_FAIL]) * 0.5f,
                                        kTierPx[T_FAIL], T_FAIL, loc),
                        rt, loc, rtHot ? kAmber : kT4, T_FAIL);
        }
    } else if (count < 0 && R > x + 8.0f) {
        RHair(dl, x, y + padTop + contentH * 0.5f, R, kLine);   // 设置屏的 .rule
    }
    return padTop + contentH + padBot;
}

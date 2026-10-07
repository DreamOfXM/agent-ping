#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// 六种语言的真实 UI 文案（状态档 / 卡片标题 / 按钮 / 一句长提示），取自现有 SwiftUI 面板。
// JSON 只是容器；这个探针量的是容器解决不了的三件事：缺字、图集体积、CJK 折行。
struct Row { const char* key; const char* t[6]; };  // 顺序固定 zh en de it ja ko

static const Row kRows[] = {
    {"state_working",    {"正在跑", "Running", "Läuft", "In esecuzione", "実行中", "실행 중"}},
    {"state_needs_user", {"等你回复", "Needs you", "Deine Antwort nötig", "Attende te", "応答待ち", "응답 대기"}},
    {"state_awaiting",   {"等你授权", "Awaiting permission", "Berechtigung nötig", "Autorizzazione required", "許可待ち", "권한 대기"}},
    {"state_queued",     {"排队中", "Queued", "In Warteschlange", "In coda", "キュー待ち", "대기열"}},
    {"state_failed",     {"异常中断", "Failed", "Abgebrochen", "Fallito", "異常終了", "오류 중단"}},
    {"state_resting",    {"已休息", "Idle", "Ruhend", "Inattivo", "休止中", "유휴"}},
    {"label_suspect",    {"疑似等你授权", "Possibly waiting", "Wartet möglicherweise", "In attesa forse", "おそらく待機", "대기 중일 수 있음"}},
    {"card_sessions",    {"今日会话", "Today's sessions", "Heutige Sitzungen", "Sessioni di oggi", "本日のセッション", "오늘 세션"}},
    {"card_present",     {"今日在场时间轴", "Present today", "Heute anwesend", "Presente oggi", "本日在席時間", "오늘 재석"}},
    {"card_cost",        {"今日消耗", "Today cost", "Heutige Kosten", "Costo di oggi", "本日の消費", "오늘 비용"}},
    {"btn_expand",       {"展开完整面板", "Expand full panel", "Volles Panel öffnen", "Espandi pannello", "パネルを展開", "패널 확장"}},
    {"btn_history",      {"历史记录", "History", "Verlauf", "Cronologia", "履歴", "이력"}},
    {"btn_settings",     {"设置", "Settings", "Einstellungen", "Impostazioni", "設定", "설정"}},
    // 长句：CJK 没有空格，专测 ImGui 的折行会不会把词切断
    {"long_hint",        {
        "点击任意一行就能跳到那条会话所在的编辑器窗口，如果目标应用没有对外接口就只把标题放进剪贴板",
        "Click any row to jump to that session in its editor; if the app exposes no interface, only the title goes to the clipboard.",
        "Klicke eine Zeile an, um zu dieser Sitzung im Editor zu springen; ohne Schnittstelle wird nur der Titel in die Zwischenablage gelegt.",
        "Clicca una riga per andare alla sessione nell'editor; se l'app non espone interfacce, il titolo va negli appunti.",
        "行をクリックするとそのセッションのエディタに移動します。インターフェースが無い場合はタイトルのみクリップボードにコピーされます。",
        "행을 클릭하면 해당 세션의 편집기로 이동합니다. 인터페이스가 없으면 제목만 클립보드에 복사됩니다."}},
};
static const int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));
static const char* kLocale[] = {"zh", "en", "de", "it", "ja", "ko"};
static const char* kFontFile[] = {"NotoSansSC.ttf", "NotoSans.ttf", "NotoSans.ttf",
                                  "NotoSans.ttf", "NotoSansJP.ttf", "NotoSansKR.ttf"};

static std::vector<unsigned int> DecodeUTF8(const char* s) {
    std::vector<unsigned int> out;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        unsigned int cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp &= 0x07; extra = 3; }
        else if (c >= 0xE0) { cp &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp &= 0x1F; extra = 1; }
        for (int i = 0; i < extra && *s; i++) cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
        out.push_back(cp);
    }
    return out;
}

// 把全部六语文本喂进 ranges，只栅格化字符串表里真正出现过的字
static void BuildRangesFromAll(ImFontAtlas* atlas, ImVector<ImWchar>* into, int field) {
    ImFontGlyphRangesBuilder rb;
    for (int r = 0; r < kRowCount; r++) rb.AddText(kRows[r].t[field]);
    rb.BuildRanges(into);
}


// 直接查字体文件 cmap：返回该字体缺哪些 codepoint。
static int FreeTypeMissing(FT_Library ft, const char* path,
                           const std::vector<unsigned int>& cps,
                           std::string* shown) {
    FT_Face face;
    if (FT_New_Face(ft, path, 0, &face)) { fprintf(stderr, "FreeType 打不开 %s\n", path); return -1; }
    int miss = 0;
    for (unsigned int cp : cps) {
        if (FT_Get_Char_Index(face, cp) == 0) {
            miss++;
            if (shown && shown->size() < 160) { char b[16]; snprintf(b, sizeof b, "U+%04X ", cp); *shown += b; }
        }
    }
    FT_Done_Face(face);
    return miss;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    const char* out = argc > 2 ? argv[2] : "i18n-probe.ppm";

    if (!glfwInit()) { fprintf(stderr, "glfwInit 失败\n"); return 2; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);       // 离屏渲染：不在用户桌面开窗
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    GLFWwindow* win = glfwCreateWindow(1200, 860, "probe", nullptr, nullptr);
    if (!win) { fprintf(stderr, "建窗失败（可能没有 GL 上下文）\n"); return 2; }
    glfwMakeContextCurrent(win);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontAtlas* atlas = io.Fonts;
    char path[1024];

    // ① 对照组：一份 NotoSansSC 通吃六语，逐 rune 查缺字
    {
        ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder rb;
        for (int r = 0; r < kRowCount; r++) for (int f = 0; f < 6; f++) rb.AddText(kRows[r].t[f]);
        rb.BuildRanges(&ranges);

        snprintf(path, sizeof path, "%s/NotoSansSC.ttf", dir);
        ImFont* solo = atlas->AddFontFromFileTTF(path, 16.0f, nullptr, ranges.Data);
        if (!solo) { fprintf(stderr, "字体加载失败: %s\n", path); return 2; }
        atlas->GetTexDataAsRGBA32(nullptr, nullptr, nullptr, nullptr);

        int total = 0, missing = 0; std::string shown;
        for (int r = 0; r < kRowCount; r++)
            for (int f = 0; f < 6; f++)
                for (unsigned int cp : DecodeUTF8(kRows[r].t[f])) {
                    if (cp < 0x20) continue;
                    total++;
                    if (!!solo->GetFontBaked(16.0f)->FindGlyphNoFallback(cp)) {
                        missing++;
                        if (shown.size() < 220) { char b[16]; snprintf(b, sizeof b, "U+%04X ", cp); shown += b; }
                    }
                }
        printf("[对照组] 单字体 NotoSansSC 扛六语：查 %d 个字形，缺 %d 个%s\n", total, missing, missing ? "" : "（无缺字）");
        if (missing) printf("          缺的是：%s\n", shown.c_str());
        printf("          图集 %d×%d = %.2f MB (RGBA)\n", atlas->TexData->Width, atlas->TexData->Height,
               atlas->TexData->Width * (double)atlas->TexData->Height * 4.0 / 1048576.0);
    }

    // ② 生产方案：每语一档，各取所需字体
    atlas->ClearInputData(); atlas->ClearTexData(); atlas->ClearFonts();
    ImFont* fonts[6] = {nullptr};
    for (int f = 0; f < 6; f++) {
        ImVector<ImWchar> ranges; BuildRangesFromAll(atlas, &ranges, f);
        snprintf(path, sizeof path, "%s/%s", dir, kFontFile[f]);
        ImFontConfig fc; snprintf(fc.Name, sizeof fc.Name, "%s", kLocale[f]);
        fonts[f] = atlas->AddFontFromFileTTF(path, 16.0f, &fc, ranges.Data);
        if (!fonts[f]) { fprintf(stderr, "字体加载失败 %s\n", path); return 2; }
        atlas->GetTexDataAsRGBA32(nullptr, nullptr, nullptr, nullptr);
        int total = 0, missing = 0;
        for (int r = 0; r < kRowCount; r++)
            for (unsigned int cp : DecodeUTF8(kRows[r].t[f])) {
                if (cp < 0x20) continue;
                total++; if (!!fonts[f]->GetFontBaked(16.0f)->FindGlyphNoFallback(cp)) missing++;
            }
        printf("[分字体] %-2s %-15s 去重后需 %4d 字形，缺 %d  | 累计图集 %d×%d\n",
               kLocale[f], kFontFile[f], total, missing, atlas->TexData->Width, atlas->TexData->Height);
    }

    // ③ 反面对照：不做按需加载、直接上 ImGui 自带的"全量中文"区间会多大
    {
        ImFontGlyphRangesBuilder rb; rb.AddRanges(atlas->GetGlyphRangesChineseFull());
        ImVector<ImWchar> rg; rb.BuildRanges(&rg);
        int n = 0; for (int i = 0; rg[i]; i += 2) n += rg[i + 1] - rg[i] + 1;
        printf("[参考] GetGlyphRangesChineseFull() 一次栅格化 %d 个字形\n", n);
    }


    // ④ 字体 × 语言 覆盖矩阵：查 cmap，不查 ImGui 图集
    {
        FT_Library ft; FT_Init_FreeType(&ft);
        printf("\n[覆盖矩阵] 每格 = 该语言全部文案里，该字体 cmap 缺多少字（0 = 能整档扛住）\n");
        printf("            %-16s %s\n", "font", "zh    en    de    it    ja    ko");
        const char* faces[] = {"NotoSansSC.ttf", "NotoSans.ttf", "NotoSansJP.ttf", "NotoSansKR.ttf"};
        for (const char* face : faces) {
            char line[256]; snprintf(line, sizeof line, "            %-16s", face);
            snprintf(path, sizeof path, "%s/%s", dir, face);
            for (int f = 0; f < 6; f++) {
                std::vector<unsigned int> cps;
                for (int r = 0; r < kRowCount; r++)
                    for (unsigned int cp : DecodeUTF8(kRows[r].t[f])) if (cp >= 0x20) cps.push_back(cp);
                std::string shown;
                int m = FreeTypeMissing(ft, path, cps, &shown);
                char cell[24];
                snprintf(cell, sizeof cell, "%-6d", m);
                strncat(line, cell, sizeof line - strlen(line) - 1);
            }
            printf("%s\n", line);
        }
        // 混排实测：德语文案里塞一个中文会话标题（本机真实会发生）
        {
            std::vector<unsigned int> mixed = DecodeUTF8("ZCode 会话标题是中文的");
            snprintf(path, sizeof path, "%s/NotoSans.ttf", dir);
            std::string shown; int m = FreeTypeMissing(ft, path, mixed, &shown);
            printf("\n[混排] 拉丁字体 NotoSans 遇到中文标题：缺 %d / %d 个字形 —— %s\n",
                   m, (int)mixed.size(), shown.c_str());
        }
        FT_Done_FreeType(ft);
    }

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    for (int pass = 0; pass < 2; pass++) {   // 两帧：第一帧建纹理，第二帧才是成品
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1200, 860));
        ImGui::Begin("probe", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextUnformatted("Agent Ping i18n 探针 — 六语同屏，每语一档字体，只栅格化字符串表出现的字");
        ImGui::Separator();

        const float colw = 186.0f;
        for (int f = 0; f < 6; f++) {
            ImGui::PushFont(fonts[f]);
            ImGui::SetCursorPos(ImVec2(14 + f * (colw + 8), 44));
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.00f, 1), "%s", kLocale[f]);
            for (int r = 0; r < 6; r++) ImGui::TextUnformatted(kRows[r].t[f]);
            ImGui::Separator();
            for (int r = 6; r < 13; r++) ImGui::TextUnformatted(kRows[r].t[f]);
            ImGui::EndGroup();
            ImGui::PopFont();
        }

        for (int f = 0; f < 6; f++) {
            ImGui::PushFont(fonts[f]);
            ImGui::SetCursorPos(ImVec2(14 + (f % 3) * 396, 470 + (f / 3) * 180));
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(1.00f, 0.70f, 0.30f, 1), "%s · 长句 @ 380px 窄栏折行", kLocale[f]);
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 380.0f);
            ImGui::TextUnformatted(kRows[13].t[f]);
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopFont();
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    int w, h; glfwGetFramebufferSize(win, &w, &h);
    std::vector<unsigned char> buf((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    { std::ofstream of(out, std::ios::binary);
      of << "P6\n" << w << "\n" << h << "\n255\n";
      for (int y = h - 1; y >= 0; y--)
          for (int x = 0; x < w; x++) { unsigned char* p = &buf[((size_t)y * w + x) * 4]; of << p[0] << p[1] << p[2]; } }
    printf("[输出] %s (%dx%d)\n", out, w, h);

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}

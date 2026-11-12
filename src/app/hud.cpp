#include "app/hud.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/i18n.h"
#include "core/util.h"

namespace kayra {

namespace {

constexpr float kPi = 3.14159265358979f;

ImU32 col(const ImVec4& c, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, std::clamp(c.w * a, 0.0f, 1.0f)));
}
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImVec2 polar(ImVec2 c, float r, float a) { return ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r); }

const ImVec4 kCyan(0.0f, 0.82f, 1.0f, 1.0f);
const ImVec4 kAmber(1.0f, 0.70f, 0.18f, 1.0f);
const ImVec4 kText(0.86f, 0.95f, 1.0f, 1.0f);
const ImVec4 kDim(0.45f, 0.62f, 0.72f, 1.0f);
const ImVec4 kRed(1.0f, 0.33f, 0.33f, 1.0f);

float text_width(ImFont* f, float size, const char* s, float spacing) {
    float w = 0;
    const char* p = s;
    int n = 0;
    while (*p) {
        unsigned int c;
        int len = ImTextCharFromUtf8(&c, p, nullptr);
        if (len <= 0) break;
        w += f->CalcTextSizeA(size, FLT_MAX, 0, p, p + len).x;
        p += len;
        n++;
    }
    return w + spacing * std::max(0, n - 1);
}

// Letter-spaced text; align 0 left, 0.5 center, 1 right. Returns the width.
float spaced(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const std::string& text, float spacing,
             float align = 0) {
    float w = text_width(f, size, text.c_str(), spacing);
    float x = pos.x - w * align;
    const char* p = text.c_str();
    while (*p) {
        unsigned int ch;
        int len = ImTextCharFromUtf8(&ch, p, nullptr);
        if (len <= 0) break;
        dl->AddText(f, size, ImVec2(x, pos.y), c, p, p + len);
        x += f->CalcTextSizeA(size, FLT_MAX, 0, p, p + len).x + spacing;
        p += len;
    }
    return w;
}

void text(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const std::string& s, float align = 0) {
    float w = f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x;
    dl->AddText(f, size, ImVec2(pos.x - w * align, pos.y), c, s.c_str());
}

void arc(ImDrawList* dl, ImVec2 c, float r, float a0, float a1, ImU32 color, float thick) {
    int seg = std::max(6, (int)(std::fabs(a1 - a0) * r / 6.0f));
    dl->PathArcTo(c, r, a0, a1, seg);
    dl->PathStroke(color, 0, thick);
}

void glow(ImDrawList* dl, unsigned int tex, ImVec2 c, float r, ImU32 color) {
    if (!tex) return;
    dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1), color);
}

// Panel with cut corners and a title.
void panel(ImDrawList* dl, ImFont* head, float k, ImVec4 r, const std::string& title, const ImVec4& accent) {
    float x0 = r.x, y0 = r.y, x1 = r.x + r.z, y1 = r.y + r.w, cut = 14 * k;
    ImVec2 pts[6] = {{x0 + cut, y0}, {x1, y0}, {x1, y1 - cut}, {x1 - cut, y1}, {x0, y1}, {x0, y0 + cut}};
    dl->AddConvexPolyFilled(pts, 6, IM_COL32(4, 18, 30, 150));
    dl->AddPolyline(pts, 6, col(accent, 0.28f), ImDrawFlags_Closed, 1.0f * k);
    // bright corner accents
    dl->AddLine(pts[5], pts[0], col(accent, 0.9f), 2.0f * k);
    dl->AddLine(pts[2], pts[3], col(accent, 0.9f), 2.0f * k);
    dl->AddLine(ImVec2(x1 - 40 * k, y0), ImVec2(x1, y0), col(accent, 0.7f), 2.0f * k);
    dl->AddLine(ImVec2(x0, y1), ImVec2(x0 + 40 * k, y1), col(accent, 0.7f), 2.0f * k);
    if (!title.empty()) {
        float w = spaced(dl, head, 15 * k, ImVec2(x0 + 22 * k, y0 + 12 * k), col(accent, 0.95f), title, 3.0f * k);
        dl->AddRectFilled(ImVec2(x0 + 12 * k, y0 + 16 * k), ImVec2(x0 + 16 * k, y0 + 26 * k), col(accent));
        dl->AddLine(ImVec2(x0 + 30 * k + w, y0 + 21 * k), ImVec2(x1 - 16 * k, y0 + 21 * k), col(accent, 0.18f), 1.0f * k);
    }
}

// high_is_bad: CPU/RAM/disk turn red near full; a battery turns red near empty.
void segbar(ImDrawList* dl, ImVec2 p, float w, float h, float v, const ImVec4& c, float k, bool high_is_bad = true) {
    int n = 26;
    float gap = 2.0f * k, sw = (w - gap * (n - 1)) / n;
    int lit = (int)std::round(std::clamp(v, 0.0f, 1.0f) * n);
    for (int i = 0; i < n; i++) {
        ImVec2 a(p.x + i * (sw + gap), p.y), b(a.x + sw, p.y + h);
        ImVec4 cc = c;
        if (high_is_bad && v > 0.85f && i >= (int)(n * 0.85f)) cc = kRed;
        if (!high_is_bad && v < 0.2f) cc = kRed;
        dl->AddRectFilled(a, b, i < lit ? col(cc, 0.9f) : col(c, 0.10f));
    }
}

void sparkline(ImDrawList* dl, ImVec2 p, float w, float h, const std::vector<float>& v, const ImVec4& c, float k) {
    if (v.size() < 2) return;
    std::vector<ImVec2> pts;
    size_t n = v.size();
    for (size_t i = 0; i < n; i++)
        pts.push_back(ImVec2(p.x + w * i / (float)(n - 1), p.y + h - h * std::clamp(v[i] / 100.0f, 0.0f, 1.0f)));
    for (size_t i = 0; i + 1 < n; i++) {
        ImVec2 q[4] = {pts[i], pts[i + 1], ImVec2(pts[i + 1].x, p.y + h), ImVec2(pts[i].x, p.y + h)};
        dl->AddConvexPolyFilled(q, 4, col(c, 0.07f));
    }
    dl->AddPolyline(pts.data(), (int)pts.size(), col(c, 0.75f), 0, 1.3f * k);
}

void weather_icon(ImDrawList* dl, ImVec2 c, float s, int code, double t) {
    ImU32 sun = col(kAmber, 0.95f), cloud = col(ImVec4(0.75f, 0.88f, 0.98f, 1), 0.9f), rain = col(kCyan, 0.9f);
    bool clear = code <= 1, part = code == 2, snowy = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool wet = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
    if (clear || part) {
        ImVec2 sc = part ? ImVec2(c.x - s * 0.22f, c.y - s * 0.2f) : c;
        float r = s * (part ? 0.28f : 0.36f);
        dl->AddCircleFilled(sc, r, sun, 32);
        for (int i = 0; i < 8; i++) {
            float a = i * kPi / 4 + (float)t * 0.3f;
            dl->AddLine(polar(sc, r * 1.3f, a), polar(sc, r * 1.65f, a), sun, s * 0.05f);
        }
        if (clear) return;
    }
    // cloud
    ImVec2 cc(c.x + s * 0.05f, c.y + s * 0.08f);
    dl->AddCircleFilled(ImVec2(cc.x - s * 0.22f, cc.y + s * 0.02f), s * 0.2f, cloud, 24);
    dl->AddCircleFilled(ImVec2(cc.x + s * 0.02f, cc.y - s * 0.1f), s * 0.27f, cloud, 24);
    dl->AddCircleFilled(ImVec2(cc.x + s * 0.27f, cc.y + s * 0.04f), s * 0.18f, cloud, 24);
    dl->AddRectFilled(ImVec2(cc.x - s * 0.22f, cc.y + s * 0.02f), ImVec2(cc.x + s * 0.27f, cc.y + s * 0.22f), cloud, s * 0.08f);
    if (wet || snowy) {
        for (int i = 0; i < 4; i++) {
            float x = cc.x - s * 0.24f + i * s * 0.16f;
            float ph = std::fmod((float)t * 1.6f + i * 0.37f, 1.0f);
            float y = cc.y + s * 0.3f + ph * s * 0.3f;
            if (snowy) dl->AddCircleFilled(ImVec2(x, y), s * 0.035f, cloud);
            else dl->AddLine(ImVec2(x, y), ImVec2(x - s * 0.05f, y + s * 0.12f), rain, s * 0.04f);
        }
    }
    if (code >= 95) {
        ImVec2 b[4] = {{cc.x + s * 0.02f, cc.y + s * 0.2f}, {cc.x - s * 0.08f, cc.y + s * 0.42f}, {cc.x + s * 0.04f, cc.y + s * 0.42f}, {cc.x - s * 0.06f, cc.y + s * 0.62f}};
        dl->AddPolyline(b, 4, sun, 0, s * 0.05f);
    }
}

std::string pct(double v) {
    char b[16];
    snprintf(b, sizeof b, "%.0f%%", v);
    return b;
}

std::string mmss(double secs) {
    long long s = (long long)std::ceil(std::max(0.0, secs));
    char b[32];
    if (s >= 3600) snprintf(b, sizeof b, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(b, sizeof b, "%02lld:%02lld", s / 60, s % 60);
    return b;
}

}  // namespace

ImVec4 look_color(Look l) {
    switch (l) {
        case Look::Thinking: return kAmber;
        case Look::Hearing: return ImVec4(0.45f, 0.95f, 1.0f, 1.0f);
        case Look::Listening: return ImVec4(0.2f, 0.9f, 1.0f, 1.0f);
        case Look::Transcribing: return ImVec4(0.3f, 1.0f, 0.85f, 1.0f);
        case Look::Speaking: return ImVec4(0.1f, 0.85f, 1.0f, 1.0f);
        case Look::Offline: return ImVec4(0.45f, 0.58f, 0.68f, 1.0f);
        case Look::Error: return kRed;
        default: return kCyan;
    }
}

void apply_style(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 3;
    st.FrameRounding = 3;
    st.GrabRounding = 3;
    st.ChildRounding = 3;
    st.PopupRounding = 3;
    st.TabRounding = 3;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 1;
    st.ScrollbarSize = 10;
    st.ItemSpacing = ImVec2(10, 8);
    st.FramePadding = ImVec2(10, 6);
    st.WindowPadding = ImVec2(18, 16);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.07f, 0.11f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.09f, 0.14f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.0f, 0.82f, 1.0f, 0.30f);
    c[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.14f, 0.21f, 0.85f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.03f, 0.22f, 0.32f, 0.9f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.04f, 0.28f, 0.40f, 0.95f);
    c[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.08f, 0.13f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.02f, 0.16f, 0.24f, 1);
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_Button] = ImVec4(0.0f, 0.45f, 0.62f, 0.45f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.7f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.75f, 0.95f, 0.9f);
    c[ImGuiCol_Header] = ImVec4(0.0f, 0.5f, 0.7f, 0.4f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.6f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.7f, 0.9f, 0.8f);
    c[ImGuiCol_Tab] = ImVec4(0.0f, 0.3f, 0.45f, 0.5f);
    c[ImGuiCol_TabHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.8f);
    c[ImGuiCol_TabSelected] = ImVec4(0.0f, 0.5f, 0.7f, 0.9f);
    c[ImGuiCol_CheckMark] = kCyan;
    c[ImGuiCol_SliderGrab] = kCyan;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.95f, 1, 1);
    c[ImGuiCol_Separator] = ImVec4(0.0f, 0.82f, 1.0f, 0.2f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.0f, 0.6f, 0.8f, 0.35f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.6f, 0.9f, 0.35f);
    st.ScaleAllSizes(scale);
}

void Hud::init(float scale) {
    scale_ = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0300, 0x036F, 0x2000, 0x206F,
                                     0x20A0, 0x20CF, 0x2100, 0x214F, 0x2190, 0x21FF, 0x2200, 0x22FF,
                                     0x2500, 0x25FF, 0x2600, 0x26FF, 0};
    std::string fonts = std::string(getenv("WINDIR") ? getenv("WINDIR") : "C:\\Windows") + "\\Fonts\\";
    auto load = [&](const char* file, float size, const char* fallback) -> ImFont* {
        ImFontConfig fc;
        fc.OversampleH = 2;
        fc.OversampleV = 2;
        std::string p = fonts + file;
        if (!file_exists(p) && fallback) p = fonts + fallback;
        ImFont* f = nullptr;
        if (file_exists(p)) f = io.Fonts->AddFontFromFileTTF(p.c_str(), size * scale, &fc, ranges);
        if (!f) {
            ImFontConfig d;
            d.SizePixels = size * scale;
            f = io.Fonts->AddFontDefault(&d);
        }
        // symbols the text fonts lack (arrows, shapes)
        std::string sym = fonts + "seguisym.ttf";
        if (file_exists(sym)) {
            ImFontConfig m;
            m.MergeMode = true;
            static const ImWchar sr[] = {0x2190, 0x21FF, 0x2300, 0x23FF, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0};
            io.Fonts->AddFontFromFileTTF(sym.c_str(), size * scale, &m, sr);
        }
        return f;
    };
    f_body = load("segoeui.ttf", 17, nullptr);
    f_small = load("segoeui.ttf", 14, nullptr);
    f_head = load("bahnschrift.ttf", 17, "seguisb.ttf");
    f_big = load("bahnschrift.ttf", 36, "seguisb.ttf");
    f_title = load("bahnschrift.ttf", 40, "seguibl.ttf");
    f_mono = load("consola.ttf", 15, nullptr);
    io.Fonts->Build();

    // soft round glow
    const int N = 128;
    std::vector<unsigned char> px(N * N * 4);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x + 0.5f - N / 2.0f) / (N / 2.0f), dy = (y + 0.5f - N / 2.0f) / (N / 2.0f);
            float d = std::sqrt(dx * dx + dy * dy);
            float a = std::pow(std::clamp(1.0f - d, 0.0f, 1.0f), 2.2f);
            unsigned char* p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char)(a * 255);
        }
    if (!glow_tex_) glGenTextures(1, &glow_tex_);
    glBindTexture(GL_TEXTURE_2D, glow_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    apply_style(scale);
}

void Hud::background(ImDrawList* dl, const HudState& s, ImVec2 size) {
    float k = k_;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(3, 9, 16, 255), IM_COL32(3, 9, 16, 255),
                                IM_COL32(5, 17, 27, 255), IM_COL32(5, 17, 27, 255));
    ImVec2 c(size.x * 0.5f, size.y * 0.47f);
    glow(dl, glow_tex_, c, size.y * 0.95f, col(color_, 0.10f));
    // hex grid fading out from the centre
    float hs = 30 * k, hw = hs * std::sqrt(3.0f);
    float maxd = size.y * 0.75f;
    for (int row = -1; row * hs * 1.5f < size.y + hs; row++) {
        for (int ci = -1; ci * hw < size.x + hw; ci++) {
            ImVec2 hc(ci * hw + (row & 1 ? hw / 2 : 0), row * hs * 1.5f);
            float d = std::sqrt((hc.x - c.x) * (hc.x - c.x) + (hc.y - c.y) * (hc.y - c.y));
            float a = 0.07f * (1.0f - d / maxd);
            float wave = 0.5f + 0.5f * std::sin((float)s.t * 0.8f - d / (90 * k));
            a *= 0.55f + 0.45f * wave;
            if (a <= 0.004f) continue;
            ImVec2 pts[6];
            for (int i = 0; i < 6; i++) pts[i] = polar(hc, hs * 0.92f, kPi / 6 + i * kPi / 3);
            dl->AddPolyline(pts, 6, col(color_, a), ImDrawFlags_Closed, 1.0f);
        }
    }
    // scan line
    float sy = std::fmod((float)s.t * 0.12f, 1.0f) * size.y;
    dl->AddRectFilledMultiColor(ImVec2(0, sy - 60 * k), ImVec2(size.x, sy), col(color_, 0), col(color_, 0), col(color_, 0.035f),
                                col(color_, 0.035f));
    // vignette
    float v = size.y * 0.18f;
    ImU32 dark = IM_COL32(0, 0, 0, 150), none = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(v, size.y), dark, none, none, dark);
    dl->AddRectFilledMultiColor(ImVec2(size.x - v, 0), size, none, dark, dark, none);
    dl->AddRectFilledMultiColor(ImVec2(0, size.y - v * 0.7f), size, none, none, dark, dark);
}

void Hud::top_bar(ImDrawList* dl, const HudState& s, ImVec2 size) {
    float k = k_;
    // mini reactor
    ImVec2 lc(46 * k, 44 * k);
    glow(dl, glow_tex_, lc, 34 * k, col(color_, 0.5f));
    dl->AddCircle(lc, 17 * k, col(color_, 0.9f), 40, 2 * k);
    for (int i = 0; i < 3; i++) {
        float a = (float)s.t * 1.2f + i * 2 * kPi / 3;
        arc(dl, lc, 11 * k, a, a + 1.4f, col(color_, 0.9f), 2.5f * k);
    }
    dl->AddCircleFilled(lc, 4.5f * k, col(ImVec4(0.85f, 0.98f, 1, 1)));
    spaced(dl, f_title, 34 * k, ImVec2(76 * k, 20 * k), col(kText), "KAYRA", 9 * k);
    spaced(dl, f_head, 12.5f * k, ImVec2(78 * k, 58 * k), col(kDim),
           "v" + s.version + "  ·  " + (is_turkish() ? "KİŞİSEL YAPAY ZEKÂ" : "PERSONAL AI"), 2.0f * k);

    // status chips
    float x = size.x * 0.5f - 300 * k;
    auto chip = [&](const std::string& label, const std::string& value, const ImVec4& c) {
        float w1 = text_width(f_head, 12.5f * k, label.c_str(), 2 * k);
        float w2 = f_body->CalcTextSizeA(15 * k, FLT_MAX, 0, value.c_str()).x;
        float w = w1 + w2 + 34 * k;
        dl->AddRect(ImVec2(x, 28 * k), ImVec2(x + w, 58 * k), col(c, 0.35f), 2 * k);
        spaced(dl, f_head, 12.5f * k, ImVec2(x + 10 * k, 36 * k), col(c, 0.85f), label, 2 * k);
        text(dl, f_body, 15 * k, ImVec2(x + 22 * k + w1, 33 * k), col(kText, 0.9f), value);
        x += w + 12 * k;
    };
    float pulse = 0.5f + 0.5f * std::sin((float)s.t * 3);
    dl->AddCircleFilled(ImVec2(x + 8 * k, 43 * k), 4.5f * k, col(ImVec4(0.2f, 1, 0.55f, 1), 0.5f + 0.5f * pulse));
    x += 18 * k;
    spaced(dl, f_head, 13 * k, ImVec2(x, 35 * k), col(ImVec4(0.2f, 1, 0.55f, 1), 0.9f), T("ui_online"), 2.5f * k);
    x += text_width(f_head, 13 * k, T("ui_online").c_str(), 2.5f * k) + 22 * k;
    chip(T("ui_brain"), s.brain_label, kCyan);
    if (!s.voice_label.empty()) chip(T("ui_voice"), s.voice_label, kCyan);

    // clock
    text(dl, f_big, 38 * k, ImVec2(size.x - 34 * k, 12 * k), col(kText), s.clock, 1.0f);
    spaced(dl, f_head, 13 * k, ImVec2(size.x - 34 * k, 60 * k), col(kDim), s.date, 1.5f * k, 1.0f);

    // divider
    float y = 84 * k;
    dl->AddRectFilledMultiColor(ImVec2(28 * k, y), ImVec2(size.x * 0.5f, y + 1 * k), col(color_, 0), col(color_, 0.45f),
                                col(color_, 0.45f), col(color_, 0));
    dl->AddRectFilledMultiColor(ImVec2(size.x * 0.5f, y), ImVec2(size.x - 28 * k, y + 1 * k), col(color_, 0.45f), col(color_, 0),
                                col(color_, 0), col(color_, 0.45f));
    for (int i = 0; i < 9; i++) {
        float tx = size.x * 0.5f + (i - 4) * 18 * k;
        dl->AddLine(ImVec2(tx, y - 4 * k), ImVec2(tx, y + 4 * k), col(color_, i == 4 ? 0.9f : 0.4f), 1.5f * k);
    }
}

void Hud::reactor(ImDrawList* dl, const HudState& s, ImVec2 c, float r) {
    float k = k_, t = (float)s.t, L = std::clamp(s.level, 0.0f, 1.0f);
    float spin = 0.25f;
    switch (s.look) {
        case Look::Thinking: spin = 2.2f; break;
        case Look::Hearing: spin = 0.9f; break;
        case Look::Transcribing: spin = 1.4f; break;
        case Look::Speaking: spin = 0.6f; break;
        case Look::Offline: spin = 0.08f; break;
        default: break;
    }
    static double last_t = 0;
    static float a1 = 0, a2 = 0, a3 = 0;
    float dt = (float)std::clamp(s.t - last_t, 0.0, 0.1);
    last_t = s.t;
    a1 += dt * spin;
    a2 += dt * spin * 0.6f;
    a3 += dt * (0.05f + spin * 0.15f);
    ImVec4 C = color_;
    ImVec4 W = mix(C, ImVec4(1, 1, 1, 1), 0.65f);

    glow(dl, glow_tex_, c, r * 2.5f, col(C, 0.16f + 0.3f * L));
    // outer ticks
    dl->AddCircle(c, r, col(C, 0.28f), 128, 1.2f * k);
    for (int i = 0; i < 120; i++) {
        float a = i * 2 * kPi / 120 + a3;
        bool major = i % 10 == 0;
        dl->AddLine(polar(c, r * 1.035f, a), polar(c, r * (major ? 1.10f : 1.06f), a), col(C, major ? 0.7f : 0.3f),
                    (major ? 2.0f : 1.0f) * k);
    }
    for (int i = 0; i < 4; i++) {
        float a = -a3 * 0.8f + i * kPi / 2;
        arc(dl, c, r * 1.17f, a, a + 0.55f, col(C, 0.55f), 2 * k);
        ImVec2 tip = polar(c, r * 1.13f, a + 0.275f);
        ImVec2 b1 = polar(c, r * 1.21f, a + 0.24f), b2 = polar(c, r * 1.21f, a + 0.31f);
        dl->AddTriangleFilled(tip, b1, b2, col(C, 0.8f));
    }
    // listening ripples
    if (s.look == Look::Listening || s.look == Look::Hearing) {
        for (int i = 0; i < 3; i++) {
            float ph = std::fmod(t * 0.7f + i / 3.0f, 1.0f);
            dl->AddCircle(c, r * (0.65f + ph * 0.7f), col(C, 0.35f * (1 - ph)), 96, 2 * k);
        }
    }
    // thick rotating arcs
    const float lens[3] = {1.25f, 0.85f, 1.6f};
    float off = 0;
    for (int i = 0; i < 3; i++) {
        float a = a1 + off;
        arc(dl, c, r * 0.9f, a, a + lens[i], col(C, 0.12f), 16 * k);
        arc(dl, c, r * 0.9f, a, a + lens[i], col(C, 0.8f), 6 * k);
        off += lens[i] + 0.45f;
    }
    // dashed ring
    for (int i = 0; i < 40; i++) {
        float a = -a2 + i * 2 * kPi / 40;
        arc(dl, c, r * 0.78f, a, a + 0.09f, col(C, 0.5f), 3 * k);
    }
    // audio bars
    int bars = 96;
    for (int i = 0; i < bars; i++) {
        float a = i * 2 * kPi / bars - kPi / 2;
        float v = 0;
        if (!s.wave.empty() && (s.look == Look::Hearing || s.look == Look::Listening || s.look == Look::Waiting)) {
            size_t idx = (size_t)((i < bars / 2 ? i : bars - 1 - i) * (s.wave.size() - 1) / (bars / 2));
            v = s.wave[s.wave.size() - 1 - std::min(idx, s.wave.size() - 1)];
        }
        v = std::max(v, L * (0.55f + 0.45f * std::sin(i * 0.9f + t * 7.0f) * std::sin(i * 0.31f - t * 3.0f)));
        if (s.look == Look::Thinking) v = 0.25f + 0.25f * std::sin(i * 0.5f + t * 6.0f);
        float len = 3 * k + std::clamp(v, 0.0f, 1.0f) * r * 0.16f;
        dl->AddLine(polar(c, r * 0.62f, a), polar(c, r * 0.62f + len, a), col(C, 0.35f + 0.5f * v), 2.2f * k);
    }
    // reactor segments
    int seg = 10;
    float rr = r * 0.5f, gap = 0.1f;
    for (int i = 0; i < seg; i++) {
        float a = i * 2 * kPi / seg + a3 * 0.5f;
        arc(dl, c, rr, a + gap / 2, a + 2 * kPi / seg - gap / 2, col(C, 0.28f + 0.45f * L), r * 0.085f);
    }
    dl->AddCircle(c, rr - r * 0.055f, col(C, 0.7f), 96, 1.5f * k);
    dl->AddCircle(c, rr + r * 0.055f, col(C, 0.7f), 96, 1.5f * k);
    // inner ring with orbiting dots
    dl->AddCircle(c, r * 0.37f, col(C, 0.85f), 96, 2.2f * k);
    for (int i = 0; i < 3; i++) dl->AddCircleFilled(polar(c, r * 0.37f, -a1 * 1.5f + i * 2 * kPi / 3), 3.5f * k, col(W));
    // triangle
    ImVec2 tri[3];
    for (int i = 0; i < 3; i++) tri[i] = polar(c, r * 0.3f, kPi / 2 + i * 2 * kPi / 3);
    dl->AddPolyline(tri, 3, col(C, 0.35f), ImDrawFlags_Closed, 1.5f * k);
    // core
    float cr = r * 0.2f * (1.0f + 0.22f * L);
    glow(dl, glow_tex_, c, r * (0.8f + 0.5f * L), col(mix(C, ImVec4(1, 1, 1, 1), 0.3f), 0.55f + 0.4f * L));
    dl->AddCircleFilled(c, cr, col(W, 0.95f), 64);
    dl->AddCircle(c, cr * 1.25f, col(C, 0.9f), 64, 2 * k);
    // thinking comet
    if (s.look == Look::Thinking || s.look == Look::Transcribing) {
        for (int i = 0; i < 14; i++) {
            float a = t * 4.0f - i * 0.06f;
            dl->AddCircleFilled(polar(c, r * 1.0f, a), (4.5f - i * 0.28f) * k, col(W, 1.0f - i / 14.0f));
        }
    }
}

void Hud::system_panel(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    panel(dl, f_head, k, r, T("ui_system"), kCyan);
    float x = r.x + 22 * k, w = r.z - 44 * k, y = r.y + 50 * k;
    auto row = [&](const std::string& label, const std::string& value, float v, const std::vector<float>* hist,
                   bool high_is_bad = true) {
        spaced(dl, f_head, 13.5f * k, ImVec2(x, y), col(kDim), label, 2 * k);
        text(dl, f_big, 22 * k, ImVec2(x + w, y - 6 * k), col(kText), value, 1.0f);
        segbar(dl, ImVec2(x, y + 24 * k), w, 7 * k, v, kCyan, k, high_is_bad);
        y += 38 * k;
        if (hist) {
            sparkline(dl, ImVec2(x, y), w, 26 * k, *hist, kCyan, k);
            y += 34 * k;
        } else {
            y += 6 * k;
        }
    };
    row(T("ui_cpu"), pct(s.stats.cpu), (float)s.stats.cpu / 100, &s.cpu_hist);
    char ram[64];
    snprintf(ram, sizeof ram, "%.1f / %.0f GB", s.stats.ram_used_gb, s.stats.ram_total_gb);
    row(T("ui_ram"), pct(s.stats.ram), (float)s.stats.ram / 100, &s.ram_hist);
    text(dl, f_small, 13 * k, ImVec2(x + w, y - 30 * k), col(kDim), ram, 1.0f);
    if (s.stats.battery >= 0)
        row(T("ui_battery") + (s.stats.charging ? "  ⚡" : ""), pct(s.stats.battery), s.stats.battery / 100.0f, nullptr, false);
    char disk[64];
    snprintf(disk, sizeof disk, "%.0f GB", s.stats.disk_free_gb);
    float used = s.stats.disk_total_gb > 0 ? (float)(1.0 - s.stats.disk_free_gb / s.stats.disk_total_gb) : 0;
    row(T("ui_disk"), disk, used, nullptr);
    // footer
    uint64_t up = s.stats.uptime_s;
    char ut[64];
    unsigned long long days = up / 86400, hours = (up / 3600) % 24, mins = (up / 60) % 60;
    bool tr = is_turkish();
    if (days > 0) snprintf(ut, sizeof ut, tr ? "%llu gün %llu sa" : "%llu d %llu h", days, hours);
    else snprintf(ut, sizeof ut, tr ? "%llu sa %llu dk" : "%llu h %llu m", hours, mins);
    float fy = r.y + r.w - 50 * k;
    dl->AddLine(ImVec2(x, fy - 8 * k), ImVec2(x + w, fy - 8 * k), col(kCyan, 0.15f), 1);
    spaced(dl, f_head, 12 * k, ImVec2(x, fy), col(kDim), T("ui_uptime"), 1.5f * k);
    text(dl, f_head, 14 * k, ImVec2(x + w * 0.52f, fy - 1 * k), col(kText), ut, 1.0f);
    spaced(dl, f_head, 12 * k, ImVec2(x + w * 0.58f, fy), col(kDim), T("ui_processes"), 1.5f * k);
    text(dl, f_head, 14 * k, ImVec2(x + w, fy - 1 * k), col(kText), std::to_string(s.stats.processes), 1.0f);
    std::string cpu = s.cpu_name;
    if (cpu.size() > 44) cpu = cpu.substr(0, 44) + "…";
    text(dl, f_small, 12.5f * k, ImVec2(x, fy + 20 * k), col(kDim, 0.8f), cpu);
}

void Hud::weather_panel(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    panel(dl, f_head, k, r, T("ui_weather"), kCyan);
    float x = r.x + 22 * k, y = r.y + 46 * k;
    const Weather& w = s.weather;
    if (!w.ok) {
        text(dl, f_body, 16 * k, ImVec2(x, y + 20 * k), col(kDim), s.weather_loading ? "…" : "—");
        return;
    }
    weather_icon(dl, ImVec2(x + 40 * k, y + 44 * k), 64 * k, w.code, s.t);
    char t[16];
    snprintf(t, sizeof t, "%.0f°", w.temp);
    text(dl, f_big, 48 * k, ImVec2(x + 92 * k, y + 8 * k), col(kText), t);
    float tx = x + 92 * k + f_big->CalcTextSizeA(48 * k, FLT_MAX, 0, t).x + 14 * k;
    std::string place = w.place;
    spaced(dl, f_head, 15 * k, ImVec2(tx, y + 14 * k), col(kCyan), lower(place) == place ? capitalize(place) : place, 1.5f * k);
    text(dl, f_body, 15 * k, ImVec2(tx, y + 36 * k), col(kText, 0.85f), capitalize(weather_text(w.code, is_turkish())));
    char d[96];
    snprintf(d, sizeof d, is_turkish() ? "En yüksek %.0f°  ·  En düşük %.0f°" : "High %.0f°  ·  Low %.0f°", w.tmax, w.tmin);
    text(dl, f_small, 13.5f * k, ImVec2(x, y + 84 * k), col(kDim), d);
    snprintf(d, sizeof d, is_turkish() ? "Nem %%%d  ·  Rüzgâr %.0f km/sa" : "Humidity %d%%  ·  Wind %.0f km/h", w.humidity, w.wind);
    text(dl, f_small, 13.5f * k, ImVec2(x, y + 103 * k), col(kDim), d);
}

void Hud::timers_panel(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    panel(dl, f_head, k, r, T("ui_timers"), kAmber);
    float x = r.x + 22 * k, w = r.z - 44 * k, y = r.y + 50 * k;
    if (s.timers.empty()) {
        text(dl, f_body, 15 * k, ImVec2(x, y), col(kDim, 0.8f), T("ui_no_timers"));
        return;
    }
    for (auto& t : s.timers) {
        if (y > r.y + r.w - 40 * k) break;
        double left = t.due - s.now;
        std::string label = t.label.empty() ? (is_turkish() ? "Zamanlayıcı" : "Timer") : t.label;
        if (label.size() > 30) label = label.substr(0, 30) + "…";
        text(dl, f_body, 15 * k, ImVec2(x, y), col(kText), label);
        text(dl, f_big, 20 * k, ImVec2(x + w, y - 3 * k), col(kAmber), mmss(left), 1.0f);
        float p = t.total > 0 ? (float)std::clamp(1.0 - left / t.total, 0.0, 1.0) : 0;
        dl->AddRectFilled(ImVec2(x, y + 26 * k), ImVec2(x + w, y + 30 * k), col(kAmber, 0.12f));
        dl->AddRectFilled(ImVec2(x, y + 26 * k), ImVec2(x + w * p, y + 30 * k), col(kAmber, 0.85f));
        y += 46 * k;
    }
}

void Hud::chat_panel(const HudState& s, ImVec4 r, HudActions& act) {
    float k = k_;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    panel(bg, f_head, k, r, T("ui_conversation"), kCyan);
    ImGui::SetNextWindowPos(ImVec2(r.x + 14 * k, r.y + 44 * k));
    ImGui::SetNextWindowSize(ImVec2(r.z - 28 * k, r.w - 58 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * k, 4 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##chat", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    float input_h = s.show_input ? 46 * k : 0;
    ImGui::BeginChild("##log", ImVec2(0, -input_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar * 0);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float wrap = ImGui::GetContentRegionAvail().x - 14 * k;
    for (size_t i = 0; i < s.chat.size(); i++) {
        const ChatEntry& e = s.chat[i];
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec4 c = e.user ? kAmber : (e.notice ? kAmber : kCyan);
        std::string who = e.user ? T("ui_you") : "KAYRA";
        float hx = p.x + 12 * k;
        float ww = spaced(dl, f_head, 13 * k, ImVec2(hx, p.y), col(c), who, 2.5f * k);
        text(dl, f_small, 12.5f * k, ImVec2(hx + ww + 10 * k, p.y + 1 * k), col(kDim, 0.8f), e.time);
        if (!e.tool.empty()) {
            std::string tag = "▸ " + e.tool;
            float tw = f_mono->CalcTextSizeA(12.5f * k, FLT_MAX, 0, tag.c_str()).x;
            float tx = p.x + wrap + 8 * k - tw;
            dl->AddRect(ImVec2(tx - 6 * k, p.y - 1 * k), ImVec2(tx + tw + 6 * k, p.y + 17 * k), col(kCyan, 0.35f), 2 * k);
            text(dl, f_mono, 12.5f * k, ImVec2(tx, p.y), col(kCyan, 0.8f), tag);
        }
        ImGui::Dummy(ImVec2(1, 21 * k));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12 * k);
        ImGui::PushFont(f_body);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap - 12 * k);
        ImGui::PushStyleColor(ImGuiCol_Text, e.user ? ImVec4(1.0f, 0.88f, 0.7f, 1) : kText);
        std::string body = e.text;
        if (e.streaming && std::fmod(s.t, 1.0) < 0.5) body += " ▍";
        ImVec2 tp = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(body.c_str());
        ImVec2 te = ImGui::GetItemRectMax();
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        dl->AddRectFilled(ImVec2(p.x + 2 * k, p.y + 2 * k), ImVec2(p.x + 4.5f * k, te.y), col(c, e.user ? 0.6f : 0.85f));
        (void)tp;
        ImGui::Dummy(ImVec2(1, 12 * k));
    }
    if (s.chat_version != seen_version_) {
        ImGui::SetScrollHereY(1.0f);
        seen_version_ = s.chat_version;
    }
    ImGui::EndChild();
    if (s.show_input) {
        ImGui::PushFont(f_body);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * k, 10 * k));
        ImGui::SetNextItemWidth(-1);
        if (!s.typing.empty()) {
            std::string shown = s.typing + (std::fmod(s.t, 1.0) < 0.5 ? "|" : "");
            char buf[1024];
            snprintf(buf, sizeof buf, "%s", shown.c_str());
            ImGui::InputText("##typed", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly);
        } else {
            if (refocus_) {
                ImGui::SetKeyboardFocusHere();
                refocus_ = false;
            }
            if (ImGui::InputTextWithHint("##in", U("ui_input_hint"), input_, sizeof input_, ImGuiInputTextFlags_EnterReturnsTrue)) {
                act.submit = input_;
                input_[0] = 0;
                refocus_ = true;
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Hud::bottom_bar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act) {
    float k = k_;
    float y = size.y - 62 * k, h = 38 * k;
    float x = 28 * k;
    auto button = [&](const char* id, const std::string& label, const std::string& value, bool on, const ImVec4& c) {
        float w = text_width(f_head, 13 * k, label.c_str(), 2 * k) + (value.empty() ? 0 : text_width(f_head, 13 * k, value.c_str(), 2 * k) + 12 * k) + 44 * k;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col(c, hov ? 0.22f : 0.10f), 3 * k);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col(c, on ? 0.8f : 0.35f), 3 * k, 0, 1.2f * k);
        dl->AddCircleFilled(ImVec2(x + 16 * k, y + h / 2), 4 * k, on ? col(c) : col(kDim, 0.6f));
        float tx = x + 28 * k;
        tx += spaced(dl, f_head, 13 * k, ImVec2(tx, y + 11 * k), col(kText, 0.9f), label, 2 * k) + 12 * k;
        if (!value.empty()) spaced(dl, f_head, 13 * k, ImVec2(tx, y + 11 * k), col(on ? c : kDim), value, 2 * k);
        x += w + 12 * k;
        return pressed;
    };
    ImGui::SetNextWindowPos(ImVec2(0, y - 4 * k));
    ImGui::SetNextWindowSize(ImVec2(size.x, h + 8 * k));
    ImGui::Begin("##bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (button("mic", T("ui_mic"), s.mic_on ? T("ui_on") : T("ui_off"), s.mic_on, kCyan)) act.toggle_mic = true;
    if (button("voice", T("ui_voice"), s.voice_on ? T("ui_on") : T("ui_off"), s.voice_on, kCyan)) act.toggle_voice = true;
    if (button("talk", is_turkish() ? "KONUŞ  ·  CTRL+SHIFT+K" : "TALK  ·  CTRL+SHIFT+K", "", s.look == Look::Listening || s.look == Look::Hearing, kAmber))
        act.talk = true;
    if (button("settings", T("ui_settings"), "", false, kCyan)) act.open_settings = true;
    ImGui::End();
    // right side: mode and download
    std::string right = s.mode_label;
    if (s.download >= 0) {
        char b[64];
        snprintf(b, sizeof b, "%d", (int)(s.download * 100));
        right = F("ui_downloading", {b});
        float bw = 220 * k;
        dl->AddRectFilled(ImVec2(size.x - 28 * k - bw, y + h - 4 * k), ImVec2(size.x - 28 * k, y + h - 1 * k), col(kCyan, 0.15f));
        dl->AddRectFilled(ImVec2(size.x - 28 * k - bw, y + h - 4 * k), ImVec2(size.x - 28 * k - bw + bw * s.download, y + h - 1 * k), col(kCyan));
    }
    spaced(dl, f_head, 13 * k, ImVec2(size.x - 28 * k, y + 10 * k), col(kDim), right, 2 * k, 1.0f);
}

HudActions Hud::draw(const HudState& s) {
    HudActions act;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = io.DisplaySize;
    k_ = std::clamp(std::min(size.y / 900.0f, size.x / 1600.0f), 0.6f, 2.0f);
    io.FontGlobalScale = k_ / scale_;
    ImVec4 target = look_color(s.look);
    color_ = mix(color_, target, 0.08f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    background(dl, s, size);
    top_bar(dl, s, size);

    float k = k_;
    float top = 100 * k, bottom = size.y - 78 * k;
    float lw = 350 * k, rw = std::min(520 * k, size.x * 0.32f);
    ImVec4 sys(28 * k, top, lw, std::min(420 * k, (bottom - top) * 0.52f));
    system_panel(dl, s, sys);
    float wy = sys.y + sys.w + 14 * k;
    ImVec4 wr(28 * k, wy, lw, 172 * k);
    weather_panel(dl, s, wr);
    float ty = wr.y + wr.w + 14 * k;
    if (bottom - ty > 90 * k) timers_panel(dl, s, ImVec4(28 * k, ty, lw, bottom - ty));

    // core
    float cx0 = 28 * k + lw, cx1 = size.x - 28 * k - rw;
    ImVec2 c((cx0 + cx1) / 2, top + (bottom - top) * 0.42f);
    float r = std::min((cx1 - cx0) * 0.30f, (bottom - top) * 0.27f);
    reactor(dl, s, c, r);
    float sy = c.y + r * 1.3f + 18 * k;
    spaced(dl, f_big, 30 * k, ImVec2(c.x, sy), col(color_), s.status, 10 * k, 0.5f);
    if (!s.hint.empty()) text(dl, f_body, 16 * k, ImVec2(c.x, sy + 46 * k), col(kDim), s.hint, 0.5f);
    if (!s.heard.empty()) {
        float maxw = (cx1 - cx0) * 0.9f;
        std::string h = s.heard;
        bool cut = false;
        auto width = [&] { return f_body->CalcTextSizeA(19 * k, FLT_MAX, 0, ("“" + h + "…”").c_str()).x; };
        while (h.size() > 4 && width() > maxw) {
            while (!h.empty() && ((unsigned char)h.back() & 0xC0) == 0x80) h.pop_back();
            if (!h.empty()) h.pop_back();
            cut = true;
        }
        std::string q = "“" + h + (cut ? "…”" : "”");
        text(dl, f_body, 19 * k, ImVec2(c.x, sy + 74 * k), col(kText, 0.92f), q, 0.5f);
    }
    chat_panel(s, ImVec4(size.x - 28 * k - rw, top, rw, bottom - top), act);
    bottom_bar(dl, s, size, act);
    return act;
}

}  // namespace kayra
